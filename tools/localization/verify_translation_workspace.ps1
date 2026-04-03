[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceDir,

    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullPath([string]$Path)
{
    if (Test-Path -LiteralPath $Path)
    {
        return (Resolve-Path -LiteralPath $Path).Path
    }

    return [System.IO.Path]::GetFullPath($Path)
}

function Get-QuietLogPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $projectFullPath = Get-FullPath $ProjectPath
    $projectDir = Split-Path $projectFullPath -Parent
    $moduleName = [System.IO.Path]::GetFileNameWithoutExtension($projectFullPath)
    return Join-Path $projectDir "$moduleName.quiet.log"
}

function Get-QuietFailureDetails
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $quietLogPath = Get-QuietLogPath -ProjectPath $ProjectPath
    if (-not (Test-Path -LiteralPath $quietLogPath))
    {
        return " Expected quiet log: $quietLogPath"
    }

    $quietLog = Get-Content -LiteralPath $quietLogPath -Raw
    if ([string]::IsNullOrWhiteSpace($quietLog))
    {
        return " Quiet log is empty: $quietLogPath"
    }

    return " Quiet log ($quietLogPath):`n$quietLog"
}

function Invoke-TranslatorQuiet
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$TranslatorExe,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$Description,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [string]$ExpectedOutputPath,

        [int[]]$ExpectedExitCodes = @(0),

        [string[]]$RequiredLogText
    )

    Write-Host "Verifying: $Description"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $TranslatorExe
    $startInfo.WorkingDirectory = Split-Path $TranslatorExe -Parent
    $startInfo.UseShellExecute = $false

    foreach ($argument in $Arguments)
    {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        $process.Kill($true)
        throw "Translator timed out while running '$Description'.$(Get-QuietFailureDetails -ProjectPath $ProjectPath)"
    }

    $quietLogPath = Get-QuietLogPath -ProjectPath $ProjectPath
    if (-not (Test-Path -LiteralPath $quietLogPath))
    {
        throw "Translator finished '$Description' without writing the expected quiet log '$quietLogPath'."
    }

    $quietLog = Get-Content -LiteralPath $quietLogPath -Raw
    if ($ExpectedExitCodes -notcontains $process.ExitCode)
    {
        throw "Translator failed while running '$Description' (exit code $($process.ExitCode)).$(Get-QuietFailureDetails -ProjectPath $ProjectPath)"
    }

    foreach ($requiredText in $RequiredLogText)
    {
        if (-not $quietLog.Contains($requiredText))
        {
            throw "Translator finished '$Description' but '$quietLogPath' did not contain the expected text '$requiredText'.$(Get-QuietFailureDetails -ProjectPath $ProjectPath)"
        }
    }

    if ($PSBoundParameters.ContainsKey('ExpectedOutputPath') -and -not (Test-Path -LiteralPath $ExpectedOutputPath))
    {
        throw "Translator finished '$Description' but did not create '$ExpectedOutputPath'.$(Get-QuietFailureDetails -ProjectPath $ProjectPath)"
    }
}

$workspaceRoot = Get-FullPath $WorkspaceDir
$translatorExe = Join-Path $workspaceRoot "runtime\\utils\\translator.exe"

if (-not (Test-Path -LiteralPath $translatorExe))
{
    throw "Workspace '$workspaceRoot' is missing runtime\\utils\\translator.exe."
}

$validations = @(
    @{
        Language = "czech"
        Module = "sally"
        ExpectedExitCodes = @(0, 1)
        RequiredLogText = @("[SUMMARY]")
    },
    @{
        Language = "slovak"
        Module = "sally"
        ExpectedExitCodes = @(0, 1)
        RequiredLogText = @("[SUMMARY]")
    },
    @{
        Language = "czech"
        Module = "webviewer"
        ExpectedExitCodes = @(0)
        RequiredLogText = @("[SUMMARY] All validations passed OK.")
    },
    @{
        Language = "slovak"
        Module = "automation"
        ExpectedExitCodes = @(0)
        RequiredLogText = @("[SUMMARY] All validations passed OK.")
    }
)

$exports = @(
    @{ Language = "czech"; Module = "sally" },
    @{ Language = "slovak"; Module = "automation" },
    @{ Language = "czech"; Module = "webviewer" }
)

foreach ($sample in $validations)
{
    $projectPath = Join-Path $workspaceRoot "projects\\$($sample.Language)\\$($sample.Module)\\$($sample.Module).atp"
    if (-not (Test-Path -LiteralPath $projectPath))
    {
        throw "Missing expected project '$projectPath'."
    }

    Invoke-TranslatorQuiet `
        -TranslatorExe $translatorExe `
        -Arguments @("-quiet-validate-all", $projectPath) `
        -Description "validate $($sample.Language)/$($sample.Module)" `
        -ProjectPath $projectPath `
        -ExpectedExitCodes $sample.ExpectedExitCodes `
        -RequiredLogText $sample.RequiredLogText
}

$exportRoot = Join-Path $workspaceRoot "verify-exports"
Remove-Item -LiteralPath $exportRoot -Recurse -Force -ErrorAction SilentlyContinue

foreach ($sample in $exports)
{
    $projectPath = Join-Path $workspaceRoot "projects\\$($sample.Language)\\$($sample.Module)\\$($sample.Module).atp"
    if (-not (Test-Path -LiteralPath $projectPath))
    {
        throw "Missing expected project '$projectPath'."
    }

    $exportDir = Join-Path $exportRoot "$($sample.Language)\\$($sample.Module)"
    $expectedOutputPath = Join-Path $exportDir "$($sample.Module).slt"
    New-Item -ItemType Directory -Path $exportDir -Force | Out-Null

    Invoke-TranslatorQuiet `
        -TranslatorExe $translatorExe `
        -Arguments @("-quiet-export-slt", $exportDir, $projectPath) `
        -Description "export $($sample.Language)/$($sample.Module)" `
        -ProjectPath $projectPath `
        -ExpectedOutputPath $expectedOutputPath
}

Write-Host ""
Write-Host "Translation workspace verification passed."
