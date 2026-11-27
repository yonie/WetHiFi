@echo off
REM Build and run hifitest - measures HiFiEngine with no plugin and no host.
REM Run from the plugin repo root:  tools\build-hifitest.bat

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul

if not exist build-tools mkdir build-tools

cl /nologo /EHsc /O2 /W4 /std:c++17 /I WetHiFi\source ^
   /Fe:build-tools\hifitest.exe /Fo:build-tools\ ^
   tools\hifitest.cpp WetHiFi\source\hifiengine.cpp
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
start "" /b /belownormal /wait build-tools\hifitest.exe %*
exit /b %errorlevel%
