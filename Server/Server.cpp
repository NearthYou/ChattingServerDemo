#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "Server.h"
#include "Common/PacketDefine.h"
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

bool Server::Init(int port)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    listenSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[Server] Listening on port " << port << std::endl;
    return true;
}

void Server::Run()
{
    std::thread acceptThread(&Server::AcceptLoop, this);
    acceptThread.join(); // 간단히 하나만
}

void Server::AcceptLoop()
{
    while (true)
    {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
            continue;

        std::cout << "[Server] Client connected!" << std::endl;

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.push_back(clientSocket);
        }

        std::thread t(&Server::ClientLoop, this, clientSocket);
        t.detach();
    }
}

void Server::ClientLoop(SOCKET clientSocket)
{
    char buffer[512] = {};
    bool isLoggedIn = false;
    std::string nickname;

    while (true)
    {
        int recvLen = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (recvLen <= 0)
        {
            std::cout << "[Server] Client disconnected" << std::endl;

            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
                nicknames.erase(clientSocket);
            }

            closesocket(clientSocket);
            break;
        }

        PacketHeader header;
        memcpy(&header, buffer, sizeof(PacketHeader));

        const char* body = buffer + sizeof(PacketHeader);
        int bodySize = header.size - sizeof(PacketHeader);

        if (header.type == PACKET_LOGIN)
        {
            nickname = std::string(body, bodySize);
            std::cout << "[Server] Login attempt: " << nickname << std::endl;

            bool loginSuccess = !nickname.empty(); // 간단 로그인 체크

            PacketHeader respHeader;
            std::vector<char> sendBuffer;

            if (loginSuccess)
            {
                respHeader.type = PACKET_LOGIN_OK;
                respHeader.size = sizeof(PacketHeader) + nickname.size();

                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    nicknames[clientSocket] = nickname;
                }
            }
            else
            {
                respHeader.type = PACKET_LOGIN_FAIL;
                respHeader.size = sizeof(PacketHeader);
            }

            sendBuffer.resize(respHeader.size);
            memcpy(sendBuffer.data(), &respHeader, sizeof(PacketHeader));
            if (loginSuccess)
                memcpy(sendBuffer.data() + sizeof(PacketHeader), nickname.data(), nickname.size());

            send(clientSocket, sendBuffer.data(), sendBuffer.size(), 0);
        }
        else if (header.type == PACKET_CHAT)
        {
            if (!isLoggedIn && nicknames.find(clientSocket) != nicknames.end())
            {
                isLoggedIn = true;
                nickname = nicknames[clientSocket];
            }

            std::string msg(body, bodySize);
            std::cout << "[Chat] " << nickname << ": " << msg << std::endl;

            // 서버가 패킷 새로 만들어서 다시 보내줌 (닉네임 + 메시지 합쳐서 보냄)
            std::string fullMsg = nickname + ": " + msg;

            PacketHeader chatHeader;
            chatHeader.type = PACKET_CHAT;
            chatHeader.size = sizeof(PacketHeader) + fullMsg.size();

            std::vector<char> chatBuffer(chatHeader.size);
            memcpy(chatBuffer.data(), &chatHeader, sizeof(PacketHeader));
            memcpy(chatBuffer.data() + sizeof(PacketHeader), fullMsg.data(), fullMsg.size());

            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& s : clients)
            {
                send(s, chatBuffer.data(), chatBuffer.size(), 0);
            }
        }
    }
}
