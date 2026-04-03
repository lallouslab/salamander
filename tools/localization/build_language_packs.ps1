<#
.SYNOPSIS
    Build translated .slg language packs from committed .slt archives.

.DESCRIPTION
    Post-build step that produces non-English .slg files for all available
    translations. Generates a translator workspace, imports .slt archives
    via translator.exe quiet mode, and copies the resulting .slg files into
    the populated runtime tree.

    This script is meant to run after a normal Sally build + populate.
    It requires translator.exe in the build output.

.PARAMETER BuildRoot
    Path to the populated build output (e.g., build/out/sally/Release_x64).

.PARAMETER Languages
    Comma-separated list of languages to build. If omitted, builds all
    languages that have translation archives in the repo.

.PARAMETER WorkspaceDir
    Path for the temporary translator workspace. Defaults to a temp directory.

.PARAMETER SkipValidation
    Skip quiet-validate-all after import (faster, less safe).

.PARAMETER AllowSeedRejections
    Treat seed rejections (untranslated modules) as warnings instead of errors.
    Useful for local dev builds where incomplete localization is acceptable.

.PARAMETER TranslatorExe
    Path to translator.exe to use instead of the one in BuildRoot.
    Enables cross-architecture builds: an x64 translator.exe can produce SLGs
    for any architecture since BeginUpdateResource is arch-agnostic.

