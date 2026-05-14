#include "app_settings.h"
#include "fishe_xbmc.h"
#include "glad/include/glad/glad.h"
#include "logger.h"
#include "settings_dialog.h"
#include "spout/SpoutSender.h"
#include "wasapi_capture.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <chrono>
#include <timeapi.h>
#include <memory>
#include <string>
#include <thread>

namespace
{
GLFWwindow* g_window = nullptr;
AppSettings g_settings;
bool g_pendingApply = false;
AppSettings g_pendingSettings;
std::unique_ptr<CFishBMC> g_visualizer;
WasapiCapture g_audioCapture;
SpoutSender g_spout;
bool g_helpVisible = false;
GLuint g_fontBase = 0;
HFONT g_font = nullptr;
int g_fontCharWidth = 0;
int g_fontCharHeight = 0;
std::string g_osdMessage;
std::chrono::steady_clock::time_point g_osdUntil;
std::chrono::steady_clock::time_point g_cursorUntil;
bool g_cursorVisible = true;
bool g_spoutReady = false;
bool g_paused = false;
bool g_openSettings = false;
bool g_fullscreen = false;
bool g_alwaysOnTop = false;
int g_windowedX = 100;
int g_windowedY = 100;
int g_windowedWidth = 1280;
int g_windowedHeight = 720;
int g_lastFramebufferWidth = 0;
int g_lastFramebufferHeight = 0;
int g_osdTextW = 0;
int g_osdTextH = 0;
std::chrono::steady_clock::time_point g_lastResizeChange;
std::chrono::steady_clock::time_point g_messageUntil;

extern "C"
{
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int clamp_renderer_dimension(int value, bool alignWidth)
{
  value = std::clamp(value, 16, 2048);
  if (alignWidth)
    value = std::max(16, value - (value % 4));
  return value;
}

double quality_scale(int quality)
{
  switch (std::clamp(quality, 0, 3))
  {
    case 0:
      return 0.50;
    case 1:
      return 0.67;
    case 2:
      return 0.85;
    case 3:
      return 1.0;
  }
  return 0.85;
}

std::pair<int, int> renderer_size_from_framebuffer(int framebufferWidth, int framebufferHeight)
{
  double scale = quality_scale(g_settings.quality);
  int width = clamp_renderer_dimension(static_cast<int>(framebufferWidth * scale), true);
  int height = clamp_renderer_dimension(static_cast<int>(framebufferHeight * scale), false);
  return {width, height};
}

void set_title_message(const std::string& message, int milliseconds = 2000)
{
  if (!g_window)
    return;

  glfwSetWindowTitle(g_window, message.empty() ? "fische" : ("fische - " + message).c_str());
  g_messageUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
}

void show_osd(const std::string& message, int milliseconds = 3000)
{
  g_osdMessage = message;
  g_osdUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

  HDC hdc = wglGetCurrentDC();
  HFONT old = (HFONT)SelectObject(hdc, g_font);
  SIZE sz = {};
  GetTextExtentPoint32A(hdc, message.c_str(), static_cast<int>(message.size()), &sz);
  SelectObject(hdc, old);
  g_osdTextW = sz.cx;
  g_osdTextH = sz.cy;
}

void update_spout_state()
{
  if (g_settings.spoutEnabled)
  {
    if (!g_spoutReady)
    {
      g_spout.SetSenderName("fische");
      g_spoutReady = true;
    }
    if (g_visualizer)
      g_visualizer->Sender = &g_spout;
  }
  else
  {
    if (g_visualizer)
      g_visualizer->Sender = nullptr;
    if (g_spoutReady)
    {
      g_spout.ReleaseSender();
      g_spoutReady = false;
    }
  }
}

void recreate_visualizer()
{
  if (!g_window)
    return;

  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(g_window, &framebufferWidth, &framebufferHeight);
  if (framebufferWidth <= 0 || framebufferHeight <= 0)
    return;

  auto [renderWidth, renderHeight] = renderer_size_from_framebuffer(framebufferWidth, framebufferHeight);

  g_audioCapture.Stop();
  {
    g_audioCapture.LockVisualizer();
    g_visualizer.reset();
    g_audioCapture.UnlockVisualizer();
  }

  g_visualizer = std::make_unique<CFishBMC>(static_cast<uint32_t>(renderWidth),
                                           static_cast<uint32_t>(renderHeight),
                                           g_settings.quality,
                                           g_settings.nervousMode,
                                           g_settings.useFilePersistence);
  g_visualizer->window = g_window;
  g_visualizer->Sender = g_settings.spoutEnabled ? &g_spout : nullptr;

  if (!g_visualizer->Start(2, 44100, 32, ""))
  {
    set_title_message("renderer failed");
    return;
  }

  g_audioCapture.Start(g_visualizer.get(), g_settings.audioDevice);
  g_audioCapture.SetSensitivity(g_settings.audioSensitivity);
  update_spout_state();
  g_lastFramebufferWidth = framebufferWidth;
  g_lastFramebufferHeight = framebufferHeight;
}

void refresh_cursor_visibility()
{
  if (!g_fullscreen)
  {
    if (!g_cursorVisible)
    {
      glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      g_cursorVisible = true;
    }
    return;
  }

  auto now = std::chrono::steady_clock::now();
  if (g_cursorVisible && now >= g_cursorUntil)
  {
    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    g_cursorVisible = false;
  }
}

void apply_settings(const AppSettings& settings)
{
  g_settings = settings;
  if (g_visualizer)
    g_visualizer->SetNervousMode(g_settings.nervousMode);
  glfwSwapInterval(g_settings.vsyncEnabled ? 1 : 0); // glfwSwapInterval(g_settings.fpsLimit <= 0 ? 1 : 0);
  g_audioCapture.SetSensitivity(g_settings.audioSensitivity);
  g_alwaysOnTop = g_settings.alwaysOnTop ? true : false;
  HWND hwnd = glfwGetWin32Window(g_window);
  SetWindowPos(hwnd, g_settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  if (!g_fullscreen)
    glfwSetWindowSize(g_window, g_settings.windowWidth, g_settings.windowHeight);
  recreate_visualizer();
}

void init_gl_font()
{
  HDC hdc = wglGetCurrentDC();
  if (!hdc)
    return;
  HFONT font = CreateFontW(28, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Times New Roman");
  HFONT old = (HFONT)SelectObject(hdc, font);
  g_fontBase = glGenLists(96);
  wglUseFontBitmapsW(hdc, 32, 96, g_fontBase);
  SIZE sz = {};
  GetTextExtentPoint32A(hdc, "M", 1, &sz);
  g_fontCharWidth = sz.cx;
  g_fontCharHeight = sz.cy;
  SelectObject(hdc, old);
  g_font = font;
  //DeleteObject(font);
}

void draw_help_screen(int fw, int fh)
{
  // Save all relevant GL state
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, fw, fh, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  // Disable anything that could interfere
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Semi-transparent dark background
  glColor4f(0.0f, 0.0f, 0.0f, 0.65f);
  glBegin(GL_QUADS);
    glVertex2i(0,  0);
    glVertex2i(fw, 0);
    glVertex2i(fw, fh);
    glVertex2i(0,  fh);
  glEnd();

  // Text
  const char* lines[] = {
    "Help - Keyboard/Mouse Shortcuts",
    "",
    "Esc            Exit",
    "F1/DLClick     Show / hide help screen",
    "F              Toggle fullscreen",
    "T              Toggle always on top",
    "N              Toggle nervous mode",
    "O/RClick       Open settings",
    "P              Pause / unpause",
    "Z              Toggle Spout output",
    "R              Reset audio sensitivity",
    "Up             Increase audio sensitivity",
    "Down           Decrease audio sensitivity",
  };

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  int y = 80;
  for (const char* line : lines)
  {
    // glRasterPos2i fails silently if it lands outside the viewport - use 2f instead
    glRasterPos2f(60.0f, static_cast<float>(y));
    glListBase(g_fontBase - 32);
    glCallLists(static_cast<GLsizei>(strlen(line)), GL_UNSIGNED_BYTE, line);
    y += 28;
  }

  // Restore everything
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glPopAttrib();
}

void draw_osd(int fw, int fh)
{
  if (g_osdMessage.empty())
    return;
  if (std::chrono::steady_clock::now() >= g_osdUntil)
  {
    g_osdMessage.clear();
    return;
  }

  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, fw, fh, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const int padX = 20;
  const int padY = 12;
  int boxW = g_osdTextW + padX * 2;
  int boxH = g_osdTextH + padY * 2;
  int boxX = (fw - boxW) / 2;
  int boxY = fh - 80;

  // Background
  glColor4f(0.0f, 0.0f, 0.0f, 0.60f);
  glBegin(GL_QUADS);
    glVertex2i(boxX,        boxY);
    glVertex2i(boxX + boxW, boxY);
    glVertex2i(boxX + boxW, boxY + boxH);
    glVertex2i(boxX,        boxY + boxH);
  glEnd();

  // Text centered inside the box
  int textX = boxX + (boxW - g_osdTextW) / 2;
  int textY = boxY + padY + g_osdTextH - 4;  // GL raster pos is baseline

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  glRasterPos2f(static_cast<float>(textX), static_cast<float>(textY));
  glListBase(g_fontBase - 32);
  glCallLists(static_cast<GLsizei>(g_osdMessage.size()), GL_UNSIGNED_BYTE, g_osdMessage.c_str());

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glPopAttrib();
}

void toggle_fullscreen()
{
  g_fullscreen = !g_fullscreen;
  if (g_fullscreen)
  {
    glfwGetWindowPos(g_window, &g_windowedX, &g_windowedY);
    glfwGetWindowSize(g_window, &g_windowedWidth, &g_windowedHeight);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(g_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  }
  else
  {
    glfwSetWindowMonitor(g_window, nullptr, g_windowedX, g_windowedY, g_windowedWidth, g_windowedHeight, 0);
    if (g_settings.alwaysOnTop)
    {
      HWND hwnd = glfwGetWin32Window(g_window);
      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
  }
  if (!g_fullscreen)
  {
    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    g_cursorVisible = true;
  }
  else
  {
    g_cursorUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  }
}

void set_always_on_top(bool enabled)
{
  g_alwaysOnTop = enabled;
  g_settings.alwaysOnTop = enabled;
  HWND hwnd = glfwGetWin32Window(g_window);
  SetWindowPos(hwnd, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  SaveSettings(g_settings);
}

void key_callback(GLFWwindow* window, int key, int, int action, int)
{
  if (action != GLFW_PRESS)
    return;

  switch (key)
  {
    case GLFW_KEY_ESCAPE:
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      break;
    case GLFW_KEY_F1:
      g_helpVisible = !g_helpVisible;
      break;
    case GLFW_KEY_F:
      toggle_fullscreen();
      break;
    case GLFW_KEY_T:
      set_always_on_top(!g_alwaysOnTop);
      show_osd(g_alwaysOnTop ? "Always on top ON" : "Always on top OFF");
      break;
    case GLFW_KEY_N:
      g_settings.nervousMode = !g_settings.nervousMode;
      if (g_visualizer)
        g_visualizer->SetNervousMode(g_settings.nervousMode);
      SaveSettings(g_settings);
      show_osd(g_settings.nervousMode ? "Nervous mode ON" : "Nervous mode OFF");
      break;
    case GLFW_KEY_O:
      if (!g_openSettings)
      {
        g_openSettings = true;
        std::thread([]() {
          glfwGetWindowSize(g_window, &g_settings.windowWidth, &g_settings.windowHeight);
          ShowSettingsDialog(glfwGetWin32Window(g_window), g_settings, [](const AppSettings& settings) {
            g_pendingSettings = settings;
            g_pendingApply = true;
          });
          g_openSettings = false;
        }).detach();
      }
      break;
    case GLFW_KEY_P:
      g_paused = !g_paused;
      glfwSetWindowTitle(g_window, g_paused ? "fische - PAUSED" : "fische");
      break;
    case GLFW_KEY_Z:
      g_settings.spoutEnabled = !g_settings.spoutEnabled;
      update_spout_state();
      SaveSettings(g_settings);
      show_osd(g_settings.spoutEnabled ? "Spout output enabled" : "Spout output disabled");
      break;
    case GLFW_KEY_R:
      g_settings.audioSensitivity = 1.0f;
      g_audioCapture.SetSensitivity(1.0f);
      SaveSettings(g_settings);
      show_osd("Audio Sensitivity reset to 1");
      break;
    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN:
    {
      float& s = g_settings.audioSensitivity;
      float step = (s >= 1.0f && key == GLFW_KEY_UP) || (s > 1.0f && key == GLFW_KEY_DOWN)
                  ? 0.1f : 0.01f;
      s += (key == GLFW_KEY_UP ? step : -step);
      s = std::round(s * 100.0f) / 100.0f;  // always 2 decimal places
      s = std::clamp(s, 0.0f, 15.0f);
      g_audioCapture.SetSensitivity(s);
      SaveSettings(g_settings);
      char buf[32];
      snprintf(buf, sizeof(buf), "Audio Sensitivity: %.2f", s);
      show_osd(buf);
      break;
    }
  }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int)
{
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
  {
    static auto lastClick = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (now - lastClick < std::chrono::milliseconds(400))
    {
      toggle_fullscreen();
      lastClick = {};
    }
    else
    {
      lastClick = now;
    }
  }
  else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
  {
    if (!g_openSettings)
    {
      g_openSettings = true;
      std::thread([]() {
        glfwGetWindowSize(g_window, &g_settings.windowWidth, &g_settings.windowHeight);
        ShowSettingsDialog(glfwGetWin32Window(g_window), g_settings, [](const AppSettings& settings) {
          g_pendingSettings = settings;
          g_pendingApply = true;
        });
        g_openSettings = false;
      }).detach();
    }
  }
}

void cursor_pos_callback(GLFWwindow*, double, double)
{
  if (!g_fullscreen)
    return;
  glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  g_cursorVisible = true;
  g_cursorUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

void maybe_handle_resize()
{
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(g_window, &framebufferWidth, &framebufferHeight);
  if (framebufferWidth <= 0 || framebufferHeight <= 0)
    return;

  glViewport(0, 0, framebufferWidth, framebufferHeight);
  if (framebufferWidth == g_lastFramebufferWidth && framebufferHeight == g_lastFramebufferHeight)
    return;

  auto now = std::chrono::steady_clock::now();
  if (g_lastResizeChange.time_since_epoch().count() == 0)
    g_lastResizeChange = now;

  if (now - g_lastResizeChange > std::chrono::milliseconds(350))
  {
    recreate_visualizer();
    g_lastResizeChange = {};
  }
}

void enforce_frame_limit(std::chrono::steady_clock::time_point frameStart)
{
  if (g_settings.fpsLimit <= 0)
    return;

  auto targetFrameTime = std::chrono::duration<double>(1.0 / g_settings.fpsLimit);
  //auto elapsed = std::chrono::steady_clock::now() - frameStart;
  //if (elapsed < targetFrameTime)
  //  std::this_thread::sleep_for(targetFrameTime - elapsed);

  while (true)
  {
    auto elapsed = std::chrono::steady_clock::now() - frameStart;
    auto remaining = targetFrameTime - elapsed;

    // If we hit or exceeded our target time, break out and render the next frame
    if (remaining <= std::chrono::duration<double>::zero())
      break;

    // If we have more than 2 milliseconds left, sleep to save CPU power.
    // (Note: On Windows, you should also call timeBeginPeriod(1) at startup for this to be tight)
    if (remaining > std::chrono::milliseconds(2))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    else
    {
      // Less than 2ms left! sleep_for is too inaccurate now. 
      // Yield the thread briefly or spin in a tight loop for high precision.
      std::this_thread::yield(); 
    }
  }
}
}

int main(int, char**)
{
  Logger::getInstance().setMinLevel(LogLevel::LevelWarning);
  g_settings = LoadSettings();

  if (!glfwInit())
    return 1;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

  g_window = glfwCreateWindow(g_settings.windowWidth, g_settings.windowHeight, "fische", nullptr, nullptr);
  if (!g_window)
  {
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    glfwTerminate();
    return 1;
  }

  HWND hwnd = glfwGetWin32Window(g_window);
  HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                MAKEINTRESOURCEW(1),
                                IMAGE_ICON, 0, 0,
                                LR_DEFAULTSIZE | LR_SHARED);
  if (icon)
  {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)icon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
  }

  glfwMakeContextCurrent(g_window);
  glfwSetKeyCallback(g_window, key_callback);
  glfwSetMouseButtonCallback(g_window, mouse_button_callback);
  glfwSetCursorPosCallback(g_window, cursor_pos_callback);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
  {
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 1;
  }

  if (g_settings.alwaysOnTop)
    set_always_on_top(true);
  
  init_gl_font();
  glfwSwapInterval(g_settings.vsyncEnabled ? 1 : 0); // glfwSwapInterval(g_settings.fpsLimit <= 0 ? 1 : 0);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT, GL_FILL);

  recreate_visualizer();

  timeBeginPeriod(1);

  while (!glfwWindowShouldClose(g_window))
  {
    auto frameStart = std::chrono::steady_clock::now();

    // if (g_openSettings)
    // {
    //   g_openSettings = false;
    //   ShowSettingsDialog(glfwGetWin32Window(g_window), g_settings, [](const AppSettings& settings) {
    //     apply_settings(settings);
    //   });
    // }

    // if (std::chrono::steady_clock::now() > g_messageUntil)
    //   glfwSetWindowTitle(g_window, "fische");

    if (g_pendingApply)
    {
      g_pendingApply = false;
      apply_settings(g_pendingSettings);
    }

    maybe_handle_resize();

    if (!g_paused && g_visualizer)
    {
      int fw, fh;
      glfwGetFramebufferSize(g_window, &fw, &fh);

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      g_visualizer->Render();

      if (g_helpVisible)
        draw_help_screen(fw, fh);

      draw_osd(fw, fh);

      glfwSwapBuffers(g_window);
    }

    glfwPollEvents();
    refresh_cursor_visibility();
    enforce_frame_limit(frameStart);
  }
  
  timeEndPeriod(1);

  glfwGetWindowSize(g_window, &g_settings.windowWidth, &g_settings.windowHeight);
  SaveSettings(g_settings);

  g_audioCapture.Stop();
  g_visualizer.reset();
  if (g_spoutReady)
  {
    g_spout.ReleaseSender();
    g_spoutReady = false;
  }

  if (g_font) { DeleteObject(g_font); g_font = nullptr; }
  glfwDestroyWindow(g_window);
  glfwTerminate();
  return 0;
}
