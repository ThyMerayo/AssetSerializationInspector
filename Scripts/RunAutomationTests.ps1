<#
.SYNOPSIS
    Builds a host project with this plugin and runs the plugin's automation tests.

.DESCRIPTION
    The same steps the CI workflow runs, so a failure on the build machine can be reproduced locally:

      1. optionally links this plugin into the host project's Plugins folder (a junction),
      2. builds the host project's editor target,
      3. runs every test of the plugin with UnrealEditor-Cmd (no UI),
      4. runs the widget tests with the full editor (Slate is only initialized there; under -Cmd they skip themselves),
      5. reads the logs: the run fails on any failed test, on a run that ended without finishing its queue (a crash or a
         timeout), and on a run that found no tests.

    Logs are copied to -OutputDir.

.PARAMETER EngineRoot
    The engine, such as D:\dev\UnrealEngine (the folder that holds Engine\).

.PARAMETER HostProject
    The .uproject of a project that can load the plugin (its Plugins folder gets a link to this plugin when -LinkPlugin is given).

.PARAMETER LinkPlugin
    Link this repository into the host project's Plugins folder, and remove the link again afterwards.

.PARAMETER Filter
    Which tests to run in the -Cmd run. Defaults to all the plugin's tests.

.PARAMETER TimeoutMinutes
    How long each editor run may take before it is stopped and counted as a failure.

.PARAMETER SkipBuild
    Do not build; use the binaries that are there.

.EXAMPLE
    .\Scripts\RunAutomationTests.ps1 -EngineRoot D:\dev\UnrealEngine -HostProject D:\dev\ASIHost\ASIHost.uproject -LinkPlugin
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $EngineRoot,
    [Parameter(Mandatory = $true)] [string] $HostProject,
    [switch] $LinkPlugin,
    [string] $Filter = 'AssetSerializationInspector',
    [int] $TimeoutMinutes = 30,
    [switch] $SkipBuild,
    [string] $OutputDir = ''
)

$ErrorActionPreference = 'Stop'

$PluginRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$HostProject = (Resolve-Path $HostProject).Path
$HostDir = Split-Path $HostProject -Parent
$ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($HostProject)
$EngineRoot = (Resolve-Path $EngineRoot).Path
$Build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$LogDir = Join-Path $HostDir 'Saved\Logs'

