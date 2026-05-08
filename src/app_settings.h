#pragma once

#include <filesystem>
#include <string>

struct AppSettings
{
  std::string audioDevice;
  int quality = 2;
  int fpsLimit = 60;
  bool nervousMode = false;
  bool useFilePersistence = false;
  bool spoutEnabled = false;
  bool vsyncEnabled = false;
  int windowWidth = 1280;
  int windowHeight = 720;
};

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetSettingsPath();
AppSettings LoadSettings();
void SaveSettings(const AppSettings& settings);

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
