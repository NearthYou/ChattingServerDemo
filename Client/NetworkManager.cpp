#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "NetworkManager.h"
#include "Common/PacketDefine.h"

bool NetworkManager::Init()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    ClientSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    serverAddr.sin_addr.s_addr = inet_addr("218.239.173.107");

    if (connect(ClientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "[Network] Failed to connect.\n";
        return false;
    }

    u_long mode = 1;
    ioctlsocket(ClientSocket, FIONBIO, &mode);

    std::cout << "[Network] Connected to server.\n";

    Running = true;
    RecvThread = std::thread(&NetworkManager::RecvLoop, this);

    return true;
}

void NetworkManager::Shutdown()
{
    Running = false;

    if (RecvThread.joinable())
        RecvThread.join();

    if (ClientSocket != INVALID_SOCKET)
    {
        closesocket(ClientSocket);
        ClientSocket = INVALID_SOCKET;
    }

    WSACleanup();
}

void NetworkManager::SendLogin(const std::string& nickname)
{
    PacketHeader header;
    header.size = sizeof(PacketHeader) + static_cast<uint16_t>(nickname.size());
    header.type = PACKET_LOGIN;

    std::vector<char> buffer(header.size);
    memcpy(buffer.data(), &header, sizeof(PacketHeader));
    memcpy(buffer.data() + sizeof(PacketHeader), nickname.data(), nickname.size());

    send(ClientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);

    SetNickname(nickname);
}

void NetworkManager::SendChatMessage(const std::string& message)
{
    PacketHeader header;
    header.size = sizeof(PacketHeader) + static_cast<uint16_t>(message.size());
    header.type = PACKET_CHAT;

    std::vector<char> buffer(header.size);
    memcpy(buffer.data(), &header, sizeof(PacketHeader));
    memcpy(buffer.data() + sizeof(PacketHeader), message.data(), message.size());

    send(ClientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);
}

void NetworkManager::RecvLoop()
{
    while (Running)
    {
        char buffer[512] = {};
        int recvLen = recv(ClientSocket, buffer, sizeof(buffer), 0);

        if (recvLen > 0)
        {
            PacketHeader header;
            memcpy(&header, buffer, sizeof(PacketHeader));

            const char* body = buffer + sizeof(PacketHeader);
            int bodySize = header.size - sizeof(PacketHeader);

            if (header.type == PACKET_CHAT)
            {
                std::string payload(body, bodySize);

                size_t sep = payload.find(':');
                if (sep != std::string::npos)
                {
                    std::string sender = payload.substr(0, sep);
                    std::string message = payload.substr(sep + 1);

                    bool isMine = (sender == Nickname);

                    std::lock_guard<std::mutex> lock(Mutex);
                    PendingMessages.push_back({ sender, message, isMine });
                }
            }
            else if (header.type == PACKET_LOGIN_OK)
            {
                std::cout << "[Network] Login Success!\n";
            }
            else if (header.type == PACKET_LOGIN_FAIL)
            {
                std::cout << "[Network] Login Failed.\n";
            }
        }
        else if (recvLen == 0)
        {
            std::cout << "[Network] Server disconnected.\n";
            Running = false;
            break;
        }
        else
        {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else
            {
                std::cout << "[Network] recv error: " << error << "\n";
                Running = false;
                break;
            }
        }
    }
}

std::vector<ChatPacket> NetworkManager::GetPendingMessages()
{
    std::lock_guard<std::mutex> lock(Mutex);
    auto result = std::move(PendingMessages);
    PendingMessages.clear();
    return result;
}