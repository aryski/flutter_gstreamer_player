#ifndef FLUTTER_PLUGIN_FLUTTER_GSTREAMER_PLAYER_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_GSTREAMER_PLAYER_PLUGIN_H_

#include <flutter/plugin_registrar.h>

#ifdef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FLUTTER_PLUGIN_EXPORT
#endif

#if defined(__cplusplus)
extern "C" {
#endif

FLUTTER_PLUGIN_EXPORT void FlutterGstreamerPlayerPluginRegisterWithRegistrar(
    flutter::PluginRegistrar* registrar);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_FLUTTER_GSTREAMER_PLAYER_PLUGIN_H_ 