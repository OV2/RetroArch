/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2012 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

// Win32/WGL context.

#include "../../driver.h"
#include "../gfx_context.h"
#include "../gl_common.h"
#include "../gfx_common.h"
#include "../../media/resource.h"
#include <windows.h>
#include <commdlg.h>
#include <string.h>
#include <process.h>

#define IDI_ICON 1
#define MAX_MONITORS 9

static HWND g_hwnd;
static HGLRC g_hrc;
static HDC g_hdc;
static HMONITOR g_last_hm;
static HMONITOR g_all_hms[MAX_MONITORS];
static unsigned g_num_mons;

static HANDLE g_evt_gui_thread_ready;
static HANDLE g_evt_gui_thread_quit;

static bool g_quit;
static bool g_inited;
static unsigned g_interval;

static unsigned g_resize_width;
static unsigned g_resize_height;
static bool g_resized;
static bool g_fullscreen;

static bool g_restore_desktop;

static void monitor_info(MONITORINFOEX *mon, HMONITOR *hm_to_use);
static void gfx_ctx_get_video_size(unsigned *width, unsigned *height);
static void gfx_ctx_destroy(void);

static BOOL (APIENTRY *p_swap_interval)(int);

static void setup_pixel_format(HDC hdc)
{
   PIXELFORMATDESCRIPTOR pfd = {0};
   pfd.nSize        = sizeof(PIXELFORMATDESCRIPTOR);
   pfd.nVersion     = 1;
   pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
   pfd.iPixelType   = PFD_TYPE_RGBA;
   pfd.cColorBits   = 32;
   pfd.cDepthBits   = 0;
   pfd.cStencilBits = 0;
   pfd.iLayerType   = PFD_MAIN_PLANE;

   SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
}

static void create_gl_context(HWND hwnd)
{
   if (g_hrc)
   {
      if (wglMakeCurrent(g_hdc, g_hrc))
         g_inited = true;
      else
         g_quit = true;
   }
   else
      g_quit = true;
}

static bool BrowseForFile(char *filename)
{
   OPENFILENAME ofn;
	memset((void *)&ofn, 0, sizeof(OPENFILENAME));

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = g_hwnd;
	ofn.lpstrFilter = "Shader Files\0*.shader;*.cg;*.cgp\0All Files\0*.*\0\0";
	ofn.lpstrFile = filename;
	ofn.lpstrTitle = "Select Shader";
	ofn.lpstrDefExt = "cg";
	ofn.nMaxFile = PATH_MAX;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	if(GetOpenFileName(&ofn)) {
		return true;
	}
   return false;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
      WPARAM wparam, LPARAM lparam)
{
   switch (message)
   {
      case WM_SYSCOMMAND:
         // Prevent screensavers, etc, while running.
         switch (wparam)
         {
            case SC_SCREENSAVE:
            case SC_MONITORPOWER:
               return 0;
         }
         break;

      case WM_SYSKEYDOWN:
         switch (wparam)
         {
            case VK_F10:
            case VK_MENU:
            case VK_RSHIFT:
               return 0;
         }
         break;

      case WM_CREATE:
         g_hdc = GetDC(hwnd);
         setup_pixel_format(g_hdc);

         g_hrc = wglCreateContext(g_hdc);
         SetEvent(g_evt_gui_thread_ready);
         return 0;

      case WM_CLOSE:
         PostQuitMessage(1);
         break;

      case WM_SIZE:
         // Do not send resize message if we minimize.
         if (wparam != SIZE_MAXHIDE && wparam != SIZE_MINIMIZED)
         {
            g_resize_width  = LOWORD(lparam);
            g_resize_height = HIWORD(lparam);
            g_resized = true;
         }
         return 0;
      case WM_COMMAND:
         switch(wparam & 0xffff)
         {
            case ID_M_TOGGLEPAUSE:
               network_cmd_send("PAUSE_TOGGLE");
               break;
            case ID_M_TOGGLEFF:
               network_cmd_send("FAST_FORWARD");
               break;
            case ID_M_FRAMEADVANCE:
               network_cmd_send("FRAMEADVANCE");
               break;
            case ID_M_RESET:
               network_cmd_send("RESET");
               break;
            case ID_M_QUIT:
               network_cmd_send("QUIT");
               break;
            case ID_M_INCSTATE:
               network_cmd_send("STATE_SLOT_PLUS");
               break;
            case ID_M_DECSTATE:
               network_cmd_send("STATE_SLOT_MINUS");
               break;
            case ID_M_LOADSTATE:
               network_cmd_send("LOAD_STATE");
               break;
            case ID_M_SAVESTATE:
               network_cmd_send("SAVE_STATE");
               break;
            case ID_M_TOGGLEFS:
               network_cmd_send("FULLSCREEN_TOGGLE");
               break;
            case ID_M_SCREENSHOT:
               network_cmd_send("SCREENSHOT");
               break;
            case ID_M_TOGGLEMUTE:
               network_cmd_send("MUTE");
               break;
            case ID_M_NEXTSHADER:
               network_cmd_send("SHADER_NEXT");
               break;
            case ID_M_PREVSHADER:
               network_cmd_send("SHADER_PREV");
               break;
            case ID_M_SELECTSHADER:
               {
                  char shader_file[PATH_MAX] = {0};
                  if(BrowseForFile(shader_file))
                  {
                     char command[12 + PATH_MAX];
                     sprintf(command,"SET_SHADER %s",shader_file);
                     network_cmd_send(command);
                  }
               }
               break;
         }
         break;
   }

   return DefWindowProc(hwnd, message, wparam, lparam);
}

