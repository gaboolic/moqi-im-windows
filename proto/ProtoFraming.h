#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace Moqi {
namespace Proto {

class FrameBuffer {
public:
  void append(const char *data, size_t len) { buffer_.append(data, len); }

  bool nextFrame(std::string &payload) {
    if (buffer_.size() < sizeof(std::uint32_t)) {
      return false;
    }

    std::uint32_t payloadSize = 0;
    std::memcpy(&payloadSize, buffer_.data(), sizeof(payloadSize));
    if (buffer_.size() < sizeof(payloadSize) + payloadSize) {
      return false;
    }

    payload.assign(buffer_.data() + sizeof(payloadSize), payloadSize);
    buffer_.erase(0, sizeof(payloadSize) + payloadSize);
    return true;
  }

  void clear() { buffer_.clear(); }

private:
  std::string buffer_;
};

inline std::string framePayload(const std::string &payload) {
  std::string framed(sizeof(std::uint32_t), '\0');
  const auto payloadSize = static_cast<std::uint32_t>(payload.size());
  std::memcpy(&framed[0], &payloadSize, sizeof(payloadSize));
  framed.append(payload);
  return framed;
}

template <typename Message>
bool serializeMessage(const Message &message, std::string &framedPayload) {
  std::string payload;
  if (!message.SerializeToString(&payload)) {
    return false;
  }
  framedPayload = framePayload(payload);
  return true;
}

template <typename Message>
bool parsePayload(const std::string &payload, Message &message) {
  return message.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
}

} // namespace Proto
} // namespace Moqi
