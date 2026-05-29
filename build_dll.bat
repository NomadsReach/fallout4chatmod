@echo off
setlocal

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg
set VCTOOLS_VERSION=14.44.35207

set DEPLOY_DIR_1=E:\Modlists\Fallen World Alpha 2\mods\FalloutChat\F4SE\Plugins
set VIEWS_DIR_1=E:\Modlists\Fallen World Alpha 2\mods\FalloutChat\PrismaUI_F4\views

set DEPLOY_DIR_2=D:\Games\ModlistDownloads\mods\FalloutChat\F4SE\Plugins
set VIEWS_DIR_2=D:\Games\ModlistDownloads\mods\FalloutChat\PrismaUI_F4\views

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

echo Deploying to Fallen World Alpha 2...
if not exist "%DEPLOY_DIR_1%" mkdir "%DEPLOY_DIR_1%"
if not exist "%VIEWS_DIR_1%" mkdir "%VIEWS_DIR_1%"
copy /Y "build\Release\FalloutChat.dll" "%DEPLOY_DIR_1%\FalloutChat.dll"
copy /Y "assets\views\chat.html" "%VIEWS_DIR_1%\chat.html"

echo Deploying to ModlistDownloads...
if not exist "%DEPLOY_DIR_2%" mkdir "%DEPLOY_DIR_2%"
if not exist "%VIEWS_DIR_2%" mkdir "%VIEWS_DIR_2%"
copy /Y "build\Release\FalloutChat.dll" "%DEPLOY_DIR_2%\FalloutChat.dll"
copy /Y "assets\views\chat.html" "%VIEWS_DIR_2%\chat.html"

echo.
echo Build and Deployment Successful!
endlocal
