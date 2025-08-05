@echo off
setlocal enabledelayedexpansion

:: === Input args ===
:: %1 = app path or short name
:: %2 = (optional) board name
if "%~1"=="" (
    echo Usage: build.bat ^<app_path_or_name^> [board]
    exit /b 1
)

set INPUT=%~1
set BOARD=%~2
if "%BOARD%"=="" set BOARD=nrf52840dongle

set BASE_DIR=zephyr\samples
set FOUND=

:: Normalize slashes
set INPUT=%INPUT:/=\%

:: Check if INPUT is a valid path
if exist "%INPUT%\CMakeLists.txt" (
    set FOUND=%INPUT%
    goto :found
)

:: Else, search by short name
echo === Searching for "%INPUT%" under "%BASE_DIR%" ===
for /r %BASE_DIR% %%d in (.) do (
    if exist "%%d\CMakeLists.txt" (
        if /i "%%~nxd"=="%INPUT%" (
            set FOUND=%%d
            goto :found
        )
    )
)

echo Error: Could not find app "%INPUT%" under "%BASE_DIR%"
exit /b 1

:found
echo === Found app: !FOUND!
echo === Target board: %BOARD%
west build -p=always -b %BOARD% "!FOUND!"

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo === Build complete ===
endlocal
