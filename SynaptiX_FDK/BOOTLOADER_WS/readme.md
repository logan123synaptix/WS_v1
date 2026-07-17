# ST Bootloader DFU

This repository contains a bootloader for STM32H563-based targets that supports firmware upgrades over USB DFU.

## Overview

The project combines:
- a custom bootloader implementation in the bootloader directory,
- STM32CubeMX-generated initialization and HAL support under Core and Drivers,
- TinyUSB DFU support for USB-based firmware updates.

The bootloader manages flash partition metadata, checks for new firmware, and can jump into the application image when no update is pending.

## Key features

- USB DFU update path
- Flash partition handling for primary/secondary/factory/scratch regions
- Boot pin-based update mode entry
- CMake/Ninja build flow for ARM Cortex-M targets
- Post-build generation of HEX/BIN firmware artifacts

## Project layout

- bootloader/: bootloader logic, flash handling, partition management
- Core/: application entry point, HAL initialization, USB integration
- Drivers/: BSP and STM32 HAL drivers
- cmake/: toolchain and STM32CubeMX CMake support
- TUSB/: TinyUSB configuration and descriptors
- libs/tinyusb/: vendored TinyUSB source

## Build

Prerequisites:
- ARM GCC toolchain (arm-none-eabi)
- CMake 3.22+
- Ninja

Build commands:

```bash
cmake --preset Release
cmake --build --preset Release
```

The build outputs are generated under build/Release/.

## Flash

A helper script is included:

```bash
./flash.sh
```

The script programs the generated firmware image using STM32_Programmer_CLI over SWD.

## Typical workflow

1. Build the firmware image.
2. Flash the bootloader image to the target.
3. Connect the board over USB.
4. Use a DFU-capable host tool to upload the application image.
5. The bootloader will install the update and jump to the new firmware.

## Notes

- The flash layout is defined in bootloader/flash_define.h.
- The bootloader behavior is controlled by the partition structure and the boot pin logic.
- If you modify the flash layout, update the related definitions carefully to avoid corrupting the image regions.
