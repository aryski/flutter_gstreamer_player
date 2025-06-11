cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "flutter_gstreamer_player")
project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-fPIC)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "flutter_gstreamer_player_plugin")

# GStreamer libraries
find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)

# Custom GStreamer player
add_library(GST_PLAYER STATIC
  "../gst_player/gst_player.cc"
)
target_include_directories(GST_PLAYER PRIVATE
  "../gst_player"
  ${GSTREAMER_INCLUDE_DIRS}
)

add_library(${PLUGIN_NAME} SHARED
  "flutter_gstreamer_player_plugin.cc"
)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(${PLUGIN_NAME} PRIVATE
  ${GSTREAMER_INCLUDE_DIRS}
)

apply_standard_settings(${PLUGIN_NAME})
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE ${GSTREAMER_LIBRARIES} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES})
target_link_libraries(${PLUGIN_NAME} PRIVATE GST_PLAYER)

# List of absolute paths to libraries that should be bundled with the plugin
set(FLUTTER_GSTREAMER_PLAYER_PLUGIN_BUNDLED_LIBRARIES
  ""
) 