static void gfx_ctx_swap_interval(unsigned interval)
{
   g_interval = interval;

   if (g_hrc && p_swap_interval)
   {
      RARCH_LOG("[WGL]: wglSwapInterval(%u)\n", g_interval);
      if (!p_swap_interval(g_interval))
         RARCH_WARN("[WGL]: wglSwapInterval() failed.\n");
   }
}

static void gfx_ctx_check_window(bool *quit,
      bool *resize, unsigned *width, unsigned *height, unsigned frame_count)
{
   (void)frame_count;

   *quit = g_quit;
   if (g_resized)
   {
      *resize = true;
      *width  = g_resize_width;
      *height = g_resize_height;
      g_resized = false;
   }
}

static void gfx_ctx_swap_buffers(void)
{
   SwapBuffers(g_hdc);
}

static void gfx_ctx_set_resize(unsigned width, unsigned height)
{
   (void)width;
   (void)height;
}

static void gfx_ctx_update_window_title(bool reset)
{
   if (reset)
      gfx_window_title_reset();

   char buf[128];
   if (gfx_get_fps(buf, sizeof(buf), false))
      SetWindowText(g_hwnd, buf);
}

static void gfx_ctx_get_video_size(unsigned *width, unsigned *height)
{
   if (!g_hwnd)
   {
      HMONITOR hm_to_use = NULL;
      MONITORINFOEX current_mon;

      monitor_info(&current_mon, &hm_to_use);
      RECT mon_rect = current_mon.rcMonitor;
      *width  = mon_rect.right - mon_rect.left;
      *height = mon_rect.bottom - mon_rect.top;
   }
   else
   {
      *width  = g_resize_width;
      *height = g_resize_height;
   }
}

static BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
   g_all_hms[g_num_mons++] = hMonitor;
   return TRUE;
}

static bool gfx_ctx_init(void)
{
   if (g_inited)
      return false;

   g_quit = false;
   g_restore_desktop = false;

   g_num_mons = 0;
   EnumDisplayMonitors(NULL, NULL, monitor_enum_proc, 0);

   WNDCLASSEX wndclass = {0};
   wndclass.cbSize = sizeof(wndclass);
   wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
   wndclass.lpfnWndProc = WndProc;
   wndclass.hInstance = GetModuleHandle(NULL);
   wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
   wndclass.lpszClassName = "RetroArch";
   wndclass.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON));
   wndclass.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, 0);

   if (!RegisterClassEx(&wndclass))
      return false;

   if (!(g_evt_gui_thread_ready = CreateEvent(NULL,false,false,NULL)))
      return false;
   
   if (!(g_evt_gui_thread_quit = CreateEvent(NULL,false,false,NULL)))
   {
      CloseHandle(g_evt_gui_thread_ready);
      return false;
   }

   return true;
}

