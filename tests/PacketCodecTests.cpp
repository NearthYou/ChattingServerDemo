#include "../Common/Protocol/ChatProtocol.h"
#include "../Client/UI/ChatMessageLayout.h"
#include "../Client/UI/RegistrationValidation.h"
#include "../Client/Network/NetworkPrimitives.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int RunNetworkManagerIntegrationTests();

namespace
{
    using chat::protocol::CodecError;
    using chat::protocol::Message;
    using chat::protocol::MessageType;
    using chat::protocol::StreamingDecoder;

    int failures = 0;

    void Check(bool condition, const char* expression, const char* testName, int line)
    {
        if (condition)
        {
            return;
        }

        ++failures;
        std::cerr << testName << ":" << line << " check failed: " << expression << '\n';
    }

#define CHECK(expression) Check((expression), #expression, __func__, __LINE__)

    std::vector<std::uint8_t> Encode(const Message& message)
    {
        auto result = chat::protocol::EncodeMessage(message);
        CHECK(result.error == CodecError::None);
        return result.bytes;
    }

    std::vector<std::uint8_t> Header(
        std::uint32_t payloadBytes,
        std::uint16_t version,
        std::uint16_t messageType,
        std::uint32_t requestId)
    {
        return {
            static_cast<std::uint8_t>(payloadBytes >> 24),
            static_cast<std::uint8_t>(payloadBytes >> 16),
            static_cast<std::uint8_t>(payloadBytes >> 8),
            static_cast<std::uint8_t>(payloadBytes),
            static_cast<std::uint8_t>(version >> 8),
            static_cast<std::uint8_t>(version),
            static_cast<std::uint8_t>(messageType >> 8),
            static_cast<std::uint8_t>(messageType),
            static_cast<std::uint8_t>(requestId >> 24),
            static_cast<std::uint8_t>(requestId >> 16),
            static_cast<std::uint8_t>(requestId >> 8),
            static_cast<std::uint8_t>(requestId)
        };
    }

    void HeaderAndFieldsUseNetworkByteOrder()
    {
        const auto bytes = Encode({ MessageType::ChatSend, 0x11223344u, { "hi" } });
        const std::vector<std::uint8_t> expected = {
            0x00, 0x00, 0x00, 0x06,
            0x00, 0x01,
            0x00, 0x03,
            0x11, 0x22, 0x33, 0x44,
            0x00, 0x00, 0x00, 0x02,
            'h', 'i'
        };

        CHECK(bytes == expected);
    }

