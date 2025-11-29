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

project(tcp_client C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# LWIP tcp client library
add_library(lwip_tcp_client lwip_tcp_client.cpp)

pico_set_program_name(lwip_tcp_client "lwip_tcp_client")
pico_set_program_version(lwip_tcp_client "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(lwip_tcp_client 0)
pico_enable_stdio_usb(lwip_tcp_client 1)

# Add the standard library to the build
target_link_libraries(lwip_tcp_client pico_stdlib)

# Add the standard include files to the build
target_include_directories(lwip_tcp_client PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(lwip_tcp_client PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(lwip_tcp_client pico_cyw43_arch_lwip_threadsafe_background)

# LWIP DEFINITIONS
add_definitions(-DNDEBUG=1)

#### Mongoose tcp client library
add_library(mongoose_tcp_client mongoose_tcp_client.cpp mongoose.c)
pico_set_program_name(mongoose_tcp_client "mongoose_tcp_client")
pico_set_program_version(mongoose_tcp_client "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(mongoose_tcp_client 0)
pico_enable_stdio_usb(mongoose_tcp_client 0)

# Add the standard library to the build
target_link_libraries(mongoose_tcp_client pico_stdlib)

# Add the standard include files to the build
target_include_directories(mongoose_tcp_client PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(mongoose_tcp_client PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(mongoose_tcp_client hardware_pio hardware_dma pico_cyw43_driver pico_cyw43_arch_none pico_rand)

# DEFINITIONS FOR MONGOOSE CLIENT LIBRARY BUILD
add_definitions(-DMG_ENABLE_TCPIP=1)
add_definitions(-DMG_ENABLE_PACKED_FS=1)
add_definitions(-DMG_ARCH=11) # configure for pico w
add_definitions(-DMG_ENABLE_DRIVER_PICO_W=1)
add_definitions(-DMG_ENABLE_PACKED_FS=1)
# add_definitions(-DMG_ENABLE_CUSTOM_RANDOM=0)
# add_definitions(-DMG_ENABLE_FILE=0)
# add_definitions(-DHTTP_URL="0.0.0.0:80")
# add_definitions(-DHTTPS_URL="0.0.0.0:443")
# pico_add_extra_outputs(tcp_client)


# MOCK tcp client library
add_library(mock_tcp_client mock_tcp_client.cpp)

pico_set_program_name(mock_tcp_client "mock_tcp_client")
pico_set_program_version(mock_tcp_client "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(mock_tcp_client 0)
pico_enable_stdio_usb(mock_tcp_client 0)

# Add the standard library to the build
target_link_libraries(mock_tcp_client pico_stdlib)

# Add the standard include files to the build
target_include_directories(mock_tcp_client PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(mock_tcp_client PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(mock_tcp_client pico_cyw43_arch_lwip_threadsafe_background)
