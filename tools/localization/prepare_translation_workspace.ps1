[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string[]]$Languages,

    [string[]]$Modules,

    [switch]$ImportArchives,

    [switch]$Force,

    [string]$TranslatorExe
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

function Get-RepoRoot
{
    return Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
}

function Expand-StringList([string[]]$Values)
{
    $items = New-Object System.Collections.Generic.List[string]

    foreach ($value in $Values)
    {
        if ([string]::IsNullOrWhiteSpace($value))
        {
            continue
        }

        foreach ($piece in ($value -split "[,;]"))
        {
            $trimmed = $piece.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed))
            {
                $items.Add($trimmed)
            }
        }
    }

    return $items.ToArray()
}

function Get-RelativePath([string]$FromDirectory, [string]$ToPath)
{
    return [System.IO.Path]::GetRelativePath($FromDirectory, $ToPath).Replace('/', '\')
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

function Get-NormalizedTextContent
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $content = Get-Content -LiteralPath $Path -Raw
    if ($content.Length -gt 0 -and $content[0] -eq [char]0xFEFF)
    {
        $content = $content.Substring(1)
    }

    return $content.Replace("`r`n", "`n").Replace("`r", "`n").Replace("`n", "`r`n")
}

function Get-RcFileForInclude
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$IncludePath
    )

    $candidate = Join-Path (Split-Path $IncludePath -Parent) "lang.rc"
    if (Test-Path -LiteralPath $candidate)
    {
        return $candidate
    }

    return $null
}

function Get-IncludeCandidatePath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$CurrentFilePath,

        [Parameter(Mandatory = $true)]
        [string]$IncludeValue
    )

    $normalizedInclude = $IncludeValue.Replace("/", "\").Replace("\\", "\")
    $candidatePath = Join-Path (Split-Path $CurrentFilePath -Parent) $normalizedInclude
    if (Test-Path -LiteralPath $candidatePath)
    {
        return (Get-FullPath $candidatePath)
    }

    return $null
}

function Get-ReferencedSymbolFiles
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$EntryPath,

        [Parameter()]
        [System.Collections.Generic.HashSet[string]]$VisitedPaths
    )

    if ([string]::IsNullOrWhiteSpace($EntryPath) -or -not (Test-Path -LiteralPath $EntryPath))
    {
        return @()
    }

    $resolvedEntryPath = Get-FullPath $EntryPath
    if (-not $VisitedPaths.Add($resolvedEntryPath))
    {
        return @()
    }

    $symbolFiles = New-Object System.Collections.Generic.List[string]
    if ([System.IO.Path]::GetExtension($resolvedEntryPath).Equals(".rh2", [System.StringComparison]::OrdinalIgnoreCase))
    {
        $symbolFiles.Add($resolvedEntryPath)
    }

    foreach ($line in Get-Content -LiteralPath $resolvedEntryPath)
    {
        if ($line -match '^\s*#include\s+"([^"]+)"')
        {
            $includedPath = Get-IncludeCandidatePath -CurrentFilePath $resolvedEntryPath -IncludeValue $matches[1]
            if ($null -eq $includedPath)
            {
                continue
            }

            $extension = [System.IO.Path]::GetExtension($includedPath)
            if ($extension -in @(".rc", ".rc2", ".rh2"))
            {
                foreach ($symbolFile in Get-ReferencedSymbolFiles -EntryPath $includedPath -VisitedPaths $VisitedPaths)
                {
                    $symbolFiles.Add($symbolFile)
                }
            }
        }
    }

    return $symbolFiles.ToArray()
}

function Get-DefinedSymbolNames
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $definedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($line in ($Content -split "`r`n"))
    {
        if ($line -match '^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\b')
        {
            [void]$definedNames.Add($matches[1])
        }
    }

    return ,$definedNames
}

