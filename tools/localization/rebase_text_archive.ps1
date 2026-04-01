[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CurrentArchive,

    [Parameter(Mandatory = $true)]
    [string]$LegacyArchive,

    [Parameter(Mandatory = $true)]
    [string]$OutputArchive
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

function Get-Sections
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $sections = New-Object System.Collections.Generic.List[object]
    $currentHeader = $null
    $currentLines = New-Object System.Collections.Generic.List[string]

    foreach ($line in Get-Content -LiteralPath $Path)
    {
        if ($line -match '^\[.+\]$')
        {
            if ($null -ne $currentHeader)
            {
                while ($currentLines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($currentLines[$currentLines.Count - 1]))
                {
                    $currentLines.RemoveAt($currentLines.Count - 1)
                }

                $sections.Add([pscustomobject]@{
                        Header = $currentHeader
                        Lines  = $currentLines.ToArray()
                    })
            }

            $currentHeader = $line
            $currentLines = New-Object System.Collections.Generic.List[string]
            continue
        }

        if ($null -eq $currentHeader)
        {
            if (-not [string]::IsNullOrWhiteSpace($line))
            {
                throw "Unexpected content before first section in '$Path'."
            }
            continue
        }

        $currentLines.Add($line)
    }

    if ($null -ne $currentHeader)
    {
        while ($currentLines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($currentLines[$currentLines.Count - 1]))
        {
            $currentLines.RemoveAt($currentLines.Count - 1)
        }

        $sections.Add([pscustomobject]@{
                Header = $currentHeader
                Lines  = $currentLines.ToArray()
            })
    }

    return $sections.ToArray()
}

function Get-SectionMap
{
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Sections
    )

    $map = @{}
    foreach ($section in $Sections)
    {
        $map[$section.Header] = $section
    }

    return $map
}

function Get-KeyedLineMap
{
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Lines
    )

    $map = @{}
    foreach ($line in $Lines)
    {
        if ([string]::IsNullOrWhiteSpace($line))
        {
            continue
        }

        $commaIndex = $line.IndexOf(',')
        if ($commaIndex -lt 0)
        {
            throw "Expected a keyed line, got '$line'."
        }

        $map[$line.Substring(0, $commaIndex)] = $line
    }

    return $map
}

function Parse-DialogCaption
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    if ($Line -notmatch '^(?<w>-?\d+),(?<h>-?\d+),(?<state>\d+),"(?<text>.*)"$')
    {
        throw "Unable to parse dialog caption line '$Line'."
    }

    return [pscustomobject]@{
        Width  = [int]$matches.w
        Height = [int]$matches.h
        State  = [int]$matches.state
        Text   = $matches.text
    }
}

function Parse-DialogItem
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    if ($Line -notmatch '^(?<id>\d+),(?<x>-?\d+),(?<y>-?\d+),(?<w>-?\d+),(?<h>-?\d+),(?<state>\d+),"(?<text>.*)"$')
    {
        throw "Unable to parse dialog item line '$Line'."
    }

    return [pscustomobject]@{
        Id     = [int]$matches.id
        X      = [int]$matches.x
        Y      = [int]$matches.y
        Width  = [int]$matches.w
        Height = [int]$matches.h
        State  = [int]$matches.state
        Text   = $matches.text
    }
}

function Parse-MenuItem
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    if ($Line -notmatch '^(?<id>\d+),(?<state>\d+),"(?<text>.*)"$')
    {
        throw "Unable to parse menu item line '$Line'."
    }

    return [pscustomobject]@{
        Id    = [int]$matches.id
        State = [int]$matches.state
        Text  = $matches.text
    }
}

function Parse-StringItem
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Line
    )

    if ($Line -notmatch '^(?<id>\d+),(?<state>\d+),"(?<text>.*)"$')
    {
        throw "Unable to parse string item line '$Line'."
    }

    return [pscustomobject]@{
        Id    = [int]$matches.id
        State = [int]$matches.state
        Text  = $matches.text
    }
}

