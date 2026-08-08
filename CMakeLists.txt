# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Components chia tầng: components/drivers chứa các device driver generic
# (tca9554...). ESP-IDF chỉ quét 1 cấp của components/ nên cần khai báo thêm.
set(EXTRA_COMPONENT_DIRS components/drivers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(callbox_sews)
