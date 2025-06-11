cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_gstreamer_player")
project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fPIC)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "flutter_gstreamer_player_plugin")

# GStreamer libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)

add_library(${PLUGIN_NAME} SHARED
  "flutter_gstreamer_player_plugin.cc"
  "../gst_player/gst_player.cc"
)
apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)

target_include_directories(${PLUGIN_NAME}
  PRIVATE
    ../gst_player
    ${GLIB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VIDEO_INCLUDE_DIRS}
)

target_link_libraries(${PLUGIN_NAME}
  PRIVATE
    ${GLIB_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VIDEO_LIBRARIES}
)

# List of absolute paths to libraries that should be bundled with the plugin
set(flutter_gstreamer_player_plugin_bundled_libraries
  ""
) 