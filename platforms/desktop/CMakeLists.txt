cmake_minimum_required (VERSION 3.10)
project(DesktopBuild)

cmake_policy(SET CMP0077 NEW)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DPLATFORM_BUILD_DESKTOP)
# 让页面代码里的 #ifdef CONFIG_IDF_TARGET_ESP32P4 分支生效（IDF API 由 compat 桩提供）
add_definitions(-DCONFIG_IDF_TARGET_ESP32P4=1)

# 桌面端兼容层（FreeRTOS 等 PC 上不存在的头）
include_directories(${CMAKE_SOURCE_DIR}/platforms/desktop/compat)

# Mooncake 
set(MOONCAKE_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake)

# Mooncake log
set(MOONCAKE_EVENT_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake_log)

# Smooth ui shit
set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/smooth_ui_toolkit)

# App layer
file(GLOB_RECURSE APP_LAYER_SRCS
    app/*.c
    app/*.cc
    app/*.cpp
)
set(APP_LAYER_INCS
    app/
)

# Lvgl
set(LV_CONF_INCLUDE_SIMPLE OFF)
set(LV_CONF_PATH ../../lv_conf.h)
add_subdirectory(dependencies/lvgl)

# SDL（保留：代码里还有 SDL 路径分支）
find_package(SDL2 REQUIRED SDL2)
include_directories(PUBLIC ${SDL2_INCLUDE_DIRS})

# Wayland：LVGL 的 wayland 驱动需要 xdg-shell 协议生成文件 + 三个客户端库
#（生成命令：wayland-scanner client-header/private-code <xdg-shell.xml>）
find_package(PkgConfig REQUIRED)
pkg_check_modules(WL_CLIENT  REQUIRED wayland-client)
pkg_check_modules(WL_CURSOR  REQUIRED wayland-cursor)
pkg_check_modules(XKBCOMMON  REQUIRED xkbcommon)
include_directories(${CMAKE_SOURCE_DIR}/platforms/desktop/wayland_protocols
                    ${WL_CLIENT_INCLUDE_DIRS} ${WL_CURSOR_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS})
target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR}/platforms/desktop/wayland_protocols)
link_directories(${WL_CLIENT_LIBRARY_DIRS} ${WL_CURSOR_LIBRARY_DIRS} ${XKBCOMMON_LIBRARY_DIRS})

# X11：LVGL 的 X11 后端（实时窗口，走 XWayland）
pkg_check_modules(X11_LIB REQUIRED x11)
pkg_check_modules(XEXT_LIB REQUIRED xext)
pkg_check_modules(XCB_LIB REQUIRED xcb)

# App layer
file(GLOB_RECURSE APP_LAYER_SRCS
    app/*.c
    app/*.cc
    app/*.cpp
)
set(APP_LAYER_INCS
    app/
)

# 桌面端源文件
file(GLOB_RECURSE APP_DESKTOP_BUILD_SRCS
    platforms/desktop/*.cpp
    platforms/desktop/*.cc
    platforms/desktop/*.c
)
# 所有页面都编进来（IDF 依赖由 compat/ 下的桩头文件兜底），
# 这样桌面预览能看到全部页面，布局源码保持一份。

# xdg-shell 协议实现（wayland-scanner 生成，LVGL wayland 驱动必需）
set(WAYLAND_PROTO_SRC ${CMAKE_SOURCE_DIR}/platforms/desktop/wayland_protocols/wayland_xdg_shell.c)

add_executable(app_desktop_build ${APP_DESKTOP_BUILD_SRCS} ${APP_LAYER_SRCS} ${WAYLAND_PROTO_SRC})
target_include_directories(app_desktop_build PUBLIC ${APP_LAYER_INCS})
target_link_libraries(app_desktop_build PUBLIC 
    mooncake 
    mooncake_log
    lvgl 
    lvgl_examples 
    lvgl_demos 
    ${SDL2_LIBRARIES}
    wayland-client wayland-cursor xkbcommon
    X11 Xext xcb X11-xcb
    smooth_ui_toolkit
    pthread
)

# 设置构建路径
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build/desktop)

# 确保构建路径存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})

# 设置生成的可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# # 设置生成的库文件输出路径
# set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# 固件版本号：从 tab5 的 CMakeLists 里读 PROJECT_VER，保证模拟器和真机显示一致
file(READ ${CMAKE_SOURCE_DIR}/platforms/tab5/CMakeLists.txt _tab5_cml)
string(REGEX MATCH "set\\(PROJECT_VER \"([^\"]+)\"\\)" _ver_match "${_tab5_cml}")
if(CMAKE_MATCH_1)
    add_compile_definitions(NOVA_FW_VER="${CMAKE_MATCH_1}")
    message(STATUS "Nova firmware version (from tab5): ${CMAKE_MATCH_1}")
endif()
