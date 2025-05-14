#include "Server.h"

int main()
{
    Server server;
    if (!server.Init(8888)) {
        return -1;
    }

    server.Run();
    return 0;
}