.EXAMPLE
    pwsh -File tools\localization\build_language_packs.ps1 `
        -BuildRoot build\out\sally\Release_x64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [string[]]$Languages,

    [string]$WorkspaceDir,

    [switch]$SkipValidation,

    [switch]$AllowSeedRejections,

    [string]$TranslatorExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$repoRoot = Split-Path (Split-Path $scriptDir -Parent) -Parent
$prepareScript = Join-Path $scriptDir "prepare_translation_workspace.ps1"

if (-not (Test-Path -LiteralPath $prepareScript))
{
    throw "Cannot find prepare_translation_workspace.ps1 at: $prepareScript"
}

$buildRootFull = if (Test-Path -LiteralPath $BuildRoot) {
    (Resolve-Path -LiteralPath $BuildRoot).Path
} else {
    throw "BuildRoot not found: $BuildRoot"
}

if (-not $TranslatorExe)
{
    $TranslatorExe = Join-Path $buildRootFull "utils\translator.exe"
}
if (-not (Test-Path -LiteralPath $TranslatorExe))
{
    throw "translator.exe not found at: $TranslatorExe"
}

# Determine languages
$translationsDir = Join-Path $repoRoot "translations"
$allLanguages = Get-ChildItem -Path $translationsDir -Directory |
    Where-Object { (Get-ChildItem -LiteralPath $_.FullName -Filter "*.slt" -File).Count -gt 0 } |
    ForEach-Object { $_.Name } |
    Sort-Object

if ($Languages -and $Languages.Count -gt 0)
{
    $requestedLanguages = @()
    foreach ($lang in $Languages)
    {
        foreach ($piece in ($lang -split "[,;]"))
        {
            $trimmed = $piece.Trim().ToLowerInvariant()
            if ($trimmed -ne "")
            {
                $requestedLanguages += $trimmed
            }
        }
    }
}
else
{
    $requestedLanguages = $allLanguages
}

Write-Host "Building language packs for: $($requestedLanguages -join ', ')"
Write-Host "Build root: $buildRootFull"

# Create workspace directory
if (-not $WorkspaceDir)
{
    $WorkspaceDir = Join-Path ([System.IO.Path]::GetTempPath()) "sally-langpack-workspace"
}

if (Test-Path -LiteralPath $WorkspaceDir)
{
    Remove-Item -Recurse -Force -LiteralPath $WorkspaceDir
}

# Generate workspace with archive import
Write-Host ""
Write-Host "Generating translator workspace..."
$prepareArgs = @{
    BuildRoot = $buildRootFull
    OutputDir = $WorkspaceDir
    Languages = ($requestedLanguages -join ",")
    ImportArchives = $true
    Force = $true
}
if ($TranslatorExe)
{
    $prepareArgs['TranslatorExe'] = (Resolve-Path -LiteralPath $TranslatorExe).Path
}
& $prepareScript @prepareArgs

if ((Test-Path variable:LASTEXITCODE) -and $LASTEXITCODE -ne 0)
{
    throw "Workspace generation failed with exit code $LASTEXITCODE"
}

# Copy translated .slg files into the runtime tree
$projectsRoot = Join-Path $WorkspaceDir "projects"
$workspaceRuntimeRoot = Join-Path $WorkspaceDir "runtime"
$copied = 0
$copyFailures = New-Object System.Collections.Generic.List[string]
$seedRejections = New-Object System.Collections.Generic.List[string]
$validated = 0
$validationWarnings = New-Object System.Collections.Generic.List[string]

foreach ($language in $requestedLanguages)
{
    $langProjectDir = Join-Path $projectsRoot $language
    if (-not (Test-Path -LiteralPath $langProjectDir))
    {
        Write-Warning "No projects found for language: $language"
        continue
    }

    foreach ($moduleDir in (Get-ChildItem -LiteralPath $langProjectDir -Directory))
    {
        $moduleName = $moduleDir.Name
        $slgFile = Join-Path $moduleDir.FullName "$language.slg"
        $atpFile = Join-Path $moduleDir.FullName "$moduleName.atp"

        if (-not (Test-Path -LiteralPath $slgFile))
        {
            $copyFailures.Add("${language}/${moduleName}: .slg not found")
            continue
        }

        # Reject untouched English seeds — if import failed, the .slg is
        # still an identical copy of english.slg and must not be shipped
        # under a translated filename.
        $englishSlgPath = if ($moduleName -eq "sally") {
            Join-Path $workspaceRuntimeRoot "lang\english.slg"
        } else {
            Join-Path $workspaceRuntimeRoot "plugins\$moduleName\lang\english.slg"
        }
        if ((Test-Path -LiteralPath $englishSlgPath) -and
            (Get-FileHash -LiteralPath $slgFile).Hash -eq (Get-FileHash -LiteralPath $englishSlgPath).Hash)
        {
            $seedRejections.Add("${language}/${moduleName}: rejected (identical to English seed)")
            continue
        }

        # Optional validation (translator.exe is a GUI app; quiet modes auto-close on completion)
        if (-not $SkipValidation -and (Test-Path -LiteralPath $atpFile))
        {
            try
            {
                $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
                $startInfo.FileName = $TranslatorExe
                $startInfo.WorkingDirectory = Split-Path $TranslatorExe -Parent
                $startInfo.UseShellExecute = $false
                [void]$startInfo.ArgumentList.Add("-quiet-validate-layout")
                [void]$startInfo.ArgumentList.Add($atpFile)

                $proc = [System.Diagnostics.Process]::Start($startInfo)
                if (-not $proc.WaitForExit(60000))
                {
                    $proc.Kill($true)
                    $validationWarnings.Add("${language}/${moduleName}: validation timed out")
                }
                else
                {
                    $validated++
                    if ($proc.ExitCode -eq 1)
                    {
                        $validationWarnings.Add("${language}/${moduleName}: layout validation has findings")
                    }
                }
            }
            catch
            {
                # Validation failure is non-fatal
                $validationWarnings.Add("${language}/${moduleName}: validation error: $($_.Exception.Message)")
            }
        }

        # Determine destination in runtime tree
        if ($moduleName -eq "sally")
        {
            $destDir = Join-Path $buildRootFull "lang"
        }
        else
        {
            $destDir = Join-Path $buildRootFull "plugins\$moduleName\lang"
        }

        if (-not (Test-Path -LiteralPath $destDir))
        {
            # Plugin directory doesn't exist in runtime — skip silently
            # (e.g., ftp, pictview archives exist but plugin doesn't build)
            continue
        }

        $destFile = Join-Path $destDir "$language.slg"
        Copy-Item -LiteralPath $slgFile -Destination $destFile -Force
        $copied++
    }
}

# Summary
Write-Host ""
Write-Host "========================================"
Write-Host "  Language Pack Build Summary"
Write-Host "========================================"
Write-Host "  Languages:           $($requestedLanguages.Count)"
Write-Host "  .slg files copied:   $copied"
Write-Host "  Seed rejections:     $($seedRejections.Count)"

if (-not $SkipValidation)
{
    Write-Host "  Projects validated:  $validated"
    Write-Host "  Validation warnings: $($validationWarnings.Count)"
}

if ($seedRejections.Count -gt 0)
{
    Write-Host ""
    Write-Warning "Seed rejections (import failed, English seed not shipped):"
    foreach ($rejection in $seedRejections)
    {
        Write-Warning "  $rejection"
    }
}

if ($copyFailures.Count -gt 0)
{
    Write-Host ""
    Write-Warning "Copy failures:"
    foreach ($failure in $copyFailures)
    {
        Write-Warning "  $failure"
    }
}

if ($validationWarnings.Count -gt 0)
{
    Write-Host ""
    Write-Host "  Validation warnings (non-blocking):"
    foreach ($warning in $validationWarnings)
    {
        Write-Host "    $warning"
    }
}

# Verify
$langDir = Join-Path $buildRootFull "lang"
$coreLangs = Get-ChildItem -LiteralPath $langDir -Filter "*.slg" | ForEach-Object { $_.Name }
Write-Host ""
Write-Host "  Language packs now in runtime lang/: $($coreLangs -join ', ')"

# Cleanup workspace
if (-not $env:SALLY_KEEP_LANGPACK_WORKSPACE)
{
    Remove-Item -Recurse -Force -LiteralPath $WorkspaceDir -ErrorAction SilentlyContinue
    Write-Host "  Workspace cleaned up."
}
else
{
    Write-Host "  Workspace preserved at: $WorkspaceDir"
}

# Fail the build if any imports produced untranslated seeds or outright failed
$fatalFailures = $copyFailures.Count + $(if ($AllowSeedRejections) { 0 } else { $seedRejections.Count })
if ($fatalFailures -gt 0)
{
    throw "Language pack build completed with $fatalFailures failure(s): $($seedRejections.Count) seed rejection(s), $($copyFailures.Count) copy failure(s). The release zip has incomplete localization."
}
