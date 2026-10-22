#include "protocol.h"

#include <arpa/inet.h>

namespace protocol {
namespace {
constexpr size_t kHeaderLen       = sizeof(int32_t);
constexpr uint32_t kMaxMessageLen = 1024 * 1024;
}  // namespace

DecodeStatus DecodeFrames(const char* data, size_t datasize, std::vector<std::string>& frames,
                          size_t& consumed) {
    consumed = 0;

    /**
     * Process all frames in the buffer at once.
     * Process all complete messages in the buffer in a loop (to handle sticky packets and
     * half-packet issues)
     */
    while (datasize - consumed >= kHeaderLen) {
        uint32_t netLen = 0;
        std::memcpy(&netLen, data + consumed, kHeaderLen);
        const int32_t bodyLen = static_cast<int32_t>(ntohl(netLen));
        if (bodyLen <= 0 || static_cast<uint32_t>(bodyLen) > kMaxMessageLen) {
            return DecodeStatus::INVALID_LENGTH;
        }
        const size_t frameLen = kHeaderLen + static_cast<size_t>(bodyLen);
        if (datasize - consumed < frameLen) {
            return DecodeStatus::NEED_MORE;
        }
        frames.emplace_back(data + consumed + kHeaderLen, static_cast<size_t>(bodyLen));
        consumed += frameLen;
    }
    return consumed == datasize ? DecodeStatus::OK : DecodeStatus::NEED_MORE;
}

bool EncodeFrame(const std::string& body, std::string& frame) {
    if (body.empty() || body.size() > kMaxMessageLen) {
        return false;
    }
    const uint32_t netLen = htonl(static_cast<uint32_t>(body.size()));
    frame.assign(reinterpret_cast<const char*>(&netLen), sizeof(netLen));
    frame += body;
    return true;
}

bool ParseJsonMessage(const std::string& body, nlohmann::json& message) {
    try {
        message = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return message.is_object() && message.contains("msgtype") &&
           message["msgtype"].is_number_integer();
}

}  // namespace protocol