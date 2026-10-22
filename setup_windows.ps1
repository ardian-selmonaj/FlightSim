<#
.SYNOPSIS
    Windows setup of FlightSim (Skybolt). Started by setup_windows.bat.

.DESCRIPTION
    Every step checks whether it is already done, so the script can be re-run at any time
    (e.g. to rebuild after changing the C++ code):
      1. Visual Studio 2022 Build Tools with the C++ workload (MSVC v143, Windows SDK, C++ CMake tools)
      2. MSVC compiler environment (replaces the "Developer Command Prompt for VS 2022")
      3. Python 3.11 virtual environment .venv with conan, jsbsim and requests
         (Python 3.11 is installed next to other Python versions if it is missing)
      4. Conan default profile
      5. Conan build of FlightSim
      6. JSBSim root directory in the Skybolt settings (Settings.json)
      7. Check that "import skybolt" works
    Map tiles are downloaded separately with Tools\Tile_Map_Downloader.py.

.PARAMETER BuildDir
    Build output folder. Defaults to FlightSimBuild next to the repository, where flightsim_env.py looks for it.

.PARAMETER PythonExe
    Path to the python.exe of a Python 3.11 installation in a non-standard location.
#>
param(
    [string]$BuildDir = "",
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # Invoke-WebRequest is very slow with the progress bar in Windows PowerShell
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$PythonVersion = "3.11"
$PythonInstallerVersion = "3.11.9"  # latest 3.11 release with a Windows installer, later 3.11 releases are source-only
$ConanVersion = "2.22.0"
$JsbSimVersion = "1.3.3"

$RepoDir = $PSScriptRoot
$DefaultBuildDir = Join-Path (Split-Path $RepoDir -Parent) "FlightSimBuild"
if (-not $BuildDir) { $BuildDir = $DefaultBuildDir }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$VenvDir = Join-Path $RepoDir ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$VsComponents = @(
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",  # MSVC v143 C++ build tools
    "Microsoft.VisualStudio.Component.VC.CMake.Project"   # C++ CMake tools for Windows
)

trap {
    Write-Host ""
    Write-Host "Setup failed: $_" -ForegroundColor Red
    exit 1
}

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    # Runs a native command and stops the setup if it fails
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "'$File $Arguments' failed with exit code $LASTEXITCODE" }
}

function Find-VsInstall {
    # Returns the path of a Visual Studio 2022 installation with all required C++ components, or $null
    if (-not (Test-Path $VsWhere)) { return $null }
    $path = & $VsWhere -latest -products * -version "[17.0,18.0)" -requires $VsComponents -property installationPath
    if ($path) { return "$path".Trim() }
    return $null
}

