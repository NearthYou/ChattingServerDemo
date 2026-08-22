#include "ChatProtocol.h"

#include <limits>
#include <utility>

namespace
{
    struct FrameHeader
    {
        std::uint32_t payloadBytes = 0;
        std::uint16_t version = chat::protocol::kProtocolVersion;
        chat::protocol::MessageType messageType = chat::protocol::MessageType::LoginRequest;
        std::uint32_t requestId = 0;
    };

    std::uint16_t ReadUint16(const std::uint8_t* bytes)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[0]) << 8) |
            static_cast<std::uint16_t>(bytes[1]));
    }

    std::uint32_t ReadUint32(const std::uint8_t* bytes)
    {
        return
            (static_cast<std::uint32_t>(bytes[0]) << 24) |
            (static_cast<std::uint32_t>(bytes[1]) << 16) |
            (static_cast<std::uint32_t>(bytes[2]) << 8) |
            static_cast<std::uint32_t>(bytes[3]);
    }

    void AppendUint16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }

    void AppendUint32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }

    std::size_t ExpectedFieldCount(chat::protocol::MessageType type)
    {
        using chat::protocol::MessageType;

        switch (type)
        {
        case MessageType::LoginRequest:
        case MessageType::RegisterRequest:
            return 2;
        case MessageType::ChatDelivered:
            return 3;
        case MessageType::ChatSend:
            return 1;
        case MessageType::LoginSucceeded:
        case MessageType::LoginFailed:
        case MessageType::RegisterSucceeded:
        case MessageType::RegisterFailed:
            return 0;
        default:
            return std::numeric_limits<std::size_t>::max();
        }
    }

    chat::protocol::CodecError DecodePayload(
        const std::uint8_t* payload,
        std::size_t payloadBytes,
        chat::protocol::Message& message)
    {
        using chat::protocol::CodecError;

        const std::size_t expectedFields = ExpectedFieldCount(message.type);
        if (expectedFields == std::numeric_limits<std::size_t>::max())
        {
            return CodecError::UnknownMessageType;
        }

        std::size_t offset = 0;
        message.fields.clear();
        message.fields.reserve(expectedFields);

        for (std::size_t index = 0; index < expectedFields; ++index)
        {
            if (payloadBytes - offset < sizeof(std::uint32_t))
            {
                return CodecError::InvalidFieldCount;
            }

            const std::size_t fieldBytes = ReadUint32(payload + offset);
            offset += sizeof(std::uint32_t);
            if (fieldBytes > payloadBytes - offset)
            {
                return CodecError::MalformedPayload;
            }

            std::string field(
                reinterpret_cast<const char*>(payload + offset),
                fieldBytes);
            if (!chat::protocol::IsValidUtf8(field))
            {
                return CodecError::InvalidUtf8;
            }

            message.fields.push_back(std::move(field));
            offset += fieldBytes;
        }

        return offset == payloadBytes ? CodecError::None : CodecError::InvalidFieldCount;
    }
}

namespace chat
{
namespace protocol
{
    bool IsValidUtf8(const std::string& value)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
        std::size_t index = 0;

        while (index < value.size())
        {
            const std::uint8_t first = bytes[index];
            if (first <= 0x7f)
            {
                ++index;
                continue;
            }

            if (first >= 0xc2 && first <= 0xdf)
            {
                if (index + 1 >= value.size() ||
                    bytes[index + 1] < 0x80 || bytes[index + 1] > 0xbf)
                {
                    return false;
                }
                index += 2;
                continue;
            }

            if (first >= 0xe0 && first <= 0xef)
            {
                if (index + 2 >= value.size())
                {
                    return false;
                }

                const std::uint8_t second = bytes[index + 1];
                const std::uint8_t third = bytes[index + 2];
                const bool secondValid =
                    (first == 0xe0 && second >= 0xa0 && second <= 0xbf) ||
                    (first == 0xed && second >= 0x80 && second <= 0x9f) ||
                    ((first >= 0xe1 && first <= 0xec || first >= 0xee) &&
                        second >= 0x80 && second <= 0xbf);
                if (!secondValid || third < 0x80 || third > 0xbf)
                {
                    return false;
                }
                index += 3;
                continue;
            }

            if (first >= 0xf0 && first <= 0xf4)
            {
                if (index + 3 >= value.size())
                {
                    return false;
                }

                const std::uint8_t second = bytes[index + 1];
                const std::uint8_t third = bytes[index + 2];
                const std::uint8_t fourth = bytes[index + 3];
                const bool secondValid =
                    (first == 0xf0 && second >= 0x90 && second <= 0xbf) ||
                    (first == 0xf4 && second >= 0x80 && second <= 0x8f) ||
                    (first >= 0xf1 && first <= 0xf3 && second >= 0x80 && second <= 0xbf);
                if (!secondValid ||
                    third < 0x80 || third > 0xbf ||
                    fourth < 0x80 || fourth > 0xbf)
                {
                    return false;
                }
                index += 4;
                continue;
            }

            return false;
        }