static bool set_fullscreen(unsigned width, unsigned height, char *dev_name)
{
   DEVMODE devmode;
   memset(&devmode, 0, sizeof(devmode));
   devmode.dmSize       = sizeof(DEVMODE);
   devmode.dmPelsWidth  = width;
   devmode.dmPelsHeight = height;
   devmode.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

   RARCH_LOG("[WGL]: Setting fullscreen to %ux%u on device %s.\n", width, height, dev_name);
   return ChangeDisplaySettingsEx(dev_name, &devmode, NULL, CDS_FULLSCREEN, NULL) == DISP_CHANGE_SUCCESSFUL;
}

static void show_cursor(bool show)
{
   if (show)
      while (ShowCursor(TRUE) < 0);
   else
      while (ShowCursor(FALSE) >= 0);
}

static void monitor_info(MONITORINFOEX *mon, HMONITOR *hm_to_use)
{
   if (!g_last_hm)
      g_last_hm = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
   *hm_to_use = g_last_hm;

   unsigned fs_monitor = g_settings.video.monitor_index;
   if (fs_monitor && fs_monitor <= g_num_mons && g_all_hms[fs_monitor - 1])
      *hm_to_use = g_all_hms[fs_monitor - 1];

   memset(mon, 0, sizeof(*mon));
   mon->cbSize = sizeof(MONITORINFOEX);
   GetMonitorInfo(*hm_to_use, (MONITORINFO*)mon);
}

unsigned __stdcall gui_thread(void * pParam)
{
   DWORD style;

   HMONITOR hm_to_use = NULL;
   MONITORINFOEX current_mon;
   RECT rect   = {0};

   monitor_info(&current_mon, &hm_to_use);
   RECT mon_rect = current_mon.rcMonitor;

   unsigned int window_width, window_height;

   bool windowed_full = g_settings.video.windowed_fullscreen;
   if (g_fullscreen)
   {
      if (windowed_full)
      {
         style = WS_EX_TOPMOST | WS_POPUP;
         g_resize_width  = window_width = mon_rect.right - mon_rect.left;
         g_resize_height = window_height = mon_rect.bottom - mon_rect.top;
      }
      else
      {
         style = WS_POPUP | WS_VISIBLE;

         if (!set_fullscreen(g_resize_width, g_resize_height, current_mon.szDevice))
            goto error;

         window_width = g_resize_width;
         window_height = g_resize_height;

         // display settings might have changed, get new coordinates
         GetMonitorInfo(hm_to_use, (MONITORINFO*)&current_mon);
         mon_rect = current_mon.rcMonitor;
         g_restore_desktop = true;
      }
   }
   else
   {
      style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
      rect.right  = g_resize_width;
      rect.bottom = g_resize_height;
      AdjustWindowRect(&rect, style, FALSE);
      window_width  = rect.right - rect.left;
      window_height = rect.bottom - rect.top;
   }

   g_hwnd = CreateWindowEx(0, "RetroArch", "RetroArch", style,
         g_fullscreen ? mon_rect.left : CW_USEDEFAULT,
         g_fullscreen ? mon_rect.top  : CW_USEDEFAULT,
         window_width, window_height,
         NULL, NULL, NULL, NULL);

   if (!g_hwnd)
      goto error;

   gfx_ctx_update_window_title(true);

   if(!g_fullscreen)
   {
      SetMenu(g_hwnd,LoadMenu(GetModuleHandle(NULL),MAKEINTRESOURCE(IDR_MENU)));
      RECT rcTemp = {0, 0, window_height, 0x7FFF}; // 0x7FFF="Infinite" height
      SendMessage(g_hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&rcTemp); // recalculate margin, taking possible menu wrap into account
      window_height += rcTemp.top + rect.top; // extend by new top margin and substract previous margin
      SetWindowPos(g_hwnd, NULL, 0, 0, window_width, window_height, SWP_NOMOVE);
   }

   if (!g_fullscreen || windowed_full)
   {
      ShowWindow(g_hwnd, SW_RESTORE);
      UpdateWindow(g_hwnd);
      SetForegroundWindow(g_hwnd);
      SetFocus(g_hwnd);
   }

   show_cursor(!g_fullscreen);

   MSG msg;
   while (GetMessage(&msg, NULL, 0, 0))
   {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }

   ReleaseDC(g_hwnd, g_hdc);
   g_hdc = NULL;
   g_hwnd = NULL;
   g_quit = true;
   SetEvent(g_evt_gui_thread_quit);
   _endthreadex(0);
   return 0;

error:
   g_hwnd = NULL;
   g_quit = true;
   SetEvent(g_evt_gui_thread_ready);
   SetEvent(g_evt_gui_thread_quit);
   _endthreadex(1);
   return 1;
}

