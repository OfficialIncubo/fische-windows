#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <functional>

struct AsioDeviceInfo
{
  std::string name;
  int         index = 0;
};

// Enumerate installed ASIO drivers from registry
std::vector<AsioDeviceInfo> EnumerateAsioDevices();

// -----------------------------------------------------------------------
// AsioCapture — callback-driven ASIO input capture
// Feeds stereo float32 interleaved data into a CFishBMC-compatible sink
// -----------------------------------------------------------------------
class AsioCapture
{
public:
  AsioCapture();
  ~AsioCapture();

  // Start capturing from the named ASIO driver.
  // visualizer must remain valid until Stop() is called.
  bool Start(void* visualizer, const std::string& driverName);
  void Stop();
  void SetSensitivity(float s) { m_sensitivity.store(s); }

  bool IsRunning() const { return m_running.load(); }

  // Called internally by the static ASIO buffer-switch callback
  void OnBufferSwitch(long index);

private:
  bool        InitDriver(const std::string& name);
  void        CleanUp();
  static void StaticBufferSwitch(long index, long processNow);
  static void StaticSampleRateChanged(double sRate);
  static long StaticAsioMessage(long sel, long val, void* msg, double* opt);

  void*              m_visualizer = nullptr;
  std::atomic<bool>  m_running{false};
  std::atomic<float> m_sensitivity{1.0f};
  long               m_inputChannels  = 0;
  long               m_bufferSize     = 0;
  double             m_sampleRate     = 44100.0;
  int                m_sampleType     = 0; // ASIOSampleType
  bool               m_postOutput     = false;
};