function Import-BatchEnvironment([string]$BatchFile) {
    # Runs a .bat file in cmd and copies the resulting environment variables into this session
    $wrapper = Join-Path $env:TEMP "flightsim_setup_env.cmd"
    Set-Content -Path $wrapper -Encoding ASCII -Value "@call `"$BatchFile`" >nul`r`n@set"
    try { $lines = & cmd.exe /d /c $wrapper }
    finally { Remove-Item $wrapper -ErrorAction SilentlyContinue }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -LiteralPath "env:$($Matches[1])" -Value $Matches[2] }
    }
}

function Get-PythonVersion([string]$Exe) {
    # Returns the version of a Python interpreter, e.g. "3.11.0", or $null
    if (-not $Exe -or -not (Test-Path $Exe)) { return $null }
    $version = & $Exe -c "import sys; print('%d.%d.%d' % sys.version_info[:3])"
    if ($LASTEXITCODE -ne 0) { return $null }
    return "$version".Trim()
}

function Test-PythonVersion([string]$Version) {
    # True for any 3.11.x version
    return [bool]$Version -and $Version.StartsWith("$PythonVersion.")
}

function Find-Python {
    # Returns the path of an installed Python 3.11 interpreter, or $null
    $ErrorActionPreference = "Continue"  # "py" reports a missing Python version on stderr
    $candidates = @()
    if ($PythonExe) { $candidates += $PythonExe }
    if (Get-Command py -ErrorAction SilentlyContinue) {
        $exe = & py -3.11 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and $exe) { $candidates += "$exe".Trim() }
    }
    $candidates += Join-Path $env:LOCALAPPDATA "Programs\Python\Python311\python.exe"
    $candidates += Join-Path $env:ProgramFiles "Python311\python.exe"

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        $version = Get-PythonVersion $candidate
        if (Test-PythonVersion $version) { return $candidate }
        if ($version) { Write-Host "Ignoring Python $version at $candidate (Python $PythonVersion is required)" }
    }
    return $null
}

# ---------------------------------------------------------------------------------------------
Write-Step "1/7 Visual Studio 2022 Build Tools"
$VsPath = Find-VsInstall
if ($VsPath) {
    Write-Host "Found $VsPath"
} else {
    $installer = Join-Path $RepoDir "Tools\VSBuildTools.exe"
    if (-not (Test-Path $installer)) {
        $installer = Join-Path $env:TEMP "vs_BuildTools.exe"
        Write-Host "Tools\VSBuildTools.exe not found in the repository, downloading it ..."
        Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" -OutFile $installer -UseBasicParsing
    }
    $vsArgs = "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    foreach ($component in $VsComponents) { $vsArgs += " --add $component" }

    # An existing Build Tools installation that lacks components is modified instead of installed again
    $existing = if (Test-Path $VsWhere) { & $VsWhere -latest -products Microsoft.VisualStudio.Product.BuildTools -version "[17.0,18.0)" -property installationPath }
    if ($existing) { $vsArgs = "modify --installPath `"$("$existing".Trim())`" $vsArgs" }

    Write-Host "Installing with $installer (confirm the administrator prompt, this takes a while) ..."
    $process = Start-Process -FilePath $installer -ArgumentList $vsArgs -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -notin 0, 3010) { throw "Visual Studio Build Tools installation failed with exit code $($process.ExitCode)" }
    $VsPath = Find-VsInstall
    if (-not $VsPath) { throw "Visual Studio Build Tools were installed, but the required C++ components are not found" }
}

# ---------------------------------------------------------------------------------------------
Write-Step "2/7 MSVC compiler environment"
$env:PATH = "$(Split-Path $VsWhere);$env:PATH"  # vcvars64.bat calls vswhere.exe
Import-BatchEnvironment (Join-Path $VsPath "VC\Auxiliary\Build\vcvars64.bat")
foreach ($tool in "cl", "cmake") {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $command) { throw "'$tool' not found after loading the MSVC compiler environment" }
    Write-Host "$tool -> $($command.Source)"
}

# ---------------------------------------------------------------------------------------------
Write-Step "3/7 Python $PythonVersion virtual environment"
if (-not (Test-Path $VenvPython)) {
    $basePython = Find-Python
    if (-not $basePython) {
        $target = Join-Path $env:LOCALAPPDATA "Programs\Python\Python311"
        if (Test-Path (Join-Path $target "python.exe")) {
            throw "The Python installation in $target does not work. Repair or remove it, or pass -PythonExe <path to a Python $PythonVersion python.exe>."
        }
        # Installed for the current user into its own folder, next to any other Python versions. PATH, file associations
        # and the py launcher are left unchanged, so the default "python" of the system stays the same.
        $pyInstaller = Join-Path $env:TEMP "python-$PythonInstallerVersion-amd64.exe"
        Write-Host "Python $PythonVersion not found, installing Python $PythonInstallerVersion into $target ..."
        Invoke-WebRequest -Uri "https://www.python.org/ftp/python/$PythonInstallerVersion/python-$PythonInstallerVersion-amd64.exe" -OutFile $pyInstaller -UseBasicParsing
        $process = Start-Process -FilePath $pyInstaller -Wait -PassThru `
            -ArgumentList "/quiet InstallAllUsers=0 PrependPath=0 AssociateFiles=0 Include_launcher=0 Include_test=0 Shortcuts=0 TargetDir=`"$target`""
        if ($process.ExitCode -ne 0) { throw "Python $PythonInstallerVersion installation failed with exit code $($process.ExitCode)" }
        $basePython = Join-Path $target "python.exe"
    }
    Write-Host "Creating $VenvDir with $basePython"
    Invoke-Checked $basePython @("-m", "venv", $VenvDir)
}
$venvVersion = Get-PythonVersion $VenvPython
if (-not (Test-PythonVersion $venvVersion)) {
    throw "$VenvDir uses Python $venvVersion instead of $PythonVersion. Delete the .venv folder and run the setup again."
}
Write-Host "$VenvDir uses Python $venvVersion"
Invoke-Checked $VenvPython @("-m", "pip", "install", "--disable-pip-version-check", "conan==$ConanVersion", "jsbsim==$JsbSimVersion", "requests")