function Get-SupplementalSymbolDefinitions
{
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$SymbolFilePaths,

        [Parameter()]
        [System.Collections.Generic.HashSet[string]]$DefinedNames
    )

    $definitionLines = New-Object System.Collections.Generic.List[string]

    function Test-TranslatorSymbolValue
    {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Value,

            [Parameter()]
            [System.Collections.Generic.HashSet[string]]$KnownNames
        )

        $trimmedValue = ($Value -replace '\s*//.*$', '').Trim()
        if ([string]::IsNullOrWhiteSpace($trimmedValue))
        {
            return $false
        }

        if ($trimmedValue -match '^(?:\d+|0x[0-9A-Fa-f]+)$')
        {
            return $true
        }

        if ($trimmedValue -match '^\(?\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)?$')
        {
            return $KnownNames.Contains($matches[1])
        }

        if ($trimmedValue -match '^\(?\s*([A-Za-z_][A-Za-z0-9_]*)\s*([+-])\s*(\d+)\s*\)?$')
        {
            return $KnownNames.Contains($matches[1])
        }

        return $false
    }

    foreach ($symbolFilePath in $SymbolFilePaths)
    {
        foreach ($line in Get-Content -LiteralPath $symbolFilePath)
        {
            if ($line -match '^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$')
            {
                $name = $matches[1]
                if ($name -like "VERSINFO_*")
                {
                    continue
                }

                $value = $matches[2]
                if ((Test-TranslatorSymbolValue -Value $value -KnownNames $DefinedNames) -and $DefinedNames.Add($name))
                {
                    $definitionLines.Add($line.TrimEnd())
                }
            }
        }
    }

    return $definitionLines.ToArray()
}

function Get-AugmentedSymbolsContent
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$IncludePath,

        [string]$RcPath
    )

    $content = Get-NormalizedTextContent -Path $IncludePath
    if ([string]::IsNullOrWhiteSpace($RcPath) -or -not (Test-Path -LiteralPath $RcPath))
    {
        return $content
    }

    $definedNames = Get-DefinedSymbolNames -Content $content
    $supplementalDefinitions = New-Object System.Collections.Generic.List[string]

    $standardDefinitions = @(
        @{ Name = "IDOK"; Value = 1 },
        @{ Name = "IDCANCEL"; Value = 2 },
        @{ Name = "IDABORT"; Value = 3 },
        @{ Name = "IDRETRY"; Value = 4 },
        @{ Name = "IDIGNORE"; Value = 5 },
        @{ Name = "IDYES"; Value = 6 },
        @{ Name = "IDNO"; Value = 7 },
        @{ Name = "IDCLOSE"; Value = 8 },
        @{ Name = "IDHELP"; Value = 9 }
    )

    foreach ($definition in $standardDefinitions)
    {
        if ($definedNames.Add($definition.Name))
        {
            $supplementalDefinitions.Add(("#define {0,-30} {1}" -f $definition.Name, $definition.Value))
        }
    }

    $visitedPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $symbolFilePaths = New-Object System.Collections.Generic.List[string]

    foreach ($symbolFilePath in Get-ReferencedSymbolFiles -EntryPath $RcPath -VisitedPaths $visitedPaths)
    {
        $symbolFilePaths.Add($symbolFilePath)
    }

    $textsRc2Path = Join-Path (Split-Path $RcPath -Parent) "texts.rc2"
    if (Test-Path -LiteralPath $textsRc2Path)
    {
        foreach ($symbolFilePath in Get-ReferencedSymbolFiles -EntryPath $textsRc2Path -VisitedPaths $visitedPaths)
        {
            $symbolFilePaths.Add($symbolFilePath)
        }
    }

    foreach ($definitionLine in Get-SupplementalSymbolDefinitions -SymbolFilePaths $symbolFilePaths.ToArray() -DefinedNames $definedNames)
    {
        $supplementalDefinitions.Add($definitionLine)
    }

    if ($supplementalDefinitions.Count -eq 0)
    {
        return $content
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append($content.TrimEnd("`r", "`n"))
    [void]$builder.Append("`r`n`r`n// Translator workspace supplemental symbols`r`n")
    foreach ($definition in $supplementalDefinitions)
    {
        [void]$builder.Append($definition)
        [void]$builder.Append("`r`n")
    }

    return $builder.ToString()
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

        [int]$TimeoutSeconds = 60
    )

    Write-Host "Running Translator: $Description"

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

    if ($process.ExitCode -ne 0)
    {
        throw "Translator failed while running '$Description' (exit code $($process.ExitCode)).$(Get-QuietFailureDetails -ProjectPath $ProjectPath)"
    }
}

function New-ProjectFile
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$OriginalPath,

        [Parameter(Mandatory = $true)]
        [string]$TranslatedPath,

        [Parameter(Mandatory = $true)]
        [string]$IncludePath,

        [Parameter(Mandatory = $true)]
        [string]$SalamanderExePath,

        [Parameter(Mandatory = $true)]
        [string]$ExportArchivePath
    )

    $content = @"
