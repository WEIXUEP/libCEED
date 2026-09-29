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
