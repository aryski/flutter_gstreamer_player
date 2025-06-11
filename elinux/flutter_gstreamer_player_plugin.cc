#include "gst_player.h"

#include "include/flutter_gstreamer_player/flutter_gstreamer_player_plugin.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>
#include <sys/utsname.h>

#include <cstring>
#include <map>
#include <memory>
#include <sstream>

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
    int64_t player_id = -1;
    if (arguments) {
      auto pipeline_it = arguments->find(flutter::EncodableValue("pipeline"));
      if (pipeline_it != arguments->end()) {
        pipeline = std::get<std::string>(pipeline_it->second);
      }
      auto player_id_it = arguments->find(flutter::EncodableValue("playerId"));
      if (player_id_it != arguments->end()) {
        player_id = player_id_it->second.LongValue();
      }
    }

    if (player_id == -1) {
      result->Error("player_id_not_found", "player_id is not found.");
      return;
    }

    auto gst_player = std::make_unique<GstPlayer>();
    auto gst_texture =
        std::make_unique<GstTexture>(registrar_->texture_registrar());

    gst_player->onVideo(
        [texture = gst_texture.get()](
            uint8_t* frame, uint32_t size, int32_t width, int32_t height,
            int32_t stride) -> void { texture->OnFrame(frame, width, height); });

    gst_player->play(pipeline);
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
      if (texture_id_it != arguments->end()) {
        texture_id = texture_id_it->second.LongValue();
      }
    }

    if (texture_id == -1) {
      result->Error("texture_id_not_found", "texture_id is not found.");
      return;
    }

    gst_players_.erase(texture_id);
    gst_textures_.erase(texture_id);

    result->Success(flutter::EncodableValue(true));
  } else {
    result->NotImplemented();
  }
}

}  // namespace

void FlutterGstreamerPlayerPluginRegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  FlutterGstreamerPlayerPlugin::RegisterWithRegistrar(registrar);
} 