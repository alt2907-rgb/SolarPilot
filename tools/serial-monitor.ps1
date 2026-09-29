$ErrorActionPreference = 'Stop'

$platformio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'

if (-not (Test-Path -LiteralPath $platformio)) {
    Write-Error "PlatformIO Core was not found at '$platformio'."
}

function Get-SerialPorts {
    Get-CimInstance Win32_SerialPort |
        Select-Object DeviceID, Name, PNPDeviceID
}

$ports = @(Get-SerialPorts)

if (-not $ports) {
    Write-Host 'No serial ports were detected.' -ForegroundColor Red
    Write-Host 'Available serial ports:'
    Get-PnpDevice -Class Ports | Select-Object Status, FriendlyName, InstanceId | Format-Table -AutoSize
    exit 1
}

$matches = @($ports | Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_1001' })

if ($matches.Count -eq 1) {
    $port = $matches[0].DeviceID
} elseif ($matches.Count -gt 1) {
    Write-Host 'Multiple ESP32-C3 serial ports matched VID:PID 303A:1001.' -ForegroundColor Red
    Write-Host 'Available matching ports:'
    $matches | Format-Table -AutoSize
    Write-Host 'All detected serial ports:'
    $ports | Format-Table -AutoSize
    exit 1
} else {
    Write-Host 'No ESP32-C3 serial port matched VID:PID 303A:1001.' -ForegroundColor Red
    Write-Host 'Available serial ports:'
    $ports | Format-Table -AutoSize
    exit 1
}

Write-Host "Starting PlatformIO serial monitor on $port at 115200 baud..."
& $platformio device monitor --port $port --baud 115200