function Format-DialogCaption
{
    param(
        [Parameter(Mandatory = $true)]
        [object]$Current,

        [object]$Legacy
    )

    $state = if ($null -ne $Legacy) { $Legacy.State } else { $Current.State }
    $text = if ($null -ne $Legacy) { $Legacy.Text } else { $Current.Text }

    return '{0},{1},{2},"{3}"' -f $Current.Width, $Current.Height, $state, $text
}

function Format-DialogItem
{
    param(
        [Parameter(Mandatory = $true)]
        [object]$Current,

        [object]$Legacy
    )

    $state = if ($null -ne $Legacy) { $Legacy.State } else { $Current.State }
    $text = if ($null -ne $Legacy) { $Legacy.Text } else { $Current.Text }

    return '{0},{1},{2},{3},{4},{5},"{6}"' -f $Current.Id, $Current.X, $Current.Y, $Current.Width, $Current.Height, $state, $text
}

function Format-KeyedTextItem
{
    param(
        [Parameter(Mandatory = $true)]
        [object]$Current,

        [object]$Legacy
    )

    $state = if ($null -ne $Legacy) { $Legacy.State } else { $Current.State }
    $text = if ($null -ne $Legacy) { $Legacy.Text } else { $Current.Text }

    return '{0},{1},"{2}"' -f $Current.Id, $state, $text
}

function Merge-DialogSection
{
    param(
        [Parameter(Mandatory = $true)]
        [object]$CurrentSection,

        [Parameter(Mandatory = $true)]
        [object]$LegacySection,

        [Parameter(Mandatory = $true)]
        [hashtable]$Stats
    )

    $mergedLines = New-Object System.Collections.Generic.List[string]

    $currentCaption = Parse-DialogCaption $CurrentSection.Lines[0]
    $legacyCaption = Parse-DialogCaption $LegacySection.Lines[0]
    $mergedLines.Add((Format-DialogCaption -Current $currentCaption -Legacy $legacyCaption))
    $Stats.DialogCaptions++

    $legacyItems = @{}
    foreach ($line in ($LegacySection.Lines | Select-Object -Skip 1))
    {
        if ([string]::IsNullOrWhiteSpace($line))
        {
            continue
        }

        $item = Parse-DialogItem $line
        $legacyItems[$item.Id] = $item
    }

    foreach ($line in ($CurrentSection.Lines | Select-Object -Skip 1))
    {
        if ([string]::IsNullOrWhiteSpace($line))
        {
            continue
        }

        $currentItem = Parse-DialogItem $line
        $legacyItem = if ($legacyItems.ContainsKey($currentItem.Id)) { $legacyItems[$currentItem.Id] } else { $null }
        if ($null -ne $legacyItem)
        {
            $Stats.DialogItems++
        }
        $mergedLines.Add((Format-DialogItem -Current $currentItem -Legacy $legacyItem))
    }

    return $mergedLines.ToArray()
}

function Merge-KeyedSection
{
    param(
        [Parameter(Mandatory = $true)]
        [object]$CurrentSection,

        [Parameter(Mandatory = $true)]
        [object]$LegacySection,

        [Parameter(Mandatory = $true)]
        [scriptblock]$ParseLine,

        [Parameter(Mandatory = $true)]
        [string]$StatKey
    )

    $legacyItems = @{}
    foreach ($line in $LegacySection.Lines)
    {
        if ([string]::IsNullOrWhiteSpace($line))
        {
            continue
        }

        $item = & $ParseLine $line
        $legacyItems[$item.Id] = $item
    }

    $mergedLines = New-Object System.Collections.Generic.List[string]
    foreach ($line in $CurrentSection.Lines)
    {
        if ([string]::IsNullOrWhiteSpace($line))
        {
            continue
        }

        $currentItem = & $ParseLine $line
        $legacyItem = if ($legacyItems.ContainsKey($currentItem.Id)) { $legacyItems[$currentItem.Id] } else { $null }
        if ($null -ne $legacyItem)
        {
            $script:MergeStats[$StatKey]++
        }
        $mergedLines.Add((Format-KeyedTextItem -Current $currentItem -Legacy $legacyItem))
    }

    return $mergedLines.ToArray()
}