    void FragmentedPacketIsEmittedOnlyWhenComplete()
    {
        const auto bytes = Encode({ MessageType::ChatDelivered, 27u, { "alice", u8"\uc548\ub155" } });
        StreamingDecoder decoder;

        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto result = decoder.Push(&bytes[index], 1);
            CHECK(result.error == CodecError::None);
            CHECK(result.messages.size() == (index + 1 == bytes.size() ? 1u : 0u));

            if (!result.messages.empty())
            {
                CHECK(result.messages[0].type == MessageType::ChatDelivered);
                CHECK(result.messages[0].requestId == 27u);
                CHECK(result.messages[0].fields == std::vector<std::string>({ "alice", u8"\uc548\ub155" }));
            }
        }
    }

    void CoalescedPacketsAreEmittedInOrder()
    {
        auto first = Encode({ MessageType::LoginSucceeded, 41u, {} });
        const auto second = Encode({ MessageType::ChatDelivered, 42u, { "bob", "hello" } });
        first.insert(first.end(), second.begin(), second.end());

        StreamingDecoder decoder;
        const auto result = decoder.Push(first.data(), first.size());

        CHECK(result.error == CodecError::None);
        CHECK(result.messages.size() == 2u);
        CHECK(result.messages[0].type == MessageType::LoginSucceeded);
        CHECK(result.messages[0].requestId == 41u);
        CHECK(result.messages[1].type == MessageType::ChatDelivered);
        CHECK(result.messages[1].requestId == 42u);
        CHECK(result.messages[1].fields == std::vector<std::string>({ "bob", "hello" }));
    }

    void OversizedPayloadIsRejectedFromHeader()
    {
        const auto bytes = Header(
            static_cast<std::uint32_t>(chat::protocol::kMaxPayloadBytes + 1),
            chat::protocol::kProtocolVersion,
            static_cast<std::uint16_t>(MessageType::ChatSend),
            1u);
        StreamingDecoder decoder;
        const auto result = decoder.Push(bytes.data(), bytes.size());

        CHECK(result.error == CodecError::PayloadTooLarge);
        CHECK(result.messages.empty());
    }

    void UnsupportedVersionIsRejected()
    {
        const auto bytes = Header(
            0u,
            chat::protocol::kProtocolVersion + 1,
            static_cast<std::uint16_t>(MessageType::LoginSucceeded),
            2u);
        StreamingDecoder decoder;
        const auto result = decoder.Push(bytes.data(), bytes.size());

        CHECK(result.error == CodecError::UnsupportedVersion);
        CHECK(result.messages.empty());
    }

    void InvalidUtf8IsRejected()
    {
        auto bytes = Header(
            6u,
            chat::protocol::kProtocolVersion,
            static_cast<std::uint16_t>(MessageType::ChatSend),
            3u);
        bytes.insert(bytes.end(), { 0x00, 0x00, 0x00, 0x02, 0xc0, 0xaf });

        StreamingDecoder decoder;
        const auto result = decoder.Push(bytes.data(), bytes.size());

        CHECK(result.error == CodecError::InvalidUtf8);
        CHECK(result.messages.empty());
    }

    void StrictUtf8RejectsSurrogatesOutOfRangeAndTruncation()
    {
        const std::vector<std::string> invalidValues = {
            std::string("\xed\xa0\x80", 3),
            std::string("\xf4\x90\x80\x80", 4),
            std::string("\xe2\x82", 2)
        };

        for (const auto& value : invalidValues)
        {
            const auto result = chat::protocol::EncodeMessage({ MessageType::ChatSend, 9u, { value } });
            CHECK(result.error == CodecError::InvalidUtf8);
            CHECK(result.bytes.empty());
        }
    }

    void PayloadLimitIncludesFieldLengthPrefixes()
    {
        const std::string field(chat::protocol::kMaxPayloadBytes, 'x');
        const auto result = chat::protocol::EncodeMessage({ MessageType::ChatSend, 10u, { field } });

        CHECK(result.error == CodecError::PayloadTooLarge);
        CHECK(result.bytes.empty());
    }

    void WrongFieldShapeIsRejected()
    {
        auto bytes = Header(
            5u,
            chat::protocol::kProtocolVersion,
            static_cast<std::uint16_t>(MessageType::LoginRequest),
            4u);
        bytes.insert(bytes.end(), { 0x00, 0x00, 0x00, 0x01, 'a' });

        StreamingDecoder decoder;
        const auto result = decoder.Push(bytes.data(), bytes.size());

        CHECK(result.error == CodecError::InvalidFieldCount);
        CHECK(result.messages.empty());
    }

    void BoundedQueueRejectsOverflowWithoutReordering()
    {
        BoundedQueue<int> queue(2);
        CHECK(queue.TryPush(10));
        CHECK(queue.TryPush(20));
        CHECK(!queue.TryPush(30));

        int value = 0;
        CHECK(queue.TryPop(value));
        CHECK(value == 10);
        CHECK(queue.TryPop(value));
        CHECK(value == 20);
        CHECK(!queue.TryPop(value));
    }

    void PartialSendKeepsFrameOrder()
    {
        SerializedSendQueue queue(2);
        CHECK(queue.TryPush({ 1, 2, 3 }));
        CHECK(queue.TryPush({ 4, 5 }));
        CHECK(!queue.TryPush({ 6 }));

        auto current = queue.Current();
        CHECK(current.size == 3u);
        CHECK(std::vector<std::uint8_t>(current.data, current.data + current.size) ==
            std::vector<std::uint8_t>({ 1, 2, 3 }));

        CHECK(queue.Consume(2));
        current = queue.Current();
        CHECK(current.size == 1u);
        CHECK(current.data[0] == 3u);

        CHECK(queue.Consume(1));
        current = queue.Current();
        CHECK(current.size == 2u);
        CHECK(std::vector<std::uint8_t>(current.data, current.data + current.size) ==
            std::vector<std::uint8_t>({ 4, 5 }));

        CHECK(queue.Consume(2));
        CHECK(queue.Empty());
    }

    void OwnMessageAlignmentStaysInsideAvailableRegion()
    {
        constexpr float cursorX = 12.0f;
        constexpr float availableWidth = 300.0f;

        const float shortMessageX = chat::ui::RightAlignedMessageX(cursorX, availableWidth, 80.0f);
        CHECK(shortMessageX == 232.0f);
        CHECK(shortMessageX + 80.0f == cursorX + availableWidth);

        const float longMessageX = chat::ui::RightAlignedMessageX(cursorX, availableWidth, 400.0f);
        CHECK(longMessageX == cursorX);

        CHECK(chat::ui::MessageBubbleMaxWidth(300.0f) == 195.0f);
        CHECK(chat::ui::MessageBubbleWidth(300.0f, 20.0f) == 48.0f);
        CHECK(chat::ui::MessageBubbleWidth(300.0f, 60.0f) == 84.0f);
        CHECK(chat::ui::MessageBubbleWidth(300.0f, 500.0f) == 195.0f);
        CHECK(chat::ui::MessageBubbleWidth(40.0f, 500.0f) == 40.0f);
        CHECK(chat::ui::MessageBubbleWidth(0.0f, 500.0f) == 0.0f);
        CHECK(chat::ui::ClampedOverlayExtent(430.0f, 900.0f) == 430.0f);
        CHECK(chat::ui::ClampedOverlayExtent(430.0f, 400.0f) == 376.0f);
        CHECK(chat::ui::ClampedOverlayExtent(430.0f, 10.0f) == 1.0f);
    }

    void RegistrationRulesExplainFailures()
    {
        CHECK(std::string(chat::ui::RegistrationValidationMessage("", "")) ==
            "Nickname and password are required.");
        CHECK(std::string(chat::ui::RegistrationValidationMessage("ab", "password")) ==
            "Nickname must be 3-20 bytes. Korean nicknames can be 3-6 characters.");
        CHECK(std::string(chat::ui::RegistrationValidationMessage("a b", "password")) ==
            "Nickname cannot contain spaces or control characters.");
        CHECK(std::string(chat::ui::RegistrationValidationMessage("alice", "short")) ==
            "Password must be 8-128 bytes. Three Korean characters meet the minimum.");
        CHECK(chat::ui::RegistrationValidationMessage("alice", "password") == nullptr);
    }
}

int main()
{
    HeaderAndFieldsUseNetworkByteOrder();
    FragmentedPacketIsEmittedOnlyWhenComplete();
    CoalescedPacketsAreEmittedInOrder();
    OversizedPayloadIsRejectedFromHeader();
    UnsupportedVersionIsRejected();
    InvalidUtf8IsRejected();
    StrictUtf8RejectsSurrogatesOutOfRangeAndTruncation();
    PayloadLimitIncludesFieldLengthPrefixes();
    WrongFieldShapeIsRejected();
    BoundedQueueRejectsOverflowWithoutReordering();
    PartialSendKeepsFrameOrder();
    OwnMessageAlignmentStaysInsideAvailableRegion();
    RegistrationRulesExplainFailures();
    failures += RunNetworkManagerIntegrationTests();

    if (failures != 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "PacketCodecTests: all checks passed\n";
    return EXIT_SUCCESS;
}
