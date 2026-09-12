# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "../../dependencies"
)

# OTA 版本号：改这里 = 发布新固件（设备端比对 remote/current 决定是否升级）
# 必须放在 project() 之前
set(PROJECT_VER "1.0.30")

project(m5stack_tab5)
