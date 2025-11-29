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

project(pico_hm01b0 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ARM DEVELOPER ECOSYSTEM HM01B0 DRIVER.
add_library(ade_pico_hm01b0 ade_hm01b0.cpp)

pico_set_program_name(ade_pico_hm01b0 "ade_pico_hm01b0")
pico_set_program_version(ade_pico_hm01b0 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ade_pico_hm01b0 0)
pico_enable_stdio_usb(ade_pico_hm01b0 1)

# Add the standard library to the build
target_link_libraries(ade_pico_hm01b0
        pico_stdlib)

target_include_directories(ade_pico_hm01b0 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(ade_pico_hm01b0 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(ade_pico_hm01b0 
    cmsis_core
    hardware_dma
    hardware_i2c
    hardware_pio
    hardware_pwm
)


# MARQUES PICO HM01B0 DRIVER LIBRARY
add_library(marques_pico_hm01b0 marques_hm01b0.cpp)

pico_set_program_name(marques_pico_hm01b0 "marques_pico_hm01b0")
pico_set_program_version(marques_pico_hm01b0 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(marques_pico_hm01b0 0)
pico_enable_stdio_usb(marques_pico_hm01b0 1)

# Add the standard library to the build
target_link_libraries(marques_pico_hm01b0
        pico_stdlib)

target_include_directories(marques_pico_hm01b0 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(marques_pico_hm01b0 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(marques_pico_hm01b0 
    cmsis_core
    hardware_dma
    hardware_i2c
    hardware_pio
    hardware_pwm
)

# BIT BANG PICO HM01B0 DRIVER LIBRARY
add_library(bit_bang_pico_hm01b0 bit_bang_hm01b0.cpp)

pico_set_program_name(bit_bang_pico_hm01b0 "bit_bang_pico_hm01b0")
pico_set_program_version(bit_bang_pico_hm01b0 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(bit_bang_pico_hm01b0 0)
pico_enable_stdio_usb(bit_bang_pico_hm01b0 1)

# Add the standard library to the build
target_link_libraries(bit_bang_pico_hm01b0
        pico_stdlib)

target_include_directories(bit_bang_pico_hm01b0 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(bit_bang_pico_hm01b0 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(bit_bang_pico_hm01b0
    cmsis_core
    hardware_i2c
)

# MOCK PICO HM01B0 DRIVER LIBRARY
add_library(mock_pico_hm01b0 mock_hm01b0.cpp)

pico_set_program_name(mock_pico_hm01b0 "mock_pico_hm01b0")
pico_set_program_version(mock_pico_hm01b0 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(mock_pico_hm01b0 0)
pico_enable_stdio_usb(mock_pico_hm01b0 1)

# Add the standard library to the build
target_link_libraries(mock_pico_hm01b0
        pico_stdlib)

target_include_directories(mock_pico_hm01b0 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add the standard include files to the build
target_include_directories(mock_pico_hm01b0 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
# target_link_libraries(mock_pico_hm01b0 
#     cmsis_core
#     hardware_dma
#     hardware_i2c
#     hardware_pio
#     hardware_pwm
# )
