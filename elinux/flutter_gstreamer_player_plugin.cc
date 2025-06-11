#include "gst_player.h"

#include "include/flutter_gstreamer_player/flutter_gstreamer_player_plugin.h"
#include "include/flutter_gstreamer_player/flutter_gstreamer_player_video_outlet.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>
#include <sys/utsname.h>

#include <cstring>
#include <map>
#include <memory>
#include <sstream>

#define FLUTTER_GSTREAMER_PLAYER_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), flutter_gstreamer_player_plugin_get_type(), \
                              FlutterGstreamerPlayerPlugin))

struct _FlutterGstreamerPlayerPlugin {
  GObject parent_instance;
  FlMethodChannel* method_channel;
  FlTextureRegistrar* texture_registrar;
};

std::unordered_map<int32_t, VideoOutlet*> g_video_outlets;

G_DEFINE_TYPE(FlutterGstreamerPlayerPlugin, flutter_gstreamer_player_plugin, g_object_get_type())

// Called when a method call is received from Flutter.
static void flutter_gstreamer_player_plugin_handle_method_call(
    FlutterGstreamerPlayerPlugin* self,
    FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  const gchar* method = fl_method_call_get_name(method_call);

  // TODO properly handle these method calls
  if (strcmp(method, "getPlatformVersion") == 0) {
    struct utsname uname_data = {};
    uname(&uname_data);
    g_autofree gchar *version = g_strdup_printf("Linux %s", uname_data.version);
    g_autoptr(FlValue) result = fl_value_new_string(version);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
  } else if (strcmp(method, "PlayerRegisterTexture") == 0) {
    auto arguments = fl_method_call_get_args(method_call);
    auto pipeline =
        fl_value_get_string(fl_value_lookup_string(arguments, "pipeline"));
    int32_t player_id =
        fl_value_get_int(fl_value_lookup_string(arguments, "playerId"));
    auto [it, added] = g_video_outlets.try_emplace(player_id, nullptr);

    GstPlayer* gstPlayer = g_players->Get(player_id);

    if (added) {
      it->second = video_outlet_new();

      FL_PIXEL_BUFFER_TEXTURE_GET_CLASS(it->second)->copy_pixels =
          video_outlet_copy_pixels;
      fl_texture_registrar_register_texture(self->texture_registrar,
                                            FL_TEXTURE(it->second));
      auto video_outlet_private = (VideoOutletPrivate*) video_outlet_get_instance_private(it->second);
      video_outlet_private->texture_id = reinterpret_cast<int64_t>(FL_TEXTURE(it->second));

      gstPlayer->onVideo([texture_registrar = self->texture_registrar,
                       video_outlet_ptr = it->second,
                       video_outlet_private = video_outlet_private]
                       (uint8_t* frame, uint32_t size, int32_t width, int32_t height, int32_t stride) -> void {
        video_outlet_private->buffer = frame;
        video_outlet_private->video_width = width;
        video_outlet_private->video_height = height;
        fl_texture_registrar_mark_texture_frame_available(
            texture_registrar,
            FL_TEXTURE(video_outlet_ptr));
      });
    }

    gstPlayer->play(pipeline);

    response =
      FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(
        ((VideoOutletPrivate*) video_outlet_get_instance_private(it->second))->texture_id
      )));
  } else if (strcmp(method, "dispose") == 0) {
    g_autoptr(FlValue) result = fl_value_new_bool(true);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void flutter_gstreamer_player_plugin_dispose(GObject* object) {
  G_OBJECT_CLASS(flutter_gstreamer_player_plugin_parent_class)->dispose(object);
}

static void flutter_gstreamer_player_plugin_class_init(FlutterGstreamerPlayerPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = flutter_gstreamer_player_plugin_dispose;
}

static void flutter_gstreamer_player_plugin_init(FlutterGstreamerPlayerPlugin* self) {}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  FlutterGstreamerPlayerPlugin* plugin = FLUTTER_GSTREAMER_PLAYER_PLUGIN(user_data);
  flutter_gstreamer_player_plugin_handle_method_call(plugin, method_call);
}

void flutter_gstreamer_player_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  FlutterGstreamerPlayerPlugin* plugin = FLUTTER_GSTREAMER_PLAYER_PLUGIN(
      g_object_new(flutter_gstreamer_player_plugin_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  g_autoptr(FlMethodChannel) channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "flutter_gstreamer_player",
                            FL_METHOD_CODEC(codec));

  plugin->texture_registrar =
      fl_plugin_registrar_get_texture_registrar(registrar);

  fl_method_channel_set_method_call_handler(channel, method_call_cb,
                                            g_object_ref(plugin),
                                            g_object_unref);

  g_object_unref(plugin);
}

