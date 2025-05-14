#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Application.h"

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    Application app;
    if (!app.Init(hInstance))
        return 1;

    int result = app.Run();
    app.Shutdown();
    return result;
}

int main()
{
    Application app;
    if (!app.Init(GetModuleHandle(NULL)))
        return -1;

    return app.Run();
}