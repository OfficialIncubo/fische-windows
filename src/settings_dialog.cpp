#include "settings_dialog.h"

#include "asio_capture.h"
#include "wasapi_capture.h"

#include <algorithm>
#include <commctrl.h>
#include <string>
#include <vector>

namespace
{
constexpr int kDialogWidth = 495;
constexpr int kDialogHeight = 473;
constexpr int kMargin = 10;
constexpr int kLabelWidth = 130;
constexpr int kControlLeft = 155;
constexpr int kControlWidth = 310;
constexpr int kRowHeight = 32;

constexpr int kAudioCombo = 1001;
constexpr int kQualityCombo = 1002;
constexpr int kFpsSlider = 1003;
constexpr int kFpsLabel = 1004;
constexpr int kNervousCheck = 1005;
constexpr int kPersistenceCheck = 1006;
constexpr int kSpoutCheck = 1007;
constexpr int kApplyButton = 1008;
constexpr int kVSyncCombo = 1009;
constexpr int kSensitivitySlider = 1010;
constexpr int kSensitivityLabel = 1011;
constexpr int kSensitivityReset = 1012;
constexpr int kAlwaysOnTopCheck = 1013;
constexpr int kWidthEdit  = 1014;
constexpr int kHeightEdit = 1015;
constexpr int kFpsReset = 1016;
constexpr int kHideWindowCheck = 1017;

struct DialogState
{
  AppSettings* settings = nullptr;
  AppSettings working;
  std::function<void(const AppSettings&)> onApply;
  std::vector<AudioDeviceInfo> devices;
  HWND fpsLabel = nullptr;
  HWND sensLabel = nullptr;
  bool accepted = false;
};

HWND create_control(HWND parent,
                    const wchar_t* className,
                    const wchar_t* text,
                    DWORD style,
                    int x,
                    int y,
                    int w,
                    int h,
                    int id)
{
  return CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void update_fps_label(DialogState* state, HWND slider)
{
  int value = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
  std::wstring text = value == 0 ? L"Unlimited" : std::to_wstring(value) + L" FPS";
  SetWindowTextW(state->fpsLabel, text.c_str());
}

void update_sens_label(DialogState* state, HWND slider)
{
  int value = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
  float sens = value / 100.0f;
  wchar_t buf[32];
  swprintf_s(buf, L"%.2f", sens);
  SetWindowTextW(state->sensLabel, buf);
}

void populate_audio_combo(HWND combo, DialogState* state)
{
  int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default output device")));
  SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(-1));
  int selected = state->working.audioDevice.empty() ? index : 0;

  for (size_t i = 0; i < state->devices.size(); ++i)
  {
    const auto& device = state->devices[i];
    std::wstring label = device.flow == AudioDeviceFlow::Render ? L"Output: " : L"Input: ";
    label += device.name;
    if (device.isDefault && device.flow == AudioDeviceFlow::Render)
      label += L" (Default)";

    index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
    SendMessageW(combo, CB_SETITEMDATA, index, static_cast<LPARAM>(i));
    if (!state->working.audioDevice.empty() && _wcsicmp(device.name.c_str(), Utf8ToWide(state->working.audioDevice).c_str()) == 0)
      selected = index;
  }

  auto asioDevices = EnumerateAsioDevices();
  for (const auto& dev : asioDevices)
  {
    std::wstring label = L"ASIO: " + Utf8ToWide(dev.name);
    std::string stored = "[ASIO] " + dev.name;
    index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(label.c_str())));
    // Use negative sentinel: -2, -3, -4... to distinguish from WASAPI
    SendMessageW(combo, CB_SETITEMDATA, index,
                static_cast<LPARAM>(-2 - dev.index));
    if (!state->working.audioDevice.empty() &&
        state->working.audioDevice == stored)
      selected = index;
  }

  SendMessageW(combo, CB_SETCURSEL, selected, 0);
}

