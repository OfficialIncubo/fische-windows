#pragma once

#include "fishe_xbmc.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

enum class AudioDeviceFlow
{
  Render,
  Capture
};

struct AudioDeviceInfo
{
  std::wstring id;
  std::wstring name;
  AudioDeviceFlow flow = AudioDeviceFlow::Render;
  bool isDefault = false;
};

std::vector<AudioDeviceInfo> EnumerateAudioDevices();

class WasapiCapture
{
public:
  WasapiCapture() = default;
  ~WasapiCapture();

  bool Start(CFishBMC* visualizer, const std::string& preferredDeviceName);
  void Stop();
  void LockVisualizer()   { m_mutex.lock(); }
  void UnlockVisualizer() { m_mutex.unlock(); }
  bool IsRunning() const { return m_running.load(); }

private:
  void ThreadMain(std::wstring preferredDeviceName);
  long CaptureOnce(const std::wstring& preferredDeviceName);

  std::mutex m_mutex;
  std::atomic<bool> m_stop{false};
  std::atomic<bool> m_running{false};
  std::thread m_thread;
  CFishBMC* m_visualizer = nullptr;
};
