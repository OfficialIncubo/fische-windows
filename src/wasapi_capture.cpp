#include "wasapi_capture.h"

#include "app_settings.h"
#include "logger.h"

#include <algorithm>
#include <audioclient.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <thread>
#include <vector>
#include <windows.h>

namespace
{
class ComApartment
{
public:
  explicit ComApartment(DWORD mode)
  {
    hr = CoInitializeEx(nullptr, mode);
    initialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE)
      hr = S_OK;
  }

  ~ComApartment()
  {
    if (initialized)
      CoUninitialize();
  }

  HRESULT hr = E_FAIL;

private:
  bool initialized = false;
};

template<typename T>
class ComPtr
{
public:
  ComPtr() = default;
  explicit ComPtr(T* value) : ptr(value) {}
  ~ComPtr() { reset(); }

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  ComPtr(ComPtr&& other) noexcept : ptr(other.ptr)
  {
    other.ptr = nullptr;
  }

  ComPtr& operator=(ComPtr&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      ptr = other.ptr;
      other.ptr = nullptr;
    }
    return *this;
  }

  T** put()
  {
    reset();
    return &ptr;
  }

  T* get() const { return ptr; }
  T* operator->() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }

  T* detach()
  {
    T* value = ptr;
    ptr = nullptr;
    return value;
  }

  void reset(T* value = nullptr)
  {
    if (ptr)
      ptr->Release();
    ptr = value;
  }

private:
  T* ptr = nullptr;
};

std::wstring get_device_name(IMMDevice* device)
{
  ComPtr<IPropertyStore> props;
  if (FAILED(device->OpenPropertyStore(STGM_READ, props.put())))
    return L"";

  PROPVARIANT value;
  PropVariantInit(&value);
  std::wstring name;
  if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
    name = value.pwszVal;
  PropVariantClear(&value);
  return name;
}

std::wstring get_device_id(IMMDevice* device)
{
  LPWSTR id = nullptr;
  if (FAILED(device->GetId(&id)) || !id)
    return L"";

  std::wstring result = id;
  CoTaskMemFree(id);
  return result;
}

bool same_text(const std::wstring& left, const std::wstring& right)
{
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool is_default_device(IMMDeviceEnumerator* enumerator, EDataFlow flow, const std::wstring& id)
{
  ComPtr<IMMDevice> defaultDevice;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, defaultDevice.put())))
    return false;
  return same_text(get_device_id(defaultDevice.get()), id);
}

void append_devices(IMMDeviceEnumerator* enumerator, EDataFlow flow, AudioDeviceFlow appFlow, std::vector<AudioDeviceInfo>& devices)
{
  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put())))
    return;

  UINT count = 0;
  if (FAILED(collection->GetCount(&count)))
    return;

  for (UINT i = 0; i < count; ++i)
  {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(i, device.put())))
      continue;

    AudioDeviceInfo info;
    info.id = get_device_id(device.get());
    info.name = get_device_name(device.get());
    info.flow = appFlow;
    info.isDefault = is_default_device(enumerator, flow, info.id);
    if (!info.id.empty() && !info.name.empty())
      devices.push_back(std::move(info));
  }
}

ComPtr<IMMDevice> find_device(IMMDeviceEnumerator* enumerator, const std::wstring& preferredName, AudioDeviceFlow& flow)
{
  if (!preferredName.empty())
  {
    auto devices = EnumerateAudioDevices();
    auto match = std::find_if(devices.begin(), devices.end(), [&](const AudioDeviceInfo& device) {
      return same_text(device.name, preferredName) || same_text(device.id, preferredName);
    });

    if (match != devices.end())
    {
      ComPtr<IMMDevice> device;
      if (SUCCEEDED(enumerator->GetDevice(match->id.c_str(), device.put())))
      {
        flow = match->flow;
        return device;
      }
    }
  }

  ComPtr<IMMDevice> defaultDevice;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, defaultDevice.put())))
  {
    flow = AudioDeviceFlow::Render;
    return defaultDevice;
  }

  return {};
}

