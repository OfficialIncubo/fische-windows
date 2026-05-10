#include "app_settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <windows.h>

namespace
{
std::string trim(std::string value)
{
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) {
                return !is_space(static_cast<unsigned char>(c));
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) {
                return !is_space(static_cast<unsigned char>(c));
              }).base(),
              value.end());
  return value;
}

std::string lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool parse_bool(const std::string& value, bool fallback)
{
  const std::string v = lower(trim(value));
  if (v == "1" || v == "true" || v == "yes" || v == "on")
    return true;
  if (v == "0" || v == "false" || v == "no" || v == "off")
    return false;
  return fallback;
}

int parse_int(const std::string& value, int fallback, int min_value, int max_value)
{
  try
  {
    int parsed = std::stoi(trim(value));
    return std::clamp(parsed, min_value, max_value);
  }
  catch (...)
  {
    return fallback;
  }
}

int parse_quality(const std::string& value, int fallback)
{
  const std::string v = lower(trim(value));
  if (v == "low")
    return 0;
  if (v == "normal")
    return 1;
  if (v == "high")
    return 2;
  if (v == "extreme")
    return 3;
  return parse_int(value, fallback, 0, 3);
}

const char* quality_name(int quality)
{
  switch (std::clamp(quality, 0, 3))
  {
    case 0:
      return "Low";
    case 1:
      return "Normal";
    case 2:
      return "High";
    case 3:
      return "Extreme";
  }
  return "High";
}
}

std::filesystem::path GetExecutableDirectory()
{
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  while (size == buffer.size())
  {
    buffer.resize(buffer.size() * 2);
    size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  }

  buffer.resize(size);
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path GetSettingsPath()
{
  return GetExecutableDirectory() / "settings.ini";
}

AppSettings LoadSettings()
{
  AppSettings settings;
  std::ifstream file(GetSettingsPath());
  if (!file)
    return settings;

  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(file, line))
  {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
      continue;

    const size_t equals = line.find('=');
    if (equals == std::string::npos)
      continue;

    values[lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
  }

  if (auto it = values.find("audio"); it != values.end())
    settings.audioDevice = it->second;
  if (auto it = values.find("audiosensitivity"); it != values.end())
    settings.audioSensitivity = std::clamp(std::stof(trim(it->second)), 0.0f, 15.0f);
  if (auto it = values.find("detail"); it != values.end())
    settings.quality = parse_quality(it->second, settings.quality);
  if (auto it = values.find("quality"); it != values.end())
    settings.quality = parse_quality(it->second, settings.quality);
  if (auto it = values.find("fpslimit"); it != values.end())
    settings.fpsLimit = parse_int(it->second, settings.fpsLimit, 0, 240);
  if (auto it = values.find("speed"); it != values.end())
    settings.fpsLimit = parse_int(it->second, settings.fpsLimit, 0, 240);
  if (auto it = values.find("nervousmode"); it != values.end())
    settings.nervousMode = parse_bool(it->second, settings.nervousMode);
  if (auto it = values.find("usefilepersistence"); it != values.end())
    settings.useFilePersistence = parse_bool(it->second, settings.useFilePersistence);
  if (auto it = values.find("spoutsenderenabled"); it != values.end())
    settings.spoutEnabled = parse_bool(it->second, settings.spoutEnabled);
  if (auto it = values.find("vsync"); it != values.end())
    settings.vsyncEnabled = parse_bool(it->second, settings.vsyncEnabled);
  if (auto it = values.find("windowwidth"); it != values.end())
    settings.windowWidth = parse_int(it->second, settings.windowWidth, 320, 7680);
  if (auto it = values.find("windowheight"); it != values.end())
    settings.windowHeight = parse_int(it->second, settings.windowHeight, 240, 4320);

  return settings;
}

void SaveSettings(const AppSettings& settings)
{
  std::ofstream file(GetSettingsPath(), std::ios::trunc);
  if (!file)
    return;

  file << "[fische]\n";
  file << "Audio=" << settings.audioDevice << "\n";
  file << "AudioSensitivity=" << settings.audioSensitivity << "\n";
  file << "Detail=" << quality_name(settings.quality) << "\n";
  file << "Quality=" << std::clamp(settings.quality, 0, 3) << "\n";
  file << "FPSLimit=" << std::clamp(settings.fpsLimit, 0, 240) << "\n";
  file << "NervousMode=" << (settings.nervousMode ? "true" : "false") << "\n";
  file << "UseFilePersistence=" << (settings.useFilePersistence ? "true" : "false") << "\n";
  file << "SpoutSenderEnabled=" << (settings.spoutEnabled ? "true" : "false") << "\n";
  file << "VSync=" << (settings.vsyncEnabled ? "true" : "false") << "\n";
  file << "WindowWidth=" << std::max(settings.windowWidth, 320) << "\n";
  file << "WindowHeight=" << std::max(settings.windowHeight, 240) << "\n";
}

std::wstring Utf8ToWide(const std::string& text)
{
  if (text.empty())
    return {};

  int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), count);
  return result;
}

std::string WideToUtf8(const std::wstring& text)
{
  if (text.empty())
    return {};

  int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
  return result;
}
