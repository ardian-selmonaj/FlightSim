@echo off
rem Updates the Python bindings (the skybolt module) after changing C++ code, e.g. in
rem src\Skybolt\SkyboltPythonBindings\PythonBindings.cpp. Builds the existing FlightSim build again with CMake,
rem which compiles only what changed, and checks that "import skybolt" works.
rem The build folder is FlightSimBuild next to the repository, or the folder in FLIGHTSIM_BUILD_DIR.
rem Run setup_windows.bat instead after adding or removing source files or changing dependencies (see README.md).
rem No labels or goto: cmd can miss labels in batch files with LF line endings, which .gitattributes enforces.
setlocal

for %%i in ("%~dp0..") do set "REPO_DIR=%%~fi"
if not defined FLIGHTSIM_BUILD_DIR set "FLIGHTSIM_BUILD_DIR=%REPO_DIR%\..\FlightSimBuild"
for %%i in ("%FLIGHTSIM_BUILD_DIR%") do set "BUILD_DIR=%%~fi"
set "FLIGHTSIM_BUILD_DIR=%BUILD_DIR%"
set "VENV_PYTHON=%REPO_DIR%\.venv\Scripts\python.exe"
set "ERROR="

rem The cmake.exe that configured the build, i.e. the one of the VS Build Tools, which is not on PATH.
rem The MSVC compiler environment is not needed, the Visual Studio generator finds MSBuild by itself.
set "CMAKE_EXE="
if exist "%BUILD_DIR%\CMakeCache.txt" for /f "tokens=1,* delims==" %%a in ('findstr /b /c:"CMAKE_COMMAND:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CMAKE_EXE=%%b"
if defined CMAKE_EXE set "CMAKE_EXE=%CMAKE_EXE:/=\%"
if not defined CMAKE_EXE set "ERROR=No FlightSim build found in %BUILD_DIR%. Run setup_windows.bat first, or set FLIGHTSIM_BUILD_DIR."
if not defined ERROR if not exist "%CMAKE_EXE%" set "ERROR=%CMAKE_EXE% of the build not found. Run setup_windows.bat."
if not defined ERROR if not exist "%VENV_PYTHON%" set "ERROR=%VENV_PYTHON% not found. Run setup_windows.bat first."

pushd "%REPO_DIR%"

if not defined ERROR echo ==^> Building into %BUILD_DIR%, only what changed is compiled again
if not defined ERROR "%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --parallel || set "ERROR=Build failed. If skybolt.cp311-win_amd64.pyd cannot be opened (LNK1104), close all Python programs that use skybolt and run this again."

if not defined ERROR echo.
if not defined ERROR echo ==^> Checking that the skybolt Python module loads
if not defined ERROR "%VENV_PYTHON%" -c "import flightsim_env, skybolt; print('import skybolt: OK')" || set "ERROR=import skybolt failed, see the error above."

popd

echo.
if defined ERROR echo Update failed: %ERROR%
if not defined ERROR echo Python bindings updated.
pause
if defined ERROR exit /b 1
exit /b 0