void apply_settings_from_controls(HWND hwnd, DialogState* state)
{
  HWND audio = GetDlgItem(hwnd, kAudioCombo);
  int audioIndex = static_cast<int>(SendMessageW(audio, CB_GETCURSEL, 0, 0));
  LPARAM data = SendMessageW(audio, CB_GETITEMDATA, audioIndex, 0);

  if (data >= 0 && static_cast<size_t>(data) < state->devices.size())
  {
    // WASAPI device
    state->working.audioDevice = WideToUtf8(state->devices[static_cast<size_t>(data)].name);
  }
  else if (data <= -2)
  {
    // ASIO device — reconstruct stored name from combo label
    wchar_t label[256] = {};
    SendMessageW(audio, CB_GETLBTEXT, audioIndex, reinterpret_cast<LPARAM>(label));
    // Label is "ASIO: <name>", stored is "[ASIO] <name>"
    std::wstring wlabel(label);
    if (wlabel.rfind(L"ASIO: ", 0) == 0)
      state->working.audioDevice = "[ASIO] " + WideToUtf8(wlabel.substr(6));
  }
  else
  {
    // Default output device
    state->working.audioDevice.clear();
  }

  HWND sens = GetDlgItem(hwnd, kSensitivitySlider);
  state->working.audioSensitivity = static_cast<float>(SendMessageW(sens, TBM_GETPOS, 0, 0)) / 100.0f;

  HWND quality = GetDlgItem(hwnd, kQualityCombo);
  state->working.quality = std::clamp(static_cast<int>(SendMessageW(quality, CB_GETCURSEL, 0, 0)), 0, 3);

  wchar_t buf[32];
  GetWindowTextW(GetDlgItem(hwnd, kWidthEdit),  buf, 32);
  state->working.windowWidth = std::clamp(_wtoi(buf), 320, 7680);
  GetWindowTextW(GetDlgItem(hwnd, kHeightEdit), buf, 32);
  state->working.windowHeight = std::clamp(_wtoi(buf), 240, 4320);

  HWND fps = GetDlgItem(hwnd, kFpsSlider);
  state->working.fpsLimit = std::clamp(static_cast<int>(SendMessageW(fps, TBM_GETPOS, 0, 0)), 0, 240);
  state->working.nervousMode = SendMessageW(GetDlgItem(hwnd, kNervousCheck), BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->working.useFilePersistence = SendMessageW(GetDlgItem(hwnd, kPersistenceCheck), BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->working.spoutEnabled = SendMessageW(GetDlgItem(hwnd, kSpoutCheck), BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->working.vsyncMode = std::clamp(static_cast<int>(SendMessageW(GetDlgItem(hwnd, kVSyncCombo), CB_GETCURSEL, 0, 0)), 0, 2);
  state->working.alwaysOnTop = SendMessageW(GetDlgItem(hwnd, kAlwaysOnTopCheck), BM_GETCHECK, 0, 0) == BST_CHECKED;
  state->working.hideWindow  = SendMessageW(GetDlgItem(hwnd, kHideWindowCheck),  BM_GETCHECK, 0, 0) == BST_CHECKED;

  *state->settings = state->working;
  SaveSettings(*state->settings);
  if (state->onApply)
    state->onApply(*state->settings);
}

LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (message)
  {
    case WM_NCCREATE:
    {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      return TRUE;
    }
    case WM_CREATE:
    {
      state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      int y = kMargin;

      create_control(hwnd, L"STATIC", L"Audio device", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND audioCombo = create_control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, kControlLeft, y,
                                       kControlWidth, 220, kAudioCombo);
      populate_audio_combo(audioCombo, state);
      y += kRowHeight + 4;

      create_control(hwnd, L"STATIC", L"A. Sensitivity", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND sensSlider = create_control(hwnd, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS,
                                      kControlLeft, y, 190, 28, kSensitivitySlider);
      // 0-1500 range maps to 0-15
      SendMessageW(sensSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1500));
      SendMessageW(sensSlider, TBM_SETTICFREQ, 100, 0);
      SendMessageW(sensSlider, TBM_SETPOS, TRUE,
                  static_cast<LPARAM>(state->working.audioSensitivity * 100.0f));
      state->sensLabel = create_control(hwnd, L"STATIC", L"", 0,
                                        kControlLeft + 195, y + 4, 45, 22, kSensitivityLabel);
      update_sens_label(state, sensSlider);
      create_control(hwnd, L"BUTTON", L"Reset", 0,
                     kControlLeft + 245, y + 2, 65, 22, kSensitivityReset);
      y += kRowHeight + 8;

      create_control(hwnd, L"STATIC", L"Detail", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND qualityCombo = create_control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, kControlLeft, y, kControlWidth, 220,
                                         kQualityCombo);
      SendMessageW(qualityCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Low"));
      SendMessageW(qualityCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Normal"));
      SendMessageW(qualityCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"High"));
      SendMessageW(qualityCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Extreme"));
      SendMessageW(qualityCombo, CB_SETCURSEL, std::clamp(state->working.quality, 0, 3), 0);
      y += kRowHeight + 4;

      create_control(hwnd, L"STATIC", L"VSync", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND vsyncCombo = create_control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, kControlLeft, y, kControlWidth, 220, kVSyncCombo);
      SendMessageW(vsyncCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
      SendMessageW(vsyncCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"On"));
      SendMessageW(vsyncCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Adaptive"));
      SendMessageW(vsyncCombo, CB_SETCURSEL, std::clamp(state->working.vsyncMode, 0, 2), 0);
      y += kRowHeight + 4;

      create_control(hwnd, L"STATIC", L"Window size", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND widthEdit = create_control(hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER, kControlLeft, y, 70, 22, kWidthEdit);
      create_control(hwnd, L"STATIC", L"\u00D7", 0, kControlLeft + 76, y + 4, 12, 22, 0);
      HWND heightEdit = create_control(hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER, kControlLeft + 94, y, 70, 22, kHeightEdit);

      SetWindowTextW(widthEdit,  std::to_wstring(state->working.windowWidth).c_str());
      SetWindowTextW(heightEdit, std::to_wstring(state->working.windowHeight).c_str());
      y += kRowHeight + 4;

      create_control(hwnd, L"STATIC", L"Speed", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      HWND fpsSlider = create_control(hwnd, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, kControlLeft, y, 160, 28, kFpsSlider);
      SendMessageW(fpsSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 240));
      SendMessageW(fpsSlider, TBM_SETTICFREQ, 30, 0);
      SendMessageW(fpsSlider, TBM_SETPOS, TRUE, std::clamp(state->working.fpsLimit, 0, 240));
      state->fpsLabel = create_control(hwnd, L"STATIC", L"", 0, kControlLeft + 165, y + 4, 75, 22, kFpsLabel);
      update_fps_label(state, fpsSlider);
      create_control(hwnd, L"BUTTON", L"Default", 0, kControlLeft + 245, y + 2, 65, 22, kFpsReset);
      y += kRowHeight + 4;

      create_control(hwnd, L"STATIC", L"", SS_ETCHEDHORZ, kMargin, y + 4, kDialogWidth - kMargin * 4, 2, 0);
      y += 12;

      create_control(hwnd, L"STATIC", L"Options", 0, kMargin, y + 4, kLabelWidth, 22, 0);
      y += 4;

      HWND nervous = create_control(hwnd, L"BUTTON", L"Nervous mode", BS_AUTOCHECKBOX, kControlLeft, y, 180, 22,
                                    kNervousCheck);
      SendMessageW(nervous, BM_SETCHECK, state->working.nervousMode ? BST_CHECKED : BST_UNCHECKED, 0);
      y += 28;

      HWND persistence = create_control(hwnd, L"BUTTON", L"Use file persistence", BS_AUTOCHECKBOX, kControlLeft, y,
                                        210, 22, kPersistenceCheck);
      SendMessageW(persistence, BM_SETCHECK, state->working.useFilePersistence ? BST_CHECKED : BST_UNCHECKED, 0);
      y += 28;

      HWND spout = create_control(hwnd, L"BUTTON", L"Spout output", BS_AUTOCHECKBOX, kControlLeft, y, 180, 22,
                                  kSpoutCheck);
      SendMessageW(spout, BM_SETCHECK, state->working.spoutEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
      y += 28;

      HWND alwaysontop = create_control(hwnd, L"BUTTON", L"Always on top", BS_AUTOCHECKBOX, kControlLeft, y, 180, 22,
                                        kAlwaysOnTopCheck);
      SendMessageW(alwaysontop, BM_SETCHECK, state->working.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
      y += 28;

      HWND hidewin = create_control(hwnd, L"BUTTON", L"Hide visual window", BS_AUTOCHECKBOX, kControlLeft, y, 210, 22,
                                    kHideWindowCheck);
      SendMessageW(hidewin, BM_SETCHECK, state->working.hideWindow ? BST_CHECKED : BST_UNCHECKED, 0);

      create_control(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, kDialogWidth - 252, kDialogHeight - 78, 70, 28, IDOK);
      create_control(hwnd, L"BUTTON", L"Apply", 0, kDialogWidth - 172, kDialogHeight - 78, 70, 28, kApplyButton);
      create_control(hwnd, L"BUTTON", L"Cancel", 0, kDialogWidth - 92, kDialogHeight - 78, 70, 28, IDCANCEL);
      return 0;
    }
    case WM_HSCROLL:
      if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, kFpsSlider))
        update_fps_label(state, reinterpret_cast<HWND>(lParam));
      if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, kSensitivitySlider))
        update_sens_label(state, reinterpret_cast<HWND>(lParam));
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
          apply_settings_from_controls(hwnd, state);
          state->accepted = true;
          DestroyWindow(hwnd);
          return 0;
        case kApplyButton:
          apply_settings_from_controls(hwnd, state);
          return 0;
        case kSensitivityReset:
        {
          HWND slider = GetDlgItem(hwnd, kSensitivitySlider);
          SendMessageW(slider, TBM_SETPOS, TRUE, 100); // 100 = 1.00
          update_sens_label(state, slider);
          return 0;
        }
        case kFpsReset:
        {
          HWND slider = GetDlgItem(hwnd, kFpsSlider);
          SendMessageW(slider, TBM_SETPOS, TRUE, 60);
          update_fps_label(state, slider);
          return 0;
        }
        case IDCANCEL:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
  }

  return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

bool ShowSettingsDialog(HWND owner, AppSettings& settings, const std::function<void(const AppSettings&)>& onApply)
{
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&controls);

  DialogState state;
  state.settings = &settings;
  state.working = settings;
  state.onApply = onApply;
  state.devices = EnumerateAudioDevices();

  WNDCLASSW wc{};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = dialog_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"FischeSettingsDialog";
  WNDCLASSW existing{};
  if (!GetClassInfoW(GetModuleHandleW(nullptr), wc.lpszClassName, &existing))
    RegisterClassW(&wc);

  RECT ownerRect{};
  if (owner && GetWindowRect(owner, &ownerRect))
  {
  }
  else
  {
    ownerRect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }

  int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - kDialogWidth) / 2;
  int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - kDialogHeight) / 2;

  HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW, wc.lpszClassName, L"fische Settings",
                              WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, kDialogWidth, kDialogHeight,
                              owner, nullptr, wc.hInstance, &state);
  if (!hwnd)
    return false;

  SetWindowTextW(hwnd, L"fische Settings");

  if (owner)
    EnableWindow(owner, FALSE);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  MSG msg{};
  while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    if (!IsDialogMessageW(hwnd, &msg))
    {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (owner)
  {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }

  return state.accepted;
}
