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

On Windows, the Makefile adds `/MD` to MSVC and NVCC host compilation and uses the shared CUDA runtime, so the resulting library can be linked by `/MD` CMake projects.

## Palace-style options (mapped to this machine)

After the setup commands above, this reproduces the supplied Palace options in separate output directories:

```bat
F:/Program_Professional/msys2/usr/bin/make.exe -j2 lib IS_MSVC=1 prefix=K:/Project_WXP/git/libCEED/build_palace_repro LDFLAGS=/machine:x64 "CC=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" "CXX=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe" FC= "OPT= -openmp:llvm -IC:/Program\ Files/NVIDIA\ GPU\ Computing\ Toolkit/CUDA/v12.4/include" STATIC=1 PEDANTIC= "LINK=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/link.exe" "AR=D:/Program_Professional/Microsoft\ Visual\ Studio/2022/BuildTools/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/lib.exe" "ARFLAGS= /NOLOGO /OUT:$@" OPENMP=1 XSMM_DIR=K:/Project_WXP/git/libCEED/build_palace_repro "BLAS_LIB=F:/Intel/oneAPI/mkl/latest/lib/mkl_intel_lp64_dll.lib F:/Intel/oneAPI/mkl/latest/lib/mkl_sequential_dll.lib F:/Intel/oneAPI/mkl/latest/lib/mkl_core_dll.lib" CUDA_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4" CUDA_LIB_DIR_OVERRIDE="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/lib/x64" CUDA_ARCH=sm_86 MAGMA_DIR=K:/Project_WXP/git/libCEED/build_palace_repro CMAKE_BUILD_TYPE_FLAG=MDd OBJDIR=build_palace_repro LIBDIR=lib_palace_repro
```

Output: `K:/Project_WXP/git/libCEED/lib_palace_repro/libceed.lib`. No libXSMM or MAGMA installation exists at the mapped prefix here, so those backends are disabled in this reproduction. `CMAKE_BUILD_TYPE_FLAG=MDd` is only printed by this Makefile; actual MSVC and NVCC compilation uses `/MD`.
