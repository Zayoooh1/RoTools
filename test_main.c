#include <windows.h>
#include <stdio.h>
#include <WebView2.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    PWSTR versionInfo = NULL;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(NULL, &versionInfo);
    
    char msg[256];
    sprintf(msg, "WebView2 Version check: 0x%X", hr);
    MessageBoxA(NULL, msg, "Test", MB_OK);
    return 0;
}
