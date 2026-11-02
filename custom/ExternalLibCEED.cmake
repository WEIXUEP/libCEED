# Native Windows/MSVC libCEED dependency for a Palace-style superbuild.
# Include this instead of the Makefile-based ExternalLibCEED.cmake.
include(ExternalProject)

# This module builds libCEED through its native CMake entry point.  The parent
# project owns the feature switches; all dependency paths are forwarded as
# CEED_* cache variables and therefore do not depend on a shell command line.

if(NOT DEFINED CEED_CODE_PROTECTION)
  set(CEED_CODE_PROTECTION OFF)
  if(DEFINED PALACE_CODE_PROTECTION AND PALACE_CODE_PROTECTION)
    set(CEED_CODE_PROTECTION ON)
  endif()
endif()

if(NOT DEFINED CEED_WITH_LIBXSMM)
  if(DEFINED PALACE_WITH_LIBXSMM)
    set(CEED_WITH_LIBXSMM ${PALACE_WITH_LIBXSMM})
  else()
    set(CEED_WITH_LIBXSMM ON)
  endif()
endif()
if(NOT DEFINED CEED_WITH_CUDA)
  if(DEFINED PALACE_WITH_CUDA)
    set(CEED_WITH_CUDA ${PALACE_WITH_CUDA})
  else()
    set(CEED_WITH_CUDA OFF)
  endif()
endif()
if(NOT DEFINED CEED_WITH_MAGMA)
  if(DEFINED PALACE_WITH_MAGMA)
    set(CEED_WITH_MAGMA ${PALACE_WITH_MAGMA})
  else()
    set(CEED_WITH_MAGMA OFF)
  endif()
endif()
if(NOT DEFINED CEED_WITH_OPENMP)
  if(DEFINED PALACE_WITH_OPENMP)
    set(CEED_WITH_OPENMP ${PALACE_WITH_OPENMP})
  else()
    set(CEED_WITH_OPENMP OFF)
  endif()
endif()

if(PALACE_WITH_HIP)
  message(FATAL_ERROR "The native libCEED CMake entry point does not yet support HIP.")
endif()

# Dependencies must finish installing before libCEED configures.
set(LIBCEED_DEPENDENCIES)
if(CEED_WITH_LIBXSMM)
  list(APPEND LIBCEED_DEPENDENCIES libxsmm)
endif()
if(CEED_WITH_MAGMA)
  list(APPEND LIBCEED_DEPENDENCIES magma)
endif()

set(LIBCEED_CMAKE_ARGS
  "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}"
  "-DBUILD_SHARED_LIBS:BOOL=OFF"
  "-DBUILD_TESTING:BOOL=OFF"
)
if(NOT CEED_XSMM_DIR)
  set(CEED_XSMM_DIR "${CMAKE_INSTALL_PREFIX}")
endif()
if(NOT CEED_MAGMA_DIR)
  set(CEED_MAGMA_DIR "${CMAKE_INSTALL_PREFIX}")
endif()
list(APPEND LIBCEED_CMAKE_ARGS
  "-DCEED_XSMM_DIR:PATH=${CEED_XSMM_DIR}"
  "-DCEED_MAGMA_DIR:PATH=${CEED_MAGMA_DIR}"
)
foreach(_feature LIBXSMM CUDA MAGMA OPENMP)
  set(_enabled OFF)
  if(CEED_WITH_${_feature})
    set(_enabled ON)
  endif()
  list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_WITH_${_feature}:BOOL=${_enabled}")
endforeach()
list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_CODE_PROTECTION:BOOL=${CEED_CODE_PROTECTION}")

# Forward tool locations without adding shell quotes inside their cache values.
foreach(_var CMAKE_TOOLCHAIN_FILE CMAKE_MAKE_PROGRAM CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_LINKER CMAKE_AR)
  if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
    list(APPEND LIBCEED_CMAKE_ARGS "-D${_var}:FILEPATH=${${_var}}")
  endif()
endforeach()

# ExternalProject uses '|' to carry semicolon lists as a single cache argument.
foreach(_var CMAKE_BUILD_TYPE CMAKE_PREFIX_PATH CMAKE_C_FLAGS CMAKE_CXX_FLAGS
    CMAKE_EXE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR
    CMAKE_MSVC_RUNTIME_LIBRARY)
  if(DEFINED ${_var})
    set(_value "${${_var}}")
    if(_var STREQUAL "CMAKE_EXE_LINKER_FLAGS")
      string(REGEX REPLACE "(^|[ ;])(-|/)openmp:llvm([ ;]|$)" "\\1\\3" _value "${_value}")
    endif()
    string(REPLACE ";" "|" _value "${_value}")
    list(APPEND LIBCEED_CMAKE_ARGS "-D${_var}:STRING=${_value}")
  endif()
