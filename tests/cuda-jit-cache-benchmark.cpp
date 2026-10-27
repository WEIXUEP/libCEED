// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
// Microbenchmark excludes process/context creation; not a substitute for application acceptance.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <cuda_runtime.h>
#include "../backends/cuda/ceed-cuda-compile.h"

static void Enable(bool on) {
#ifdef _WIN32
  _putenv_s("CEED_JIT_CACHE", on ? "ON" : "OFF");
#else
  setenv("CEED_JIT_CACHE", on ? "ON" : "OFF", 1);
#endif
}
int main(int argc, char **argv) {
  Ceed ceed;
  CeedCall(CeedInit("/gpu/cuda/ref", &ceed));
  // Caller supplies a fresh private cache directory. Never delete user data.
  const int rounds = argc > 1 ? std::atoi(argv[1]) : 20;
  if (rounds <= 0) return 1;
  const char *source  = "extern \"C\" __global__ void CacheBenchmark(double *x) { x[0] = SCALE * x[0]; }\n";
  auto        compile = [&](int scale, double &elapsed) -> int {
    CUmodule   module;
    const auto start = std::chrono::steady_clock::now();
    CeedCall(CeedCompile_Cuda(ceed, source, "CacheBenchmark", &module, 1, "SCALE", scale));
    elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CUfunction kernel;
    if (cuModuleGetFunction(&kernel, module, "CacheBenchmark") != CUDA_SUCCESS) return 1;
    double *device, value = 3;
    if (cudaMalloc(&device, sizeof(value)) != cudaSuccess || cudaMemcpy(device, &value, sizeof(value), cudaMemcpyHostToDevice) != cudaSuccess)
      return 1;
    void *args[] = {&device};
    if (cuLaunchKernel(kernel, 1, 1, 1, 1, 1, 1, 0, nullptr, args, nullptr) != CUDA_SUCCESS ||
        cudaMemcpy(&value, device, sizeof(value), cudaMemcpyDeviceToHost) != cudaSuccess)
      return 1;
    cudaFree(device);
    cuModuleUnload(module);
    return value == 3 * scale ? 0 : 1;
  };
  double elapsed;
  Enable(false);
  CeedCall(compile(1, elapsed));  // initialize NVRTC outside measurement
  double baseline = 0, cold = 0, warm = 0, worst = 0;
  for (int i = 0; i < rounds; i++) {
    // Alternate baseline/miss to reduce clock drift; identical compile inputs.
    Enable(false);
    CeedCall(compile(i + 2, elapsed));
    baseline += elapsed;
    Enable(true);
    CeedCall(compile(i + 2, elapsed));
    cold += elapsed;
    CeedCall(compile(i + 2, elapsed));
    warm += elapsed;
    worst = std::max(worst, elapsed);
  }
  std::cout << "rounds=" << rounds << " disabled_mean_ms=" << baseline / rounds << " cold_mean_ms=" << cold / rounds
            << " hit_mean_ms=" << warm / rounds << " hit_max_ms=" << worst << " cold_overhead_percent=" << 100 * (cold / baseline - 1) << "\n";
  CeedCall(CeedDestroy(&ceed));
  return 0;
}