static bool gfx_ctx_set_video_mode(
      unsigned width, unsigned height,
      bool fullscreen)
{
   g_resize_width  = width;
   g_resize_height = height;
   g_fullscreen = fullscreen;

   ResetEvent(g_evt_gui_thread_ready);
   ResetEvent(g_evt_gui_thread_quit);

   _beginthreadex(NULL, 0, gui_thread, NULL, 0, NULL);

   // Wait until GL context is created (or failed to do so ...)
   WaitForSingleObject(g_evt_gui_thread_ready,INFINITE);

   create_gl_context(g_hwnd);

   if (g_quit)
      goto error;

   p_swap_interval = (BOOL (APIENTRY *)(int))wglGetProcAddress("wglSwapIntervalEXT");

   gfx_ctx_swap_interval(g_interval);

   driver.display_type  = RARCH_DISPLAY_WIN32;
   driver.video_display = 0;
   driver.video_window  = (uintptr_t)g_hwnd;

   return true;

error:
   gfx_ctx_destroy();
   return false;
}

static void gfx_ctx_destroy(void)
{
   if (g_hrc)
   {
      wglMakeCurrent(NULL, NULL);
      wglDeleteContext(g_hrc);
      g_hrc = NULL;
   }

   if (g_hwnd)
   {
      g_last_hm = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
      SendMessage(g_hwnd,WM_CLOSE,0,0);
      WaitForSingleObject(g_evt_gui_thread_quit,INFINITE);
      UnregisterClass("RetroArch", GetModuleHandle(NULL));
   }

   if (g_evt_gui_thread_ready)
   {
      CloseHandle(g_evt_gui_thread_ready);
      g_evt_gui_thread_ready = NULL;
   }
   
   if (g_evt_gui_thread_quit)
   {
      CloseHandle(g_evt_gui_thread_quit);
      g_evt_gui_thread_quit = NULL;
   }

   if (g_restore_desktop)
   {
      MONITORINFOEX current_mon;
      memset(&current_mon, 0, sizeof(current_mon));
      current_mon.cbSize = sizeof(MONITORINFOEX);
      GetMonitorInfo(g_last_hm, (MONITORINFO*)&current_mon);
      ChangeDisplaySettingsEx(current_mon.szDevice, NULL, NULL, 0, NULL);
      g_restore_desktop = false;
   }

   g_inited = false;
}

static void gfx_ctx_input_driver(const input_driver_t **input, void **input_data)
{
   void *dinput = input_dinput.init();
   *input       = dinput ? &input_dinput : NULL;
   *input_data  = dinput;
}

static bool gfx_ctx_has_focus(void)
{
   if (!g_inited)
      return false;

   return GetFocus() == g_hwnd;
}

static gfx_ctx_proc_t gfx_ctx_get_proc_address(const char *symbol)
{
   return (gfx_ctx_proc_t)wglGetProcAddress(symbol);
}

static bool gfx_ctx_bind_api(enum gfx_ctx_api api)
{
   return api == GFX_CTX_OPENGL_API;
}

static bool gfx_ctx_init_egl_image_buffer(const video_info_t *video)
{
   return false;
}

static bool gfx_ctx_write_egl_image(const void *frame, unsigned width, unsigned height, unsigned pitch, bool rgb32, unsigned index, void **image_handle)
{
   return false;
}

const gfx_ctx_driver_t gfx_ctx_wgl = {
   gfx_ctx_init,
   gfx_ctx_destroy,
   gfx_ctx_bind_api,
   gfx_ctx_swap_interval,
   gfx_ctx_set_video_mode,
   gfx_ctx_get_video_size,
   NULL,
   gfx_ctx_update_window_title,
   gfx_ctx_check_window,
   gfx_ctx_set_resize,
   gfx_ctx_has_focus,
   gfx_ctx_swap_buffers,
   gfx_ctx_input_driver,
   gfx_ctx_get_proc_address,
   gfx_ctx_init_egl_image_buffer,
   gfx_ctx_write_egl_image,
   NULL,
   "wgl",
};

