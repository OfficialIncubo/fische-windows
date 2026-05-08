#pragma once

#include "app_settings.h"

#include <functional>
#include <windows.h>

bool ShowSettingsDialog(HWND owner, AppSettings& settings, const std::function<void(const AppSettings&)>& onApply);
