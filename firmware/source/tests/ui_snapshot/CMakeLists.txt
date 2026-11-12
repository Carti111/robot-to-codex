cmake_minimum_required(VERSION 3.20)
project(spark_ui_snapshot LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
# Zig ships its own archiver. Avoid relying on an external LLVM installation.
set(CMAKE_C_ARCHIVE_CREATE "\"${CMAKE_CURRENT_SOURCE_DIR}/zig-ar.cmd\" qc <TARGET> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "\"${CMAKE_CURRENT_SOURCE_DIR}/zig-ranlib.cmd\" <TARGET>")
get_filename_component(FIRMWARE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BOARD "${FIRMWARE}/main/boards/esp-sparkbot")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stubs/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${FIRMWARE}/managed_components/lvgl__lvgl" lvgl)
file(GLOB TINYGL "${FIRMWARE}/components/tinygl/TinyGL/src/*.c")
add_library(tinygl_snapshot ${TINYGL})
target_include_directories(tinygl_snapshot PUBLIC stubs "${FIRMWARE}/components/tinygl/TinyGL/src" "${FIRMWARE}/components/tinygl/TinyGL/include")
add_executable(spark_ui_snapshot snapshot.cpp
 "${BOARD}/spark_features.cc" "${BOARD}/spark_dice.cc" "${BOARD}/spark_game_2048.c"
 "${BOARD}/spark_factory_assets.c" "${BOARD}/spark_font_ComfortaaBold75.c"
 "${BOARD}/spark_font_OPPOSansH18.c" "${BOARD}/spark_font_OPPOSansH25.c"
 "${FIRMWARE}/managed_components/78__xiaozhi-fonts/src/font_puhui_20_4.c")
target_include_directories(spark_ui_snapshot PRIVATE stubs "${BOARD}")
target_link_libraries(spark_ui_snapshot PRIVATE lvgl tinygl_snapshot)

target_compile_options(spark_ui_snapshot PRIVATE "-include${CMAKE_CURRENT_SOURCE_DIR}/stubs/platform_compat.h")