bool is_float_format(const WAVEFORMATEX* format)
{
  if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    return true;
  if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    return false;

  const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
  return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
}

bool is_pcm_format(const WAVEFORMATEX* format)
{
  if (format->wFormatTag == WAVE_FORMAT_PCM)
    return true;
  if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
    return false;

  const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
  return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
}

float read_pcm_sample(const BYTE* frame, int bytesPerSample)
{
  switch (bytesPerSample)
  {
    case 1:
      return (static_cast<int>(*frame) - 128) / 128.0f;
    case 2:
      return static_cast<float>(*reinterpret_cast<const int16_t*>(frame)) / 32768.0f;
    case 3:
    {
      int32_t value = (static_cast<int32_t>(frame[0]) << 8) |
                      (static_cast<int32_t>(frame[1]) << 16) |
                      (static_cast<int32_t>(frame[2]) << 24);
      value >>= 8;
      return static_cast<float>(value) / 8388608.0f;
    }
    case 4:
      return static_cast<float>(*reinterpret_cast<const int32_t*>(frame)) / 2147483648.0f;
    default:
      return 0.0f;
  }
}

std::vector<float> convert_to_stereo_float(const BYTE* data, UINT32 frames, DWORD flags, const WAVEFORMATEX* format)
{
  std::vector<float> output(static_cast<size_t>(frames) * 2, 0.0f);
  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data || !format)
    return output;

  const int channels = std::max<int>(format->nChannels, 1);
  const int bytesPerSample = std::max<int>(format->wBitsPerSample / 8, 1);
  const bool floatFormat = is_float_format(format);
  const bool pcmFormat = is_pcm_format(format);

  if (!floatFormat && !pcmFormat)
    return output;

  for (UINT32 frameIndex = 0; frameIndex < frames; ++frameIndex)
  {
    float left = 0.0f;
    float right = 0.0f;
    const BYTE* frame = data + static_cast<size_t>(frameIndex) * format->nBlockAlign;

    if (floatFormat && bytesPerSample == 4)
    {
      const auto* samples = reinterpret_cast<const float*>(frame);
      left = samples[0];
      right = channels > 1 ? samples[1] : left;
    }
    else
    {
      left = read_pcm_sample(frame, bytesPerSample);
      right = channels > 1 ? read_pcm_sample(frame + bytesPerSample, bytesPerSample) : left;
    }

    output[static_cast<size_t>(frameIndex) * 2] = std::clamp(left, -1.0f, 1.0f);
    output[static_cast<size_t>(frameIndex) * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
  }

  return output;
}
}

std::vector<AudioDeviceInfo> EnumerateAudioDevices()
{
  ComApartment com(COINIT_MULTITHREADED);
  std::vector<AudioDeviceInfo> devices;
  if (FAILED(com.hr))
    return devices;

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
  if (FAILED(hr))
    return devices;

  append_devices(enumerator.get(), eRender, AudioDeviceFlow::Render, devices);
  append_devices(enumerator.get(), eCapture, AudioDeviceFlow::Capture, devices);
  return devices;
}

WasapiCapture::~WasapiCapture()
{
  Stop();
}

bool WasapiCapture::Start(CFishBMC* visualizer, const std::string& preferredDeviceName)
{
  Stop();
  if (!visualizer)
    return false;

  m_visualizer = visualizer;
  m_stop.store(false);
  m_running.store(true);
  m_thread = std::thread(&WasapiCapture::ThreadMain, this, Utf8ToWide(preferredDeviceName));
  return true;
}

void WasapiCapture::Stop()
{
  m_stop.store(true);
  if (m_thread.joinable())
    m_thread.join();
  m_running.store(false);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_visualizer = nullptr;
  }
}

void WasapiCapture::ThreadMain(std::wstring preferredDeviceName)
{
  ComApartment com(COINIT_MULTITHREADED);
  if (FAILED(com.hr))
  {
    //FISHE_LOG_WARN("WASAPI COM initialization failed: 0x%08lx", com.hr);
    m_running.store(false);
    return;
  }

  while (!m_stop.load())
  {
    HRESULT hr = CaptureOnce(preferredDeviceName);

    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue; // retry — will re-enumerate and reconnect
    }

    if (m_stop.load())
      break;

    //FISHE_LOG_WARN("WASAPI capture stopped, retrying: 0x%08lx", hr);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  m_running.store(false);
}