$currentArchivePath = Get-FullPath $CurrentArchive
$legacyArchivePath = Get-FullPath $LegacyArchive
$outputArchivePath = Get-FullPath $OutputArchive

$currentSections = Get-Sections -Path $currentArchivePath
$legacySections = Get-Sections -Path $legacyArchivePath
$currentMap = Get-SectionMap -Sections $currentSections
$legacyMap = Get-SectionMap -Sections $legacySections

$script:MergeStats = @{
    DialogCaptions = 0
    DialogItems    = 0
    MenuItems      = 0
    StringItems    = 0
    AddedSections  = 0
    DroppedSections = 0
}

# Log sections present in current but not in legacy (new UI) and vice versa (removed UI)
$currentStructuralHeaders = [System.Collections.Generic.HashSet[string]]::new()
$currentSections | Where-Object { $_.Header -match '^\[(DIALOG|MENU|STRINGTABLE) ' } | ForEach-Object { [void]$currentStructuralHeaders.Add($_.Header) }
$legacyStructuralHeaders = [System.Collections.Generic.HashSet[string]]::new()
$legacySections | Where-Object { $_.Header -match '^\[(DIALOG|MENU|STRINGTABLE) ' } | ForEach-Object { [void]$legacyStructuralHeaders.Add($_.Header) }

foreach ($h in $currentStructuralHeaders)
{
    if (-not $legacyStructuralHeaders.Contains($h))
    {
        Write-Host "  Added section (new in current, will use English): $h"
        $script:MergeStats.AddedSections++
    }
}
foreach ($h in $legacyStructuralHeaders)
{
    if (-not $currentStructuralHeaders.Contains($h))
    {
        Write-Host "  Dropped section (removed from current): $h"
        $script:MergeStats.DroppedSections++
    }
}

$mergedSections = New-Object System.Collections.Generic.List[object]

