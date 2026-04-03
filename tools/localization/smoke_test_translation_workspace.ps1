[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspaceDir,

    [int]$StartupWaitSeconds = 3
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

function Get-PortableExecutableMachine
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try
    {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Seek(0x3C, [System.IO.SeekOrigin]::Begin) | Out-Null
        $peOffset = $reader.ReadInt32()
        $stream.Seek($peOffset + 4, [System.IO.SeekOrigin]::Begin) | Out-Null
        return $reader.ReadUInt16()
    }
    finally
    {
        $stream.Dispose()
    }
}

function Test-TranslatorProjectOpen
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$TranslatorExe,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Write-Host "Smoke test: $Description"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $TranslatorExe
    $startInfo.WorkingDirectory = Split-Path $TranslatorExe -Parent
    $startInfo.UseShellExecute = $false
    [void]$startInfo.ArgumentList.Add($ProjectPath)

    $process = [System.Diagnostics.Process]::Start($startInfo)
    if ($process.WaitForExit($StartupWaitSeconds * 1000))
    {
        throw "Translator exited while opening '$Description' (exit code $($process.ExitCode))."
    }

    try
    {
        $process.Kill($true)
        [void]$process.WaitForExit(5000)
    }
    catch
    {
        throw "Translator stayed alive for '$Description', but cleanup failed: $($_.Exception.Message)"
    }
}

$workspaceRoot = Get-FullPath $WorkspaceDir
$translatorExe = Join-Path $workspaceRoot "runtime\\utils\\translator.exe"

if (-not (Test-Path -LiteralPath $translatorExe))
{
    throw "Workspace '$workspaceRoot' is missing runtime\\utils\\translator.exe."
}

$translatorMachine = Get-PortableExecutableMachine -Path $translatorExe

$samples = @(
    @{ Language = "czech"; Module = "sally" },
    @{ Language = "slovak"; Module = "sally" },
    @{ Language = "czech"; Module = "webviewer" }
)

foreach ($sample in $samples)
{
    $projectPath = Join-Path $workspaceRoot "projects\\$($sample.Language)\\$($sample.Module)\\$($sample.Module).atp"
    if (-not (Test-Path -LiteralPath $projectPath))
    {
        throw "Missing expected project '$projectPath'."
    }

    Test-TranslatorProjectOpen `
        -TranslatorExe $translatorExe `
        -ProjectPath $projectPath `
        -Description "open $($sample.Language)/$($sample.Module)"
}

Write-Host ""
Write-Host "Translation workspace project-open smoke tests passed."
