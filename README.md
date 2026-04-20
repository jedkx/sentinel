# Sentinel

A lightweight system monitor for Raspberry Pi Zero 2W with a 2.13" Waveshare e-paper display.  
Displays CPU, memory, network and uptime on a black-background terminal-style UI, inspired by mission telemetry readouts.

## Hardware

| Component | Model |
|-----------|-------|
| SBC | Raspberry Pi Zero 2W |
| Display | Waveshare 2.13" e-Paper V4 (250×122) |
| Interface | SPI (hardware) + lgpio |

## Quick start

```bash
git clone https://github.com/jedkx/sentinel
cd sentinel
chmod +x scripts/install.sh
./scripts/install.sh
```

`install.sh` builds the binary, copies it to `/usr/local/bin`, installs and enables the systemd service.

## Manual build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo ./sentinel
```

## Project layout

```
sentinel/
├── src/
│   ├── main.cpp         # init, signal handling, main loop
│   ├── telemetry.cpp    # CPU / memory / network / uptime readers
│   └── ui_engine.cpp    # e-paper layout and rendering
├── include/
│   ├── telemetry.h
│   └── ui_engine.h
├── vendor/e-Paper/      # Waveshare driver (unmodified)
├── scripts/
│   ├── install.sh
│   └── sentinel.service
└── CMakeLists.txt
```

## Service management

```bash
sudo systemctl status sentinel
sudo systemctl restart sentinel
sudo journalctl -u sentinel -f   # live logs
```

## Display refresh

The screen refreshes every 30 seconds (configurable via `REFRESH_INTERVAL_S` in `main.cpp`).  
A full e-ink refresh is used — no partial refresh — to avoid ghosting artifacts on the V4 panel.

## License

MIT
