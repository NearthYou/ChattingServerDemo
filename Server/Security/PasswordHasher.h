#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

struct PasswordRecord
{
    std::array<std::uint8_t, 16> salt{};
    std::array<std::uint8_t, 32> hash{};
    std::uint32_t iterations = 0;
};

class PasswordHasher
{
public:
    static constexpr std::size_t kSaltBytes = 16;
    static constexpr std::size_t kHashBytes = 32;
    static constexpr std::uint32_t kIterations = 600000;

    static bool Hash(const std::string& password, PasswordRecord& record);
    static bool Derive(
        const std::string& password,
        const std::array<std::uint8_t, kSaltBytes>& salt,
        std::uint32_t iterations,
        std::array<std::uint8_t, kHashBytes>& hash);
    static bool Verify(const std::string& password, const PasswordRecord& record);
    static bool DummyVerify(const std::string& password);
};
