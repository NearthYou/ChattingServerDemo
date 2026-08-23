#include "DatabaseManager.h"

#include "OdbcDriverDiscovery.h"
#include "OdbcUnicode.h"
#include "../../Common/Utils/StringUtils.h"

#include <Windows.h>
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
    class Statement
    {
    public:
        Statement(SQLHDBC connection, std::uint32_t timeoutSeconds)
        {
            if (SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, connection, &handle)))
            {
                SQLSetStmtAttr(
                    handle,
                    SQL_ATTR_QUERY_TIMEOUT,
                    reinterpret_cast<SQLPOINTER>(static_cast<std::uintptr_t>(timeoutSeconds)),
                    0);
            }
        }

        ~Statement()
        {
            if (handle != SQL_NULL_HSTMT)
            {
                SQLFreeHandle(SQL_HANDLE_STMT, handle);
            }
        }

        SQLHSTMT Get() const { return handle; }

    private:
        SQLHSTMT handle = SQL_NULL_HSTMT;
    };

    bool Prepare(SQLHSTMT statement, const wchar_t* query)
    {
        return statement != SQL_NULL_HSTMT &&
            SQL_SUCCEEDED(SQLPrepareW(
                statement,
                reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(query)),
                SQL_NTS));
    }

    bool BindWideText(SQLHSTMT statement, SQLUSMALLINT index, OdbcWideText& value)
    {
        return SQL_SUCCEEDED(SQLBindParameter(
            statement,
            index,
            SQL_PARAM_INPUT,
            value.cType,
            value.sqlType,
            static_cast<SQLULEN>(std::max<std::size_t>(1, value.value.size())),
            0,
            value.value.data(),
            static_cast<SQLLEN>((value.value.size() + 1) * sizeof(SQLWCHAR)),
            &value.indicatorBytes));
    }

    template <std::size_t Size>
    bool BindBinary(
        SQLHSTMT statement,
        SQLUSMALLINT index,
        const std::array<std::uint8_t, Size>& value,
        SQLLEN& length)
    {
        length = static_cast<SQLLEN>(value.size());
        return SQL_SUCCEEDED(SQLBindParameter(
            statement,
            index,
            SQL_PARAM_INPUT,
            SQL_C_BINARY,
            SQL_VARBINARY,
            value.size(),
            0,
            const_cast<std::uint8_t*>(value.data()),
            static_cast<SQLLEN>(value.size()),
            &length));
    }

    bool IsIntegrityConflict(SQLHSTMT statement)
    {
        SQLWCHAR state[6]{};
        SQLINTEGER nativeError = 0;
        SQLWCHAR message[2]{};
        SQLSMALLINT messageLength = 0;
        const SQLRETURN status = SQLGetDiagRecW(
            SQL_HANDLE_STMT,
            statement,
            1,
            state,
            &nativeError,
            message,
            static_cast<SQLSMALLINT>(std::size(message)),
            &messageLength);
        return SQL_SUCCEEDED(status) && state[0] == L'2' && state[1] == L'3';
    }

    std::string ConnectionDiagnostic(SQLHDBC connection)
    {
        SQLWCHAR state[6]{};
        SQLINTEGER nativeError = 0;
        SQLWCHAR message[2]{};
        SQLSMALLINT messageLength = 0;
        if (SQL_SUCCEEDED(SQLGetDiagRecW(
                SQL_HANDLE_DBC,
                connection,
                1,
                state,
                &nativeError,
                message,
                static_cast<SQLSMALLINT>(std::size(message)),
                &messageLength)))
        {
            std::string stateUtf8;
            if (StringUtils::TryWStringToString(
                    std::wstring(reinterpret_cast<wchar_t*>(state), 5),
                    stateUtf8))
            {
                return "Database connection failed (SQLSTATE " + stateUtf8 + ").";
            }
        }
        return "Database connection failed.";
    }

    std::wstring EscapeOdbcValue(const std::wstring& value)
    {
        std::wstring escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back(L'{');
        for (const wchar_t character : value)
        {
            escaped.push_back(character);
            if (character == L'}')
            {
                escaped.push_back(L'}');
            }
        }
        escaped.push_back(L'}');
        return escaped;
    }
}

class DatabaseManager::Impl
{
public:
    ~Impl() { Disconnect(); }

