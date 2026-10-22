#include "protocol.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>

namespace {
std::string Frame(const std::string& body) {
    std::string result;
    EXPECT_TRUE(protocol::EncodeFrame(body, result));
    return result;
}
}  // namespace

TEST(ProtocolTest, DecodesOneCompleteFrame) {
    const std::string input = Frame("{\"msgtype\":1}");
    std::vector<std::string> bodies;
    size_t consumed = 0;

    EXPECT_EQ(protocol::DecodeFrames(input.data(), input.size(), bodies, consumed),
              protocol::DecodeStatus::OK);
    ASSERT_EQ(bodies.size(), 1u);
    EXPECT_EQ(bodies[0], "{\"msgtype\":1}");
    EXPECT_EQ(consumed, input.size());
}

TEST(ProtocolTest, RetainsPartialHeaderAndBody) {
    const std::string frame = Frame("payload");
    for (size_t size = 1; size < frame.size(); ++size) {
        std::vector<std::string> bodies;
        size_t consumed = 99;
        EXPECT_EQ(protocol::DecodeFrames(frame.data(), size, bodies, consumed),
                  protocol::DecodeStatus::NEED_MORE);
        EXPECT_TRUE(bodies.empty());
        EXPECT_EQ(consumed, 0u);
    }
}

TEST(ProtocolTest, DecodesMultipleFramesAndLeavesTrailingPartialFrame) {
    const std::string first   = Frame("first");
    const std::string second  = Frame("second");
    const std::string partial = Frame("third").substr(0, 6);
    const std::string input   = first + second + partial;
    std::vector<std::string> bodies;
    size_t consumed = 0;

    EXPECT_EQ(protocol::DecodeFrames(input.data(), input.size(), bodies, consumed),
              protocol::DecodeStatus::NEED_MORE);
    ASSERT_EQ(bodies.size(), 2u);
    EXPECT_EQ(bodies[0], "first");
    EXPECT_EQ(bodies[1], "second");
    EXPECT_EQ(consumed, first.size() + second.size());
}

TEST(ProtocolTest, RejectsZeroNegativeAndOversizedLengths) {
    const uint32_t invalidLengths[] = {0, htonl(static_cast<uint32_t>(-1)), htonl(1024 * 1024 + 1)};
    for (const uint32_t encoded : invalidLengths) {
        const std::string input(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
        std::vector<std::string> bodies;
        size_t consumed = 0;
        EXPECT_EQ(protocol::DecodeFrames(input.data(), input.size(), bodies, consumed),
                  protocol::DecodeStatus::INVALID_LENGTH);
        EXPECT_TRUE(bodies.empty());
    }
}

TEST(ProtocolTest, EncodeRejectsEmptyAndOversizedBodies) {
    std::string frame;
    EXPECT_FALSE(protocol::EncodeFrame("", frame));
    EXPECT_FALSE(protocol::EncodeFrame(std::string(1024 * 1024 + 1, 'x'), frame));
}

TEST(ProtocolTest, EncodesLengthInNetworkByteOrder) {
    const std::string body  = "{}";
    const std::string frame = Frame(body);
    ASSERT_EQ(frame.size(), sizeof(uint32_t) + body.size());
    uint32_t encodedLength = 0;
    std::memcpy(&encodedLength, frame.data(), sizeof(encodedLength));
    EXPECT_EQ(ntohl(encodedLength), body.size());
    EXPECT_EQ(frame.substr(sizeof(encodedLength)), body);
}

TEST(ProtocolTest, ParsesOnlyValidMessageEnvelopes) {
    nlohmann::json message;
    EXPECT_TRUE(protocol::ParseJsonMessage(R"({"msgtype":1,"data":{}})", message));
    EXPECT_EQ(message["msgtype"], 1);

    EXPECT_FALSE(protocol::ParseJsonMessage("not json", message));
    EXPECT_FALSE(protocol::ParseJsonMessage(R"({"data":{}})", message));
    EXPECT_FALSE(protocol::ParseJsonMessage(R"({"msgtype":"1"})", message));
    EXPECT_FALSE(protocol::ParseJsonMessage(R"([{"msgtype":1}])", message));
}
