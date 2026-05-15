#include "asio_capture.h"
#include "fishe_xbmc.h"

// ASIO SDK headers — adjust path to match your project layout
#include "asio/common/asiosys.h"
#include "asio/common/asio.h"
#include "asio/host/asiodrivers.h"
#include "asio/host/pc/asiolist.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <vector>

// --------------------------------------------------------------------------
// The ASIO SDK requires a single global AsioDrivers object and a single
// global instance pointer for the static callbacks.
// --------------------------------------------------------------------------
extern AsioDrivers* asioDrivers;
bool loadAsioDriver(char* name);

static AsioCapture* g_asioInstance = nullptr;

// --------------------------------------------------------------------------
// Device enumeration — reads HKLM\SOFTWARE\ASIO registry key
// --------------------------------------------------------------------------
std::vector<AsioDeviceInfo> EnumerateAsioDevices()
{
  std::vector<AsioDeviceInfo> result;

  HKEY hKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return result;

  char  name[128];
  DWORD nameLen = sizeof(name);
  DWORD index   = 0;

  while (RegEnumKeyExA(hKey, index++, name, &nameLen, nullptr,
                       nullptr, nullptr, nullptr) == ERROR_SUCCESS)
  {
    AsioDeviceInfo info;
    info.name  = name;
    info.index = static_cast<int>(result.size());
    result.push_back(info);
    nameLen = sizeof(name);
  }

  RegCloseKey(hKey);
  return result;
}

// --------------------------------------------------------------------------
// Helpers — convert one ASIO sample to float
// --------------------------------------------------------------------------
static float AsioSampleToFloat(void* buf, long sampleType)
{
  switch (sampleType)
  {
    case 8:  // ASIOSTInt32LSB
    case 16:
    {
      int32_t v;
      memcpy(&v, buf, 4);
      return static_cast<float>(v) / static_cast<float>(0x7fffffff);
    }
    case 14: // ASIOSTFloat32LSB
    {
      float v;
      memcpy(&v, buf, 4);
      return v;
    }
    case 15: // ASIOSTFloat64LSB
    {
      double v;
      memcpy(&v, buf, 8);
      return static_cast<float>(v);
    }
    default:
    {
      int32_t v;
      memcpy(&v, buf, 4);
      return static_cast<float>(v) / static_cast<float>(0x7fffffff);
    }
  }
}

// --------------------------------------------------------------------------
// ASIOBufferInfo / ASIOChannelInfo / ASIOCallbacks storage
// --------------------------------------------------------------------------
static ASIOBufferInfo  g_bufferInfos[2];
static ASIOChannelInfo g_channelInfos[2];
static ASIOCallbacks   g_callbacks;

// --------------------------------------------------------------------------
// AsioCapture
// --------------------------------------------------------------------------
AsioCapture::AsioCapture() = default;

AsioCapture::~AsioCapture()
{
  Stop();
}

bool AsioCapture::Start(void* visualizer, const std::string& driverName)
{
  if (m_running.load())
    Stop();

  m_visualizer   = visualizer;
  g_asioInstance = this;

  if (!InitDriver(driverName))
  {
    g_asioInstance = nullptr;
    return false;
  }

  m_running.store(true);
  return true;
}

void AsioCapture::Stop()
{
  if (!m_running.load())
    return;

  m_running.store(false);

  ASIOStop();
  ASIODisposeBuffers();
  ASIOExit();

  if (asioDrivers)
    asioDrivers->removeCurrentDriver();

  g_asioInstance = nullptr;
  m_visualizer   = nullptr;
}

bool AsioCapture::InitDriver(const std::string& name)
{
  char nameBuf[128] = {};
  strncpy_s(nameBuf, name.c_str(), sizeof(nameBuf) - 1);

  if (!loadAsioDriver(nameBuf))
    return false;

  ASIODriverInfo driverInfo = {};
  if (ASIOInit(&driverInfo) != ASE_OK)
    return false;

  long numIn = 0, numOut = 0;
  if (ASIOGetChannels(&numIn, &numOut) != ASE_OK)
    return false;

  m_inputChannels = std::min(numIn, 2L);

  long minBuf, maxBuf, prefBuf, gran;
  if (ASIOGetBufferSize(&minBuf, &maxBuf, &prefBuf, &gran) != ASE_OK)
    return false;
  m_bufferSize = prefBuf;

  ASIOSampleRate sr = 0.0;
  if (ASIOGetSampleRate(&sr) != ASE_OK || sr <= 0.0)
    sr = 44100.0;
  m_sampleRate = sr;

  m_postOutput = (ASIOOutputReady() == ASE_OK);

  for (long i = 0; i < m_inputChannels; ++i)
  {
    g_bufferInfos[i].isInput    = ASIOTrue;
    g_bufferInfos[i].channelNum = i;
    g_bufferInfos[i].buffers[0] = nullptr;
    g_bufferInfos[i].buffers[1] = nullptr;
  }

  g_callbacks.bufferSwitch         = &AsioCapture::StaticBufferSwitch;
  g_callbacks.sampleRateDidChange  = &AsioCapture::StaticSampleRateChanged;
  g_callbacks.asioMessage          = &AsioCapture::StaticAsioMessage;
  g_callbacks.bufferSwitchTimeInfo = nullptr;

  if (ASIOCreateBuffers(g_bufferInfos, m_inputChannels,
                        m_bufferSize, &g_callbacks) != ASE_OK)
    return false;

  g_channelInfos[0].channel = 0;
  g_channelInfos[0].isInput = ASIOTrue;
  ASIOGetChannelInfo(&g_channelInfos[0]);
  m_sampleType = g_channelInfos[0].type;

  if (ASIOStart() != ASE_OK)
  {
    ASIODisposeBuffers();
    return false;
  }

  return true;
}

void AsioCapture::OnBufferSwitch(long index)
{
  if (!m_visualizer || !m_running.load())
    return;

  std::vector<float> samples(m_bufferSize * 2, 0.0f);

  for (long s = 0; s < m_bufferSize; ++s)
  {
    for (long ch = 0; ch < m_inputChannels; ++ch)
    {
      uint8_t* base = static_cast<uint8_t*>(g_bufferInfos[ch].buffers[index]);
      void*    src  = base + s * 4;
      samples[static_cast<size_t>(s * 2 + ch)] = AsioSampleToFloat(src, m_sampleType);
    }
  }

  CFishBMC* vis = static_cast<CFishBMC*>(m_visualizer);
  vis->AudioData(reinterpret_cast<const char*>(samples.data()),
                 samples.size() * sizeof(float));

  if (m_postOutput)
    ASIOOutputReady();
}

void AsioCapture::StaticBufferSwitch(long index, long /*processNow*/)
{
  if (g_asioInstance)
    g_asioInstance->OnBufferSwitch(index);
}

void AsioCapture::StaticSampleRateChanged(double /*sRate*/) {}

long AsioCapture::StaticAsioMessage(long selector, long /*value*/,
                                    void* /*msg*/, double* /*opt*/)
{
  switch (selector)
  {
    case kAsioSelectorSupported:
    case kAsioEngineVersion:
      return 2;
    case kAsioResetRequest:
      return 1;
    default:
      return 0;
  }
}
