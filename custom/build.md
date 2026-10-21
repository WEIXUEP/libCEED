# Windows build commands

Open `cmd.exe` and run these setup commands first. oneAPI `setvars.bat` initializes the VS 2022 x64 environment; the build commands below still use MSVC and MSYS2 make.

```bat
set "PATH=F:\Program_Professional\msys2\usr\bin;F:\Program_Professional\msys2\mingw64\bin;%PATH%"
call "F:\Intel\oneAPI\setvars.bat" intel64
cd /d K:\Project_WXP\git\libCEED
```

## CPU

```bat
F:/Program_Professional/msys2/usr/bin/make.exe -j2 lib prefix=K:/Project_WXP/git/libCEED/build_windows IS_MSVC=1 LDFLAGS=/machine:x64 "CC=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "CXX=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "LINK=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/link.exe" "AR=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/lib.exe" FC= OPT=/O2 STATIC=1 PEDANTIC= CUDA_DIR= OBJDIR=build_cpu LIBDIR=lib_cpu
```

Output: `K:/Project_WXP/git/libCEED/lib_cpu/libceed.lib`.

## GPU (CUDA 12.4, sm_86)

```bat
F:/Program_Professional/msys2/usr/bin/make.exe -j2 lib prefix=K:/Project_WXP/git/libCEED/build_windows IS_MSVC=1 LDFLAGS=/machine:x64 "CC=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "CXX=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "LINK=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/link.exe" "AR=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/lib.exe" FC= OPT=/O2 STATIC=1 PEDANTIC= CUDA_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4" CUDA_LIB_DIR_OVERRIDE="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/lib/x64" CUDA_ARCH=sm_86
```

Output: `K:/Project_WXP/git/libCEED/lib/libceed.lib` with CUDA ref/shared/gen backends.

The original example's standalone `-openmp:llvm` and `-I...` arguments are not valid make variable assignments. Do not put them between make arguments; the shown commands use the Makefile's tested MSVC flags.

On Windows, `CMAKE_BUILD_TYPE_FLAG` controls the MSVC runtime used by C and C++ compilation and by the NVCC host compiler. Use `CMAKE_BUILD_TYPE_FLAG=MD` for Release consumers (`_ITERATOR_DEBUG_LEVEL=0`) or `CMAKE_BUILD_TYPE_FLAG=MDd` for Debug consumers (`_ITERATOR_DEBUG_LEVEL=2`). The Palace application and libCEED must use the same value. CUDA still uses the shared runtime (`-cudart shared`).

## MAGMA (Windows)

Set `MAGMA_DIR` to the MAGMA installation prefix. Headers must be in `MAGMA_DIR/include` (including `magma_v2.h`), and the library may be either `MAGMA_DIR/lib/magma.lib` or `MAGMA_DIR/lib/libmagma.lib`. If both exist, `magma.lib` takes precedence. Paths containing spaces are supported for MAGMA library detection and include arguments.

Windows uses MSVC `dumpbin.exe` next to the configured `link.exe` to distinguish CUDA and HIP MAGMA libraries; GNU `nm` is not required. The make `info` output prints the selected `MAGMA_LIB` path. With a CUDA MAGMA library and detected CUDA libraries, the enabled backends include `/gpu/cuda/magma` and `/gpu/cuda/magma/det`. The generated `ceed.pc` keeps MAGMA and CUDA dependencies in `-L<dir> -l<name>` form so CMake/pkg-config can import them; `CUDA_LIB_DIR_OVERRIDE` can specify the CUDA library directory.

For MSVC, the MAGMA OpenMP metadata uses `-openmp:llvm`; GNU/Clang builds continue to use `-fopenmp`. Keep `OPT` consistent with the compiler, for example `OPT="/O2 -openmp:llvm ..."` for MSVC.

`CODE_PROTECTION=1` requests protection, which is applied only in Release mode (`CMAKE_BUILD_TYPE_FLAG=MD`). Debug mode (`CMAKE_BUILD_TYPE_FLAG=MDd`) preserves diagnostic filenames, line numbers, QFunction source locations, assertions, and archive/debug metadata, even when `CODE_PROTECTION=1`. The default is `CODE_PROTECTION=0`. When protection is active, `CeedError` reports `<protected>:0`, and the Makefile passes `CEED_CODE_PROTECTION=1` to C, C++, and CUDA compilation. Consumers compiling their own calls to `CeedError` must use matching Release-only definitions; installed headers do not force protection on consumers.

