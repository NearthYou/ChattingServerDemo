#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "Server.h"
#include "../Database/DatabaseManager.h"
#include "../../Common/PacketDefine.h"
#include <iostream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

Server::Server() : serverSocket(INVALID_SOCKET), isRunning(false)
{
}

Server::~Server()
{
    Shutdown();
}

bool Server::Init(int port)
{
    listenPort = port;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "Listen failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }
    std::cout << "Server is listening on port " << listenPort << "..." << std::endl;

    // 데이터베이스 초기화
    if (!DatabaseManager::GetInstance().Init())
    {
        std::cerr << "Database initialization failed" << std::endl;
        return false;
    }

    std::string connectionString = "DRIVER={SQL Server};SERVER=localhost;DATABASE=ChatDB;Trusted_Connection=yes;";
    if (!DatabaseManager::GetInstance().Connect(connectionString))
    {
        std::cerr << "Database connection failed" << std::endl;
        return false;
    }

    isRunning = true;
    return true;
}

void Server::Run()
{
    while (isRunning)
    {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET)
        {
            if (isRunning)
            {
                std::cerr << "Accept failed" << std::endl;
            }
            continue;
        }

        connectedClients.push_back(clientSocket);
        std::thread clientThread(&Server::ClientLoop, this, clientSocket);
        clientThread.detach();
    }
}

void Server::Shutdown()
{
    isRunning = false;
    closesocket(serverSocket);
    WSACleanup();
    DatabaseManager::GetInstance().Cleanup();
}

void Server::ClientLoop(SOCKET clientSocket)
{
    char buffer[4096];
    while (isRunning)
    {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            break;
        }

        std::string data(buffer, bytesReceived);
        std::istringstream iss(data);
        std::string typeStr, sender, message;
        
        iss >> typeStr;
        PacketType type = static_cast<PacketType>(std::stoi(typeStr));

        switch (type)
        {
            case PACKET_TYPE_LOGIN:
            {
                iss >> sender >> message; // message는 비밀번호
                bool success = DatabaseManager::GetInstance().ValidateUser(sender, message);
                
                std::string response = std::to_string(success ? PACKET_TYPE_LOGIN_SUCCESS : PACKET_TYPE_LOGIN_FAILED);
                send(clientSocket, response.c_str(), response.length(), 0);
                break;
            }
            case PACKET_TYPE_REGISTER:
            {
                iss >> sender >> message; // message는 비밀번호
                bool success = DatabaseManager::GetInstance().RegisterUser(sender, message);
                
                std::string response = std::to_string(success ? PACKET_TYPE_REGISTER_SUCCESS : PACKET_TYPE_REGISTER_FAILED);
                send(clientSocket, response.c_str(), response.length(), 0);
                break;
            }
            case PACKET_TYPE_CHAT:
            {
                iss >> sender;
                std::getline(iss, message);
                if (!message.empty() && message[0] == ' ')
                {
                    message = message.substr(1);
                }

                // 메시지를 데이터베이스에 저장
                DatabaseManager::GetInstance().SaveChatMessage(sender, message);

                // 모든 클라이언트에게 메시지 브로드캐스트
                std::string broadcast = std::to_string(PACKET_TYPE_CHAT) + " " + sender + " " + message;
                for (SOCKET sock : connectedClients)
                {
                    if (sock != clientSocket)
                    {
                        send(sock, broadcast.c_str(), broadcast.length(), 0);
                    }
                }
                break;
            }
        }
    }

    // 클라이언트 연결 종료 처리
    auto it = std::find(connectedClients.begin(), connectedClients.end(), clientSocket);
    if (it != connectedClients.end())
    {
        connectedClients.erase(it);
    }
    closesocket(clientSocket);
}