foreach ($currentSection in $currentSections)
{
    switch -Regex ($currentSection.Header)
    {
        '^\[EXPORTINFO\]$'
        {
            $currentInfo = Get-KeyedLineMap -Lines $currentSection.Lines
            $legacyInfo = if ($legacyMap.ContainsKey($currentSection.Header)) { Get-KeyedLineMap -Lines $legacyMap[$currentSection.Header].Lines } else { @{} }

            $lines = @()
            $lines += if ($legacyInfo.ContainsKey('PROJECTNAME')) { $legacyInfo['PROJECTNAME'] } else { $currentInfo['PROJECTNAME'] }
            $lines += if ($currentInfo.ContainsKey('TEXTVERSION')) { $currentInfo['TEXTVERSION'] } elseif ($legacyInfo.ContainsKey('TEXTVERSION')) { $legacyInfo['TEXTVERSION'] }
            $lines += if ($currentInfo.ContainsKey('VERSION')) { $currentInfo['VERSION'] } elseif ($legacyInfo.ContainsKey('VERSION')) { $legacyInfo['VERSION'] }

            $mergedSections.Add([pscustomobject]@{
                    Header = $currentSection.Header
                    Lines  = $lines
                })
            continue
        }

        '^\[TRANSLATION\]$'
        {
            $currentTranslation = Get-KeyedLineMap -Lines $currentSection.Lines
            $legacyTranslation = if ($legacyMap.ContainsKey($currentSection.Header)) { Get-KeyedLineMap -Lines $legacyMap[$currentSection.Header].Lines } else { @{} }

            $keys = @('LANGID', 'AUTHOR', 'WEB', 'COMMENT', 'HELPDIR', 'SLGINCOMPLETE')
            $lines = New-Object System.Collections.Generic.List[string]
            foreach ($key in $keys)
            {
                if ($legacyTranslation.ContainsKey($key))
                {
                    $lines.Add($legacyTranslation[$key])
                }
                elseif ($currentTranslation.ContainsKey($key))
                {
                    $lines.Add($currentTranslation[$key])
                }
            }

            $mergedSections.Add([pscustomobject]@{
                    Header = $currentSection.Header
                    Lines  = $lines.ToArray()
                })
            continue
        }

        '^\[DIALOG '
        {
            if ($legacyMap.ContainsKey($currentSection.Header))
            {
                $legacySection = $legacyMap[$currentSection.Header]
                $mergedSections.Add([pscustomobject]@{
                        Header = $currentSection.Header
                        Lines  = Merge-DialogSection -CurrentSection $currentSection -LegacySection $legacySection -Stats $script:MergeStats
                    })
            }
            else
            {
                # New section — emit current (English) as-is
                $mergedSections.Add($currentSection)
            }
            continue
        }

        '^\[MENU '
        {
            if ($legacyMap.ContainsKey($currentSection.Header))
            {
                $legacySection = $legacyMap[$currentSection.Header]
                $mergedSections.Add([pscustomobject]@{
                        Header = $currentSection.Header
                        Lines  = Merge-KeyedSection -CurrentSection $currentSection -LegacySection $legacySection -ParseLine ${function:Parse-MenuItem} -StatKey 'MenuItems'
                    })
            }
            else
            {
                $mergedSections.Add($currentSection)
            }
            continue
        }

        '^\[STRINGTABLE '
        {
            if ($legacyMap.ContainsKey($currentSection.Header))
            {
                $legacySection = $legacyMap[$currentSection.Header]
                $mergedSections.Add([pscustomobject]@{
                        Header = $currentSection.Header
                        Lines  = Merge-KeyedSection -CurrentSection $currentSection -LegacySection $legacySection -ParseLine ${function:Parse-StringItem} -StatKey 'StringItems'
                    })
            }
            else
            {
                $mergedSections.Add($currentSection)
            }
            continue
        }

        '^\[RELAYOUT\]$'
        {
            $lines = if ($legacyMap.ContainsKey($currentSection.Header)) { $legacyMap[$currentSection.Header].Lines } else { $currentSection.Lines }
            $mergedSections.Add([pscustomobject]@{
                    Header = $currentSection.Header
                    Lines  = $lines
                })
            continue
        }

        default
        {
            $mergedSections.Add($currentSection)
            continue
        }
    }
}

$outputLines = New-Object System.Collections.Generic.List[string]
foreach ($section in $mergedSections)
{
    $outputLines.Add($section.Header)
    foreach ($line in $section.Lines)
    {
        $outputLines.Add($line)
    }
    $outputLines.Add('')
}

$outputText = [string]::Join("`r`n", $outputLines)
$utf8WithBom = [System.Text.UTF8Encoding]::new($true)
[System.IO.File]::WriteAllText($outputArchivePath, $outputText, $utf8WithBom)

Write-Host "Rebased archive written to: $outputArchivePath"
Write-Host "Reused dialog captions: $($script:MergeStats.DialogCaptions)"
Write-Host "Reused dialog items: $($script:MergeStats.DialogItems)"
Write-Host "Reused menu items: $($script:MergeStats.MenuItems)"
Write-Host "Reused string items: $($script:MergeStats.StringItems)"
if ($script:MergeStats.AddedSections -gt 0)
{
    Write-Host "Added sections (English): $($script:MergeStats.AddedSections)"
}
if ($script:MergeStats.DroppedSections -gt 0)
{
    Write-Host "Dropped sections: $($script:MergeStats.DroppedSections)"
}