        return true;
    }

    CodecError ValidateMessage(const Message& message)
    {
        const std::size_t expectedFields = ExpectedFieldCount(message.type);
        if (expectedFields == std::numeric_limits<std::size_t>::max())
        {
            return CodecError::UnknownMessageType;
        }
        if (message.fields.size() != expectedFields)
        {
            return CodecError::InvalidFieldCount;
        }

        std::size_t payloadBytes = 0;
        for (const auto& field : message.fields)
        {
            if (!IsValidUtf8(field))
            {
                return CodecError::InvalidUtf8;
            }
            if (payloadBytes > kMaxPayloadBytes - sizeof(std::uint32_t))
            {
                return CodecError::PayloadTooLarge;
            }
            payloadBytes += sizeof(std::uint32_t);
            if (field.size() > kMaxPayloadBytes - payloadBytes)
            {
                return CodecError::PayloadTooLarge;
            }
            payloadBytes += field.size();
        }

        return CodecError::None;
    }

    EncodeResult EncodeMessage(const Message& message)
    {
        EncodeResult result;
        result.error = ValidateMessage(message);
        if (result.error != CodecError::None)
        {
            return result;
        }

        std::size_t payloadBytes = 0;
        for (const auto& field : message.fields)
        {
            payloadBytes += sizeof(std::uint32_t) + field.size();
        }

        result.bytes.reserve(kFrameHeaderBytes + payloadBytes);
        AppendUint32(result.bytes, static_cast<std::uint32_t>(payloadBytes));
        AppendUint16(result.bytes, kProtocolVersion);
        AppendUint16(result.bytes, static_cast<std::uint16_t>(message.type));
        AppendUint32(result.bytes, message.requestId);
        for (const auto& field : message.fields)
        {
            AppendUint32(result.bytes, static_cast<std::uint32_t>(field.size()));
            result.bytes.insert(result.bytes.end(), field.begin(), field.end());
        }

        return result;
    }

    DecodeResult StreamingDecoder::Push(const std::uint8_t* data, std::size_t size)
    {
        DecodeResult result;
        if (terminalError != CodecError::None)
        {
            result.error = terminalError;
            return result;
        }
        if (size != 0 && data == nullptr)
        {
            terminalError = CodecError::MalformedPayload;
            result.error = terminalError;
            return result;
        }

        if (size != 0)
        {
            buffer.insert(buffer.end(), data, data + size);
        }
        std::size_t consumedBytes = 0;

        while (buffer.size() - consumedBytes >= kFrameHeaderBytes)
        {
            const auto* headerBytes = buffer.data() + consumedBytes;
            FrameHeader header;
            header.payloadBytes = ReadUint32(headerBytes);
            header.version = ReadUint16(headerBytes + 4);
            header.messageType = static_cast<MessageType>(ReadUint16(headerBytes + 6));
            header.requestId = ReadUint32(headerBytes + 8);

            if (header.payloadBytes > kMaxPayloadBytes)
            {
                terminalError = CodecError::PayloadTooLarge;
                break;
            }
            if (header.version != kProtocolVersion)
            {
                terminalError = CodecError::UnsupportedVersion;
                break;
            }
            if (ExpectedFieldCount(header.messageType) == std::numeric_limits<std::size_t>::max())
            {
                terminalError = CodecError::UnknownMessageType;
                break;
            }

            const std::size_t frameBytes = kFrameHeaderBytes + header.payloadBytes;
            if (buffer.size() - consumedBytes < frameBytes)
            {
                break;
            }

            Message message;
            message.type = header.messageType;
            message.requestId = header.requestId;
            const CodecError payloadError = DecodePayload(
                headerBytes + kFrameHeaderBytes,
                header.payloadBytes,
                message);
            if (payloadError != CodecError::None)
            {
                terminalError = payloadError;
                break;
            }

            result.messages.push_back(std::move(message));
            consumedBytes += frameBytes;
        }

        if (terminalError != CodecError::None)
        {
            buffer.clear();
            result.messages.clear();
            result.error = terminalError;
            return result;
        }

        if (consumedBytes != 0)
        {
            buffer.erase(buffer.begin(), buffer.begin() + consumedBytes);
        }
        return result;
    }

}
}
