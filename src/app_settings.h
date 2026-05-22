#pragma once

#include <filesystem>
#include <string>

struct AppSettings
{
  std::string audioDevice;
  int quality = 2;
  int fpsLimit = 60;
  bool alwaysOnTop = false;
  bool hideWindow = false;
  bool nervousMode = false;
  bool useFilePersistence = false;
  bool spoutEnabled = false;
  int vsyncMode = 0; // 0 = Off, 1 = On, 2 = Adaptive
  float audioSensitivity = 1.0f;
  int windowWidth = 1280;
  int windowHeight = 720;
};

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetSettingsPath();
AppSettings LoadSettings();
void SaveSettings(const AppSettings& settings);

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
