#!/usr/bin/env bash
# Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
# SPDX-License-Identifier: BSD-2-Clause
# Arguments: external-jit probe, private cache test executable, fresh work directory.
set -euo pipefail
probe=$1
unit=$2
work=$3
mkdir -p "$work"
export CEED_DEBUG=1 CEED_JIT_CACHE=ON CEED_JIT_CACHE_DIR="$work/cache"
sources="$work/external sources"
"$unit" unit "$work/unit"
"$probe" write "$sources" 2
for backend in ref shared gen; do
  export CEED_JIT_CACHE_DIR="$work/$backend"
  "$probe" "/gpu/cuda/$backend" "$sources" 6 >"$work/$backend.cold" 2>&1
  "$probe" "/gpu/cuda/$backend" "$sources" 6 >"$work/$backend.warm" 2>&1
  test "$(grep -c 'ATTEMPTING TO COMPILE' "$work/$backend.cold")" -eq "$(grep -c 'cache hit' "$work/$backend.warm")"
  ! grep -E 'cache miss|cache unavailable|ATTEMPTING TO COMPILE' "$work/$backend.warm"
done
"$probe" /gpu/cuda/gen "$sources" 6 2 >"$work/instances" 2>&1
test "$(grep -c 'cache hit' "$work/instances")" -ge 2
! grep -q 'ATTEMPTING TO COMPILE' "$work/instances"
export CEED_JIT_CACHE_DIR="$work/ref"
"$probe" write "$sources" 3
"$probe" /gpu/cuda/ref "$sources" 9 >"$work/changed" 2>&1
grep -q 'cache miss' "$work/changed"
for file in "$CEED_JIT_CACHE_DIR"/*.bin; do printf short >"$file"; done
"$probe" /gpu/cuda/ref "$sources" 9 >"$work/repaired" 2>&1
grep -q 'cache rejected' "$work/repaired"
"$probe" /gpu/cuda/ref "$sources" 9 >"$work/reused" 2>&1
! grep -q 'ATTEMPTING TO COMPILE' "$work/reused"
"$unit" poison "$CEED_JIT_CACHE_DIR"
"$probe" /gpu/cuda/ref "$sources" 9 >"$work/load-failure" 2>&1
grep -q 'cache rejected' "$work/load-failure"
CEED_JIT_CACHE=OFF "$probe" /gpu/cuda/ref "$sources" 9 >"$work/disabled" 2>&1
grep -q 'ATTEMPTING TO COMPILE' "$work/disabled"
! grep -q 'CUDA JIT cache' "$work/disabled"
export CEED_JIT_CACHE_DIR="$work/concurrent"
env -u CEED_DEBUG "$probe" /gpu/cuda/gen "$sources" 9 &
first=$!
env -u CEED_DEBUG "$probe" /gpu/cuda/gen "$sources" 9 &
second=$!
wait "$first"
wait "$second"
"$probe" /gpu/cuda/gen "$sources" 9 >"$work/concurrent-warm" 2>&1
! grep -q 'ATTEMPTING TO COMPILE' "$work/concurrent-warm"
test -z "$(find "$CEED_JIT_CACHE_DIR" -name '*.tmp' -print -quit)"
# /sys is read-only even in a root-run WSL session (chmod would not deny root).
CEED_JIT_CACHE_DIR=/sys/ceed-cache-test "$probe" /gpu/cuda/gen "$sources" 9 >"$work/read-only" 2>&1
grep -q 'cache write failed' "$work/read-only"
"$probe" write "$sources" 4 macro-include
"$probe" /gpu/cuda/ref "$sources" 12 >"$work/macro" 2>&1
grep -q 'cache unavailable' "$work/macro"
echo 'Linux cross-process reuse, dependency invalidation, corruption/load recovery, disable, concurrent publication and read-only fallback passed.'
