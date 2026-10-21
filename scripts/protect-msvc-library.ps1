# Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
# All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
#
# SPDX-License-Identifier: BSD-2-Clause

param(
    [Parameter(Mandatory = $true)][string]$Librarian,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$ObjectList
)

$ErrorActionPreference = 'Stop'

function Clear-CoffDebugRecords([string]$InputPath, [string]$OutputPath) {
    $data = [IO.File]::ReadAllBytes($InputPath)
    if ($data.Length -lt 20 -or [BitConverter]::ToUInt16($data, 0) -notin @(0x8664, 0x014c, 0xaa64)) {
        throw 'CPU archive protection requires ordinary MSVC COFF objects; disable /GL and /bigobj.'
    }
    $sectionCount = [BitConverter]::ToUInt16($data, 2)
    $symbolOffset = [BitConverter]::ToUInt32($data, 8)
    $symbolCount = [BitConverter]::ToUInt32($data, 12)
    $sectionOffset = 20 + [BitConverter]::ToUInt16($data, 16)
    if ($sectionOffset + 40 * $sectionCount -gt $data.Length -or
        [long]$symbolOffset + 18 * [long]$symbolCount -gt $data.Length) { throw 'Invalid COFF tables.' }
    $debugSections = @{}
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $header = $sectionOffset + 40 * $index
        $name = [Text.Encoding]::ASCII.GetString($data, $header, 8).Trim([char]0)
        if (!$name.StartsWith('.debug')) { continue }
        $size = [BitConverter]::ToUInt32($data, $header + 16)
        $offset = [BitConverter]::ToUInt32($data, $header + 20)
        if ([long]$offset + $size -gt $data.Length) { throw 'Invalid COFF debug section.' }

        # Keep section indices stable: MSVC .pdata/.voltbl metadata uses them.
        [Array]::Clear($data, $offset, $size)
        [Array]::Clear($data, $header + 16, 20)
        $data[$header + 39] = $data[$header + 39] -band 0xfe
        $debugSections[$index + 1] = $true
    }
    for ($index = 0; $index -lt $symbolCount; $index++) {
        $symbol = $symbolOffset + 18 * $index
        $section = [BitConverter]::ToInt16($data, $symbol + 12)
        $storageClass = $data[$symbol + 16]
        $auxCount = $data[$symbol + 17]
        if ($index + $auxCount -ge $symbolCount) { throw 'Invalid COFF auxiliary records.' }
        if ($auxCount -and $debugSections.ContainsKey([int]$section) -and $storageClass -eq 3) {
            [Array]::Clear($data, $symbol + 18, 12)
        }
        if ($storageClass -eq 103) {
            [Array]::Clear($data, $symbol + 18, 18 * $auxCount)
        }
        $index += $auxCount
    }
    [IO.File]::WriteAllBytes($OutputPath, $data)
}

$outputPath = [IO.Path]::GetFullPath($Output)
$objects = @(Get-Content -LiteralPath $ObjectList | Where-Object { $_.Trim() } | ForEach-Object { [IO.Path]::GetFullPath($_) })
if (!$objects.Count) { throw 'No objects supplied for the protected library.' }
$stagingPath = Join-Path ([IO.Path]::GetDirectoryName($outputPath)) ([IO.Path]::GetRandomFileName())
$null = New-Item -ItemType Directory -Path $stagingPath
try {
    $members = @()
    for ($index = 0; $index -lt $objects.Count; $index++) {
        $member = 'u{0:D6}.obj' -f $index
        Clear-CoffDebugRecords $objects[$index] (Join-Path $stagingPath $member)
        $members += $member
    }
    Push-Location -LiteralPath $stagingPath
    try {
        & $Librarian /NOLOGO /OUT:library.lib @members
        if ($LASTEXITCODE) { throw 'Failed to archive protected objects.' }
    } finally {
        Pop-Location
    }

    # Verify known source/object names and build paths before publishing the archive.
    $libraryPath = Join-Path $stagingPath 'library.lib'
    $bytes = [IO.File]::ReadAllBytes($libraryPath)
    $byteEncoding = [Text.Encoding]::GetEncoding(28591)
    $text = $byteEncoding.GetString($bytes)
    $root = (Get-Location).Path
    $sensitiveNames = @($root, $root.Replace('\', '/'))
    $sensitiveNames += @($objects | ForEach-Object { [IO.Path]::GetFileName($_) })
    foreach ($sourceDir in @('interface', 'backends', 'gallery', 'include')) {
        $sensitiveNames += @(Get-ChildItem -LiteralPath (Join-Path $root $sourceDir) -Recurse -File |
            Where-Object { $_.Extension -in @('.c', '.cpp', '.cu', '.h') } | ForEach-Object { $_.Name })
    }
    foreach ($name in ($sensitiveNames | Sort-Object -Unique)) {
        foreach ($encoding in @([Text.Encoding]::UTF8, [Text.Encoding]::Unicode)) {
            if ($text.Contains($byteEncoding.GetString($encoding.GetBytes($name)))) {
                throw "Protected archive still contains source/object/build information: $name"
            }
        }
    }
    foreach ($decoded in @($text, [Text.Encoding]::Unicode.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1))) {
        if ($decoded -match '(?i)[a-z]:[\\/][a-z0-9_. -]+[\\/]|\\\\[a-z0-9_.-]+\\[a-z0-9_. -]+\\') {
            throw 'Protected archive still contains an absolute filesystem path.'
        }
    }
    if (Test-Path -LiteralPath $outputPath) {
        [IO.File]::Replace($libraryPath, $outputPath, [NullString]::Value)
    } else {
        [IO.File]::Move($libraryPath, $outputPath)
    }
    Write-Output "Protected CPU archive: $Output ($($members.Count) anonymous members, debug records cleared)"
} finally {
    Remove-Item -LiteralPath $stagingPath -Recurse
    Remove-Item -LiteralPath $ObjectList
}
