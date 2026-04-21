# Sentinel

Small Pi monitor that always logs to terminal + local file, and optionally renders on Waveshare **2.13" e-Paper V4** when display hardware is available.

Sentinel behavior:
- If e-paper exists and init succeeds: render metrics + terminal strip and keep logging.
- If e-paper is missing or init fails: continue in headless mode (no crash), keep logging.
- Optional external log sources can be tailed and merged into Sentinel output.

Target hardware in mind: **Pi Zero 2W**. Other Pis may work if the Waveshare build (vendor driver + lgpio) still builds.

## Install

```bash
git clone https://github.com/jedkx/sentinel
cd sentinel
chmod +x scripts/install.sh
./scripts/install.sh
```

That script is the one-command deploy path: installs apt deps (`cmake`, `build-essential`, `liblgpio-dev`, `logrotate`, `git`), auto-fetches Waveshare vendor sources when missing, does a Release build, installs `/usr/local/bin/sentinel`, `/usr/local/bin/sentinelctl`, service and logrotate policy, then runs `daemon-reload`, `enable`, `restart`, and verifies service status.

You can run it as normal user (with sudo) or root.

## Build without installing

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
sudo ./sentinel
```

If Waveshare sources exist under `vendor/e-Paper/` (paths match `CMakeLists.txt`), Sentinel builds with e-paper support.
If that vendor tree is missing, build automatically falls back to headless mode (logging + web features continue).
`install.sh` first tries to auto-fetch Waveshare sources from `https://github.com/waveshareteam/e-Paper.git` (branch `master`) before building.

You can override source/revision for vendor fetch:

```bash
WAVESHARE_REPO_URL=https://github.com/waveshareteam/e-Paper.git \
WAVESHARE_REPO_REF=master \
./scripts/install.sh
```
If `SENTINEL_DISABLE_DISPLAY=1`, vendor fetch is skipped and install proceeds headless.

## Copying the tree from Windows

No rsync on stock PowerShell — tarball works:

```powershell
tar -czf sentinel.tar.gz --exclude=build --exclude=.git .
scp .\sentinel.tar.gz pi@YOUR_PI:/home/pi/
```

On the Pi:

```bash
mkdir -p ~/sentinel && tar -xzf ~/sentinel.tar.gz -C ~/sentinel
```

## Layout

- `src/main.cpp` — init display, main loop on 1s wall-clock edges, heartbeat fields, journal prints.
- `src/telemetry.cpp` — `/proc`, `/sys`, plus a few lightweight shell calls where there is no stable sysfs API (`vcgencmd`, `ip`, `iwgetid`). Sampling is tiered to stay efficient (fast/medium/slow/very-slow windows).
- `src/ui_engine.cpp` — framebuffer through Waveshare paint API, full vs partial refresh cadence.
- `vendor/e-Paper/` — upstream Waveshare C tree; try not to fork it casually.

## systemd

```bash
sudo systemctl status sentinel --no-pager
sudo systemctl restart sentinel
sudo journalctl -u sentinel -f
```

## Operations CLI

`install.sh` also installs `sentinelctl` to `/usr/local/bin/sentinelctl` for day-to-day operations.

Common commands:

```bash
sentinelctl status
sentinelctl logs 200 --follow
sentinelctl config-show
sentinelctl config-set SENTINEL_WEB_ENABLE 1
sentinelctl config-set SENTINEL_WEB_TOKEN my-secret-token
sentinelctl apply
sentinelctl log-clear
sentinelctl rotate-now
sentinelctl web-url
```

`config-set` and `config-unset` write to `/etc/systemd/system/sentinel.service.d/override.conf`, so the base unit file stays unchanged.

Service runs as **root** (SPI/gpio access as shipped). Adjust if you move to a gpio group / udev rule.

## Runtime configuration

Set with systemd `Environment=` or shell env vars:

- `SENTINEL_LOG_FILE`: local log file path. Default `/var/log/sentinel.log`.
- `SENTINEL_WATCH`: comma-separated sources, format `name=/path/to/log,/other/path.log`.
- `SENTINEL_DISABLE_DISPLAY`: set `1` to force headless mode.
- `SENTINEL_LOG_INTERVAL_S`: status log cadence in seconds. Default `2`.
- `SENTINEL_WATCH_MAX_EVENTS`: max external log lines processed per loop. Default `32`.
- `SENTINEL_WEB_ENABLE`: set `1` to enable read-only web view. Default `0`.
- `SENTINEL_WEB_BIND`: bind address for web view. Default `127.0.0.1`.
- `SENTINEL_WEB_PORT`: web port. Default `9090`.
- `SENTINEL_WEB_TOKEN`: optional API token, passed with `X-Sentinel-Token` header.

Example:

```bash
export SENTINEL_LOG_FILE=/var/log/sentinel.log
export SENTINEL_WATCH="nginx=/var/log/nginx/error.log,api=/opt/myapp/app.log"
export SENTINEL_DISABLE_DISPLAY=0
export SENTINEL_LOG_INTERVAL_S=2
export SENTINEL_WATCH_MAX_EVENTS=32
export SENTINEL_WEB_ENABLE=0
export SENTINEL_WEB_BIND=127.0.0.1
export SENTINEL_WEB_PORT=9090
export SENTINEL_WEB_TOKEN=
sudo ./sentinel
```

## Remote web view (default off)

Sentinel includes an optional read-only web view that mirrors the same status model used by the e-paper UI.

- `GET /health`
- `GET /api/state`
- `GET /api/events?limit=100`
- `GET /ui`

Security model:

- Disabled by default (`SENTINEL_WEB_ENABLE=0`)
- Local bind by default (`127.0.0.1`)
- Optional token auth via `X-Sentinel-Token`

Recommended remote access (SSH tunnel):

```bash
ssh -L 9090:127.0.0.1:9090 pi@YOUR_PI
```

Then open `http://127.0.0.1:9090/ui` on your local machine.

### Telemetry profile notes

Sentinel now uses a single balanced profile: core `/proc` and `/sys` metrics are sampled frequently, heavier device probes are sampled less often, and ping-based packet-loss probing is disabled to keep runtime overhead predictable.

## Log retention (logrotate)

Sentinel installs `/etc/logrotate.d/sentinel` with a Pi-friendly policy based on common Debian/logrotate defaults (time-based rotation + compression):

- `weekly`
- `rotate 7`
- `maxsize 10M`
- `compress` + `delaycompress`
- `missingok`, `notifempty`, `create 0640 root root`
- `postrotate` sends `SIGHUP` so Sentinel reopens the file handle cleanly

This keeps about 7 rotated files plus current log, with a practical upper bound around 80 MB uncompressed (typically much lower on disk after gzip compression).

## Screen update behavior

The loop wakes once per **second** (aligned to `time()`), not a blind `sleep(1)` drift loop.  
Most frames use the panel’s **partial** update; every N frames it does a **full** refresh to wash out ghosting. Expect tradeoffs: partial is faster-looking, full is cleaner but flashes.

## journald lines

Roughly:

- `STAT` every `SENTINEL_LOG_INTERVAL_S` seconds (default 2) — simplified status line (CPU/temp/memory/net).
- `NET` every 30 seconds — SSID, packet loss, iface and IP.
- `EXT[...]` when external watched logs emit new lines.
- `ALERT` on threshold transitions (thermal/cpu/memory/network), and `ALERT CLEARED` when normalized.

If a helper binary is missing, that line usually goes empty or stale until the tool exists — nothing crashes on purpose.

## License

MIT