endforeach()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _libceed_config)
if(NOT _libceed_config)
  set(_libceed_config RELEASE)
endif()
foreach(_var CMAKE_C_FLAGS_${_libceed_config} CMAKE_CXX_FLAGS_${_libceed_config}
    CMAKE_EXE_LINKER_FLAGS_${_libceed_config} CMAKE_STATIC_LINKER_FLAGS_${_libceed_config})
  if(DEFINED ${_var})
    string(REPLACE ";" "|" _value "${${_var}}")
    list(APPEND LIBCEED_CMAKE_ARGS "-D${_var}:STRING=${_value}")
  endif()
endforeach()

if(CEED_WITH_CUDA)
  if(CEED_CUDA_DIR)
    list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_CUDA_DIR:PATH=${CEED_CUDA_DIR}")
  elseif(CUDAToolkit_LIBRARY_ROOT)
    list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_CUDA_DIR:PATH=${CUDAToolkit_LIBRARY_ROOT}")
  elseif(CUDAToolkit_ROOT)
    list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_CUDA_DIR:PATH=${CUDAToolkit_ROOT}")
  endif()
  foreach(_var CMAKE_CUDA_COMPILER CMAKE_CUDA_HOST_COMPILER)
    if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
      list(APPEND LIBCEED_CMAKE_ARGS "-D${_var}:FILEPATH=${${_var}}")
    endif()
  endforeach()
  foreach(_var CMAKE_CUDA_ARCHITECTURES CMAKE_CUDA_FLAGS CMAKE_CUDA_FLAGS_${_libceed_config})
    if(DEFINED ${_var})
      string(REPLACE ";" "|" _value "${${_var}}")
      list(APPEND LIBCEED_CMAKE_ARGS "-D${_var}:STRING=${_value}")
    endif()
  endforeach()
endif()

if(CEED_BLAS_LIB)
  set(_blas "${CEED_BLAS_LIB}")
elseif(BLAS_LAPACK_LIBRARIES)
  string(REPLACE "$<SEMICOLON>" ";" _blas "${BLAS_LAPACK_LIBRARIES}")
endif()
if(_blas)
  string(REPLACE ";" "|" _blas "${_blas}")
  list(APPEND LIBCEED_CMAKE_ARGS "-DCEED_BLAS_LIB:STRING=${_blas}")
endif()

# A pre-extracted checkout can be supplied without downloading or switching it.
if(LIBCEED_SOURCE_DIR)
  set(_libceed_source SOURCE_DIR "${LIBCEED_SOURCE_DIR}" DOWNLOAD_COMMAND "")
else()
  if(NOT EXTERN_LIBCEED_URL OR NOT EXTERN_LIBCEED_GIT_TAG)
    message(FATAL_ERROR "Set LIBCEED_SOURCE_DIR, or both EXTERN_LIBCEED_URL and EXTERN_LIBCEED_GIT_TAG.")
  endif()
  set(_libceed_source
    SOURCE_DIR "${CMAKE_BINARY_DIR}/extern/libCEED"
    GIT_REPOSITORY "${EXTERN_LIBCEED_URL}" GIT_TAG "${EXTERN_LIBCEED_GIT_TAG}")
endif()

message(STATUS "====================== libCEED CMake arguments ======================")
foreach(_arg IN LISTS LIBCEED_CMAKE_ARGS)
  message(STATUS "  ${_arg}")
endforeach()
ExternalProject_Add(libCEED
  DEPENDS ${LIBCEED_DEPENDENCIES}
  ${_libceed_source}
  BINARY_DIR "${CMAKE_BINARY_DIR}/extern/libCEED-build"
  INSTALL_DIR "${CMAKE_INSTALL_PREFIX}"
  PREFIX "${CMAKE_BINARY_DIR}/extern/libCEED-cmake"
  CMAKE_GENERATOR "${CMAKE_GENERATOR}"
  CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
  CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
  LIST_SEPARATOR "|"
  CMAKE_ARGS ${LIBCEED_CMAKE_ARGS}
  UPDATE_COMMAND ""
  TEST_COMMAND ""
)
