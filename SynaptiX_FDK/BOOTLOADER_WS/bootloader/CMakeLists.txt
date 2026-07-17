cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_PROJECT_NAME bootloader)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_PROJECT_VERSION 1.0.0)

set(CMAKE_AUTHER MinhNhan)
add_library(${CMAKE_PROJECT_NAME} STATIC)

message("Bootloader version: " ${CMAKE_PROJECT_VERSION})
message("Auther: " ${CMAKE_AUTHER})
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
message(STATUS "Build timestamp: ${BUILD_TIMESTAMP}")
enable_language(C ASM)

file(GLOB BOOTLOADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
                        "${CMAKE_CURRENT_SOURCE_DIR}/*.s")

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
    # Add user defined symbols
    PROJECT_VERSION="${CMAKE_PROJECT_VERSION}"
    PROJECT_AUTHER="${CMAKE_AUTHER}"
    PROJECT_TIME="${BUILD_TIMESTAMP}"
    PROJECT_PHONE_NUMBER="+84963234016"
    CFG_BOOT_MCU=BOOT_MCU_H5
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${BOOTLOADER_SRC})

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)



target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx
    libs
)
