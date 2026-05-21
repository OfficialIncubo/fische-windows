@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0;C:\Program Files\CMake\bin"
set "REQUESTED_ARCH=%~1"
if "%REQUESTED_ARCH%"=="" set "REQUESTED_ARCH=both"

if /I "%REQUESTED_ARCH%"=="both" (
  call :build_arch x64 x64-windows-static build-nmake-static fische-x64.exe
  if errorlevel 1 exit /b !errorlevel!
  call :build_arch x86 x86-windows-static build-nmake-static-x86 fische-x86.exe
  if errorlevel 1 exit /b !errorlevel!
  exit /b 0
)

if /I "%REQUESTED_ARCH%"=="x64" (
  call :build_arch x64 x64-windows-static build-nmake-static fische-x64.exe
  exit /b !errorlevel!
)

if /I "%REQUESTED_ARCH%"=="x86" (
  call :build_arch x86 x86-windows-static build-nmake-static-x86 fische-x86.exe
  exit /b !errorlevel!
)

echo Usage: build-vs2026.cmd [x64^|x86^|both]
exit /b 1

:build_arch
setlocal
set "ARCH=%~1"
set "TRIPLET=%~2"
set "BUILD_DIR=%~3"
set "OUTPUT_NAME=%~4"

echo.
echo ==== Building %ARCH% (%TRIPLET%) ====

call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=%ARCH%
if errorlevel 1 exit /b %errorlevel%

where cl
where nmake

cmake -S . -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=%TRIPLET% -DVCPKG_MANIFEST_MODE=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

copy /Y "%BUILD_DIR%\fische.exe" "%BUILD_DIR%\%OUTPUT_NAME%" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist dist mkdir dist
copy /Y "%BUILD_DIR%\%OUTPUT_NAME%" "dist\%OUTPUT_NAME%" >nul
if errorlevel 1 exit /b %errorlevel%

echo Output: %BUILD_DIR%\%OUTPUT_NAME%
echo Dist:   dist\%OUTPUT_NAME%
endlocal
exit /b 0
