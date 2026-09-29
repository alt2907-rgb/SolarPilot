# SolarPilot Development Runbook

## Returning to SolarPilot after a break

If the project has been idle for a while, start by checking the hardware and the serial connection instead of reflashing immediately.

1. Connect the ESP32-C3 by USB.
2. Verify that Windows detects the device as a serial port.
3. Open the serial monitor at `115200` baud.
4. If SolarPilot prints normal runtime output such as GoodWe grid-power readings, the firmware is already running and does **not** need to be reflashed.

### Recommended start procedure on Windows

Use the local PlatformIO Core installation at:

`%USERPROFILE%\.platformio\penv\Scripts\platformio.exe`

Example checks:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device list
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor --port COM4 --baud 115200
```

Do not assume the device is always `COM4`; use the detected port shown by Windows or the helper script in `tools/serial-monitor.ps1`.

### When VS Code or PlatformIO points at the wrong executable

If the PlatformIO monitor task fails with a message like:

`Path to shell executable "...\\SolarPilot\\platformio.exe" does not exist`

then VS Code is trying to run a stale or incorrect PlatformIO path from inside the project directory. The repository does not ship a `platformio.exe` binary. Use the PlatformIO Core executable from the user profile path above, or run the helper task/script that resolves it automatically.

### What success looks like

When the monitor is attached correctly, SolarPilot should soon emit normal status lines such as GoodWe communication and grid-power readings. That is the sign that the firmware is alive and the issue was only the monitor setup.

## Serial monitor troubleshooting

- If Windows does not show any serial port for the ESP32-C3, reconnect the USB cable and try another USB port.
- If multiple serial devices are present, prefer the one whose USB VID:PID is `303A:1001`.
- If the helper cannot identify a single matching port, it will print the available serial ports and stop instead of guessing.
