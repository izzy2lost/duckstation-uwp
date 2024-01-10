// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once

#include <atomic>

#include "common/types.h"
#include "common/windows_headers.h"

#include "duckstation-nogui/nogui_platform.h"

#include "util/window_info.h"

#include <gamingdeviceinformation.h>

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.Core.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.System.h>

class WinRTNoGUIPlatform : public NoGUIPlatform
{
public:
  WinRTNoGUIPlatform();
  ~WinRTNoGUIPlatform();

  bool Initialize();

  void ReportError(const std::string_view& title, const std::string_view& message) override;
  bool ConfirmMessage(const std::string_view& title, const std::string_view& message) override;

  void SetDefaultConfig(SettingsInterface& si) override;

  bool CreatePlatformWindow(std::string title) override;
  bool HasPlatformWindow() const override;
  void DestroyPlatformWindow() override;
  std::optional<WindowInfo> GetPlatformWindowInfo() override;
  void SetPlatformWindowTitle(std::string title) override;

  std::optional<u32> ConvertHostKeyboardStringToCode(const std::string_view& str) override;
  std::optional<std::string> ConvertHostKeyboardCodeToString(u32 code) override;

  void RunMessageLoop() override;
  void ExecuteInMessageLoop(std::function<void()> func) override;
  void QuitMessageLoop() override;

  void SetFullscreen(bool enabled) override;

  bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;

  bool OpenURL(const std::string_view& url) override;
  bool CopyTextToClipboard(const std::string_view& text) override;

private:
  DWORD m_window_thread_id = 0;
  RECT m_windowed_rect = {};
  float m_window_scale = 1.0f;

  std::atomic_bool m_message_loop_running{false};
  std::atomic_bool m_fullscreen{false};
};