# Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
# SPDX-License-Identifier: BSD-2-Clause
param([Parameter(Mandatory = $true)][string]$Probe, [Parameter(Mandatory = $true)][string]$WorkDir,
      [string]$Protected = 'OFF', [string]$CacheProbe = '')
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
    if ($Protected -eq 'ON' -and $output -match 'Final buffer:|Source:\s*\n|#include') { throw 'Protected JIT printed source text.' }
    return $output
}

try {
    $null = Invoke-Probe -Arguments @('write', $sources, '2')
    foreach ($backend in @('/gpu/cuda/ref', '/gpu/cuda/shared', '/gpu/cuda/gen')) {
        $env:CEED_JIT_CACHE_DIR = Join-Path $runRoot ($backend.Replace('/', '_'))
        $first = Invoke-Probe -Arguments @($backend, $sources, '6')
        $second = Invoke-Probe -Arguments @($backend, $sources, '6')
        if ($second -notmatch 'cache hit' -or $second -match 'cache miss|cache unavailable|ATTEMPTING TO COMPILE') {
            throw "Cross-process reuse failed for $backend`: $second"
        }
        if ($first -notmatch 'ATTEMPTING TO COMPILE' -or
            ([regex]::Matches($first, 'ATTEMPTING TO COMPILE')).Count -ne ([regex]::Matches($second, 'cache hit')).Count) {
            throw "Warm hits did not match cold compilation count for $backend"
        }
    }
    $instances = Invoke-Probe -Arguments @('/gpu/cuda/gen', $sources, '6', '2')
    if ($instances -match 'ATTEMPTING TO COMPILE' -or ([regex]::Matches($instances, 'cache hit')).Count -lt 2) {
        throw 'Reuse across operator instances failed.'
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
    if ($CacheProbe) {
        & $CacheProbe unit (Join-Path $runRoot 'unit')
        if ($LASTEXITCODE) { throw 'Private cache unit tests failed.' }
        & $CacheProbe poison $env:CEED_JIT_CACHE_DIR
        if ($LASTEXITCODE) { throw 'Could not create load-failure fixture.' }
        $loadFailure = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
        if ($loadFailure -notmatch 'cache rejected' -or $loadFailure -notmatch 'ATTEMPTING TO COMPILE') {
            throw 'Module-load failure did not fall back to compilation.'
        }
    }
    $env:CEED_JIT_CACHE = 'OFF'
    $disabled = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '9')
    if ($disabled -match 'CUDA JIT cache' -or $disabled -notmatch 'ATTEMPTING TO COMPILE') { throw 'Cache disable setting was ignored.' }
    $env:CEED_JIT_CACHE = 'ON'
    $env:CEED_JIT_CACHE_DIR = Join-Path $runRoot 'concurrent'
    $processes = @()
    for ($rank = 0; $rank -lt 2; $rank++) {
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = $Probe
        $start.Arguments = '/gpu/cuda/gen "{0}" 9' -f $sources
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.EnvironmentVariables.Remove('CEED_DEBUG')
        $processes += [Diagnostics.Process]::Start($start)
    }
    foreach ($process in $processes) {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw 'Concurrent cold process failed.' }
    }
    $concurrentWarm = Invoke-Probe -Arguments @('/gpu/cuda/gen', $sources, '9')
    if ($concurrentWarm -notmatch 'cache hit' -or $concurrentWarm -match 'ATTEMPTING TO COMPILE') {
        throw 'Concurrent publication did not produce reusable entries.'
    }
    if (@(Get-ChildItem -LiteralPath $env:CEED_JIT_CACHE_DIR -Filter '*.tmp').Count) { throw 'Publication left temporary files.' }
    $env:CEED_JIT_CACHE_DIR = Join-Path $runRoot 'read-only'
    $null = New-Item -ItemType Directory -Path $env:CEED_JIT_CACHE_DIR
    $savedAcl = [IO.Directory]::GetAccessControl($env:CEED_JIT_CACHE_DIR)
    try {
        $acl = [IO.Directory]::GetAccessControl($env:CEED_JIT_CACHE_DIR)
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
        $rule = New-Object Security.AccessControl.FileSystemAccessRule($identity, 'Write, Delete',
            'ContainerInherit, ObjectInherit', 'None', 'Deny')
        $acl.AddAccessRule($rule)
        [IO.Directory]::SetAccessControl($env:CEED_JIT_CACHE_DIR, $acl)
        $readOnly = Invoke-Probe -Arguments @('/gpu/cuda/gen', $sources, '9')
        if ($readOnly -notmatch 'cache write failed') { throw 'Read-only directory did not exercise failed publication.' }
    } finally {
        [IO.Directory]::SetAccessControl($env:CEED_JIT_CACHE_DIR, $savedAcl)
    }
    $env:CEED_JIT_CACHE_DIR = Join-Path $runRoot 'macro'
    $null = Invoke-Probe -Arguments @('write', $sources, '4', 'macro-include')
    $dynamic = Invoke-Probe -Arguments @('/gpu/cuda/ref', $sources, '12')
    if ($dynamic -notmatch 'cache unavailable') { throw 'Unverified macro include was cached.' }
    Write-Output 'Verified reuse, dependency changes, corruption/load recovery, disable, concurrent writers, read-only fallback and conservative bypass.'
} finally {
    $env:CEED_DEBUG = $originalDebug
    $env:CEED_JIT_CACHE_DIR = $originalCache
    $env:CEED_JIT_CACHE = $originalEnabled
}
