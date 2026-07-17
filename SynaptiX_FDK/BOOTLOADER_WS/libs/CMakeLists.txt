cmake_minimum_required(VERSION 3.22)

add_library(libs INTERFACE)

add_subdirectory(tinyusb/src)

add_library(tinyusb)

tinyusb_target_add(tinyusb)

file(GLOB TINYUSB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../TUSB/usb_descriptors.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/st/stm32_fsdev/*.c")

target_sources(tinyusb PRIVATE ${TINYUSB_SRC})

target_include_directories(tinyusb PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../TUSB
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/common 
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/device
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb/src/portable/st/stm32_fsdev
)

target_link_libraries(tinyusb PUBLIC 
    stm32cubemx)

target_link_libraries(libs INTERFACE tinyusb)