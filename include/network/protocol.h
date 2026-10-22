#pragma once

#include <string>
#include <vector>

#include "json.hpp"

namespace protocol {
enum class DecodeStatus { OK, NEED_MORE, INVALID_LENGTH };

/**
 * Decode complete [4-byte length][JSON body] frames from a TCP input buffer.
 * Returns INVALID_LENGTH for an invalid frame length. Incomplete frames remain buffered.
 * `consumed` is the number of bytes safe to remove from the input buffer.
 */
DecodeStatus DecodeFrames(const char* data, size_t size, std::vector<std::string>& frames,
                          size_t& consumed);

// Encodes a body using the format accepted by Server::OnMessage.
// [4-byte length][JSON body] frames.
bool EncodeFrame(const std::string& body, std::string& frame);

// Parses a complete JSON body and validates the required message.
bool ParseJsonMessage(const std::string& body, nlohmann::json& message);

}  // namespace protocol