# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Target board")
endif()

include(pico_sdk_import.cmake)

project(PicoShack C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(FetchContent)
FetchContent_Declare(pico_examples
    GIT_REPOSITORY https://github.com/raspberrypi/pico-examples.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(pico_examples)
if (NOT pico_examples_POPULATED)
    FetchContent_Populate(pico_examples)
endif()

add_executable(PicoShack
    src/main.c
    src/audio.c
    src/dtmf.c
    src/fox.c
    src/keyer.c
    src/morse.c
    src/radio.c
    src/settings.c
    src/station_control.c
    src/web.c
    src/web_settings.c
    src/workflow.c
    ${pico_examples_SOURCE_DIR}/pico_w/wifi/access_point/dhcpserver/dhcpserver.c
    ${pico_examples_SOURCE_DIR}/pico_w/wifi/access_point/dnsserver/dnsserver.c
)

target_include_directories(PicoShack PRIVATE
    src
    ${pico_examples_SOURCE_DIR}/pico_w/wifi/access_point/dhcpserver
    ${pico_examples_SOURCE_DIR}/pico_w/wifi/access_point/dnsserver
    ${PICO_LWIP_CONTRIB_PATH}/apps/httpd
)

target_link_libraries(PicoShack PRIVATE
    pico_stdlib
    hardware_clocks
    hardware_adc
    hardware_flash
    hardware_pwm
    hardware_watchdog
    pico_flash
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mdns
)

pico_configure_ip4_address(PicoShack PRIVATE CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1)

pico_add_library(PicoShackWebContent NOFLAG)
pico_set_lwip_httpd_content(PicoShackWebContent INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/web/apply-complete.html
    ${CMAKE_CURRENT_LIST_DIR}/web/cw-settings.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/cw.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/cw-apply-complete.html
    ${CMAKE_CURRENT_LIST_DIR}/web/device-apply-complete.html
    ${CMAKE_CURRENT_LIST_DIR}/web/picoshack.css
    ${CMAKE_CURRENT_LIST_DIR}/web/index.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/fox.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/fox-settings.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/key-state.txt
    ${CMAKE_CURRENT_LIST_DIR}/web/reboot.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/settings.shtml
    ${CMAKE_CURRENT_LIST_DIR}/web/status.shtml
)
target_link_libraries(PicoShack PRIVATE PicoShackWebContent)

pico_enable_stdio_usb(PicoShack 1)
pico_enable_stdio_uart(PicoShack 0)
pico_add_extra_outputs(PicoShack)
