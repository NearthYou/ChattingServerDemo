#include "DatabaseManager.h"
#include <iostream>
#include "../../Common/Utils/StringUtils.h"

#ifndef SQL_SS_LENGTH_UNLIMITED
#define SQL_SS_LENGTH_UNLIMITED (-1)
#endif

DatabaseManager& DatabaseManager::GetInstance()
{
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::DatabaseManager() : hEnv(NULL), hDbc(NULL), isConnected(false)
{
}

DatabaseManager::~DatabaseManager()
{
    Cleanup();
}

bool DatabaseManager::Init()
{
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    if (!SQL_SUCCEEDED(ret))
    {
        std::cerr << "Failed to allocate environment handle" << std::endl;
        return false;
    }

    ret = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret))
    {
        std::cerr << "Failed to set ODBC version" << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    if (!SQL_SUCCEEDED(ret))
    {
        std::cerr << "Failed to allocate connection handle" << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    return true;
}

bool DatabaseManager::Connect(const std::string& connectionString)
{
    if (isConnected) return true;

    SQLWCHAR outConnectionString[1024];
    SQLSMALLINT outConnectionStringLength;
    SQLRETURN ret = SQLDriverConnectW(hDbc, NULL,
        (SQLWCHAR*)StringUtils::StringToWString(connectionString).c_str(), SQL_NTS,
        outConnectionString, sizeof(outConnectionString),
        &outConnectionStringLength, SQL_DRIVER_NOPROMPT);

    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hDbc, SQL_HANDLE_DBC);
        return false;
    }

    isConnected = true;
    return true;
}

void DatabaseManager::Cleanup()
{
    if (hDbc)
    {
        if (isConnected)
        {
            SQLDisconnect(hDbc);
        }
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        hDbc = NULL;
    }

    if (hEnv)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        hEnv = NULL;
    }

    isConnected = false;
}

bool DatabaseManager::RegisterUser(const std::string& username, const std::string& password)
{
    if (!isConnected) return false;

    SQLHSTMT hStmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hDbc, SQL_HANDLE_DBC);
        return false;
    }

    std::wstring query = L"INSERT INTO Users (Username, Password) VALUES (?, ?)";
    SQLWCHAR* wUsername = (SQLWCHAR*)StringUtils::StringToWString(username).c_str();
    SQLWCHAR* wPassword = (SQLWCHAR*)StringUtils::StringToWString(password).c_str();

    ret = SQLPrepareW(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 50, 0, wUsername, 0, NULL);
    ret = SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 100, 0, wPassword, 0, NULL);

    ret = SQLExecute(hStmt);
    bool success = SQL_SUCCEEDED(ret);
    if (!success)
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return success;
}

bool DatabaseManager::ValidateUser(const std::string& username, const std::string& password)
{
    if (!isConnected) return false;

    SQLHSTMT hStmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hDbc, SQL_HANDLE_DBC);
        return false;
    }

    std::wstring query = L"SELECT UserID FROM Users WHERE Username = ? AND Password = ?";
    SQLWCHAR* wUsername = (SQLWCHAR*)StringUtils::StringToWString(username).c_str();
    SQLWCHAR* wPassword = (SQLWCHAR*)StringUtils::StringToWString(password).c_str();

    ret = SQLPrepareW(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 50, 0, wUsername, 0, NULL);
    ret = SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 100, 0, wPassword, 0, NULL);

    ret = SQLExecute(hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    ret = SQLFetch(hStmt);
    bool valid = (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return valid;
}

bool DatabaseManager::SaveChatMessage(const std::string& username, const std::string& message)
{
    if (!isConnected) return false;

    SQLHSTMT hStmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hDbc, SQL_HANDLE_DBC);
        return false;
    }

    std::wstring query = L"INSERT INTO ChatLogs (UserID, Message) "
        L"SELECT UserID, ? FROM Users WHERE Username = ?";
    SQLWCHAR* wMessage = (SQLWCHAR*)StringUtils::StringToWString(message).c_str();
    SQLWCHAR* wUsername = (SQLWCHAR*)StringUtils::StringToWString(username).c_str();

    ret = SQLPrepareW(hStmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    ret = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, SQL_SS_LENGTH_UNLIMITED, 0, wMessage, 0, NULL);
    ret = SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 50, 0, wUsername, 0, NULL);

    ret = SQLExecute(hStmt);
    bool success = SQL_SUCCEEDED(ret);
    if (!success)
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return success;
}

std::vector<std::pair<std::string, std::string>> DatabaseManager::GetChatHistory(int limit)
{
    std::vector<std::pair<std::string, std::string>> history;
    if (!isConnected) return history;

    SQLHSTMT hStmt;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hDbc, SQL_HANDLE_DBC);
        return history;
    }

    wchar_t query[512];
    swprintf(query, sizeof(query) / sizeof(wchar_t),
        L"SELECT TOP (%d) u.Username, c.Message "
        L"FROM ChatLogs c "
        L"JOIN Users u ON c.UserID = u.UserID "
        L"ORDER BY c.SentAt DESC", limit);

    ret = SQLExecDirectW(hStmt, query, SQL_NTS);
    if (!SQL_SUCCEEDED(ret))
    {
        HandleError(hStmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return history;
    }

    SQLWCHAR username[50];
    SQLWCHAR message[4000];
    SQLLEN usernameLen, messageLen;

    while (SQL_SUCCEEDED(SQLFetch(hStmt)))
    {
        ret = SQLGetData(hStmt, 1, SQL_C_WCHAR, username, sizeof(username), &usernameLen);
        ret = SQLGetData(hStmt, 2, SQL_C_WCHAR, message, sizeof(message), &messageLen);

        if (SQL_SUCCEEDED(ret))
        {
            history.push_back({
                StringUtils::WStringToString(username),
                StringUtils::WStringToString(message)
            });
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return history;
}

void DatabaseManager::HandleError(SQLHANDLE handle, SQLSMALLINT type)
{
    SQLWCHAR sqlState[6];
    SQLINTEGER nativeError;
    SQLWCHAR messageText[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT textLength;

    SQLSMALLINT i = 1;
    while (SQL_SUCCEEDED(SQLGetDiagRecW(type, handle, i, sqlState, &nativeError,
        messageText, sizeof(messageText), &textLength)))
    {
        std::wcerr << L"SQL Error: " << sqlState << L" - " << messageText << std::endl;
        i++;
    }
} 