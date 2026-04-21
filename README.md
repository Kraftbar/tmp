# ESP32 Docker Starter

Minimal ESP-IDF project for a Windows + Docker build workflow.

## What this gives you

- Build in Docker with a pinned `ESP-IDF` image.
- Flash from Windows PowerShell with `esptool`.
- Edit from WSL, Vim, or your Windows editor.

## Layout

- `main/main.c`: program entry and main loop.
- `main/board.c`, `main/board.h`: board-specific pin setup.
- `main/status.c`, `main/status.h`: status LED patterns.
- `main/wifi.c`, `main/wifi.h`: Wi-Fi station connect and link logging.
- `main/wifi_secrets.h`: local untracked Wi-Fi credentials.
- `main/wifi_secrets.example.h`: sample credentials header.
- `scripts/build.ps1`: builds with Docker.
- `scripts/flash.ps1`: flashes the built binaries to your board.

Wi-Fi credentials live in `main/wifi_secrets.h`.
If you set this project up on another machine, copy `main/wifi_secrets.example.h` to `main/wifi_secrets.h` and edit it.

## Requirements

- Docker Desktop
- PowerShell
- Python on Windows
- `esptool` installed on Windows: `py -m pip install esptool`

## Build

From PowerShell in the project root:

```powershell
.\scripts\build.ps1
```

## Flash

```powershell
.\scripts\flash.ps1 -Port COM6
```

`flash.ps1` now defaults to `115200` baud because it is slower but more reliable on flaky boards.

If flashing fails, close any open serial monitor first and retry.

This project is set for `4MB` flash and the larger single-app partition layout.

If your board still needs you to hold `BOOT` while flashing starts, that is usually a board auto-reset quirk, not a firmware bug.

## Monitor

```powershell
.\scripts\monitor.ps1 -Port COM6
```

This opens the serial log at `115200` baud using a local Python tracer adapted from `Kraftbar/uart-tracer` and writes `serial.log` in the project root.

## Build, flash, and monitor

```powershell
.\scripts\build-flash-monitor.ps1 -Port COM6
```

This builds first, flashes only on success, then opens a serial monitor at `115200` baud.

If you already have a monitor open from an earlier run, close it before flashing again.

Use `Ctrl+C` to stop the monitor.

## WSL workflow

You can stay in WSL to inspect files, edit code, and ask me to make changes.

If Docker is available from WSL, you can also try:

```bash
docker run --rm -v "$(pwd):/project" -w /project espressif/idf:v5.2.2 idf.py set-target esp32 build
```

Flashing is still simplest from Windows PowerShell because the serial port is already there as `COM6`.
