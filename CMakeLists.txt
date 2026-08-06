cmake_minimum_required(VERSION 3.22)

# These are explicit so nested platform components remain discoverable without
# introducing a generic component discovery mechanism.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers/drv_tca9554"
    "${CMAKE_CURRENT_LIST_DIR}/components/bsp/bsp_waveshare_s3_8di8do"
    "${CMAKE_CURRENT_LIST_DIR}/applications/board_hardware_test")

# Build only the Phase 0-1 application and its declared dependency closure.
# This prevents unrelated connectivity, OTA, TF, and fieldbus components from
# becoming part of the firmware merely because they exist in ESP-IDF.
set(COMPONENTS
    main
    board_hardware_test
    bsp_waveshare_s3_8di8do
    drv_tca9554)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tunglam_esp32_s3_platform)
