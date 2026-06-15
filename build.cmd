@echo off
setlocal
cd /d "%~dp0"

if exist "%~dp0env.local.cmd" call "%~dp0env.local.cmd"

if not defined NCS_ROOT set "NCS_ROOT=F:\ncs\v3.3.0"
if not defined NCS_TOOLCHAIN set "NCS_TOOLCHAIN=C:\ncs\toolchains\fd21892d0f"
if not defined BOARD set "BOARD=nrf54lm20dk/nrf54lm20a/cpuapp"

set "BUILD_DIR=build_nrf54lm20_se05x"
set "APP_DIR=%CD%"
set "BUILD_TMP=%APP_DIR%\.build_tmp"

if not exist "%NCS_ROOT%\zephyr" (
  echo NCS source tree not found: %NCS_ROOT%
  exit /b 1
)

if not exist "%NCS_TOOLCHAIN%\opt\bin\Scripts\west.exe" (
  echo west.exe not found in Nordic toolchain: %NCS_TOOLCHAIN%
  exit /b 1
)

if not exist "%BUILD_TMP%" mkdir "%BUILD_TMP%"
set "TMP=%BUILD_TMP%"
set "TEMP=%BUILD_TMP%"

set "PATH=%NCS_TOOLCHAIN%;%NCS_TOOLCHAIN%\mingw64\bin;%NCS_TOOLCHAIN%\bin;%NCS_TOOLCHAIN%\opt\bin;%NCS_TOOLCHAIN%\opt\bin\Scripts;%NCS_TOOLCHAIN%\opt\nanopb\generator-bin;%NCS_TOOLCHAIN%\nrfutil\bin;%NCS_TOOLCHAIN%\opt\zephyr-sdk\arm-zephyr-eabi\bin;%NCS_TOOLCHAIN%\opt\zephyr-sdk\riscv64-zephyr-elf\bin;%PATH%"
set "PYTHONPATH=%NCS_TOOLCHAIN%\opt\bin;%NCS_TOOLCHAIN%\opt\bin\Lib;%NCS_TOOLCHAIN%\opt\bin\Lib\site-packages"
set "NRFUTIL_HOME=%NCS_TOOLCHAIN%\nrfutil\home"
set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr"
set "ZEPHYR_SDK_INSTALL_DIR=%NCS_TOOLCHAIN%\opt\zephyr-sdk"

pushd "%NCS_ROOT%"
"%NCS_TOOLCHAIN%\opt\bin\Scripts\west.exe" build --build-dir "%APP_DIR%\%BUILD_DIR%" "%APP_DIR%" --pristine --board "%BOARD%" -- -DCONF_FILE=prj.conf
set "BUILD_RESULT=%ERRORLEVEL%"
popd

if not "%BUILD_RESULT%"=="0" exit /b %BUILD_RESULT%

set "ARTIFACT_DIR=%APP_DIR%\%BUILD_DIR%\zephyr"
if not exist "%ARTIFACT_DIR%\zephyr.hex" set "ARTIFACT_DIR=%APP_DIR%\%BUILD_DIR%\nrf54lm20_se05x\zephyr"

echo.
echo Build finished.
echo ELF:
echo   %ARTIFACT_DIR%\zephyr.elf
echo HEX:
echo   %ARTIFACT_DIR%\zephyr.hex
endlocal
