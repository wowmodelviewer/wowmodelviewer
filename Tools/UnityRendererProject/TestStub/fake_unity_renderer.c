/*
 * fake_unity_renderer.c
 *
 * Tiny stand-in for the Unity standalone player, honouring the same embedding
 * contract the WMV UnityRendererHost relies on:
 *
 *   UnityRenderer.exe -parentHWND <decimal hwnd> [delayed] [-logFile <path>]
 *
 *   - creates its window as a WS_CHILD of the given parent HWND
 *   - does NOT resize itself; the host resizes it (MoveWindow) on pane resize
 *   - exits its message loop when it receives WM_CLOSE (the host's polite shutdown)
 *
 * Purpose: verify the WMV-side embedding (launch / dock / resize / shutdown /
 * process lifetime) without a Unity install. It paints a gradient + label so an
 * embedded, live-resizing child is visually obvious.
 *
 * Build (any MSVC x64/x86 developer prompt):
 *   cl fake_unity_renderer.c user32.lib gdi32.lib shell32.lib /Fe:UnityRenderer.exe
 *
 * Then place UnityRenderer.exe at tools\unity-renderer\ next to wowmodelviewer.exe.
 * Never commit the built exe.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);

      /* vertical blue->dark gradient so resizes are visually obvious */
      int h = rc.bottom > 0 ? rc.bottom : 1;
      for (int y = 0; y < rc.bottom; y++)
      {
        int shade = 40 + (150 * (h - y)) / h;
        HBRUSH brush = CreateSolidBrush(RGB(20, 30, shade));
        RECT line = { 0, y, rc.right, y + 1 };
        FillRect(dc, &line, brush);
        DeleteObject(brush);
      }

      char text[128];
      _snprintf(text, sizeof(text), "UnityRenderer stub  %ldx%ld", rc.right, rc.bottom);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(230, 230, 240));
      DrawTextA(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE:
      InvalidateRect(hwnd, NULL, TRUE);
      return 0;
    case WM_CLOSE:      /* the host's polite shutdown path */
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show)
{
  HWND parent = NULL;
  FILE * log = NULL;

  int argc = 0;
  LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc - 1; i++)
  {
    if (wcscmp(argv[i], L"-parentHWND") == 0)
      parent = (HWND)(UINT_PTR)_wcstoui64(argv[i + 1], NULL, 10);
    else if (wcscmp(argv[i], L"-logFile") == 0)
      log = _wfopen(argv[i + 1], L"w");
  }
  LocalFree(argv);

  if (log)
  {
    fprintf(log, "fake_unity_renderer: parentHWND=%p\n", (void *)parent);
    fflush(log);
  }

  WNDCLASSA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "FakeUnityRenderer";
  RegisterClassA(&wc);

  DWORD style;
  int w = 640, h = 480;
  if (parent && IsWindow(parent))
  {
    style = WS_CHILD | WS_VISIBLE;
    RECT rc;
    if (GetClientRect(parent, &rc))
    {
      w = rc.right > 0 ? rc.right : w;
      h = rc.bottom > 0 ? rc.bottom : h;
    }
  }
  else
  {
    style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;  /* standalone fallback for manual runs */
    parent = NULL;
  }

  HWND hwnd = CreateWindowA("FakeUnityRenderer", "UnityRenderer stub", style,
                            0, 0, w, h, parent, NULL, inst, NULL);
  if (!hwnd)
  {
    if (log) { fprintf(log, "CreateWindow failed (%lu)\n", GetLastError()); fclose(log); }
    return 1;
  }

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  if (log)
  {
    fprintf(log, "fake_unity_renderer: clean exit\n");
    fclose(log);
  }
  return 0;
}
