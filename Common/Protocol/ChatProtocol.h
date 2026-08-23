#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat
{
namespace protocol
{
    constexpr std::size_t kFrameHeaderBytes = 12;
    constexpr std::size_t kMaxPayloadBytes = 64 * 1024;
    constexpr std::uint16_t kProtocolVersion = 2;

    enum class MessageType : std::uint16_t
    {
        LoginRequest = 1,
        RegisterRequest = 2,
        ChatSend = 3,
        LoginSucceeded = 4,
        LoginFailed = 5,
        RegisterSucceeded = 6,
        RegisterFailed = 7,
        ChatDelivered = 8
    };

    struct Message
    {
        MessageType type = MessageType::LoginRequest;
        std::uint32_t requestId = 0;
        std::vector<std::string> fields;
    };

    enum class CodecError
    {
        None,
        PayloadTooLarge,
        UnsupportedVersion,
        UnknownMessageType,
        InvalidUtf8,
        MalformedPayload,
        InvalidFieldCount
    };

    struct EncodeResult
    {
        CodecError error = CodecError::None;
        std::vector<std::uint8_t> bytes;
    };

    struct DecodeResult
    {
        CodecError error = CodecError::None;
        std::vector<Message> messages;
    };

    bool IsValidUtf8(const std::string& value);
    CodecError ValidateMessage(const Message& message);
    EncodeResult EncodeMessage(const Message& message);

    class StreamingDecoder
    {
    public:
        DecodeResult Push(const std::uint8_t* data, std::size_t size);
    private:
        std::vector<std::uint8_t> buffer;
        CodecError terminalError = CodecError::None;
    };
}
}
