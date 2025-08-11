cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

set(UWB_CHIP_TYPE "CX310")

if(UWB_CHIP_TYPE STREQUAL "DW1000")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC UWB_CHIP_TYPE_DW1000)
    target_link_libraries(${CMAKE_PROJECT_NAME} dw1000)
elseif(UWB_CHIP_TYPE STREQUAL "CX310")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC UWB_CHIP_TYPE_CX310)
    target_link_libraries(${CMAKE_PROJECT_NAME} cx310)
else()
    message(FATAL_ERROR "Unsupported chip type: ${UWB_CHIP_TYPE}")
endif()

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(User)
add_subdirectory(easylogger)
add_subdirectory(FreeRTOScpp)
add_subdirectory(protocol)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # Add user defined libraries
    # dw1000
    cx310
    easylogger
    FreeRTOScpp
    WhtsProtocol
)

# 设置cmake install指令，烧录程序
include(${PROJECT_SOURCE_DIR}/CMake/flash_by_install.cmake)
include(${PROJECT_SOURCE_DIR}/CMake/create_bin.cmake)