cmake_minimum_required(VERSION 3.15)
project(VentusSimulator 
        VERSION 1.0
        LANGUAGES C CXX)

#
# global settings
#

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# use ccache
set(CMAKE_C_COMPILER_LAUNCHER "ccache")
set(CMAKE_CXX_COMPILER_LAUNCHER "ccache")
# use -O2 & enable assert & LTO for release build
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")
add_compile_options(
  $<$<AND:$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-UNDEBUG>
  $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-UNDEBUG>
  $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/U"NDEBUG">
  $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:MSVC>>:/U"NDEBUG">
)
# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
# use spdlog trace level for debug build
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
else()
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
endif()
# export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#
# dependency packages
#

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# Get SystemC PATH from user
if(DEFINED SYSTEMC_HOME AND NOT SYSTEMC_HOME STREQUAL "")
  # use cmake cmd arg -DSYSTEMC_HOME
elseif(DEFINED ENV{SYSTEMC_HOME} AND NOT "$ENV{SYSTEMC_HOME}" STREQUAL "")
  # use environment variable
  set(SYSTEMC_HOME "$ENV{SYSTEMC_HOME}" CACHE PATH "SystemC install root (from environment)" FORCE)
else() # Error
  message(FATAL_ERROR "SYSTEMC_HOME not set."
    "Please use -DSYSTEMC_HOME=/path/to/systemc or set SYSTEMC_HOME in the environment.")
endif()
file(TO_CMAKE_PATH "${SYSTEMC_HOME}" SYSTEMC_HOME) # canonicalize path
if(NOT EXISTS "${SYSTEMC_HOME}")
  message(FATAL_ERROR "SYSTEMC_HOME points to a non-existent directory: ${SYSTEMC_HOME}")
endif()
set(SYSTEMC_INCLUDE_DIR ${SYSTEMC_HOME}/include)
set(SYSTEMC_LIBRARY_DIR ${SYSTEMC_HOME}/lib-linux64)

# Get GVM_REF_DIR from user
if(DEFINED GVM_REF_DIR AND NOT GVM_REF_DIR STREQUAL "")
  # use cmake cmd arg
elseif(DEFINED ENV{GVM_REF_DIR} AND NOT "$ENV{GVM_REF_DIR}" STREQUAL "")
  set(GVM_REF_DIR "$ENV{GVM_REF_DIR}" CACHE PATH "GVM Ref install root" FORCE)
else()
  if(EXISTS "${CMAKE_INSTALL_PREFIX}/lib/libgvmref.so")
    set(GVM_REF_DIR "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "GVM Ref install root" FORCE)
  endif()
endif()

#
# include submodules
#
add_subdirectory(dependencies/membox)
add_subdirectory(dependencies/ramulator2)

#
# target libVentusCycleSim.so
#
file(GLOB_RECURSE SM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/sm/*.cpp")
add_library(VentusCycleSim SHARED
    ${SM_SOURCES}
    src/context_model.cpp 
    src/CTA_Scheduler.cpp 
    src/parameters.cpp 
    src/top_gpgpu.cpp
    src/ramulator.cpp
    src/gvm.cpp
    src/gvm_care_insns.cpp
    src/gvm_dpic.cpp
    src/gvm_global_var.cpp
    src/ventus_cyclesim.cpp 
    src/ventus_cyclesim_impl.cpp
)
target_include_directories(VentusCycleSim
    PRIVATE ${SYSTEMC_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_directories(VentusCycleSim
    PRIVATE ${SYSTEMC_LIBRARY_DIR}
)
if(DEFINED GVM_REF_DIR)
    target_link_directories(VentusCycleSim PRIVATE ${GVM_REF_DIR})
endif()
target_link_libraries(VentusCycleSim
    PRIVATE SV
    PRIVATE ramulator
    PRIVATE systemc
    PRIVATE spdlog
    PRIVATE fmt
    PRIVATE gvmref
)
set_target_properties(VentusCycleSim PROPERTIES
    # libramulator.so will be installed together, use $ORIGIN
    INSTALL_RPATH "${SYSTEMC_LIBRARY_DIR};$ORIGIN"
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/ventus_cyclesim.h"
)
if(DEFINED GVM_REF_DIR)
    set_property(TARGET VentusCycleSim APPEND PROPERTY INSTALL_RPATH "${GVM_REF_DIR}")
endif()
target_compile_definitions(VentusCycleSim PRIVATE
    VENTUS_CYCLESIM_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
install(TARGETS VentusCycleSim
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

#
# target main exe
#
add_executable(main
    src/cmdarg.cpp
    src/parse_kernel.cpp
    src/main.cpp
    src/task.cpp
)
target_link_libraries(main
    PRIVATE VentusCycleSim
    PRIVATE spdlog
    PRIVATE fmt
)
target_compile_definitions(main PRIVATE
    VENTUS_CYCLESIM_PROJECT_DIR="\"${CMAKE_CURRENT_SOURCE_DIR}\""
)
