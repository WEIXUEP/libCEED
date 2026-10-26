# Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
# All Rights Reserved. See the top-level LICENSE and NOTICE files for details.
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

# Prohibit in-source builds.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are prohibited; specify a separate directory with -B")
endif()

# NMake and JOM are single-configuration generators. Select the runtime from the build type.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" _ceed_build_type)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Initialize the project and validate the supported toolchain.
project(libCEED VERSION 0.12.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  message(FATAL_ERROR
    "This CMake entry point currently supports Windows MSVC. Initialize oneAPI/VS and "
    "select cl.exe for C and C++; other platforms retain the Makefile."
  )
endif()
if(CMAKE_CONFIGURATION_TYPES)
  message(FATAL_ERROR "Use a single-configuration generator, such as NMake Makefiles or NMake Makefiles JOM.")
endif()

# Define build settings and defaults.
option(BUILD_SHARED_LIBS "Build shared libraries (not yet supported by this entry point)" OFF)
if(BUILD_SHARED_LIBS)
  message(FATAL_ERROR "The native CMake entry point currently supports static libraries; use BUILD_SHARED_LIBS=OFF.")
endif()

include(GNUInstallDirs)
include(CTest)
option(CEED_CODE_PROTECTION "Protect non-Debug source locations, CPU archives, and embedded GPU JIT sources" OFF)

# Optional backends: enable only libXSMM by default.
option(CEED_WITH_LIBXSMM "Build libXSMM CPU backends" ON)
option(CEED_WITH_CUDA "Build CUDA ref/shared/gen backends" OFF)
option(CEED_WITH_MAGMA "Build CUDA MAGMA backends" OFF)
option(CEED_WITH_OPENMP "Use LLVM OpenMP for shared-memory parallelism" OFF)

# Dependency locations do not enable backends; use the switches above.
set(CEED_XSMM_DIR "" CACHE PATH "libXSMM installation prefix (defaults to CMAKE_INSTALL_PREFIX)")
set(CEED_CUDA_DIR "" CACHE PATH "CUDA toolkit root")
set(CEED_MAGMA_DIR "" CACHE PATH "CUDA MAGMA installation prefix (defaults to CMAKE_INSTALL_PREFIX)")
set(CEED_BLAS_LIB "" CACHE STRING "BLAS dependencies as a semicolon-separated list of libraries")
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND NOT DEFINED ENV{CUDAARCHS})
  set(CMAKE_CUDA_ARCHITECTURES "86" CACHE STRING "CUDA architectures, for example 80;86")
endif()

if(CEED_WITH_MAGMA AND NOT CEED_WITH_CUDA)
  message(FATAL_ERROR "CEED_WITH_MAGMA requires CEED_WITH_CUDA=ON")
endif()