namespace {

// See flutter-elinux/flutter/shell/platform/linux_embedded/plugins/texture_registrar.h
// The pixel buffer struct for texture.
struct FlutterDesktopPixelBuffer {
  const uint8_t* buffer;
  size_t width;
  size_t height;
  // User data to be passed to the destruction callback.
  void* user_data;
};

// The callback type for pixel buffer destruction.
typedef void (*FlutterDesktopDestructionCallback)(void* user_data);

class GstTexture {
 public:
  GstTexture(flutter::TextureRegistrar* texture_registrar)
      : texture_registrar_(texture_registrar),
        texture_id_(-1),
        buffer_(nullptr) {
    texture_ =
        std::make_unique<flutter::TextureVariant>(flutter::PixelBufferTexture(
            [this](size_t width,
                   size_t height) -> const FlutterDesktopPixelBuffer* {
              return this->Convert();
            }));
    texture_id_ = texture_registrar_->RegisterTexture(texture_.get());
  }

  ~GstTexture() {
    if (texture_id_ != -1) {
      texture_registrar_->UnregisterTexture(texture_id_);
    }
  }

  void OnFrame(const uint8_t* buffer, int32_t width, int32_t height) {
    buffer_ = buffer;
    width_ = width;
    height_ = height;
    texture_registrar_->MarkTextureFrameAvailable(texture_id_);
  }

  int64_t texture_id() const { return texture_id_; }

 private:
  const FlutterDesktopPixelBuffer* Convert() {
    if (!buffer_) {
      return nullptr;
    }
    pixel_buffer_.buffer = buffer_;
    pixel_buffer_.width = width_;
    pixel_buffer_.height = height_;
    return &pixel_buffer_;
  }

  flutter::TextureRegistrar* texture_registrar_;
  int64_t texture_id_;
  std::unique_ptr<flutter::TextureVariant> texture_;
  const uint8_t* buffer_;
  int32_t width_;
  int32_t height_;
  FlutterDesktopPixelBuffer pixel_buffer_;
};

class FlutterGstreamerPlayerPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  FlutterGstreamerPlayerPlugin(flutter::PluginRegistrar* registrar);

  virtual ~FlutterGstreamerPlayerPlugin();

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  flutter::PluginRegistrar* registrar_;
  std::map<int64_t, std::unique_ptr<GstPlayer>> gst_players_;
  std::map<int64_t, std::unique_ptr<GstTexture>> gst_textures_;
};

void FlutterGstreamerPlayerPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "flutter_gstreamer_player",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<FlutterGstreamerPlayerPlugin>(registrar);

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

FlutterGstreamerPlayerPlugin::FlutterGstreamerPlayerPlugin(
    flutter::PluginRegistrar* registrar)
    : registrar_(registrar) {}

FlutterGstreamerPlayerPlugin::~FlutterGstreamerPlayerPlugin() {}

void FlutterGstreamerPlayerPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (strcmp(method_call.method_name().c_str(), "getPlatformVersion") == 0) {
    struct utsname uname_data = {};
    uname(&uname_data);
    std::ostringstream version_stream;
    version_stream << "eLinux " << uname_data.version;
    result->Success(flutter::EncodableValue(version_stream.str()));
  } else if (strcmp(method_call.method_name().c_str(),
                   "PlayerRegisterTexture") == 0) {
    const auto* arguments =
        std::get_if<flutter::EncodableMap>(method_call.arguments());
    std::string pipeline;
    if (arguments) {
      auto pipeline_it = arguments->find(flutter::EncodableValue("pipeline"));
      if (pipeline_it != arguments->end()) {
        pipeline = std::get<std::string>(pipeline_it->second);
      }
    }

    auto gst_player = std::make_unique<GstPlayer>();
    auto gst_texture =
        std::make_unique<GstTexture>(registrar_->texture_registrar());

    gst_player->onVideo(
        [texture = gst_texture.get()](
            uint8_t* frame, uint32_t size, int32_t width, int32_t height,
            int32_t stride) { texture->OnFrame(frame, width, height); });

    gst_player->play(pipeline.c_str());

    auto texture_id = gst_texture->texture_id();
    gst_players_[texture_id] = std::move(gst_player);
    gst_textures_[texture_id] = std::move(gst_texture);

    result->Success(flutter::EncodableValue(texture_id));
  } else if (strcmp(method_call.method_name().c_str(), "dispose") == 0) {
    const auto* arguments =
        std::get_if<flutter::EncodableMap>(method_call.arguments());
    int64_t texture_id = -1;
    if (arguments) {
      auto texture_id_it =
          arguments->find(flutter::EncodableValue("textureId"));
      if (texture_id_it != arguments->end() && !texture_id_it->second.IsNull()) {
        texture_id = texture_id_it->second.LongValue();
      }
    }

    if (texture_id == -1) {
      result->Error("texture_id not provided");
      return;
    }

    gst_players_.erase(texture_id);
    gst_textures_.erase(texture_id);
    result->Success();
  } else {
    result->NotImplemented();
  }
}

}  // namespace

void FlutterGstreamerPlayerPluginRegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  FlutterGstreamerPlayerPlugin::RegisterWithRegistrar(registrar);
} 