if ($OutputDir -eq '') { $OutputDir = Join-Path $PluginRoot 'Saved\AutomationLogs' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

foreach ($Required in @($Build, $EditorCmd, $Editor)) {
    if (-not (Test-Path $Required)) { throw "Not found: $Required (is -EngineRoot an engine built from source?)" }
}

function Write-Step([string] $Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------------------------------------------- link
$Link = Join-Path $HostDir 'Plugins\AssetSerializationInspector'
$LinkCreated = $false

if ($LinkPlugin) {
    Write-Step "Linking the plugin into $Link"
    if (Test-Path $Link) {
        $Existing = Get-Item $Link -Force
        $Target = if ($Existing.LinkType) { $Existing.Target | Select-Object -First 1 } else { $null }
        if ($Target -ne $PluginRoot) {
            throw "$Link exists and is not a link to $PluginRoot. Remove it or run without -LinkPlugin."
        }
    }
    else {
        New-Item -ItemType Directory -Force -Path (Split-Path $Link -Parent) | Out-Null
        New-Item -ItemType Junction -Path $Link -Target $PluginRoot | Out-Null
        $LinkCreated = $true
    }
}

# ---------------------------------------------------------------------------------------------------------------- run
function Invoke-Editor([string] $Exe, [string] $TestFilter, [string] $LogName) {
    $LogPath = Join-Path $LogDir $LogName
    Remove-Item $LogPath -ErrorAction SilentlyContinue

    $Arguments = @(
        "`"$HostProject`"",
        "-ExecCmds=`"Automation RunTests $TestFilter`"",
        '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nullrhi', '-nosplash',
        "-log=$LogName"
    )

    $Process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru -WorkingDirectory $HostDir -WindowStyle Hidden
    if (-not $Process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        Write-Host "Stopping $Exe after $TimeoutMinutes minutes." -ForegroundColor Red
        Stop-Process -Id $Process.Id -Force
        return @{ Log = $LogPath; TimedOut = $true }
    }

    return @{ Log = $LogPath; TimedOut = $false }
}

function Read-Results([string] $LogPath, [string] $Name) {
    if (-not (Test-Path $LogPath)) {
        Write-Host "${Name}: no log was written ($LogPath)." -ForegroundColor Red
        return @{ Passed = 0; Failed = 1; Finished = $false }
    }

    $Lines = Get-Content $LogPath
    $Passed = @($Lines | Where-Object { $_ -match 'Test Completed\. Result=\{Success\}' }).Count
    $FailedLines = @($Lines | Where-Object { $_ -match 'Test Completed\. Result=\{Fail\}' })
    # The line the editor logs when it leaves because the queue is empty. The command line, which the log echoes, also holds the words
    # 'Automation Test Queue Empty' (it is the -TestExit argument), so matching them alone passes a run that crashed.
    $Finished = @($Lines | Where-Object { $_ -match '\*\*\*\* TestExit: Automation Test Queue Empty \*\*\*\*' }).Count -gt 0

    Write-Host "${Name}: $Passed passed, $($FailedLines.Count) failed, queue finished: $Finished"

    if ($FailedLines.Count -gt 0) {
        foreach ($Line in $FailedLines) { Write-Host "  FAILED: $($Line -replace '^.*Name=\{([^}]*)\}.*Path=\{([^}]*)\}.*$', '$2')" -ForegroundColor Red }

        # The assertion messages of the failed tests, to read in the CI log without opening the artifact.
        $Lines | Where-Object { $_ -match 'Error: Expected|Error: Condition failed' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
    }

    return @{ Passed = $Passed; Failed = $FailedLines.Count; Finished = $Finished }
}

$Failed = $false

try {
    if (-not $SkipBuild) {
        Write-Step "Building ${ProjectName}Editor"
        & $Build "${ProjectName}Editor" Win64 Development "-Project=$HostProject" -WaitMutex
        if ($LASTEXITCODE -ne 0) { throw "The build failed (exit code $LASTEXITCODE)." }
    }

    Write-Step "Running '$Filter' with UnrealEditor-Cmd"
    $Cmd = Invoke-Editor $EditorCmd $Filter 'AsiCiAll.log'
    Copy-Item $Cmd.Log (Join-Path $OutputDir 'AsiCiAll.log') -ErrorAction SilentlyContinue
    $CmdResults = Read-Results $Cmd.Log 'All tests (-Cmd)'

    Write-Step 'Running the widget tests with the full editor'
    $Gui = Invoke-Editor $Editor "$Filter.Widgets" 'AsiCiWidgets.log'
    Copy-Item $Gui.Log (Join-Path $OutputDir 'AsiCiWidgets.log') -ErrorAction SilentlyContinue
    $GuiResults = Read-Results $Gui.Log 'Widget tests (full editor)'

    foreach ($Run in @(@{ Name = 'The -Cmd run'; Results = $CmdResults; Timed = $Cmd.TimedOut }, @{ Name = 'The widget run'; Results = $GuiResults; Timed = $Gui.TimedOut })) {
        if ($Run.Timed) { Write-Host "$($Run.Name) timed out." -ForegroundColor Red; $Failed = $true }
        if ($Run.Results.Passed -eq 0) { Write-Host "$($Run.Name) found no passing test." -ForegroundColor Red; $Failed = $true }
        if (-not $Run.Results.Finished) { Write-Host "$($Run.Name) did not finish its queue (a crash or a stop)." -ForegroundColor Red; $Failed = $true }
        if ($Run.Results.Failed -gt 0) { $Failed = $true }
    }
}
finally {
    if ($LinkCreated) {
        # Removes the junction only, not the repository it points at.
        (Get-Item $Link -Force).Delete()
    }
}

if ($Failed) {
    Write-Host "`nAutomation tests FAILED. Logs: $OutputDir" -ForegroundColor Red
    exit 1
}

Write-Host "`nAutomation tests passed. Logs: $OutputDir" -ForegroundColor Green
exit 0