    DatabaseStatus Connect(const DatabaseConfig& config, std::string& diagnostic)
    {
        diagnostic.clear();
        if (connected)
        {
            return DatabaseStatus::Succeeded;
        }

        const auto driver = DiscoverMySqlUnicodeDriver(config.driverOverride);
        if (!driver.found)
        {
            diagnostic = driver.diagnostic;
            return DatabaseStatus::Unavailable;
        }

        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment)) ||
            !SQL_SUCCEEDED(SQLSetEnvAttr(
                environment,
                SQL_ATTR_ODBC_VERSION,
                reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3_80),
                0)) ||
            !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, environment, &connection)))
        {
            diagnostic = "Could not initialize ODBC.";
            Disconnect();
            return DatabaseStatus::Unavailable;
        }

        SQLSetConnectAttr(
            connection,
            SQL_ATTR_LOGIN_TIMEOUT,
            reinterpret_cast<SQLPOINTER>(static_cast<std::uintptr_t>(config.loginTimeoutSeconds)),
            0);
        SQLSetConnectAttr(
            connection,
            SQL_ATTR_CONNECTION_TIMEOUT,
            reinterpret_cast<SQLPOINTER>(static_cast<std::uintptr_t>(config.loginTimeoutSeconds)),
            0);

        std::wstring host;
        std::wstring database;
        std::wstring username;
        std::wstring password;
        if (!StringUtils::TryStringToWString(config.host, host) ||
            !StringUtils::TryStringToWString(config.database, database) ||
            !StringUtils::TryStringToWString(config.username, username) ||
            !StringUtils::TryStringToWString(config.password, password))
        {
            diagnostic = "Database configuration is not valid UTF-8.";
            Disconnect();
            return DatabaseStatus::Unavailable;
        }

        std::wostringstream builder;
        builder
            << L"DRIVER=" << EscapeOdbcValue(driver.driver)
            << L";SERVER=" << EscapeOdbcValue(host)
            << L";PORT=" << config.port
            << L";DATABASE=" << EscapeOdbcValue(database)
            << L";USER=" << EscapeOdbcValue(username)
            << L";PASSWORD=" << EscapeOdbcValue(password)
            << L";CHARSET=utf8mb4"
            << L";CONNECT_TIMEOUT=" << config.loginTimeoutSeconds
            << L";READ_TIMEOUT=" << config.statementTimeoutSeconds
            << L";WRITE_TIMEOUT=" << config.statementTimeoutSeconds
            << L';';
        std::wstring connectionText = builder.str();

        std::array<SQLWCHAR, 1024> output{};
        SQLSMALLINT outputLength = 0;
        const SQLRETURN status = SQLDriverConnectW(
            connection,
            nullptr,
            reinterpret_cast<SQLWCHAR*>(connectionText.data()),
            static_cast<SQLSMALLINT>(connectionText.size()),
            output.data(),
            static_cast<SQLSMALLINT>(output.size()),
            &outputLength,
            SQL_DRIVER_NOPROMPT);

        std::fill(connectionText.begin(), connectionText.end(), L'\0');
        std::fill(password.begin(), password.end(), L'\0');
        if (!SQL_SUCCEEDED(status))
        {
            diagnostic = ConnectionDiagnostic(connection);
            Disconnect();
            return DatabaseStatus::Unavailable;
        }

        statementTimeoutSeconds = config.statementTimeoutSeconds;
        connected = true;
        return DatabaseStatus::Succeeded;
    }

    void Disconnect()
    {
        if (connection != SQL_NULL_HDBC)
        {
            if (connected)
            {
                SQLDisconnect(connection);
            }
            SQLFreeHandle(SQL_HANDLE_DBC, connection);
            connection = SQL_NULL_HDBC;
        }
        if (environment != SQL_NULL_HENV)
        {
            SQLFreeHandle(SQL_HANDLE_ENV, environment);
            environment = SQL_NULL_HENV;
        }
        connected = false;
    }

    DatabaseStatus InsertUser(const std::string& username, const PasswordRecord& credential)
    {
        if (!connected)
        {
            return DatabaseStatus::Unavailable;
        }
        Statement statement(connection, statementTimeoutSeconds);
        constexpr wchar_t query[] =
            L"INSERT INTO users (username, password_salt, password_hash, password_iterations) VALUES (?, ?, ?, ?)";
        if (!Prepare(statement.Get(), query))
        {
            return DatabaseStatus::Unavailable;
        }

        OdbcWideText usernameText;
        SQLLEN saltLength = 0;
        SQLLEN hashLength = 0;
        SQLLEN iterationsLength = 0;
        SQLUINTEGER iterations = credential.iterations;
        if (!TryMakeOdbcWideText(username, false, usernameText) ||
            !BindWideText(statement.Get(), 1, usernameText) ||
            !BindBinary(statement.Get(), 2, credential.salt, saltLength) ||
            !BindBinary(statement.Get(), 3, credential.hash, hashLength) ||
            !SQL_SUCCEEDED(SQLBindParameter(
                statement.Get(), 4, SQL_PARAM_INPUT, SQL_C_ULONG, SQL_INTEGER,
                0, 0, &iterations, 0, &iterationsLength)))
        {
            return DatabaseStatus::Unavailable;
        }

        const SQLRETURN status = SQLExecute(statement.Get());
        if (SQL_SUCCEEDED(status))
        {
            return DatabaseStatus::Succeeded;
        }
        return IsIntegrityConflict(statement.Get())
            ? DatabaseStatus::Conflict
            : DatabaseStatus::Unavailable;
    }

    CredentialLookupResult LoadCredential(const std::string& username)
    {
        CredentialLookupResult result;
        if (!connected)
        {
            return result;
        }
        Statement statement(connection, statementTimeoutSeconds);
        constexpr wchar_t query[] =
            L"SELECT password_salt, password_hash, password_iterations FROM users WHERE username = ?";
        OdbcWideText usernameText;
        if (!Prepare(statement.Get(), query) ||
            !TryMakeOdbcWideText(username, false, usernameText) ||
            !BindWideText(statement.Get(), 1, usernameText) ||
            !SQL_SUCCEEDED(SQLExecute(statement.Get())))
        {
            return result;
        }

        const SQLRETURN fetched = SQLFetch(statement.Get());
        if (fetched == SQL_NO_DATA)
        {
            result.status = DatabaseStatus::NotFound;
            return result;
        }
        if (!SQL_SUCCEEDED(fetched))
        {
            return result;
        }

        SQLLEN saltLength = 0;
        SQLLEN hashLength = 0;
        SQLLEN iterationsLength = 0;
        SQLUINTEGER iterations = 0;
        if (!SQL_SUCCEEDED(SQLGetData(
                statement.Get(), 1, SQL_C_BINARY, result.credential.salt.data(),
                static_cast<SQLLEN>(result.credential.salt.size()), &saltLength)) ||
            !SQL_SUCCEEDED(SQLGetData(
                statement.Get(), 2, SQL_C_BINARY, result.credential.hash.data(),
                static_cast<SQLLEN>(result.credential.hash.size()), &hashLength)) ||
            !SQL_SUCCEEDED(SQLGetData(
                statement.Get(), 3, SQL_C_ULONG, &iterations, sizeof(iterations), &iterationsLength)))
        {
            return result;
        }

        result.credential.iterations = iterations;
        if (saltLength != static_cast<SQLLEN>(result.credential.salt.size()) ||
            hashLength != static_cast<SQLLEN>(result.credential.hash.size()) ||
            iterations != PasswordHasher::kIterations)
        {
            result.status = DatabaseStatus::InvalidData;
            return result;
        }
        result.status = DatabaseStatus::Succeeded;
        return result;
    }

    DatabaseStatus InsertMessage(const std::string& username, const std::string& message)
    {
        if (!connected)
        {
            return DatabaseStatus::Unavailable;
        }
        Statement statement(connection, statementTimeoutSeconds);
        constexpr wchar_t query[] =
            L"INSERT INTO chat_messages (user_id, body) SELECT id, ? FROM users WHERE username = ?";
        OdbcWideText messageText;
        OdbcWideText usernameText;
        if (!Prepare(statement.Get(), query) ||
            !TryMakeOdbcWideText(message, true, messageText) ||
            !TryMakeOdbcWideText(username, false, usernameText) ||
            !BindWideText(statement.Get(), 1, messageText) ||
            !BindWideText(statement.Get(), 2, usernameText) ||
            !SQL_SUCCEEDED(SQLExecute(statement.Get())))
        {
            return DatabaseStatus::Unavailable;
        }

        SQLLEN insertedRows = 0;
        if (!SQL_SUCCEEDED(SQLRowCount(statement.Get(), &insertedRows)))
        {
            return DatabaseStatus::Unavailable;
        }
        return insertedRows == 1 ? DatabaseStatus::Succeeded : DatabaseStatus::NotFound;
    }

    DatabaseHistoryResult LoadRecentHistory(std::size_t limit)
    {
        DatabaseHistoryResult result;
        if (!connected || limit == 0 ||
            limit > static_cast<std::size_t>((std::numeric_limits<SQLUINTEGER>::max)()))
        {
            return result;
        }
        Statement statement(connection, statementTimeoutSeconds);
        constexpr wchar_t query[] =
            L"SELECT username, body, CAST(UNIX_TIMESTAMP(created_at) * 1000 AS SIGNED) FROM ("
            L"SELECT u.username AS username, c.body AS body, c.created_at AS created_at, c.id AS id "
            L"FROM chat_messages c JOIN users u ON u.id = c.user_id "
            L"ORDER BY c.created_at DESC, c.id DESC LIMIT ?"
            L") recent ORDER BY created_at ASC, id ASC";
        SQLUINTEGER rowLimit = static_cast<SQLUINTEGER>(limit);
        SQLLEN limitLength = 0;
        if (!Prepare(statement.Get(), query) ||
            !SQL_SUCCEEDED(SQLBindParameter(
                statement.Get(), 1, SQL_PARAM_INPUT, SQL_C_ULONG, SQL_INTEGER,
                0, 0, &rowLimit, 0, &limitLength)) ||
            !SQL_SUCCEEDED(SQLExecute(statement.Get())))
        {
            return result;
        }

        for (;;)
        {
            const SQLRETURN fetched = SQLFetch(statement.Get());
            if (fetched == SQL_NO_DATA)
            {
                result.status = DatabaseStatus::Succeeded;
                return result;
            }
            if (!SQL_SUCCEEDED(fetched))
            {
                result.messages.clear();
                return result;
            }

            std::array<SQLWCHAR, 21> username{};
            std::array<SQLWCHAR, 1001> message{};
            SQLLEN usernameLength = 0;
            SQLLEN messageLength = 0;
            SQLBIGINT timestampMilliseconds = 0;
            SQLLEN timestampLength = 0;
            const SQLRETURN usernameStatus = SQLGetData(
                statement.Get(), 1, SQL_C_WCHAR, username.data(), sizeof(username), &usernameLength);
            const SQLRETURN messageStatus = SQLGetData(
                statement.Get(), 2, SQL_C_WCHAR, message.data(), sizeof(message), &messageLength);
            const SQLRETURN timestampStatus = SQLGetData(
                statement.Get(), 3, SQL_C_SBIGINT,
                &timestampMilliseconds, sizeof(timestampMilliseconds), &timestampLength);
            if (usernameStatus != SQL_SUCCESS || messageStatus != SQL_SUCCESS ||
                timestampStatus != SQL_SUCCESS ||
                usernameLength < 0 || messageLength < 0 ||
                timestampLength == SQL_NULL_DATA || timestampMilliseconds < 0)
            {
                result.status = DatabaseStatus::InvalidData;
                result.messages.clear();
                return result;
            }
            StoredChatMessage converted;
            if (!TryReadOdbcWideText(
                    username.data(), username.size(), usernameLength, converted.username) ||
                !TryReadOdbcWideText(
                    message.data(), message.size(), messageLength, converted.message))
            {
                result.status = DatabaseStatus::InvalidData;
                result.messages.clear();
                return result;
            }
            converted.timestampMilliseconds = static_cast<std::int64_t>(timestampMilliseconds);
            result.messages.push_back(std::move(converted));
        }
    }

private:
    SQLHENV environment = SQL_NULL_HENV;
    SQLHDBC connection = SQL_NULL_HDBC;
    bool connected = false;
    std::uint32_t statementTimeoutSeconds = 5;
};

DatabaseManager::DatabaseManager() : impl(std::make_unique<Impl>()) {}
DatabaseManager::~DatabaseManager() = default;

DatabaseStatus DatabaseManager::Connect(const DatabaseConfig& config, std::string& diagnostic)
{
    return impl->Connect(config, diagnostic);
}

void DatabaseManager::Disconnect() { impl->Disconnect(); }

DatabaseStatus DatabaseManager::InsertUser(const std::string& username, const PasswordRecord& credential)
{
    return impl->InsertUser(username, credential);
}

CredentialLookupResult DatabaseManager::LoadCredential(const std::string& username)
{
    return impl->LoadCredential(username);
}

DatabaseStatus DatabaseManager::InsertMessage(const std::string& username, const std::string& message)
{
    return impl->InsertMessage(username, message);
}

DatabaseHistoryResult DatabaseManager::LoadRecentHistory(std::size_t limit)
{
    return impl->LoadRecentHistory(limit);
}
