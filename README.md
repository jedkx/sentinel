# Sentinel

Small Pi app that reads system stats and draws them on a Waveshare **2.13" e-Paper V4** (250×122, SPI + lgpio). Upper half is a fixed panel (time, CPU/temp, memory, uptime + IP); below that is a scrolling text strip fed from telemetry. On exit (`SIGINT` / `SIGTERM`) it tries to push a last shutdown line and do a full refresh so something readable stays on the panel.

Target hardware in mind: **Pi Zero 2W**. Other Pis may work if the Waveshare build (vendor driver + lgpio) still builds.

## Install

```bash
git clone https://github.com/jedkx/sentinel
cd sentinel
chmod +x scripts/install.sh
./scripts/install.sh
```

That script: apt deps (`cmake`, `build-essential`, `liblgpio-dev`), Release build, copies the binary to `/usr/local/bin/sentinel`, installs `scripts/sentinel.service`, `daemon-reload`, enable + restart.

Run as a normal user with sudo — the script refuses if you invoke it as root.

## Build without installing

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
sudo ./sentinel
```

You need the Waveshare sources under `vendor/e-Paper/` (paths match `CMakeLists.txt`).

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
- `src/telemetry.cpp` — `/proc`, `/sys`, plus a few shell calls where there is no stable sysfs API (`vcgencmd`, `ip`, `ping`, `bluetoothctl` where present). Hotter paths are not re-read every millisecond; some values are cached on 5s / 30s / 60s intervals.
- `src/ui_engine.cpp` — framebuffer through Waveshare paint API, full vs partial refresh cadence.
- `vendor/e-Paper/` — upstream Waveshare C tree; try not to fork it casually.

## systemd

```bash
sudo systemctl status sentinel --no-pager
sudo systemctl restart sentinel
sudo journalctl -u sentinel -f
```

Service runs as **root** (SPI/gpio access as shipped). Adjust if you move to a gpio group / udev rule.

## Screen update behavior

The loop wakes once per **second** (aligned to `time()`), not a blind `sleep(1)` drift loop.  
Most frames use the panel’s **partial** update; every N frames it does a **full** refresh to wash out ghosting. Expect tradeoffs: partial is faster-looking, full is cleaner but flashes.

## journald lines

Roughly:

- `[METRICS]` — CPU %, SoC temp, memory %, IP, uptime.
- `[NETTEST]` about every 30 iterations — SSID (if `iwgetid`/`iw` works), RX/TX KB/s from `/proc/net/dev` counters over time, packet loss from an async `ping` to the default gateway when that runs.

If a helper binary is missing, that line usually goes empty or stale until the tool exists — nothing crashes on purpose.

## License

MIT
