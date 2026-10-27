# Windows build commands

## Native CMake with oneAPI, NMake or JOM

This entry point uses MSVC `cl.exe` inside the Intel oneAPI environment. It does not invoke MSYS2 or GNU make. Run these commands in `cmd.exe`:

```bat
call "F:\Intel\oneAPI\setvars.bat" intel64
cd /d K:\Project_WXP\git\libCEED
where cl
where cmake
where nmake
where jom
```

CPU Release with protection, using NMake (explicitly disable libXSMM because it is not installed on this machine):

```bat
cmake -S K:/Project_WXP/git/libCEED -B K:/Project_WXP/git/libCEED/build/cmake_cpu_nmake -G "NMake Makefiles" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release -DCEED_WITH_CUDA=OFF -DCEED_WITH_OPENMP=OFF -DCEED_WITH_LIBXSMM=OFF -DCEED_WITH_MAGMA=OFF -DCEED_CODE_PROTECTION=ON -DCMAKE_INSTALL_PREFIX=K:/Project_WXP/git/libCEED/build/cmake_cpu_install
cmake --build K:/Project_WXP/git/libCEED/build/cmake_cpu_nmake
ctest --test-dir K:/Project_WXP/git/libCEED/build/cmake_cpu_nmake --output-on-failure
cmake --install K:/Project_WXP/git/libCEED/build/cmake_cpu_nmake
```

Output: `K:/Project_WXP/git/libCEED/build/cmake_cpu_nmake/lib/libceed.lib`. Installed library: `K:/Project_WXP/git/libCEED/build/cmake_cpu_install/lib/libceed.lib`.

CUDA Debug using JOM:

```bat
set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin;%PATH%"
cmake -S K:/Project_WXP/git/libCEED -B K:/Project_WXP/git/libCEED/build/cmake_gpu_jom -G "NMake Makefiles JOM" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Debug -DCEED_WITH_CUDA=ON -DCEED_WITH_OPENMP=OFF -DCEED_WITH_LIBXSMM=OFF -DCEED_WITH_MAGMA=OFF -DCEED_CODE_PROTECTION=ON "-DCEED_CUDA_DIR=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4" -DCMAKE_CUDA_ARCHITECTURES=86 -DCMAKE_INSTALL_PREFIX=K:/Project_WXP/git/libCEED/build/cmake_gpu_install
cmake --build K:/Project_WXP/git/libCEED/build/cmake_gpu_jom --parallel 2
ctest --test-dir K:/Project_WXP/git/libCEED/build/cmake_gpu_jom --output-on-failure
cmake --install K:/Project_WXP/git/libCEED/build/cmake_gpu_jom
```

Output: `K:/Project_WXP/git/libCEED/build/cmake_gpu_jom/lib/libceed.lib`. Installed library: `K:/Project_WXP/git/libCEED/build/cmake_gpu_install/lib/libceed.lib`. Debug prints effective `CEED_CODE_PROTECTION=OFF` and the reason. For CUDA Release, use a separate build/install directory and change the mode to `-DCMAKE_BUILD_TYPE=Release`.

Both generators support CPU and CUDA; choose a new build directory when changing the generator. NMake builds serially; JOM accepts `--parallel 2`. If JOM is not on PATH, add `-DCMAKE_MAKE_PROGRAM=F:/Program_Professional/jom_1_1_7/jom.exe` when configuring JOM. Match the consumer's Debug/Release mode. `CMAKE_BUILD_TYPE` alone controls the mode: Debug selects `/MDd` and `_ITERATOR_DEBUG_LEVEL=2`; Release selects `/MD` and `_ITERATOR_DEBUG_LEVEL=0`, including CUDA host compilation. A fresh configuration defaults to Release. The native CMake build does not use `CMAKE_BUILD_TYPE_FLAG`; that parameter remains specific to the Makefile commands below.

