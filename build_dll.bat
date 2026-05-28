@echo off
setlocal

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg
set VCTOOLS_VERSION=14.44.35207
set DEPLOY_DIR=E:\Modlists\Fallen World Alpha 2 NG\mods\FalloutChat\F4SE\Plugins

if not exist "build" (
    echo Configuring CMake...
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T "version=%VCTOOLS_VERSION%" --toolchain "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    if %ERRORLEVEL% NEQ 0 (
        echo CMake configuration failed!
        exit /b %ERRORLEVEL%
    )
)

echo Building C++ Plugin...
%MSBUILD% build\FalloutChat.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=%VCTOOLS_VERSION% /v:m
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo Deploying DLL and assets...
if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
copy /Y "build\Release\FalloutChat.dll" "%DEPLOY_DIR%\FalloutChat.dll"
copy /Y "fonts\fa-brands-400.ttf" "%DEPLOY_DIR%\fa-brands-400.ttf"
copy /Y "fonts\fa-solid-900.ttf" "%DEPLOY_DIR%\fa-solid-900.ttf"

echo.
echo Build and Deployment Successful!
endlocal
