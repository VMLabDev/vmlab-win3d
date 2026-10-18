/* Window-alpha repro for docs/present.md (the translucent Blender window).
 *
 *   gl_alpha.exe <seconds> <out.txt> [alpha]
 *
 * Opens a solid magenta GDI window, then an OpenGL window on top of it at the
 * same place, with a pixel format that has 8 alpha bits. Every frame clears
 * to RGB (204, 51, 51) with the given alpha (default 0) and swaps. After a
 * second it reads the composed desktop at the window's centre with
 * GetPixel on the screen DC. Windows presents a GL window opaque unless the
 * application asks for transparency through DWM, so the pixel must be the
 * clear colour. If the framebuffer's alpha reaches the compositor, the
 * magenta shows through. "result: PASS" when every sample is within 3 of
 * the clear colour. */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
   if (m == WM_ERASEBKGND && GetPropW(h, L"magenta")) {
      RECT r;
      HBRUSH b = CreateSolidBrush(RGB(255, 0, 255));
      GetClientRect(h, &r);
      FillRect((HDC)w, &r, b);
      DeleteObject(b);
      return 1;
   }
   return DefWindowProcW(h, m, w, l);
}

static void pump(void)
{
   MSG msg;
   while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
   }
}

int wmain(int argc, wchar_t **argv)
{
   if (argc < 3) {
      fprintf(stderr, "usage: gl_alpha <seconds> <out.txt> [alpha]\n");
      return 2;
   }
   double seconds = _wtof(argv[1]);
   FILE *out = _wfopen(argv[2], L"w");
   float alpha = argc > 3 ? (float)_wtof(argv[3]) : 0.0f;
   if (!out)
      return 2;
   SetProcessDPIAware();

   WNDCLASSW wc = {0};
   wc.lpfnWndProc = wndproc;
   wc.hInstance = GetModuleHandleW(NULL);
   wc.lpszClassName = L"gl_alpha";
   wc.style = CS_OWNDC;
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   RegisterClassW(&wc);
   const int X = 100, Y = 100, W = 400, H = 300;
   HWND back = CreateWindowExW(0, L"gl_alpha", L"gl_alpha backdrop", WS_POPUP | WS_VISIBLE,
                               X - 40, Y - 40, W + 80, H + 80, NULL, NULL, wc.hInstance, NULL);
   SetPropW(back, L"magenta", (HANDLE)1);
   InvalidateRect(back, NULL, TRUE);
   HWND hwnd = CreateWindowExW(0, L"gl_alpha", L"gl_alpha", WS_POPUP | WS_VISIBLE,
                               X, Y, W, H, NULL, NULL, wc.hInstance, NULL);
   SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
   pump();

   HDC dc = GetDC(hwnd);
   PIXELFORMATDESCRIPTOR pfd = {0};
   pfd.nSize = sizeof pfd;
   pfd.nVersion = 1;
   pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
   pfd.iPixelType = PFD_TYPE_RGBA;
   pfd.cColorBits = 32;
   pfd.cAlphaBits = 8;
   pfd.cDepthBits = 24;
   int pf = ChoosePixelFormat(dc, &pfd);
   if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
      fprintf(out, "error: pixel format\nresult: FAIL\n");
      return 1;
   }
   DescribePixelFormat(dc, pf, sizeof pfd, &pfd);
   HGLRC rc = wglCreateContext(dc);
   if (!rc || !wglMakeCurrent(dc, rc)) {
      fprintf(out, "error: context\nresult: FAIL\n");
      return 1;
   }
   fprintf(out, "renderer: %s\n", (const char *)glGetString(GL_RENDERER));
   fprintf(out, "pixel_format: %d color=%d alpha=%d\n", pf, pfd.cColorBits, pfd.cAlphaBits);
   fprintf(out, "clear: 204 51 51 alpha=%.2f\n", alpha);

   DWORD t0 = GetTickCount();
   int frames = 0, samples = 0, bad = 0;
   DWORD next_sample = t0 + 1000;
   while (GetTickCount() - t0 < (DWORD)(seconds * 1000)) {
      pump();
      glClearColor(204 / 255.0f, 51 / 255.0f, 51 / 255.0f, alpha);
      glClear(GL_COLOR_BUFFER_BIT);
      SwapBuffers(dc);
      frames++;
      if (GetTickCount() >= next_sample) {
         next_sample += 500;
         HDC sdc = GetDC(NULL);
         COLORREF c = GetPixel(sdc, X + W / 2, Y + H / 2);
         ReleaseDC(NULL, sdc);
         int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
         int ok = abs(r - 204) <= 3 && abs(g - 51) <= 3 && abs(b - 51) <= 3;
         samples++;
         bad += !ok;
         if (samples <= 3 || !ok)
            fprintf(out, "sample %d: %d %d %d %s\n", samples, r, g, b, ok ? "opaque" : "NOT the clear colour");
      }
   }
   fprintf(out, "frames: %d\nsamples: %d\nbad_samples: %d\n", frames, samples, bad);
   fprintf(out, "result: %s\n", samples && !bad ? "PASS" : "FAIL");
   fclose(out);
   wglMakeCurrent(NULL, NULL);
   wglDeleteContext(rc);
   ReleaseDC(hwnd, dc);
   DestroyWindow(hwnd);
   DestroyWindow(back);
   return samples && !bad ? 0 : 1;
}