GPU architectures are controlled only by the standard `CMAKE_CUDA_ARCHITECTURES` input, for example `-DCMAKE_CUDA_ARCHITECTURES=86` or `"-DCMAKE_CUDA_ARCHITECTURES=80;86"`. CMake generates both native code and PTX for each unsuffixed value; use `"-DCMAKE_CUDA_ARCHITECTURES=80-real;86-real;86-virtual"` for native code at both architectures and PTX only at 86. The default is 86 unless `CUDAARCHS` is supplied by the environment. `CUDA_ARCH` and `CUDA_TARGETS` are not used by this CMake entry point. CUDA uses the shared runtime.

Feature switches follow the Palace naming style and are grouped near the top of `CMakeLists.txt`. Dependency, build, installation, test, and configuration-printing logic remains in that single file; the files under `cmake/` are package metadata templates.

| CMake parameter | Default | Dependency path | Palace equivalent |
| --- | --- | --- | --- |
| `CEED_WITH_LIBXSMM=ON/OFF` | ON | `CEED_XSMM_DIR` (headers in `include`, library in `lib`) | `PALACE_WITH_LIBXSMM` |
| `CEED_WITH_CUDA=ON/OFF` | OFF | `CEED_CUDA_DIR` (CUDA toolkit root) | `PALACE_WITH_CUDA` |
| `CEED_WITH_MAGMA=ON/OFF` | OFF | `CEED_MAGMA_DIR` (headers in `include`, library in `lib`) | `PALACE_WITH_MAGMA` |
| `CEED_WITH_OPENMP=ON/OFF` | OFF | MSVC/oneAPI environment (`LIB`) | `PALACE_WITH_OPENMP` |
| `BUILD_TESTING=ON/OFF` | ON | Focused CTest suite | Standard CMake option |
| `BUILD_SHARED_LIBS=OFF` | OFF | Static library; shared builds not supported yet | Standard CMake option |
| `CEED_CODE_PROTECTION=ON/OFF` | OFF | Non-Debug protection | Explicit libCEED setting |

In a fresh build, libXSMM is the only optional backend enabled by default; built-in ref/opt CPU backends remain available. Each `CEED_WITH_*` switch explicitly controls the corresponding feature. Providing a dependency path does not enable its backend, and old `CEED_ENABLE_CUDA`/`OPENMP` inputs are not used. Existing build directories retain their cached switches: pass `-DCEED_WITH_LIBXSMM=ON` to enable it there. Missing libXSMM is a configuration error; supply its prefix with `-DCEED_XSMM_DIR=...`, or explicitly disable it with `-DCEED_WITH_LIBXSMM=OFF`.

Dependency inputs are `-DCEED_CUDA_DIR=...`, `-DCEED_XSMM_DIR=...`, `-DCEED_MAGMA_DIR=...`, and `-DCEED_BLAS_LIB="path/to/a.lib;path/to/b.lib"`; the old unprefixed names are not used by CMake. When libXSMM or MAGMA is ON and no prefix is given, the search uses `CMAKE_INSTALL_PREFIX`, matching Palace's shared dependency installation. MAGMA requires CUDA, enables LLVM OpenMP, and accepts `magma.lib` or `libmagma.lib`. If `CEED_BLAS_LIB` is empty, the build searches BLAS when needed (sequential LP64 MKL when `MKLROOT` is set). `-DCEED_WITH_OPENMP=ON` requires its import library and runtime DLL. Configuration prints the compiler, runtime, feature switches, resolved dependencies, backends, and effective protection values.

### Palace ExternalProject integration

`custom/ExternalLibCEED.cmake` is a complete native CMake replacement for the old Makefile caller. It keeps dependency ordering, configures in a separate build directory, and uses ExternalProject's default CMake configure/build/install steps. Use it in place of the original `ExternalLibCEED.cmake` include; do not include both because they define the same `libCEED` target. The Palace repository is not modified by this example.

