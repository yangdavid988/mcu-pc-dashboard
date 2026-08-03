param(
    [string]$RunCommand = ""
)

# ============================================
#  xxx_demo\env.ps1
#  Calls SDK env.bat for environment setup, then enters pwsh / powershell
#  Detects PS edition at runtime — works with both PowerShell 7+ and 5.1
#  Does NOT modify any SDK files — remains usable after SDK sync
#  Supports -RunCommand parameter for automatic command execution
# ============================================

# Switch to demo directory (ensures ameba.py runs in the correct path)
Set-Location $PSScriptRoot
$demoName = Split-Path -Leaf $PSScriptRoot

$sdkRoot = $env:AMEBA_ENV_PATH
if (-not $sdkRoot) {
    # When env variable is not set, infer SDK path from script location
    $sdkRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "ameba-rtos"
    if (-not (Test-Path $sdkRoot)) {
        Write-Host "ERROR: AMEBA_ENV_PATH not set and SDK not found at $sdkRoot." -ForegroundColor Red
        Write-Host "Set system env var, e.g.:" -ForegroundColor Yellow
        Write-Host "  AMEBA_ENV_PATH = C:\path\to\ameba-rtos" -ForegroundColor Yellow
        exit 1
    }
    Write-Host "⚠ AMEBA_ENV_PATH not set, using SDK from script location: $sdkRoot" -ForegroundColor Yellow
}

# Ensure env variable is available at process level, inheritable by child processes (cmd.exe -> pwsh)
$env:AMEBA_ENV_PATH = $sdkRoot

$originalBat = Join-Path $sdkRoot "env.bat"
if (-not (Test-Path $originalBat)) {
    Write-Host "ERROR: SDK env.bat not found at $originalBat" -ForegroundColor Red
    exit 1
}

# Detect PS edition — choose pwsh (Core) or powershell (Desktop) for the launch command
$shellExe = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh' } else { 'powershell' }

# Clean up any leftover temp files from previous runs
$tempBat = Join-Path $PSScriptRoot "_env_ps.bat"
Remove-Item $tempBat -Force -ErrorAction SilentlyContinue

# Read SDK env.bat, replace BASE_DIR and last line cmd.exe /k -> PS launch command
$lines = Get-Content $originalBat

# Since _env_ps.bat is in the demo directory, override BASE_DIR to SDK root
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq 'set "BASE_DIR=%~dp0"') {
        $lines[$i] = 'set "BASE_DIR=' + $sdkRoot + '"'
        break
    }
}

# If -RunCommand is specified, append to launch command tail
$extraCmd = ""
if ($RunCommand) { $extraCmd = "; $RunCommand" }

$psCommand = "$shellExe -NoExit -Command ""& '%BASE_DIR%\.venv\Scripts\Activate.ps1'; " + `
    "`$Host.UI.RawUI.WindowTitle = '$demoName'; " + `
    "function build.py { python build.py `$args }; " + `
    "function menuconfig.py { python menuconfig.py `$args }; " + `
    "function flash.py { python flash.py `$args }; " + `
    "function monitor.py { python monitor.py `$args }; " + `
    "function ameba.py { python '%BASE_DIR%\ameba.py' `$args }; " + `
    "function bb { ameba.py build }; " + `
    "function bm { ameba.py menuconfig }; " + `
    "function bp { ameba.py build -p }; " + `
    "function bms { ameba.py menuconfig -s prj.conf }" + `
    "$extraCmd"""

$lines[-1] = $psCommand

# Write to demo directory (%~dp0 is overridden, so path doesn't matter)
$lines | Out-File -FilePath $tempBat -Encoding ascii -Force

try {
    # Run temp bat -> SDK init -> auto-enter pwsh (with venv + aliases)
    & cmd.exe /c $tempBat
}
finally {
    # Clean up temp file
    Remove-Item $tempBat -Force -ErrorAction SilentlyContinue
}
