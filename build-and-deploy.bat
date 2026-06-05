@echo off
setlocal enabledelayedexpansion

REM Get script directory for absolute paths
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

echo ========================================
echo   FalloutChat — Build and Deploy
echo ========================================
echo.

set /p TARGET_VER="Build for Old-Gen (OG) or Next-Gen (NG)? [OG/NG]: "
if /i "!TARGET_VER!"=="OG" (
    set PRISMA_TARGET=og
    echo Building for Old-Gen...
) else (
    set PRISMA_TARGET=ng
    echo Building for Next-Gen...
)
echo.

set /p DEPLOY_PATH="Enter deployment path: "

rem Strip double quotes if the user dragged-and-dropped or typed them
if defined DEPLOY_PATH set DEPLOY_PATH=!DEPLOY_PATH:"=!

echo.

echo Building TypeScript bundle...
cd /d "%SCRIPT_DIR%assets\views"
call npm run build
if errorlevel 1 (
    echo ERROR: TypeScript build failed
    cd /d "%SCRIPT_DIR%"
    pause
    exit /b 1
)
cd /d "%SCRIPT_DIR%"

if not exist "%SCRIPT_DIR%assets\views\dist\chat-bundle.js" (
    echo ERROR: Bundle file was not created after npm build
    pause
    exit /b 1
)

echo.
echo Building FalloutChat DLL...
xmake f -c -y
xmake
if errorlevel 1 (
    echo ERROR: xmake build failed
    pause
    exit /b 1
)

if not exist "%SCRIPT_DIR%build\windows\x64\release\FalloutChat.dll" (
    echo ERROR: Build output not found at build\windows\x64\release\FalloutChat.dll
    pause
    exit /b 1
)

echo.
echo Verifying build outputs...
if not exist "%SCRIPT_DIR%assets\views\chat.html" (
    echo ERROR: chat.html not found
    pause
    exit /b 1
)

echo.
echo ✓ Build successful
echo Note: Deployment is handled automatically by xmake's after_build hook
echo Deployed to predefined paths in xmake.lua
echo.
pause