# Configure built-in CPU backends.
file(GLOB _ceed_sources CONFIGURE_DEPENDS
  interface/ceed*.c gallery/*/ceed*.c
  backends/ref/*.c backends/blocked/*.c backends/opt/*.c
)
list(REMOVE_ITEM _ceed_sources
  "${PROJECT_SOURCE_DIR}/interface/ceed-cuda.c"
  "${PROJECT_SOURCE_DIR}/interface/ceed-hip.c"
  "${PROJECT_SOURCE_DIR}/interface/ceed-jit-source-root-install.c")
set(_ceed_definitions UNDERSCORE=1)
set(_ceed_libraries "")
set(_ceed_dependency_includes "")
set(_ceed_pc_private "")
set(_ceed_backends /cpu/self/ref/serial /cpu/self/ref/blocked /cpu/self/opt/serial /cpu/self/opt/blocked)
set(_ceed_gpu_backends "")

# Configure CUDA backends.
message(STATUS "===================== Configuring CUDA backends =====================")
if(CEED_WITH_CUDA)
  if(CEED_CUDA_DIR)
    set(CUDAToolkit_ROOT "${CEED_CUDA_DIR}")
  endif()
  if(CEED_CUDA_DIR AND NOT CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_COMPILER "${CEED_CUDA_DIR}/bin/nvcc.exe")
  endif()
  if(NOT CMAKE_CUDA_HOST_COMPILER AND NOT DEFINED ENV{CUDAHOSTCXX})
    set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")
  endif()
  set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  file(GLOB _ceed_cuda_sources CONFIGURE_DEPENDS
    backends/cuda/*.c backends/cuda/*.cpp backends/cuda-ref/*.c backends/cuda-ref/*.cpp
    backends/cuda-ref/kernels/*.cu backends/cuda-shared/*.c backends/cuda-gen/*.c backends/cuda-gen/*.cpp)
  list(APPEND _ceed_sources interface/ceed-cuda.c ${_ceed_cuda_sources})
  list(APPEND _ceed_definitions CEED_BUILD_CUDA)
  list(APPEND _ceed_dependency_includes "${CUDAToolkit_INCLUDE_DIRS}")
  list(APPEND _ceed_libraries CUDA::cudart CUDA::cuda_driver CUDA::nvrtc CUDA::cublas)
  get_filename_component(_ceed_cuda_libdir "${CUDA_cudart_LIBRARY}" DIRECTORY)
  string(APPEND _ceed_pc_private " -L\"${_ceed_cuda_libdir}\" -lcudart -lnvrtc -lcuda -lcublas")
  list(APPEND _ceed_gpu_backends /gpu/cuda/ref /gpu/cuda/shared /gpu/cuda/gen)
endif()

# Configure optional libXSMM backends.
message(STATUS "==================== Configuring libXSMM backends ===================")
if(CEED_WITH_LIBXSMM)
  if(NOT CEED_XSMM_DIR)
    set(CEED_XSMM_DIR "${CMAKE_INSTALL_PREFIX}")
  endif()
  find_path(CEED_XSMM_INCLUDE_DIR libxsmm.h PATHS "${CEED_XSMM_DIR}/include" NO_DEFAULT_PATH)
  find_library(CEED_XSMM_LIBRARY NAMES libxsmm xsmm PATHS "${CEED_XSMM_DIR}/lib" NO_DEFAULT_PATH)
  if(NOT CEED_XSMM_INCLUDE_DIR OR NOT CEED_XSMM_LIBRARY)
    message(FATAL_ERROR
      "libXSMM is enabled but its headers or library were not found in '${CEED_XSMM_DIR}'. "
      "Set CEED_XSMM_DIR to its installation prefix, or disable it with -DCEED_WITH_LIBXSMM=OFF."
    )
  endif()
  file(GLOB _ceed_xsmm_sources CONFIGURE_DEPENDS backends/xsmm/*.c)
  list(APPEND _ceed_sources ${_ceed_xsmm_sources})
  list(APPEND _ceed_definitions CEED_BUILD_XSMM)
  list(APPEND _ceed_dependency_includes "${CEED_XSMM_INCLUDE_DIR}")
  list(APPEND _ceed_libraries "${CEED_XSMM_LIBRARY}")
  string(APPEND _ceed_pc_private " -L\"${CEED_XSMM_DIR}/lib\" -lxsmm")
  list(APPEND _ceed_backends /cpu/self/xsmm/serial /cpu/self/xsmm/blocked)
endif()

# Configure optional CUDA MAGMA backends.
message(STATUS "===================== Configuring MAGMA backends ====================")
if(CEED_WITH_MAGMA)
  if(NOT CEED_MAGMA_DIR)
    set(CEED_MAGMA_DIR "${CMAKE_INSTALL_PREFIX}")
  endif()
  find_path(CEED_MAGMA_INCLUDE_DIR magma_v2.h PATHS "${CEED_MAGMA_DIR}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(CEED_MAGMA_LIBRARY NAMES magma libmagma PATHS "${CEED_MAGMA_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  file(GLOB _ceed_magma_sources CONFIGURE_DEPENDS backends/magma/*.c backends/magma/*.cpp)
  list(APPEND _ceed_sources ${_ceed_magma_sources})
  list(APPEND _ceed_definitions CEED_BUILD_MAGMA ADD_)
  list(APPEND _ceed_dependency_includes "${CEED_MAGMA_INCLUDE_DIR}")
  list(APPEND _ceed_libraries "${CEED_MAGMA_LIBRARY}" CUDA::cusparse)
  message(STATUS "Enabling LLVM OpenMP required by MAGMA")
  set(CEED_WITH_OPENMP ON)
  string(APPEND _ceed_pc_private " -L\"${CEED_MAGMA_DIR}/lib\" -lmagma -lcublas -lcusparse -lcudart")
  list(APPEND _ceed_gpu_backends /gpu/cuda/magma /gpu/cuda/magma/det)
endif()

# Configure BLAS dependencies.
if(CEED_BLAS_LIB)
  list(APPEND _ceed_libraries ${CEED_BLAS_LIB})
elseif(CEED_WITH_LIBXSMM OR CEED_WITH_MAGMA)
  if(DEFINED ENV{MKLROOT})
    set(BLA_VENDOR Intel10_64lp_seq)
  endif()
  find_package(BLAS REQUIRED)
  set(CEED_BLAS_LIB "${BLAS_LIBRARIES}")
  list(APPEND _ceed_libraries ${BLAS_LIBRARIES})
endif()
foreach(_blas IN LISTS CEED_BLAS_LIB)
  if(IS_ABSOLUTE "${_blas}")
    get_filename_component(_dir "${_blas}" DIRECTORY)
    get_filename_component(_name "${_blas}" NAME_WE)
    string(APPEND _ceed_pc_private " -L\"${_dir}\" -l${_name}")
  else()
    string(APPEND _ceed_pc_private " ${_blas}")
  endif()
endforeach()

# Configure OpenMP dependencies.
if(CEED_WITH_OPENMP)
  include(CheckCCompilerFlag)
  check_c_compiler_flag("-openmp:llvm" CEED_HAS_LLVM_OPENMP)
  if(NOT CEED_HAS_LLVM_OPENMP)
    message(FATAL_ERROR "The selected MSVC compiler does not support -openmp:llvm")
  endif()
  find_library(CEED_OPENMP_LIBRARY NAMES libomp libomp140.x86_64 PATHS ENV LIB REQUIRED)
  list(APPEND _ceed_libraries "${CEED_OPENMP_LIBRARY}")
  get_filename_component(_dir "${CEED_OPENMP_LIBRARY}" DIRECTORY)
  get_filename_component(_name "${CEED_OPENMP_LIBRARY}" NAME_WE)
  string(APPEND _ceed_pc_private " -L\"${_dir}\" -l${_name}")
endif()
list(APPEND _ceed_backends ${_ceed_gpu_backends})

# Determine effective Release protection after backend selection.
set(CODE_PROTECTION_ACTIVE OFF)
set(PROTECT_CPU_JIT OFF)
set(PROTECT_CPU_ARCHIVE OFF)
set(PROTECT_GPU_JIT OFF)
if(CEED_CODE_PROTECTION AND NOT _ceed_build_type STREQUAL "debug")
  set(CODE_PROTECTION_ACTIVE ON)
  list(APPEND _ceed_definitions CEED_CODE_PROTECTION=1)
  if(NOT _ceed_gpu_backends)
    set(PROTECT_CPU_JIT ON)
    set(PROTECT_CPU_ARCHIVE ON)
    list(APPEND _ceed_definitions CEED_DISABLE_JIT_SOURCE=1)
  else()
    set(PROTECT_GPU_JIT ON)
    list(APPEND _ceed_definitions CEED_EMBEDDED_JIT=1)
  endif()
endif()

if(PROTECT_GPU_JIT)
  file(GLOB_RECURSE _ceed_embedded_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/ceed/jit-source/cuda/*.h"
    "${PROJECT_SOURCE_DIR}/include/ceed/jit-source/gallery/*.h")
  if(CEED_WITH_MAGMA)
    file(GLOB_RECURSE _ceed_magma_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/ceed/jit-source/magma/*.h")
    list(APPEND _ceed_embedded_headers ${_ceed_magma_headers})
  endif()
  list(APPEND _ceed_embedded_headers "${PROJECT_SOURCE_DIR}/include/ceed/types.h"
    "${PROJECT_SOURCE_DIR}/include/ceed/ceed-f32.h" "${PROJECT_SOURCE_DIR}/include/ceed/ceed-f64.h")
  list(SORT _ceed_embedded_headers)
  string(REPLACE ";" "\n" _ceed_embedded_manifest "${_ceed_embedded_headers}")
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/generated/jit-files" CONTENT "${_ceed_embedded_manifest}\n")
  add_executable(ceed-jit-pack EXCLUDE_FROM_ALL scripts/embed-jit-source.cpp)
  target_compile_features(ceed-jit-pack PRIVATE cxx_std_17)
  target_compile_options(ceed-jit-pack PRIVATE /EHsc)
  target_link_libraries(ceed-jit-pack PRIVATE bcrypt)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/generated/ceed-jit-resources.h"
    COMMAND ceed-jit-pack "${PROJECT_SOURCE_DIR}/include" "${PROJECT_BINARY_DIR}/generated/jit-files"
      "${PROJECT_BINARY_DIR}/generated/ceed-jit-resources.h"
    DEPENDS ceed-jit-pack ${_ceed_embedded_headers} "${PROJECT_BINARY_DIR}/generated/jit-files" VERBATIM)
  list(APPEND _ceed_sources interface/ceed-jit-source-embedded.cpp "${PROJECT_BINARY_DIR}/generated/ceed-jit-resources.h")
  list(APPEND _ceed_dependency_includes "${PROJECT_BINARY_DIR}/generated")
  list(APPEND _ceed_libraries bcrypt)
  string(APPEND _ceed_pc_private " -lbcrypt")
endif()

# Build the library.
message(STATUS "======================= Configuring libCEED =========================")
add_library(ceed_objects OBJECT ${_ceed_sources})
target_include_directories(ceed_objects PRIVATE include ${_ceed_dependency_includes})
target_compile_definitions(ceed_objects PRIVATE ${_ceed_definitions})
set_target_properties(ceed_objects PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
  CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED YES
)
target_compile_options(ceed_objects PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
if(_ceed_build_type STREQUAL "debug")
  target_compile_definitions(ceed_objects PRIVATE _ITERATOR_DEBUG_LEVEL=2)
else()
  target_compile_definitions(ceed_objects PRIVATE _ITERATOR_DEBUG_LEVEL=0)
endif()
if(CEED_WITH_OPENMP)
  target_compile_options(ceed_objects PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-openmp:llvm>")
endif()
if(PROTECT_CPU_JIT OR PROTECT_GPU_JIT)
  set(_ceed_jit_root "")
else()
  set(_ceed_jit_root "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
  set_property(SOURCE interface/ceed.c APPEND PROPERTY COMPILE_DEFINITIONS "CEED_JIT_SOURCE_ROOT_BUILD=\"${PROJECT_SOURCE_DIR}/include/\"")
endif()
set_property(SOURCE interface/ceed-jit-source-root-default.c APPEND PROPERTY COMPILE_DEFINITIONS "CEED_JIT_SOURCE_ROOT_DEFAULT=\"${_ceed_jit_root}\"")

add_library(ceed STATIC $<TARGET_OBJECTS:ceed_objects>)
add_library(CEED::ceed ALIAS ceed)
set_target_properties(ceed PROPERTIES
  OUTPUT_NAME libceed
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
)
target_include_directories(ceed PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(ceed PUBLIC ${_ceed_libraries})
# Propagate protection to consumers of this CMake target, including QFunction macros.
if(CODE_PROTECTION_ACTIVE)
  target_compile_definitions(ceed INTERFACE CEED_CODE_PROTECTION=1)
endif()
if(PROTECT_CPU_JIT)
  target_compile_definitions(ceed INTERFACE CEED_DISABLE_JIT_SOURCE=1)
endif()
if(PROTECT_GPU_JIT)
  target_compile_definitions(ceed INTERFACE CEED_EMBEDDED_JIT=1)
endif()
if(PROTECT_CPU_ARCHIVE)
  find_program(CEED_POWERSHELL powershell REQUIRED)
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/ceed.objects.in" CONTENT "$<JOIN:$<TARGET_OBJECTS:ceed_objects>,\n>\n")
  add_custom_command(TARGET ceed POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_BINARY_DIR}/ceed.objects.in" "$<TARGET_FILE:ceed>.objects"
    COMMAND "${CEED_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${PROJECT_SOURCE_DIR}/scripts/protect-msvc-library.ps1"
      -Librarian "${CMAKE_AR}" -Output "$<TARGET_FILE:ceed>" -ObjectList "$<TARGET_FILE:ceed>.objects"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
  set_property(SOURCE interface/ceed-jit-source-root-default.c APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/scripts/protect-msvc-library.ps1")
endif()

# Generate package metadata and installation rules.
include(CMakePackageConfigHelpers)
set(_ceed_pc_cflags "")
if(CEED_WITH_OPENMP)
  set(_ceed_pc_cflags "-openmp:llvm")
endif()
configure_file(cmake/ceed.pc.in "${PROJECT_BINARY_DIR}/pkgconfig/ceed.pc" @ONLY)
configure_package_config_file(cmake/CEEDConfig.cmake.in "${PROJECT_BINARY_DIR}/CEEDConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CEED"
)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/CEEDConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion
)
install(TARGETS ceed EXPORT CEEDTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/ceed.h include/ceedf.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(PROTECT_GPU_JIT)
  install(DIRECTORY include/ceed/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ceed"
    FILES_MATCHING PATTERN "*.h" PATTERN "jit-source" EXCLUDE)
else()
  install(DIRECTORY include/ceed/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ceed" FILES_MATCHING PATTERN "*.h")
endif()
install(FILES "${PROJECT_BINARY_DIR}/pkgconfig/ceed.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(EXPORT CEEDTargets NAMESPACE CEED:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CEED")
install(FILES
  "${PROJECT_BINARY_DIR}/CEEDConfig.cmake" "${PROJECT_BINARY_DIR}/CEEDConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CEED"
)

# Configure the focused CTest suite.
if(BUILD_TESTING)
  # Focused, MSVC-compatible tests; the full Makefile suite includes C VLAs.
  foreach(_test t000-ceed t001-ceed t010-config t011-code-protection)
    add_executable(${_test} "tests/${_test}.c")
    target_link_libraries(${_test} PRIVATE ceed)
    set_property(TARGET ${_test} PROPERTY C_STANDARD 11)
    foreach(_resource IN LISTS _ceed_backends)
      string(REPLACE "/" "_" _suffix "${_resource}")
      add_test(NAME "${_test}${_suffix}" COMMAND ${_test} "${_resource}")
      set_tests_properties("${_test}${_suffix}" PROPERTIES
        FAIL_REGULAR_EXPRESSION ".+" ENVIRONMENT_MODIFICATION "CEED_DEBUG=unset:;DEBUG=unset:;DBG=unset:")
    endforeach()
  endforeach()
endif()

# Print effective build settings.
message(STATUS "===================== libCEED build configuration ===================")
foreach(_var CMAKE_GENERATOR CMAKE_MAKE_PROGRAM CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_BUILD_TYPE
    CMAKE_LINKER CMAKE_AR CMAKE_MSVC_RUNTIME_LIBRARY BUILD_SHARED_LIBS BUILD_TESTING
    CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_CUDA_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS
    CEED_WITH_CUDA CEED_WITH_OPENMP CEED_WITH_LIBXSMM CEED_WITH_MAGMA
    CMAKE_CUDA_COMPILER CMAKE_CUDA_ARCHITECTURES CMAKE_CUDA_HOST_COMPILER
    CEED_OPENMP_LIBRARY CEED_CUDA_DIR CEED_XSMM_DIR CEED_MAGMA_DIR CEED_XSMM_INCLUDE_DIR CEED_XSMM_LIBRARY
    CEED_MAGMA_INCLUDE_DIR CEED_MAGMA_LIBRARY CEED_BLAS_LIB
    CMAKE_INSTALL_PREFIX CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR)
  message(STATUS "  ${_var}=${${_var}}")
endforeach()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _ceed_config)
foreach(_kind C CXX CUDA EXE_LINKER STATIC_LINKER)
  if(_kind STREQUAL "CUDA" AND NOT CEED_WITH_CUDA)
    continue()
  endif()
  set(_var "CMAKE_${_kind}_FLAGS_${_ceed_config}")
  message(STATUS "  ${_var}=${${_var}}")
endforeach()
message(STATUS "  CEED_CODE_PROTECTION_REQUESTED=${CEED_CODE_PROTECTION}")
message(STATUS "  CEED_CODE_PROTECTION=${CODE_PROTECTION_ACTIVE}")
if(NOT CODE_PROTECTION_ACTIVE)
  if(NOT CEED_CODE_PROTECTION)
    message(STATUS "  CEED_CODE_PROTECTION_DISABLED_REASON=Protection was not requested (CEED_CODE_PROTECTION=OFF)")
  else()
    message(STATUS "  CEED_CODE_PROTECTION_DISABLED_REASON=CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}; protection requires a non-Debug build")
  endif()
endif()
foreach(_var CODE_PROTECTION_ACTIVE PROTECT_CPU_JIT PROTECT_CPU_ARCHIVE PROTECT_GPU_JIT)
  message(STATUS "  ${_var}=${${_var}}")
endforeach()
if(PROTECT_GPU_JIT)
  message(STATUS "  GPU_JIT_SOURCE=AES-256-GCM embedded resources; NVRTC only; application sources remain external")
  message(STATUS "  GPU_JIT_CACHE=ON (per-user binary cache; CEED_JIT_CACHE=OFF disables reuse)")
endif()
message(STATUS "  BACKENDS=${_ceed_backends}")
if(_ceed_gpu_backends)
  message(STATUS "  GPU_BACKENDS=${_ceed_gpu_backends}")
else()
  message(STATUS "  GPU_BACKENDS=(none)")
endif()
message(STATUS "===================== Configure stage complete ======================")
