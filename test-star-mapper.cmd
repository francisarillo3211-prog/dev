@echo off
title STAR - test RobRunMapper
setlocal
net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo [STAR] Requesting Administrator privileges ^(UAC^)...
    powershell.exe -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
    exit /b %ERRORLEVEL%
)

set "BUNDLE=%LOCALAPPDATA%\STAR\bundle"
echo.
echo [STAR] Mapper test folder:
echo   %BUNDLE%
echo.

if not exist "%BUNDLE%" (
    echo [STAR] Bundle folder not found.
    echo        Run STAR.exe once first ^(even if it errors^) to extract the files.
    echo.
    pause
    exit /b 2
)

cd /d "%BUNDLE%"
echo Files here:
dir /b
echo.

if not exist "RobRunMapper.exe" (
    echo [STAR] RobRunMapper.exe missing - run STAR.exe once first.
    pause
    exit /b 3
)
if not exist "RobRun.sys" (
    echo [STAR] RobRun.sys missing - run STAR.exe once first.
    pause
    exit /b 4
)

echo [STAR] Running RobRunMapper.exe RobRun.sys ...
echo        ^(you should see output below; if it closes instantly, antivirus may have removed it^)
echo.
RobRunMapper.exe RobRun.sys
set "MAPERR=%ERRORLEVEL%"
echo.
echo [STAR] Mapper exit code: %MAPERR%  ^(0=OK^)
echo.

reg query "HKLM\SYSTEM\RobRun" /v RingToken >nul 2>&1
if errorlevel 1 (
    echo [STAR] No RingToken in registry - mapping failed.
    echo        Common error: vulnerable driver: service 0xc0000022
) else (
  echo [STAR] OK - RingToken found. You can run STAR.exe ^(as admin^).
)

echo.
pause
exit /b %MAPERR%