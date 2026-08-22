#include "../Common/Utils/StringUtils.h"
#include "../Server/Application/DatabaseExecutor.h"
#include "../Server/Application/IChatService.h"
#include "../Server/Application/InputValidation.h"
#include "../Server/Database/OdbcDriverDiscovery.h"
#include "../Server/Database/OdbcUnicode.h"
#include "../Server/Security/PasswordHasher.h"
#include "../Client/UI/ClientPacketReducer.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template <typename Predicate>
    void WaitUntil(Predicate predicate, const std::string& message)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return;
            }
            std::this_thread::sleep_for(1ms);
        }
        Require(predicate(), message);
    }

    class RecordingService final : public IChatService
    {
    public:
        ChatServiceStatus Start() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            lifecycle.push_back("start");
            return ChatServiceStatus::Succeeded;
        }

        void Stop() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            lifecycle.push_back("stop");
        }

        ChatServiceStatus RegisterUser(const std::string&, const std::string&) override
        {
            return ChatServiceStatus::Succeeded;
        }

        LoginResult Login(const std::string&, const std::string&, std::size_t) override
        {
            return { ChatServiceStatus::Succeeded, {} };
        }

        ChatServiceStatus StoreMessage(const std::string&, const std::string&) override
        {
            return ChatServiceStatus::Succeeded;
        }

        std::vector<std::string> Lifecycle() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return lifecycle;
        }

    private:
        mutable std::mutex mutex;
        std::vector<std::string> lifecycle;
    };

    void TestUtf8Conversions()
    {
        std::wstring wide;
        std::string narrow;
        Require(StringUtils::TryStringToWString("", wide) && wide.empty(), "empty UTF-8 did not remain empty");
        Require(StringUtils::TryStringToWString(u8"채팅", wide), "Korean UTF-8 conversion failed");
        Require(StringUtils::TryWStringToString(wide, narrow) && narrow == u8"채팅", "Korean UTF-8 round trip failed");

        const std::string invalid("\xc0\xaf", 2);
        Require(!StringUtils::TryStringToWString(invalid, wide), "invalid UTF-8 was accepted");
    }

    void TestOdbcWideTextBindingAndRetrieval()
    {
        OdbcWideText text;
        Require(TryMakeOdbcWideText(u8"채팅", false, text), "ODBC wide text rejected Korean UTF-8");
        Require(text.cType == SQL_C_WCHAR && text.sqlType == SQL_WVARCHAR,
            "ODBC text did not select Unicode binding types");
        Require(text.indicatorBytes == static_cast<SQLLEN>(text.value.size() * sizeof(SQLWCHAR)),
            "ODBC wide indicator was not measured in bytes");

        std::array<SQLWCHAR, 8> buffer{};
        std::copy(text.value.begin(), text.value.end(), reinterpret_cast<wchar_t*>(buffer.data()));
        std::string roundTrip;
        Require(TryReadOdbcWideText(buffer.data(), buffer.size(), text.indicatorBytes, roundTrip),
            "ODBC wide result conversion failed");
        Require(roundTrip == u8"채팅", "ODBC wide result changed Korean text");
        Require(!TryReadOdbcWideText(buffer.data(), buffer.size(), text.indicatorBytes + 1, roundTrip),
            "ODBC result accepted a non-character-aligned indicator");
    }

    void TestPasswordHasherVectorAndVerification()
    {
        std::array<std::uint8_t, PasswordHasher::kSaltBytes> salt{};
        for (std::size_t index = 0; index < salt.size(); ++index)
        {
            salt[index] = static_cast<std::uint8_t>(index);
        }

        std::array<std::uint8_t, PasswordHasher::kHashBytes> derived{};
        Require(
            PasswordHasher::Derive("password", salt, PasswordHasher::kIterations, derived),
            "PBKDF2 derivation failed");
        const std::array<std::uint8_t, PasswordHasher::kHashBytes> expected = {
            0x3b, 0xc3, 0x71, 0x18, 0xe6, 0x25, 0x09, 0x3e,
            0x9b, 0x79, 0xed, 0x08, 0x93, 0x0e, 0xa7, 0xaf,
            0x73, 0x89, 0x59, 0x12, 0x33, 0xfd, 0xd9, 0x2d,
            0xdd, 0xf3, 0x69, 0x37, 0x1e, 0x60, 0xdb, 0xc0
        };
        if (derived != expected)
        {
            std::ostringstream actual;
            std::ostringstream wanted;
            for (const auto byte : derived)
            {
                actual << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            for (const auto byte : expected)
            {
                wanted << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            throw std::runtime_error("PBKDF2 output did not match the independent vector: " + actual.str() + " != " + wanted.str());
        }

        PasswordRecord first;
        PasswordRecord second;
        Require(PasswordHasher::Hash("correct-password", first), "first password hash failed");
        Require(PasswordHasher::Hash("correct-password", second), "second password hash failed");
        Require(first.salt != second.salt, "fresh password hashes reused a salt");
        Require(PasswordHasher::Verify("correct-password", first), "correct password was rejected");
        Require(!PasswordHasher::Verify("wrong-password", first), "wrong password was accepted");

        PasswordRecord malformed = first;
        malformed.iterations = PasswordHasher::kIterations - 1;
        Require(!PasswordHasher::Verify("correct-password", malformed), "malformed iteration count was accepted");
        Require(!PasswordHasher::DummyVerify("unknown-password"), "dummy verification reported success");
    }

    void TestInputValidationBoundaries()
    {
        Require(chat::validation::IsValidUsername("alice"), "ordinary username was rejected");
        Require(chat::validation::IsValidUsername(u8"사용자"), "Korean username was rejected");
        Require(!chat::validation::IsValidUsername(u8"ab\u200b"), "zero-width username was accepted");
        Require(!chat::validation::IsValidUsername(u8"ab\u202e"), "bidi override username was accepted");
        Require(!chat::validation::IsValidUsername("ab"), "short username was accepted");
        Require(!chat::validation::IsValidUsername(std::string(21, 'a')), "long username was accepted");
        Require(chat::validation::IsValidPassword(std::string(8, 'p')), "minimum password was rejected");
        Require(!chat::validation::IsValidPassword(std::string(7, 'p')), "short password was accepted");
        Require(chat::validation::IsValidPassword(u8"password\u202e"), "valid UTF-8 password format control was rejected");
        Require(chat::validation::IsValidMessage("x"), "minimum message was rejected");
        Require(chat::validation::IsValidMessage(u8"hello\u200bworld"), "valid UTF-8 message format control was rejected");
        Require(!chat::validation::IsValidMessage(""), "empty message was accepted");
        Require(!chat::validation::IsValidMessage(std::string(1001, 'x')), "oversized message was accepted");
    }

    void TestDatabaseExecutorFifoOverflowAndStop()
    {
        RecordingService service;
        DatabaseExecutor executor(service, 2);
        Require(executor.Start(), "database executor failed to start");

        std::mutex gateMutex;
        std::condition_variable gate;
        bool release = false;
        std::mutex orderMutex;
        std::vector<int> order;
        std::atomic<int> completed{ 0 };
        Require(executor.TrySubmit([&](IChatService&) {
            std::unique_lock<std::mutex> lock(gateMutex);
            gate.wait(lock, [&] { return release; });
            lock.unlock();
            {
                std::lock_guard<std::mutex> orderLock(orderMutex);
                order.push_back(1);
            }
            completed.fetch_add(1);
        }), "blocking job was rejected");

        WaitUntil([&] { return executor.RunningJobCount() == 1; }, "executor did not start the first job");
        Require(executor.TrySubmit([&](IChatService&) {
            std::lock_guard<std::mutex> lock(orderMutex);
            order.push_back(2);
            completed.fetch_add(1);
        }), "first queued job was rejected");
        Require(executor.TrySubmit([&](IChatService&) {
            std::lock_guard<std::mutex> lock(orderMutex);
            order.push_back(3);
            completed.fetch_add(1);
        }), "second queued job was rejected");

        const auto submitStarted = std::chrono::steady_clock::now();
        Require(!executor.TrySubmit([&](IChatService&) { order.push_back(4); }), "overflowing job was accepted");
        Require(std::chrono::steady_clock::now() - submitStarted < 100ms, "overflow submission blocked");

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            release = true;
        }
        gate.notify_all();
        WaitUntil([&] { return completed.load() == 3; }, "queued jobs did not drain in time");
        executor.Stop();

        {
            std::lock_guard<std::mutex> lock(orderMutex);
            Require(order == std::vector<int>({ 1, 2, 3 }), "executor did not preserve FIFO order");
        }
        Require(!executor.TrySubmit([](IChatService&) {}), "executor accepted work after stop");
        Require(service.Lifecycle() == std::vector<std::string>({ "start", "stop" }), "service lifecycle escaped executor thread");
    }

    void TestClientPacketReducerUsesPacketType()
    {
        ClientPacketReducer reducer;
        reducer.AppendChat("old", "stale", false);
        reducer.Apply({ PACKET_TYPE_REGISTER_SUCCESS, {}, "Login/Register successful", false });
        Require(!reducer.IsLoggedIn(), "registration success logged the client in");
        Require(reducer.ChatCount() == 1, "registration success cleared chat history");

        reducer.BeginLogin("alice");
        reducer.Apply({ PACKET_TYPE_LOGIN_SUCCESS, {}, "ignored text", false });
        Require(reducer.IsLoggedIn(), "login success did not log the client in");
        Require(reducer.ChatCount() == 0, "login success did not clear stale chat history");

        reducer.Apply({ PACKET_TYPE_CHAT, "alice", "history", false, 1763856000123LL });
        Require(reducer.ChatCount() == 1, "history delivery was not appended");
        Require(reducer.ChatMessages()[0].isMine, "restored own history was rendered as another user");
        Require(reducer.ChatMessages()[0].timestampMilliseconds == 1763856000123LL,
            "restored history lost its timestamp");
        reducer.Apply({ PACKET_TYPE_CHAT, "bob", "peer history", false });
        Require(!reducer.ChatMessages()[1].isMine, "peer history was rendered as the current user");
        reducer.Disconnect();
        Require(!reducer.IsLoggedIn(), "disconnect left the client logged in");
        Require(reducer.ChatCount() == 2, "disconnect discarded visible chat before reconnect");

        reducer.BeginLogin("bob");
        reducer.Apply({ PACKET_TYPE_LOGIN_SUCCESS, {}, "ignored text", false });
        reducer.Apply({ PACKET_TYPE_CHAT, "alice", "older history", false });
        reducer.Apply({ PACKET_TYPE_CHAT, "bob", "new own history", false });
        Require(!reducer.ChatMessages()[0].isMine, "previous account history stayed marked as mine after relogin");
        Require(reducer.ChatMessages()[1].isMine, "relogged account history was not marked as mine");
    }

    void TestMissingDriverDiagnosticIsActionableAndPrivate()
    {
        const auto result = DiscoverMySqlUnicodeDriver(L"Chat Missing Unicode Driver");
        Require(!result.found, "missing exact driver unexpectedly resolved");
        Require(result.diagnostic.find("x64 MySQL Connector/ODBC Unicode driver") != std::string::npos,
            "missing driver error was not actionable");
        Require(result.diagnostic.find("CHAT_DB_PASSWORD") == std::string::npos,
            "missing driver error exposed credential configuration");
    }

    void Run(const char* name, const std::function<void()>& test)
    {
        test();
        std::cout << "PASS " << name << '\n';
    }
}

int main()
{
    try
    {
        Run("UTF-8 conversions", TestUtf8Conversions);
        Run("ODBC Unicode text", TestOdbcWideTextBindingAndRetrieval);
        Run("password hashing", TestPasswordHasherVectorAndVerification);
        Run("input validation", TestInputValidationBoundaries);
        Run("database executor", TestDatabaseExecutorFifoOverflowAndStop);
        Run("client packet reducer", TestClientPacketReducerUsesPacketType);
        Run("ODBC driver discovery", TestMissingDriverDiagnosticIsActionableAndPrivate);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
