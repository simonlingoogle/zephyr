@echo off
set HW_VERSION=52
set APP_VERSION=1
set HEX_FILE=build\zephyr\zephyr.hex
set DFU_PKG=app.zip

echo Generating DFU package...
nrfutil pkg generate --hw-version %HW_VERSION% --sd-req 0x00 --application %HEX_FILE% --application-version %APP_VERSION% %DFU_PKG%

if errorlevel 1 (
    echo Failed to generate DFU package.
    exit /b 1
)

echo Flashing firmware via Nordic DFU...
nrfutil device program --firmware %DFU_PKG% --traits nordicDfu

if errorlevel 1 (
    echo DFU programming failed.
    exit /b 1
)

echo Flash successful.
