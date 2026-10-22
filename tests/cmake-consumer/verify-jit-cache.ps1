# Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
# SPDX-License-Identifier: BSD-2-Clause
param([Parameter(Mandatory = $true)][string]$Probe, [Parameter(Mandatory = $true)][string]$WorkDir)
$ErrorActionPreference = 'Stop'
$originalDebug = $env:CEED_DEBUG
$originalCache = $env:CEED_JIT_CACHE_DIR
$originalEnabled = $env:CEED_JIT_CACHE
$runRoot = Join-Path ([IO.Path]::GetFullPath($WorkDir)) ([Guid]::NewGuid().ToString('N'))
$env:CEED_JIT_CACHE_DIR = Join-Path $runRoot 'cache'
$env:CEED_JIT_CACHE = 'ON'
$env:CEED_DEBUG = '1'
$sources = Join-Path $runRoot 'external sources'

function Invoke-Probe([string[]]$Arguments) {
    $output = (& $Probe @Arguments 2>&1 | Out-String)
    if ($LASTEXITCODE) { throw "JIT probe failed: $output" }
    if ($output -match 'Final buffer:|Source:\s*\n|#include') { throw 'Protected JIT printed source text.' }
    return $output
}

try {
    $null = Invoke-Probe -Arguments @('write', $sources, '2')
    foreach ($backend in @('/gpu/cuda/ref', '/gpu/cuda/shared', '/gpu/cuda/gen')) {
        $first = Invoke-Probe -Arguments @($backend, $sources, '6')
        $second = Invoke-Probe -Arguments @($backend, $sources, '6')
        if ($second -notmatch 'cache hit' -or $second -match 'cache miss|cache unavailable') {
            throw "Cross-process reuse failed for $backend`: $second"
        }
    }
    $before = @(Get-ChildItem -LiteralPath $env:CEED_JIT_CACHE_DIR -Filter '*.bin').Count
    $null = Invoke-Probe -Arguments @('write', $sources, '3')
    $changed = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
    $after = @(Get-ChildItem -LiteralPath $env:CEED_JIT_CACHE_DIR -Filter '*.bin').Count
    if ($changed -notmatch 'cache miss' -or $after -le $before) { throw 'Changed include did not invalidate the cache.' }

    foreach ($file in Get-ChildItem -LiteralPath $env:CEED_JIT_CACHE_DIR -Filter '*.bin') {
        [IO.File]::WriteAllBytes($file.FullName, [byte[]](1, 2, 3))
    }
    $repaired = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
    $reused = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
    if ($repaired -notmatch 'cache miss' -or $reused -notmatch 'cache hit' -or $reused -match 'cache miss') {
        throw 'Corrupted cache did not recover.'
    }
    $env:CEED_JIT_CACHE = 'OFF'
    $disabled = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
    if ($disabled -notmatch 'cache unavailable' -or $disabled -match 'cache hit') { throw 'Cache disable setting was ignored.' }
    $env:CEED_JIT_CACHE = 'ON'
    $null = Invoke-Probe -Arguments @('write', $sources, '4', 'macro-include')
    $dynamic = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '12')
    if ($dynamic -notmatch 'cache unavailable') { throw 'Unverified macro include was cached.' }
    Write-Output 'Verified process reuse, nested-header invalidation, corruption recovery, explicit disable, and conservative cache bypass.'
} finally {
    $env:CEED_DEBUG = $originalDebug
    $env:CEED_JIT_CACHE_DIR = $originalCache
    $env:CEED_JIT_CACHE = $originalEnabled
}
