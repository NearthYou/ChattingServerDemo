#include "PasswordHasher.h"

#include <Windows.h>
#include <bcrypt.h>

#include <limits>

namespace
{
    bool ConstantTimeEqual(
        const std::array<std::uint8_t, PasswordHasher::kHashBytes>& left,
        const std::array<std::uint8_t, PasswordHasher::kHashBytes>& right)
    {
        volatile std::uint8_t difference = 0;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            difference = static_cast<std::uint8_t>(difference | (left[index] ^ right[index]));
        }
        return difference == 0;
    }
}

bool PasswordHasher::Hash(const std::string& password, PasswordRecord& record)
{
    record = {};
    if (BCryptGenRandom(
            nullptr,
            record.salt.data(),
            static_cast<ULONG>(record.salt.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    {
        return false;
    }

    record.iterations = kIterations;
    if (!Derive(password, record.salt, record.iterations, record.hash))
    {
        record = {};
        return false;
    }
    return true;
}

bool PasswordHasher::Derive(
    const std::string& password,
    const std::array<std::uint8_t, kSaltBytes>& salt,
    std::uint32_t iterations,
    std::array<std::uint8_t, kHashBytes>& hash)
{
    hash.fill(0);
    if (iterations != kIterations ||
        password.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)()))
    {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0)
    {
        return false;
    }

    status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        iterations,
        hash.data(),
        static_cast<ULONG>(hash.size()),
        0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
    {
        hash.fill(0);
        return false;
    }
    return true;
}

bool PasswordHasher::Verify(const std::string& password, const PasswordRecord& record)
{
    if (record.iterations != kIterations)
    {
        return false;
    }

    std::array<std::uint8_t, kHashBytes> derived{};
    return Derive(password, record.salt, record.iterations, derived) &&
        ConstantTimeEqual(derived, record.hash);
}

bool PasswordHasher::DummyVerify(const std::string& password)
{
    const std::array<std::uint8_t, kSaltBytes> salt = {
        0x63, 0x68, 0x61, 0x74, 0x2d, 0x64, 0x75, 0x6d,
        0x6d, 0x79, 0x2d, 0x73, 0x61, 0x6c, 0x74, 0x21
    };
    std::array<std::uint8_t, kHashBytes> derived{};
    const bool derivedSuccessfully = Derive(password, salt, kIterations, derived);
    const std::array<std::uint8_t, kHashBytes> impossible{};
    const bool compared = ConstantTimeEqual(derived, impossible);
    return derivedSuccessfully && compared && false;
}
