#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "NetworkManager.h"
#include "../../Common/PacketDefine.h"
#include <iostream>
#include <sstream>
#include <Ws2tcpip.h>

NetworkManager::NetworkManager() : clientSocket(INVALID_SOCKET), isConnected(false)
{
}

NetworkManager::~NetworkManager()
{
    Disconnect();
}

bool NetworkManager::Connect(const std::string& address, int port)
{
    if (isConnected) return true;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Connection failed" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    isConnected = true;
    receiveThread = std::thread(&NetworkManager::ReceiveLoop, this);
    return true;
}

void NetworkManager::Disconnect()
{
    if (isConnected)
    {
        isConnected = false;
        closesocket(clientSocket);
        if (receiveThread.joinable())
        {
            receiveThread.join();
        }
        WSACleanup();
    }
}

bool NetworkManager::IsConnected() const
{
    return isConnected;
}

void NetworkManager::ReceiveLoop()
{
    char buffer[4096];
    while (isConnected)
    {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0)
        {
            break;
        }

        std::string data(buffer, bytesReceived);
        std::istringstream iss(data);
        std::string typeStr;
        iss >> typeStr;
        PacketType type = static_cast<PacketType>(std::stoi(typeStr));

        ChatPacket packet;
        packet.type = type;

        switch (type)
        {
            case PACKET_TYPE_CHAT:
            {
                std::string sender, message;
                iss >> sender;
                std::getline(iss, message);
                if (!message.empty() && message[0] == ' ')
                {
                    message = message.substr(1);
                }
                packet.sender = sender;
                packet.message = message;
                packet.isMine = false;
                break;
            }
            case PACKET_TYPE_LOGIN_SUCCESS:
            case PACKET_TYPE_LOGIN_FAILED:
            case PACKET_TYPE_REGISTER_SUCCESS:
            case PACKET_TYPE_REGISTER_FAILED:
                packet.message = std::string("Login/Register ") + (type == PACKET_TYPE_LOGIN_SUCCESS || type == PACKET_TYPE_REGISTER_SUCCESS ? "successful" : "failed");
                break;
        }

        {
            std::lock_guard<std::mutex> lock(messagesMutex);
            pendingMessages.push_back(packet);
        }
    }
}

bool NetworkManager::SendLoginRequest(const std::string& username, const std::string& password)
{
    if (!isConnected) return false;

    std::string packet = std::to_string(PACKET_TYPE_LOGIN) + " " + username + " " + password;
    return send(clientSocket, packet.c_str(), packet.length(), 0) != SOCKET_ERROR;
}

bool NetworkManager::SendRegisterRequest(const std::string& username, const std::string& password)
{
    if (!isConnected) return false;

    std::string packet = std::to_string(PACKET_TYPE_REGISTER) + " " + username + " " + password;
    return send(clientSocket, packet.c_str(), packet.length(), 0) != SOCKET_ERROR;
}

bool NetworkManager::SendChatMessage(const std::string& username, const std::string& message)
{
    if (!isConnected) return false;

    std::string packet = std::to_string(PACKET_TYPE_CHAT) + " " + username + " " + message;
    return send(clientSocket, packet.c_str(), packet.length(), 0) != SOCKET_ERROR;
}

std::vector<ChatPacket> NetworkManager::GetPendingMessages()
{
    std::lock_guard<std::mutex> lock(messagesMutex);
    std::vector<ChatPacket> messages = pendingMessages;
    pendingMessages.clear();
    return messages;
}