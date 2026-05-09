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
std::unique_ptr<CFishBMC> g_visualizer;
WasapiCapture g_audioCapture;
SpoutSender g_spout;
bool g_helpVisible = false;
GLuint g_fontBase = 0;
bool g_spoutReady = false;
bool g_paused = false;
bool g_openSettings = false;
bool g_fullscreen = false;
int g_windowedX = 100;
int g_windowedY = 100;
int g_windowedWidth = 1280;
int g_windowedHeight = 720;
int g_lastFramebufferWidth = 0;
int g_lastFramebufferHeight = 0;
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

void set_title_message(const std::string& message, int milliseconds = 1800)
{
  if (!g_window)
    return;

  glfwSetWindowTitle(g_window, message.empty() ? "fische" : ("fische - " + message).c_str());
  g_messageUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
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
  update_spout_state();
  g_lastFramebufferWidth = framebufferWidth;
  g_lastFramebufferHeight = framebufferHeight;
}

void apply_settings(const AppSettings& settings)
{
  g_settings = settings;
  if (g_visualizer)
    g_visualizer->SetNervousMode(g_settings.nervousMode);
  glfwSwapInterval(g_settings.vsyncEnabled ? 1 : 0); // glfwSwapInterval(g_settings.fpsLimit <= 0 ? 1 : 0);
  recreate_visualizer();
}

void init_gl_font()
{
  HDC hdc = wglGetCurrentDC();
  if (!hdc)
    return;
  HFONT font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HFONT old = (HFONT)SelectObject(hdc, font);
  g_fontBase = glGenLists(96);
  wglUseFontBitmapsW(hdc, 32, 96, g_fontBase);
  SelectObject(hdc, old);
  DeleteObject(font);
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
    "fische - Keyboard Shortcuts",
    "",
    "F1      Show / hide help screen",
    "F       Toggle fullscreen",
    "N       Toggle nervous mode",
    "O       Open settings",
    "P       Pause / unpause",
    "Z       Toggle Spout output",
    "Esc     Exit",
  };

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  int y = 80;
  for (const char* line : lines)
  {
    // glRasterPos2i fails silently if it lands outside the viewport   use 2f instead
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
  }
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
    case GLFW_KEY_N:
      g_settings.nervousMode = !g_settings.nervousMode;
      if (g_visualizer)
        g_visualizer->SetNervousMode(g_settings.nervousMode);
      SaveSettings(g_settings);
      set_title_message(g_settings.nervousMode ? "nervous mode on" : "nervous mode off");
      break;
    case GLFW_KEY_O:
      g_openSettings = true;
      break;
    case GLFW_KEY_P:
      g_paused = !g_paused;
      set_title_message(g_paused ? "paused" : "playing");
      break;
    case GLFW_KEY_Z:
      g_settings.spoutEnabled = !g_settings.spoutEnabled;
      update_spout_state();
      SaveSettings(g_settings);
      set_title_message(g_settings.spoutEnabled ? "Spout output on" : "Spout output off");
      break;
  }
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
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(g_window);
  glfwSetKeyCallback(g_window, key_callback);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
  {
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 1;
  }
  
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

    if (g_openSettings)
    {
      g_openSettings = false;
      ShowSettingsDialog(glfwGetWin32Window(g_window), g_settings, [](const AppSettings& settings) {
        apply_settings(settings);
      });
    }

    if (std::chrono::steady_clock::now() > g_messageUntil)
      glfwSetWindowTitle(g_window, "fische");

    maybe_handle_resize();

    if (!g_paused && g_visualizer)
    {
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      g_visualizer->Render();
      if (g_helpVisible)
      {
        int fw, fh;
        glfwGetFramebufferSize(g_window, &fw, &fh);
        draw_help_screen(fw, fh);
      }
      glfwSwapBuffers(g_window);
    }

    glfwPollEvents();
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

  glfwDestroyWindow(g_window);
  glfwTerminate();
  return 0;
}
