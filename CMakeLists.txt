# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Components chia tang: components/drivers chua cac device driver generic
# (tca9554...), components/platform chua cac platform service generic
# (platform_time...). ESP-IDF chi quet 1 cap cua components/ nen can khai bao them.
set(EXTRA_COMPONENT_DIRS components/drivers
                        components/platform)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_s3_common)
