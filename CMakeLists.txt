cmake_minimum_required(VERSION 3.15)
project(VentusSimulator 
        VERSION 1.0
        LANGUAGES C CXX)

# 检查 SYSTEMC_HOME 环境变量
if(NOT DEFINED ENV{SYSTEMC_HOME})
    message(FATAL_ERROR "请设置 SYSTEMC_HOME 环境变量！")
endif()

#
# global settings
#

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# use ccache
set(CMAKE_C_COMPILER_LAUNCHER "ccache")
set(CMAKE_CXX_COMPILER_LAUNCHER "ccache")
# use -O2 and enable assert for release build
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
# use spdlog trace level for debug build
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
endif()
# export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#
# dependency packages
#

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# SystemC PATH
set(SYSTEMC_HOME $ENV{SYSTEMC_HOME})
set(SYSTEMC_INCLUDE_DIR ${SYSTEMC_HOME}/include)
set(SYSTEMC_LIBRARY_DIR ${SYSTEMC_HOME}/lib-linux64)

#
# include submodules
#
add_subdirectory(dependencies/membox)
add_subdirectory(dependencies/ramulator2)

#
# target libVentusCycleSim.so
#
file(GLOB_RECURSE SM_SOURCES "src/sm/*.cpp")
set(CORE_SOURCES 
    src/context_model.cpp 
    src/CTA_Scheduler.cpp 
    src/parameters.cpp 
    src/top_gpgpu.cpp
    src/ramulator.cpp
    src/ventus_cyclesim.cpp 
    src/ventus_cyclesim_impl.cpp
)
add_library(VentusCycleSim SHARED
    ${SM_SOURCES}
    ${CORE_SOURCES}
)
target_include_directories(VentusCycleSim
    PRIVATE ${SYSTEMC_INCLUDE_DIR}
)
target_link_directories(VentusCycleSim
    PRIVATE ${SYSTEMC_LIBRARY_DIR}
)
target_link_libraries(VentusCycleSim
    PRIVATE SV
    PRIVATE ramulator
    PRIVATE systemc
    PRIVATE spdlog
    PRIVATE fmt
)
set_target_properties(VentusCycleSim PROPERTIES
    INSTALL_RPATH "${SYSTEMC_LIBRARY_DIR};${RAMULATOR2_SRC_DIR}"
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ventus_cyclesim.h"
)
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