The parent supplies its existing `PALACE_WITH_LIBXSMM`, `PALACE_WITH_CUDA`, `PALACE_WITH_MAGMA`, `PALACE_WITH_OPENMP`, and `PALACE_CODE_PROTECTION` settings. The module maps them to `CEED_WITH_*` and `CEED_CODE_PROTECTION`, explicitly forwarding ON/OFF rather than relying on dependency auto-detection. libXSMM and MAGMA are searched under the shared installation prefix and their ExternalProject targets finish before libCEED configures. HIP is explicitly rejected by this Windows entry point.

| Old make input | Native CMake input |
| --- | --- |
| `prefix` | `CMAKE_INSTALL_PREFIX` |
| `CC`, `CXX`, `LINK`, `AR` | `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`, `CMAKE_LINKER`, `CMAKE_AR` |
| `OPT`, `PEDANTIC` | `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and their configuration-specific variants |
| `LDFLAGS` | `CMAKE_EXE_LINKER_FLAGS` for tests/consumers; `CMAKE_STATIC_LINKER_FLAGS` for the librarian |
| `STATIC=1` | `BUILD_SHARED_LIBS=OFF` (the CMake entry point has no `STATIC` switch) |
| `CMAKE_BUILD_TYPE_FLAG=MD/MDd` | `CMAKE_BUILD_TYPE=Release/Debug` |
| `CUDA_ARCH`, `CUDA_TARGETS` | `CMAKE_CUDA_ARCHITECTURES`, including semicolon lists and real/virtual suffixes |
| `CUDA_DIR`, `XSMM_DIR`, `MAGMA_DIR`, `BLAS_LIB` | `CEED_CUDA_DIR`, `CEED_XSMM_DIR`, `CEED_MAGMA_DIR`, `CEED_BLAS_LIB` |
| `CODE_PROTECTION=1/0` | `CEED_CODE_PROTECTION=ON/OFF` |

Keep compiler paths as typed FILEPATH values with no additional literal quotes inside the value. CMake handles spaces and escaping; do not assemble shell command strings. CUDA includes/libraries and MSVC runtime flags are supplied by libCEED's targets and build mode, so do not copy the old `-I`, `/MD`, or OpenMP flags into `OPT`. Configure OpenMP through the backend switch. Changing compiler/toolkit installations requires a fresh build directory.

The module forwards normal compiler flags, the active configuration's flags, linker/librarian flags, toolchain/make paths, prefix search paths and installation directories. CUDA arguments are forwarded only when CUDA is enabled. `BLAS_LAPACK_LIBRARIES` becomes the semicolon-separated `CEED_BLAS_LIB` list. Multi-value arguments are encoded with ExternalProject's `LIST_SEPARATOR "|"`, preserving each list as one child cache value. For example, architecture `80;86` and two BLAS library paths remain lists rather than separate command-line arguments.

For an already extracted checkout, set `LIBCEED_SOURCE_DIR`; no download, checkout or update is performed. Otherwise provide `EXTERN_LIBCEED_URL` and `EXTERN_LIBCEED_GIT_TAG` for the standard Git download step. The default build directory is `${CMAKE_BINARY_DIR}/extern/libCEED-build`. The module prints every forwarded cache argument, and the child prints resolved dependencies, effective flags and protection state.

For example, a parent can use the local checkout directly:

```cmake
set(LIBCEED_SOURCE_DIR "K:/Project_WXP/git/libCEED")
include("${LIBCEED_SOURCE_DIR}/custom/ExternalLibCEED.cmake")
```

The `tests/cmake-superbuild` fixture exercises this module through a real ExternalProject. Verified here with oneAPI/MSVC: NMake CPU Release build/install and two installed-consumer tests; JOM CPU Debug build/install and two consumer tests, including `/MDd` and effective protection OFF; JOM CUDA Release build/install with `80-real;86`, LLVM OpenMP and a three-library MKL list, followed by nine consumer/cache tests. Prefixes and compiler/library paths containing spaces, custom C/C++/CUDA flags, linker/librarian flags, and both semicolon and `$<SEMICOLON>` library lists were checked. MAGMA/libXSMM full builds remain unverified because those installations are unavailable on this machine.

For a smaller custom caller, the essential arguments are:

```cmake
string(REPLACE ";" "|" LIBCEED_CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
string(REPLACE ";" "|" LIBCEED_BLAS_LIB "${BLAS_LAPACK_LIBRARIES}")

set(LIBCEED_CMAKE_ARGS
  "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}"
  "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
  "-DBUILD_SHARED_LIBS:BOOL=OFF"
  "-DBUILD_TESTING:BOOL=OFF"
  "-DCEED_WITH_CUDA:BOOL=${PALACE_WITH_CUDA}"
  "-DCEED_WITH_OPENMP:BOOL=${PALACE_WITH_OPENMP}"
  "-DCEED_WITH_LIBXSMM:BOOL=${PALACE_WITH_LIBXSMM}"
  "-DCEED_WITH_MAGMA:BOOL=${PALACE_WITH_MAGMA}"
  "-DCMAKE_CUDA_ARCHITECTURES:STRING=${LIBCEED_CUDA_ARCHITECTURES}"
  "-DCEED_CUDA_DIR:PATH=${CUDAToolkit_LIBRARY_ROOT}"
  "-DCEED_XSMM_DIR:PATH=${CMAKE_INSTALL_PREFIX}"
  "-DCEED_MAGMA_DIR:PATH=${CMAKE_INSTALL_PREFIX}"
  "-DCEED_BLAS_LIB:STRING=${LIBCEED_BLAS_LIB}"
  "-DCEED_CODE_PROTECTION:BOOL=${PALACE_CODE_PROTECTION}"
)
```

The complete template forwards `PALACE_CODE_PROTECTION` from the parent as `CEED_CODE_PROTECTION`; when configuring libCEED directly, use `-DCEED_CODE_PROTECTION=ON/OFF`. The old `CODE_PROTECTION` argument is not used by CMake. libCEED determines the effective state from the build mode and backends. Release/MD enables protection when requested; Debug/MDd prints `CEED_CODE_PROTECTION=OFF` with the disabled reason. CPU archive and JIT metadata protection additionally require a CPU-only build. The Makefile interface below is unchanged.

These are `CMAKE_ARGS` for a native CMake `ExternalProject`, with the generator inherited from the parent. Set `LIST_SEPARATOR "|"` in `ExternalProject_Add` when using the smaller argument list. Do not blank `CONFIGURE_COMMAND` or `BUILD_COMMAND`, and do not use `BUILD_IN_SOURCE TRUE`: those belonged to the old Makefile workflow. Public SDK installation and protected resource packaging are handled by libCEED's own install rules; no extra header-copy command is needed.

Installed packages provide `CEED::ceed` through `find_package(CEED CONFIG REQUIRED)`, with CUDA link dependencies and active protection definitions propagated to consumers. Set `CMAKE_PREFIX_PATH` to the installation prefix. `ceed.pc` is also installed with `-L`/`-l` dependencies. Configure the final installation prefix up front; changing only `cmake --install --prefix` will not update the compiled JIT root. Nonprotected builds search the configured installation include directory first and the source include directory second.

### Protected CUDA JIT and reuse across runs

For a protected CUDA Release build, run after the oneAPI setup above:

```bat
set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin;%PATH%"
cmake -S K:/Project_WXP/git/libCEED -B K:/Project_WXP/git/libCEED/build/cmake_gpu_protected -G "NMake Makefiles JOM" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release -DCEED_WITH_LIBXSMM=OFF -DCEED_WITH_CUDA=ON -DCEED_CODE_PROTECTION=ON "-DCEED_CUDA_DIR=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4" -DCMAKE_CUDA_ARCHITECTURES=86 -DCMAKE_INSTALL_PREFIX=K:/Project_WXP/git/libCEED/build/cmake_gpu_protected_install
cmake --build K:/Project_WXP/git/libCEED/build/cmake_gpu_protected --parallel 2
ctest --test-dir K:/Project_WXP/git/libCEED/build/cmake_gpu_protected --output-on-failure
cmake --install K:/Project_WXP/git/libCEED/build/cmake_gpu_protected
```

`CEED_CODE_PROTECTION=ON` with CUDA in a non-Debug build prints `PROTECT_GPU_JIT=ON`. The built-in CUDA/gallery headers (and MAGMA JIT headers when enabled) are bundled into `libceed.lib` using AES-256-GCM. NVRTC receives decrypted headers in memory; libCEED does not write their plaintext to temporary files or print source/compiler logs in this mode. Clang JIT is rejected because it writes source files. Public SDK headers remain installed, but `include/ceed/jit-source` is excluded. Use a fresh install prefix: installation does not remove plaintext headers left by an earlier unprotected installation.

CUDA builds (ordinary and protected) reuse compiled kernels between separate runs on the same computer. The default cache directory is `%LOCALAPPDATA%\compute-cache\jit-cache-v2`; it stores CUBIN/PTX binaries, not source text. The first run compiles and stores a kernel; subsequent runs load it when the key matches. Each process still creates its CUDA context and loads modules, and checks source dependencies. Set these runtime environment variables before launching the application to override the directory or disable reuse:

```bat
set "CEED_JIT_CACHE_DIR=K:\compute-cache\jit-cache-v2"
set "CEED_JIT_CACHE=OFF"
```

Leave `CEED_JIT_CACHE` unset or set it to `ON` to enable reuse. Keys include the generated source, decrypted built-in headers, recursively resolved application headers, compilation options/device architecture, NVRTC version, CUDA toolkit version, and driver version. Changed dependencies produce a new key. Macro includes or dependencies that cannot be verified bypass the cache. Missing, corrupt, incompatible, or unwritable cache entries fall back to compilation. Publication is atomic and never replaces a concurrent winner; separate processes can share the directory. With `CEED_DEBUG=1`, hit/miss/rejected/write-failure messages report cache behavior without printing protected source. Linux uses $XDG_CACHE_HOME/compute-cache/jit-cache-v2, or $HOME/.cache/compute-cache/jit-cache-v2. See [cache implementation and validation](cuda-jit-cache.md) for limits and remaining acceptance checks.

This protects libCEED's built-in JIT source distribution only. Palace/application QFunction files still use their existing external source roots and must remain available; their dependencies participate in cache invalidation. The decryption key ships in the library, so this is not protection against extracting code from a running process. Logical header names, public symbols, and GPU object metadata are not anonymized. CPU archive anonymization remains CPU-only. Embedded source protection applies to the native Windows CMake entry point; persistent binary caching also applies to ordinary CUDA Makefile builds on Windows/Linux.

Verified with MSVC 14.43 and CUDA 12.4: protected CUDA ref/shared/gen tests, installed C/C++ consumers without either installed or source-tree JIT headers, cross-process cache hits, nested-header invalidation, corruption recovery, and conservative bypass for macro includes.

This initial native CMake entry point covers Windows MSVC static CPU/CUDA libraries, optional CUDA MAGMA/libXSMM, installation, protection, and four basic C tests per backend. It does not replace the Makefile's HIP/SYCL, shared-library, external-example, Fortran-test, full-test-suite, documentation, or packaging targets. The Makefile remains available below.

Verified on this machine using oneAPI `setvars.bat`, MSVC 14.43, and CUDA 12.4: NMake CPU Release with protection (16 CTest cases); JOM CUDA Debug (28 cases); JOM CUDA Release with LLVM OpenMP and `CMAKE_CUDA_ARCHITECTURES=80;86` (28 cases). Separate C and C++ consumers link the installed packages with `/WHOLEARCHIVE` and exercise CPU and CUDA ref/shared/gen; the CUDA consumer also verifies that JIT resolves installed headers. MAGMA/libXSMM installations are unavailable on this machine, so their full backend builds have not been validated. A fresh configuration was checked to enable only libXSMM by default and report its missing installation with instructions to set `CEED_XSMM_DIR` or explicitly disable the backend.

## MSYS2 Makefile

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
