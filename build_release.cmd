@echo off
setlocal
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b %errorlevel%
cmake --build build --config Release
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build complete. Look in build\CloPlayer_artefacts\Release\VST3\
