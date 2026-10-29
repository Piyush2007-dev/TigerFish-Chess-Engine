@echo off
setlocal enabledelayedexpansion

echo ==============================================================================
echo  TIGERFISH TRACY PROFILER RUNNER
echo ==============================================================================

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
cd /d "%REPO_ROOT%"

set TRACY_GUI=tools\tracy\gui\tracy-profiler.exe
set TRACY_ENGINE=tools\bin\game_tracy.exe

if not exist "%TRACY_GUI%" (
    echo [ERROR] Tracy Profiler GUI not found at %TRACY_GUI%!
    pause
    exit /b 1
)

:: Build instrumented game_tracy.exe if not present or rebuild requested
if "%1"=="--build" goto do_build
if not exist "%TRACY_ENGINE%" goto do_build
goto launch_gui

:do_build
echo [BUILD] Compiling TigerFish with Tracy instrumentation (-DTRACY_ENABLE)...
if not exist "tools\bin" mkdir tools\bin
g++ -O3 -std=c++20 -DTRACY_ENABLE -I tools/tracy tools/tracy/TracyClient.cpp engine/main.cpp -o "%TRACY_ENGINE%" -lws2_32 -ldbghelp -lsecur32
if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)
echo [BUILD] Successfully compiled %TRACY_ENGINE%!
if "%1"=="--build" shift

:launch_gui
:: Check if Tracy GUI is already running
tasklist /fi "imagename eq tracy-profiler.exe" 2>NUL | find /i "tracy-profiler.exe" >NUL
if errorlevel 1 (
    echo [PROFILER] Launching Tracy Profiler GUI in background...
    start "" "%TRACY_GUI%" -a 127.0.0.1
    echo [PROFILER] Waiting 2 seconds for Tracy GUI to initialize socket...
    timeout /t 2 /nobreak >nul
) else (
    echo [PROFILER] Tracy Profiler GUI is already running.
)

:: Prepare search workload
set SEARCH_DEPTH=15
if not "%1"=="" set SEARCH_DEPTH=%1

set FEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
if not "%2"=="" set FEN=%2

echo [RUN] Running TigerFish search at depth %SEARCH_DEPTH%...
echo [RUN] Tracy client is broadcasting to localhost:8086...
echo.

"%TRACY_ENGINE%" analyze %FEN% %SEARCH_DEPTH%

echo.
echo ==============================================================================
echo  Profile session complete! View zones, flamegraphs, and plots in Tracy GUI.
echo ==============================================================================
pause