[Files]
Original=$OriginalPath
Translated=$TranslatedPath
Include=$IncludePath
SalamanderExe=$SalamanderExePath
ExportAsTextArchive=$ExportArchivePath

[Settings]
ExpandStrings=1
ExpandMenus=1
ExpandDialogs=1
SelectedTreeItem=0

[DialogsTranslation]

[MenusTranslation]

[StringsTranslation]

[Relayout]
"@

    $normalizedContent = $content.Replace("`r`n", "`n").Replace("`n", "`r`n")
    [System.IO.File]::WriteAllText($ProjectPath, $normalizedContent, [System.Text.Encoding]::ASCII)
}

function Copy-TranslatorIncludeFile
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath,

        [string]$RcPath
    )

    $normalizedContent = Get-AugmentedSymbolsContent -IncludePath $SourcePath -RcPath $RcPath
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($DestinationPath, $normalizedContent, $utf8NoBom)
}

function Get-IncludeFileForPlugin
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$PluginName
    )

    $exactPath = Join-Path $RepoRoot "src\\plugins\\$PluginName\\lang\\lang.rh"
    if (Test-Path -LiteralPath $exactPath)
    {
        return $exactPath
    }

    if ($PluginName -eq "pak")
    {
        $pakPath = Join-Path $RepoRoot "src\\plugins\\pak\\spl\\lang\\lang.rh"
        if (Test-Path -LiteralPath $pakPath)
        {
            return $pakPath
        }
    }

    $candidates = Get-ChildItem -Path (Join-Path $RepoRoot "src\\plugins") -Recurse -Filter lang.rh |
        Where-Object { $_.FullName -match "\\$([regex]::Escape($PluginName))(\\|$)" }

    if ($candidates.Count -eq 1)
    {
        return $candidates[0].FullName
    }

    throw "Could not resolve lang.rh for plugin '$PluginName'."
}

$repoRoot = Get-FullPath (Get-RepoRoot)
$buildRootPath = Get-FullPath $BuildRoot
$outputDirPath = Get-FullPath $OutputDir
$translationsRepoRoot = Join-Path $repoRoot "translations"
$runtimeRoot = Join-Path $outputDirPath "runtime"
$projectsRoot = Join-Path $outputDirPath "projects"
$workspaceTranslationsRoot = Join-Path $outputDirPath "translations"

if (-not (Test-Path -LiteralPath (Join-Path $buildRootPath "sally.exe")))
{
    throw "Build root '$buildRootPath' does not look like a populated Sally output directory."
}

if (-not $TranslatorExe)
{
    $buildTranslator = Join-Path $buildRootPath "utils\\translator.exe"
    if (-not (Test-Path -LiteralPath $buildTranslator))
    {
        throw "Build root '$buildRootPath' is missing utils\\translator.exe."
    }
}

if (Test-Path -LiteralPath $outputDirPath)
{
    if (-not $Force)
    {
        throw "Output directory '$outputDirPath' already exists. Re-run with -Force to replace it."
    }

    Remove-Item -LiteralPath $outputDirPath -Recurse -Force
}

New-Item -ItemType Directory -Path $outputDirPath, $runtimeRoot, $projectsRoot, $workspaceTranslationsRoot | Out-Null

Write-Host "Copying populated build output to workspace runtime..."
Copy-Item -Path (Join-Path $buildRootPath "*") -Destination $runtimeRoot -Recurse -Force

$requestedLanguages = @()
if ($Languages -and $Languages.Count -gt 0)
{
    $requestedLanguages = Expand-StringList $Languages
}
else
{
    $requestedLanguages = Get-ChildItem -Path $translationsRepoRoot -Directory | ForEach-Object { $_.Name } | Sort-Object
}

$requestedLanguages = $requestedLanguages | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique

Copy-Item -LiteralPath (Join-Path $translationsRepoRoot "readme.txt") -Destination $workspaceTranslationsRoot -Force
foreach ($language in $requestedLanguages)
{
    $sourceLanguageDir = Join-Path $translationsRepoRoot $language
    if (-not (Test-Path -LiteralPath $sourceLanguageDir))
    {
        throw "Unknown language '$language'."
    }

    Copy-Item -LiteralPath $sourceLanguageDir -Destination $workspaceTranslationsRoot -Recurse -Force
}

$moduleSpecs = @{}

