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

project(web_cam C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(web_cam main.cpp tcp_streamer.cpp cam_streamer.cpp)

pico_set_program_name(web_cam "web_cam")
pico_set_program_version(web_cam "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(web_cam 0)
pico_enable_stdio_usb(web_cam 1)

# Add the standard library to the build
target_link_libraries(web_cam
    hardware_i2c
    hardware_pio
    pico_multicore
    pico_stdlib
)

# Add the standard include files to the build
target_include_directories(web_cam PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    C:/Users/kier/source/pico/lib/pico_hm01b0/include
    C:/Users/kier/source/pico/lib/tcp_client/include
    C:/Users/kier/source/pico/lib/too_jpeg/include
)

#Add any user requested libraries
target_link_libraries(web_cam
  #pico_cyw43_arch_lwip_threadsafe_background

  C:/Users/kier/source/pico/lib/pico_hm01b0/build/libade_pico_hm01b0.a
  #C:/Users/kier/source/pico/lib/pico_hm01b0/build/libmock_pico_hm01b0.a
  #C:/Users/kier/source/pico/lib/pico_hm01b0/build/libbit_bang_pico_hm01b0.a
  #C:/Users/kier/source/pico/lib/pico_hm01b0/build/libmarques_pico_hm01b0.a

  C:/Users/kier/source/pico/lib/tcp_client/build/liblwip_tcp_client.a
  #C:/Users/kier/source/pico/lib/tcp_client/build/libmock_tcp_client.a

  C:/Users/kier/source/pico/lib/too_jpeg/build/libtoo_jpeg.a
  #C:/Users/kier/source/pico/lib/too_jpeg/build/libstb_too_jpeg.a
  #C:/Users/kier/source/pico/lib/too_jpeg/build/libmock_too_jpeg.a
)

pico_add_extra_outputs(web_cam)

