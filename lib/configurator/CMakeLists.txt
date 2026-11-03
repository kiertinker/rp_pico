add_library(configurator INTERFACE)

target_sources(configurator INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/configurator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/http_server/http_server.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver/dhcpserver.cpp
)

target_include_directories(configurator INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/http_server
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
    $ENV{PICO_LIB_PATH}/utils/include
)

# NOTE the consuming binary must contain lwipopts.h in it's target_include_directories path
# and that lwipopts.h must contain the following definitions:
#define LWIP_HTTPD                         1
#define LWIP_HTTPD_SSI                     1
#define LWIP_HTTPD_SUPPORT_SSI             1  // Server-Side Includes (optional)
#define LWIP_HTTPD_SSI_BY_FILE_EXTENSION   1
#define LWIP_HTTPD_CGI                     1  // Include the SSI tag in the output
#define LWIP_HTTPD_SUPPORT_CGI             1  // Common Gateway Interface (optional)
#define LWIP_HTTPD_DYNAMIC_HEADERS         1  // Auto-generate HTTP response headers
#define HTTPD_USE_CUSTOM_FSDATA            1
#define LWIP_HTTPD_SSI_MULTIPART           1

# Add the standard library to the build
target_link_libraries(configurator INTERFACE
    pico_stdlib
    pico_lwip_http                                # Links the lwIP HTTP server application components
    pico_cyw43_arch_lwip_threadsafe_background
)