$coreSlg = Join-Path $runtimeRoot "lang\\english.slg"
$coreInclude = Join-Path $repoRoot "src\\lang\\lang.rh"
if (Test-Path -LiteralPath $coreSlg)
{
    $moduleSpecs["sally"] = [pscustomobject]@{
        Name = "sally"
        SourceSlg = $coreSlg
        IncludeFile = $coreInclude
        RcFile = Get-RcFileForInclude -IncludePath $coreInclude
    }
}

Get-ChildItem -Path (Join-Path $runtimeRoot "plugins") -Recurse -Filter english.slg | ForEach-Object {
    $relativePath = Get-RelativePath -FromDirectory $runtimeRoot -ToPath $_.FullName
    if ($relativePath -match "^plugins\\([^\\]+)\\lang\\english\.slg$")
    {
        $pluginName = $matches[1].ToLowerInvariant()
        $includeFile = Get-IncludeFileForPlugin -RepoRoot $repoRoot -PluginName $pluginName
        $moduleSpecs[$pluginName] = [pscustomobject]@{
            Name = $pluginName
            SourceSlg = $_.FullName
            IncludeFile = $includeFile
            RcFile = Get-RcFileForInclude -IncludePath $includeFile
        }
    }
}

$selectedModules = @()
if ($Modules -and $Modules.Count -gt 0)
{
    $selectedModules = Expand-StringList $Modules | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique
}
else
{
    $selectedModules = $moduleSpecs.Keys | Sort-Object
}

foreach ($module in $selectedModules)
{
    if (-not $moduleSpecs.ContainsKey($module))
    {
        throw "Unknown module '$module'."
    }
}

if ($TranslatorExe) {
    $translatorExe = $TranslatorExe
} else {
    $translatorExe = Join-Path $runtimeRoot "utils\\translator.exe"
}
$generatedProjects = 0
$importedArchives = 0
$importFailures = New-Object System.Collections.Generic.List[string]
$discoveredArchives = 0
$seedOnlyProjects = 0

foreach ($language in $requestedLanguages)
{
    foreach ($module in $selectedModules)
    {
        $spec = $moduleSpecs[$module]
        $projectDir = Join-Path $projectsRoot "$language\\$module"
        New-Item -ItemType Directory -Path $projectDir -Force | Out-Null

        $projectPath = Join-Path $projectDir "$module.atp"
        $targetSlgPath = Join-Path $projectDir "$language.slg"
        $includeDestination = Join-Path $projectDir "lang.rh"
        $translationArchivePath = Join-Path $workspaceTranslationsRoot "$language\\$module.slt"

        Copy-Item -LiteralPath $spec.SourceSlg -Destination $targetSlgPath -Force
        Copy-TranslatorIncludeFile -SourcePath $spec.IncludeFile -DestinationPath $includeDestination -RcPath $spec.RcFile

        New-ProjectFile `
            -ProjectPath $projectPath `
            -OriginalPath $spec.SourceSlg `
            -TranslatedPath $targetSlgPath `
            -IncludePath $includeDestination `
            -SalamanderExePath (Join-Path $runtimeRoot "sally.exe") `
            -ExportArchivePath $translationArchivePath

        $generatedProjects++

        if (Test-Path -LiteralPath $translationArchivePath)
        {
            $discoveredArchives++
            if ($ImportArchives)
            {
                $translationDir = Split-Path $translationArchivePath -Parent
                try
                {
                    Invoke-TranslatorQuiet `
                        -TranslatorExe $translatorExe `
                        -Arguments @("-quiet-import-slt", $translationDir, $projectPath) `
                        -Description "$language/$module import" `
                        -ProjectPath $projectPath
                    $importedArchives++
                }
                catch
                {
                    $importFailures.Add("${language}/${module}: $($_.Exception.Message)")
                    Write-Warning "Translator import did not complete cleanly for ${language}/${module}. The generated workspace is still usable for manual investigation."
                }
            }
        }
        else
        {
            $seedOnlyProjects++
        }
    }
}

Write-Host ""
Write-Host "Translation workspace created at: $outputDirPath"
Write-Host "Projects generated: $generatedProjects"
Write-Host "Existing archives discovered: $discoveredArchives"
if ($ImportArchives)
{
    Write-Host "Existing archives imported: $importedArchives"
    Write-Host "Archive import failures: $($importFailures.Count)"
}
Write-Host "Seed-only projects: $seedOnlyProjects"

if ($importFailures.Count -gt 0)
{
    Write-Host ""
    Write-Warning "Import failures:"
    foreach ($failure in $importFailures)
    {
        Write-Warning "  $failure"
    }
}
