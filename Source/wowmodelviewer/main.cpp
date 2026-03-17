#include "app.h"
#include <windows.h>

// tell wxwidgets which class is our app
IMPLEMENT_APP_NO_MAIN(WowModelViewApp)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	return wxEntry(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
