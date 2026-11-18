@echo off
title STAR - disable HVCI and driver blocklist
setlocal
cd /d "%~dp0"
goto :MAIN

::STAR_PS1_BEGIN
# Disable Windows blocks that stop STAR's driver mapper (HVCI + vulnerable driver blocklist).
#Requires -Version 5.1
param(
    [switch] $Reboot,
    [switch] $WhatIf
)
$ErrorActionPreference = 'Stop'
function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Get-BlockState {
    $dgPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard'
    $hvciPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
    $ciPath   = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Config'
    $hvci = $null; $vdb = $null; $vbs = $null
    if (Test-Path $hvciPath) { $hvci = (Get-ItemProperty $hvciPath -ErrorAction SilentlyContinue).Enabled }
    if (Test-Path $ciPath)   { $vdb  = (Get-ItemProperty $ciPath -ErrorAction SilentlyContinue).VulnerableDriverBlocklistEnable }
    if (Test-Path $dgPath)   { $vbs  = (Get-ItemProperty $dgPath -ErrorAction SilentlyContinue).EnableVirtualizationBasedSecurity }
    foreach ($ref in @([ref]$hvci, [ref]$vdb, [ref]$vbs)) { if ($null -eq $ref.Value) { $ref.Value = '(not set)' } }
    [PSCustomObject]@{ Hvci = $hvci; Vdb = $vdb; Vbs = $vbs }
}
function Wait-Enter {
    param([string] $Label = 'Press Enter to close this window')
    Write-Host ''; Read-Host $Label | Out-Null
}
if (-not (Test-IsAdmin)) {
    Write-Host ''
    Write-Host 'Kailangan Administrator. Magpapakita ang UAC - piliin ang Yes.' -ForegroundColor Yellow
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Reboot) { $argList += '-Reboot' }
    if ($WhatIf) { $argList += '-WhatIf' }
    try {
        Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argList -Wait
    } catch {
        Write-Host 'Hindi na-elevate (UAC na-cancel?). Right-click ang .cmd at Run as administrator.' -ForegroundColor Red
        Wait-Enter; exit 1
    }
    Write-Host 'Tapos na sa admin window (kung na-approve ang UAC).' -ForegroundColor Green
    Wait-Enter; exit 0
}
try {
    Write-Host ''
    Write-Host '=== STAR: disable HVCI + vulnerable driver blocklist ===' -ForegroundColor Cyan
    Write-Host 'Lowers Windows security for this loader. Use only on your own PC.' -ForegroundColor DarkGray
    $before = Get-BlockState
    Write-Host ''
    Write-Host ("Before  HVCI (Memory integrity): $($before.Hvci)  [0=OFF, 1=ON]")
    Write-Host ("Before  VulnerableDriverBlocklist: $($before.Vdb)  [0=OFF, 1=ON]")
    Write-Host ("Before  VBS (DeviceGuard):         $($before.Vbs)  [0=OFF, 1=ON]")
    $dgKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard'
    $hvciKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity'
    $ciKey  = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Config'
    if ($WhatIf) {
        Write-Host ''; Write-Host 'WhatIf: would set HVCI and VDB to 0.' -ForegroundColor Yellow
        Wait-Enter; exit 0
    }
    New-Item -Path $hvciKey -Force | Out-Null
    New-ItemProperty -Path $hvciKey -Name 'Enabled' -PropertyType DWord -Value 0 -Force | Out-Null
    New-ItemProperty -Path $hvciKey -Name 'Locked' -PropertyType DWord -Value 0 -Force -ErrorAction SilentlyContinue | Out-Null
    New-Item -Path $ciKey -Force | Out-Null
    New-ItemProperty -Path $ciKey -Name 'VulnerableDriverBlocklistEnable' -PropertyType DWord -Value 0 -Force | Out-Null
    New-Item -Path $dgKey -Force | Out-Null
    New-ItemProperty -Path $dgKey -Name 'EnableVirtualizationBasedSecurity' -PropertyType DWord -Value 0 -Force | Out-Null
    $after = Get-BlockState
    Write-Host ''
    Write-Host ("After   HVCI (Memory integrity): $($after.Hvci)") -ForegroundColor Green
    Write-Host ("After   VulnerableDriverBlocklist: $($after.Vdb)") -ForegroundColor Green
    Write-Host ("After   VBS (DeviceGuard):         $($after.Vbs)") -ForegroundColor Green
    Write-Host ''
    Write-Host 'KAILANGAN REBOOT bago test ang RobRunMapper o STAR.' -ForegroundColor Yellow
    if ($Reboot) {
        Write-Host 'Rebooting in 10 seconds...' -ForegroundColor Magenta
        shutdown.exe /r /t 10 /c 'STAR driver blocks disabled'
        exit 0
    }
    Write-Host ''
    $ans = Read-Host 'Reboot now? (Y/N)'
    if ($ans -match '^[Yy]') { shutdown.exe /r /t 5 /c 'STAR driver blocks disabled' ; exit 0 }
    Wait-Enter
} catch {
    Write-Host ''; Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    Wait-Enter; exit 1
}
::STAR_PS1_END

:MAIN
echo.
echo [STAR] Papatayin ang Memory integrity (HVCI) at Vulnerable Driver Blocklist sa registry.
echo        Pagkatapos: KAILANGAN REBOOT bago gumana ang mapper.
echo.
echo Iisa lang na file ang kailangan (ito). Right-click ^> Run as administrator = mas mabilis.
echo.

set "TMPPS1=%TEMP%\STAR-disable-driver-blocks.ps1"
set "LOCALPS1=%~dp0disable-star-driver-blocks.ps1"
set "BATCH=%~f0"

if exist "%LOCALPS1%" (
  copy /Y "%LOCALPS1%" "%TMPPS1%" >nul
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$b=[IO.File]::ReadAllText($env:BATCH); $s='::STAR_PS1_BEGIN'; $e='::STAR_PS1_END'; $i=$b.IndexOf($s); $j=$b.IndexOf($e); if($i -lt 0 -or $j -le $i){ throw 'Missing embedded script in .cmd' }; $t=$b.Substring($i+$s.Length,$j-$i-$s.Length).Trim(); [IO.File]::WriteAllText($env:TMPPS1,$t,[Text.UTF8Encoding]::new($false))"
  if errorlevel 1 (
    echo [STAR] Hindi ma-load ang script. I-download muli ang disable-star-driver-blocks.cmd
    pause
    exit /b 1
  )
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%TMPPS1%" %*
set "ERR=%ERRORLEVEL%"

echo.
if not "%ERR%"=="0" echo [STAR] May error (exit %ERR%).
echo.
pause
exit /b %ERR%