Append only `CODE_PROTECTION=1` to the existing Release build command to enable protection. No additional protection switches or changes to backend settings are required. For CPU-only Release builds, it automatically omits QFunction source locations and the default JIT source root when no CUDA, HIP, SYCL, or MAGMA GPU backend is compiled. CPU QFunctions still execute their compiled function pointers. The Makefile supplies the internal `CEED_DISABLE_JIT_SOURCE` definition automatically. Installed headers do not force this mode on consumers; applications defining their own QFunctions must use matching definitions if they also want to omit their source locations.

Make prints the input as `CODE_PROTECTION_REQUESTED` and the effective value as `CODE_PROTECTION` on every invocation. When protection is inactive, it prints `CODE_PROTECTION=0` and `CODE_PROTECTION_DISABLED_REASON` explaining whether protection was not requested or the build mode is not Release (`MD`). `CODE_PROTECTION_ACTIVE=1` means diagnostic protection is applied; `PROTECT_CPU_JIT=1` means JIT source locations are omitted; `PROTECT_CPU_ARCHIVE=1` means MSVC static archive metadata is sanitized. These three values are computed internally, not additional user switches. Debug builds print all three as `0`. `GPU_BACKENDS` lists the compiled GPU backends or `(none)`.

For MSVC CPU-only static libraries, protection also clears object debug records and archives objects under anonymous member names. COFF section indices and code/relocation data are preserved, including MSVC-specific metadata. Assertions retain their condition checks and failure behavior but report `<protected>:0`. Unused JIT source parsing is disabled in this mode and its entry points return `CEED_ERROR_UNSUPPORTED`. The archive is checked for original source/header/object names and the build root in UTF-8 and UTF-16 before it replaces the output. This uses Windows PowerShell and MSVC, with no additional binary tools. Ordinary COFF objects are required; `/GL` and `/bigobj` inputs are rejected rather than emitted without protection. Original intermediate objects and separately produced PDBs are not distribution artifacts and remain unchanged. Public symbols, function names, and error message contents remain available; this is not symbol obfuscation. Prebuilt dependencies distributed separately are not sanitized.

When any GPU backend is compiled, QFunction source locations and JIT source roots remain available because those backends need them to load source code, even when the selected runtime resource is CPU; archive metadata protection is CPU-only. Rebuild all objects when changing `CODE_PROTECTION` or enabling/disabling GPU backends; make does not track command-line flag changes. Use separate `OBJDIR`/`LIBDIR` values for protected and normal builds.

For the Palace installation shown in the logs, keep `MAGMA_DIR=H:/fem/palace/build_debug_gpu_260930`; renaming `magma.lib` is unnecessary. MAGMA itself and its dependencies must already be installed before libCEED is built. The name detection was verified using MSVC test archives, including an installation path with spaces; this machine has no MAGMA installation for a full MAGMA backend build.

## Palace-style options (mapped to this machine)

After the setup commands above, this reproduces the supplied Palace options in separate output directories:

```bat
F:/Program_Professional/msys2/usr/bin/make.exe -j2 lib IS_MSVC=1 prefix=K:/Project_WXP/git/libCEED/build_palace_repro LDFLAGS=/machine:x64 "CC=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "CXX=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" FC= "OPT= -openmp:llvm -IC:/Program\ Files/NVIDIA\ GPU\ Computing\ Toolkit/CUDA/v12.4/include" STATIC=1 PEDANTIC= "LINK=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/link.exe" "AR=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/lib.exe" "ARFLAGS= /NOLOGO /OUT:$@" OPENMP=1 XSMM_DIR=K:/Project_WXP/git/libCEED/build_palace_repro "BLAS_LIB=F:/Intel/oneAPI/mkl/latest/lib/mkl_intel_lp64_dll.lib F:/Intel/oneAPI/mkl/latest/lib/mkl_sequential_dll.lib F:/Intel/oneAPI/mkl/latest/lib/mkl_core_dll.lib" CUDA_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4" CUDA_LIB_DIR_OVERRIDE="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/lib/x64" CUDA_ARCH=sm_86 MAGMA_DIR=K:/Project_WXP/git/libCEED/build_palace_repro CMAKE_BUILD_TYPE_FLAG=MDd OBJDIR=build_palace_repro LIBDIR=lib_palace_repro
```

Output: `K:/Project_WXP/git/libCEED/lib_palace_repro/libceed.lib`. No libXSMM or MAGMA installation exists at the mapped prefix here, so those backends are disabled in this reproduction. This command uses `CMAKE_BUILD_TYPE_FLAG=MDd`, so the Palace Debug configuration must also use `/MDd` and `_ITERATOR_DEBUG_LEVEL=2`. For a Release Palace build, change it to `CMAKE_BUILD_TYPE_FLAG=MD`.