# Use the venv for the rest of the setup (conanfile.py calls "conan" itself)
$env:VIRTUAL_ENV = $VenvDir
$env:PATH = "$VenvDir\Scripts;$env:PATH"

# ---------------------------------------------------------------------------------------------
Write-Step "4/7 Conan profile"
$conanHome = "$(& conan config home)".Trim()
$profilePath = Join-Path $conanHome "profiles\default"
if (Test-Path $profilePath) {
    Write-Host "Using existing profile $profilePath"
} else {
    Invoke-Checked conan @("profile", "detect")
}

# ---------------------------------------------------------------------------------------------
Write-Step "5/7 Building FlightSim into $BuildDir (the first build compiles all dependencies and takes long)"
$python3Exe = $VenvPython -replace '\\', '/'
Invoke-Checked conan @(
    "build", $RepoDir,
    "--output-folder=$BuildDir",
    "--build=missing",
    "-s:a", "compiler.cppstd=17",  # required by JSBSim
    "-o", "openscenegraph-mr/*:shared=True",
    "-o", "enable_jsbsim=True",
    "-o", "enable_python=True",
    "-o", "enable_qt=False",
    "-c", "tools.cmake.cmaketoolchain:extra_variables={'Python3_EXECUTABLE': '$python3Exe'}"  # build the module for the venv's Python
)

# ---------------------------------------------------------------------------------------------
Write-Step "6/7 Skybolt settings"
# Same location as getAppUserDataDirectory("Skybolt") in Skybolt
$settingsDir = Join-Path "$env:HOMEDRIVE$env:HOMEPATH" "AppData\Local\Skybolt"
$settingsFile = Join-Path $settingsDir "Settings.json"
$settings = if (Test-Path $settingsFile) { Get-Content $settingsFile -Raw | ConvertFrom-Json } else { New-Object PSObject }
$jsbRoot = $settings.jsb.rootDir
if ($jsbRoot -and (Test-Path (Join-Path $jsbRoot "aircraft"))) {
    Write-Host "Keeping JSBSim root directory $jsbRoot from $settingsFile"
} else {
    # The jsbsim Python package ships the aircraft, engine and systems data
    $jsbRoot = & $VenvPython -c "import jsbsim; print(jsbsim.get_default_root_dir().replace('\\', '/'))"
    if ($LASTEXITCODE -ne 0 -or -not $jsbRoot) { throw "Could not locate the data of the jsbsim Python package" }
    $jsbRoot = "$jsbRoot".Trim()
    if (-not $settings.jsb) { $settings | Add-Member -NotePropertyName jsb -NotePropertyValue (New-Object PSObject) }
    $settings.jsb | Add-Member -NotePropertyName rootDir -NotePropertyValue $jsbRoot -Force
    New-Item -ItemType Directory -Force -Path $settingsDir | Out-Null
    # UTF-8 without BOM
    [System.IO.File]::WriteAllText($settingsFile, ($settings | ConvertTo-Json -Depth 10), (New-Object System.Text.UTF8Encoding $false))
    Write-Host "Set jsb.rootDir to $jsbRoot in $settingsFile"
}

# ---------------------------------------------------------------------------------------------
Write-Step "7/7 Checking that the skybolt Python module loads"
$env:FLIGHTSIM_BUILD_DIR = $BuildDir
Push-Location $RepoDir
try { Invoke-Checked $VenvPython @("-c", "import flightsim_env, skybolt; print('import skybolt: OK')") }
finally { Pop-Location }

Write-Host ""
Write-Host "Setup complete." -ForegroundColor Green
Write-Host "Next steps, from $RepoDir :"
Write-Host "  .venv\Scripts\python Tools\Tile_Map_Downloader.py   (map tiles of Switzerland)"
Write-Host "  .venv\Scripts\python Test_FlightSim_Windows.py"
if ($BuildDir -ne $DefaultBuildDir) {
    Write-Host "Set the environment variable FLIGHTSIM_BUILD_DIR=$BuildDir so that flightsim_env.py finds the build." -ForegroundColor Yellow
}
