#------------------------------------------------------------------------------#
# Set the minimum CMake version
#------------------------------------------------------------------------------#

cmake_minimum_required(VERSION 3.2)
cmake_policy(SET CMP0074 NEW)
include(ExternalProject)

# a macro for gather options matching a pattern
macro(GatherVariables VARNAME PATTERN)
  set(${VARNAME} "")
  get_cmake_property(_variableNames VARIABLES)
  foreach (_variableName ${_variableNames})
    if(_variableName MATCHES ${PATTERN})
      list(APPEND ${VARNAME} "-D${_variableName}=${${_variableName}} ")
    endif()
  endforeach()
endmacro()

#------------------------------------------------------------------------------#
# Setup the project
#------------------------------------------------------------------------------#

project(CONTRA-TPL VERSION 0.1.0)

# cmake module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/../cmake")

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# copy some pertinent cmake flags
set(COMPILE_FLAGS
  -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
  -DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS}
  -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
  -DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}
  -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
  -DCMAKE_PREFIX_PATH:FILEPATH=${CMAKE_PREFIX_PATH})


#------------------------------------------------------------------------------#
# LLVM
#------------------------------------------------------------------------------#
option(BUILD_LLVM "Build llvm." ON)
if (BUILD_LLVM)
  MESSAGE(STATUS "Building LLVM.")

  # Create a list with an alternate separator e.g. pipe symbol
  set(LLVM_TARGETS_TO_BUILD "X86;AMDGPU")
  string(REPLACE ";" "|" LLVM_TARGETS_TO_BUILD_ALT_SEP "${LLVM_TARGETS_TO_BUILD}")

  ExternalProject_Add(llvm
    URL https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.1/llvm-10.0.1.src.tar.xz
    INSTALL_DIR  ${CMAKE_INSTALL_PREFIX}
    UPDATE_COMMAND  ""
    #SOURCE_SUBDIR llvm

    LIST_SEPARATOR | # Use the alternate list separator

    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DLLVM_ENABLE_RTTI=ON
      -DLLVM_BUILD_LLVM_DYLIB=${BUILD_SHARED_LIBS}
      #-DLLVM_TARGETS_TO_BUILD=${LLVM_TARGETS_TO_BUILD_ALT_SEP}
      #-DLLVM_ENABLE_PROJECTS=clang
      ${COMPILE_FLAGS}
  )
endif()

#------------------------------------------------------------------------------#
# Legion
#------------------------------------------------------------------------------#
option(BUILD_LEGION "Build legion." ON)
if (BUILD_LEGION)
  MESSAGE(STATUS "Building Legion.")
  ExternalProject_Add(legion
    GIT_REPOSITORY https://github.com/StanfordLegion/legion.git
    INSTALL_DIR  ${CMAKE_INSTALL_PREFIX}
    UPDATE_COMMAND  ""
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DBUILD_SHARED_LIBS=True
      ${COMPILE_FLAGS}
  )
endif()


