# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(too_jpeg C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_library(too_jpeg too_jpeg.cpp )

pico_set_program_name(too_jpeg "too_jpeg")
pico_set_program_version(too_jpeg "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(too_jpeg 0)
pico_enable_stdio_usb(too_jpeg 1)

# Add the standard library to the build
target_link_libraries(too_jpeg pico_stdlib)

target_compile_options(too_jpeg PRIVATE -O3)

target_include_directories(too_jpeg PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(too_jpeg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)


# MOCK TOO JPEG LIBRARY
add_library(mock_too_jpeg mock_too_jpeg.cpp)

pico_set_program_name(mock_too_jpeg "mock_too_jpeg")
pico_set_program_version(mock_too_jpeg "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(mock_too_jpeg 0)
pico_enable_stdio_usb(mock_too_jpeg 1)

# Add the standard library to the build
target_link_libraries(mock_too_jpeg
        pico_stdlib)

target_include_directories(mock_too_jpeg PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(mock_too_jpeg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)


# STB TOO JPEG LIBRARY
add_library(stb_too_jpeg stb_too_jpeg.cpp)

pico_set_program_name(stb_too_jpeg "stb_too_jpeg")
pico_set_program_version(stb_too_jpeg "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(stb_too_jpeg 0)
pico_enable_stdio_usb(stb_too_jpeg 1)

# Add the standard library to the build
target_link_libraries(stb_too_jpeg
        pico_stdlib)

target_include_directories(stb_too_jpeg PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(stb_too_jpeg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# pico_add_extra_outputs(too_jpeg)