long WasapiCapture::CaptureOnce(const std::wstring& preferredDeviceName)
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
  if (FAILED(hr))
    return hr;

  AudioDeviceFlow flow = AudioDeviceFlow::Render;
  ComPtr<IMMDevice> device = find_device(enumerator.get(), preferredDeviceName, flow);
  if (!device)
    return E_FAIL;

  FISHE_LOG_INFO("Capturing %s device: %s",
                 flow == AudioDeviceFlow::Render ? "output" : "input",
                 WideToUtf8(get_device_name(device.get())).c_str());

  ComPtr<IAudioClient> audioClient;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audioClient.put()));
  if (FAILED(hr))
    return hr;

  WAVEFORMATEX* mixFormat = nullptr;
  hr = audioClient->GetMixFormat(&mixFormat);
  if (FAILED(hr))
    return hr;

  struct FormatReleaser
  {
    WAVEFORMATEX* value = nullptr;
    ~FormatReleaser() { CoTaskMemFree(value); }
  } formatReleaser{mixFormat};

  REFERENCE_TIME defaultPeriod = 0;
  audioClient->GetDevicePeriod(&defaultPeriod, nullptr);

  DWORD streamFlags = flow == AudioDeviceFlow::Render ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
  hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, mixFormat, nullptr);
  if (FAILED(hr))
    return hr;

  ComPtr<IAudioCaptureClient> captureClient;
  hr = audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(captureClient.put()));
  if (FAILED(hr))
    return hr;

  HANDLE wakeTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  if (!wakeTimer)
    return HRESULT_FROM_WIN32(GetLastError());

  struct HandleCloser
  {
    HANDLE value = nullptr;
    ~HandleCloser()
    {
      if (value)
      {
        CancelWaitableTimer(value);
        CloseHandle(value);
      }
    }
  } closeTimer{wakeTimer};

  LARGE_INTEGER firstFire{};
  firstFire.QuadPart = defaultPeriod > 0 ? -defaultPeriod / 2 : -10'000;
  LONG intervalMs = defaultPeriod > 0 ? static_cast<LONG>(std::max<REFERENCE_TIME>(1, defaultPeriod / 2 / 10'000)) : 5;
  SetWaitableTimer(wakeTimer, &firstFire, intervalMs, nullptr, nullptr, FALSE);

  hr = audioClient->Start();
  if (FAILED(hr))
    return hr;

  struct ClientStopper
  {
    IAudioClient* value = nullptr;
    ~ClientStopper()
    {
      if (value)
        value->Stop();
    }
  } stopClient{audioClient.get()};

  while (!m_stop.load())
  {
    UINT32 packetFrames = 0;
    hr = captureClient->GetNextPacketSize(&packetFrames);
    if (FAILED(hr))
      return hr;

    while (packetFrames > 0)
    {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);


      if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
      {
        //FISHE_LOG_DEBUG("Audio device disconnected, will retry");
        return hr;
      }

      if (FAILED(hr))
        return hr;

      auto samples = convert_to_stereo_float(data, frames, flags, mixFormat);
      if (!samples.empty())
      {
        float gain = m_sensitivity.load();
        if (gain != 1.0f)
          for (auto& s : samples)
            s = std::clamp(s * gain, -1.0f, 1.0f);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_visualizer)
          m_visualizer->AudioData(reinterpret_cast<const char*>(samples.data()),
                                  samples.size() * sizeof(float));
      }

      hr = captureClient->ReleaseBuffer(frames);
      if (FAILED(hr))
        return hr;

      hr = captureClient->GetNextPacketSize(&packetFrames);
      if (FAILED(hr))
        return hr;
    }

    WaitForSingleObject(wakeTimer, 50);
  }

  return S_OK;
}
