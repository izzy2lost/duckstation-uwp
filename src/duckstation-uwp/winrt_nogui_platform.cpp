// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#define NOMINMAX

#include "pch.h"

#include <deque>

#include "winrt_nogui_platform.h"
#include "duckstation-nogui/nogui_host.h"
#include "duckstation-nogui/resource.h"
#include "duckstation-nogui/win32_key_names.h"

#include "core/host.h"

#include "util/imgui_manager.h"
#include "util/input_manager.h"

#include "common/scoped_guard.h"
#include "common/string_util.h"
#include "common/threading.h"

#include <Dbt.h>
#include <shellapi.h>
#include <tchar.h>

#include <gamingdeviceinformation.h>

#include <windows.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.Core.h>

using namespace winrt::Windows;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Display::Core;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Composition;

static winrt::Windows::UI::Core::CoreWindow s_corewind{nullptr};
static std::deque<std::function<void()>> m_event_queue;
static std::mutex m_event_mutex;

namespace WinRTHost {
    static std::optional<WindowInfo> GetPlatformWindowInfo();
    static void ProcessEventQueue();
} // namespace WinRTHost

static float GetWindowScale(HWND hwnd)
{
  // TODO: handle DPI cases
  return 1.0;
}

WinRTNoGUIPlatform::WinRTNoGUIPlatform()
{
  m_message_loop_running.store(true, std::memory_order_release);
}

WinRTNoGUIPlatform::~WinRTNoGUIPlatform()
{
    // No-op
    // TODO: Anything need to happen here?
}

bool WinRTNoGUIPlatform::Initialize()
{
  s_corewind = CoreWindow::GetForCurrentThread();

  auto navigation = winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
  navigation.BackRequested([](const winrt::Windows::Foundation::IInspectable&,
                              const winrt::Windows::UI::Core::BackRequestedEventArgs& args) { args.Handled(true); });


  namespace WGI = winrt::Windows::Gaming::Input;

  try
  {
    WGI::RawGameController::RawGameControllerAdded([](auto&&, const WGI::RawGameController raw_game_controller) {
      m_event_queue.push_back([]() { InputManager::ReloadDevices(); });
    });

    WGI::RawGameController::RawGameControllerRemoved([](auto&&, const WGI::RawGameController raw_game_controller) {
      m_event_queue.push_back([]() { InputManager::ReloadDevices(); });
    });
  }
  catch (winrt::hresult_error)
  {
  }

  return true;
}

void WinRTNoGUIPlatform::ReportError(const std::string_view& title, const std::string_view& message)
{
  //MessageBoxW(m_hwnd, message_copy.c_str(), title_copy.c_str(), MB_ICONERROR | MB_OK);
}

bool WinRTNoGUIPlatform::ConfirmMessage(const std::string_view& title, const std::string_view& message)
{
  //return (MessageBoxW(m_hwnd, message_copy.c_str(), title_copy.c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES);
  return true;
}

void WinRTNoGUIPlatform::SetDefaultConfig(SettingsInterface& si)
{
  // noop
}

bool WinRTNoGUIPlatform::CreatePlatformWindow(std::string title)
{
  return true;
}

bool WinRTNoGUIPlatform::HasPlatformWindow() const
{
  return true;
}

void WinRTNoGUIPlatform::DestroyPlatformWindow()
{
    // No-op
}

std::optional<WindowInfo> WinRTNoGUIPlatform::GetPlatformWindowInfo()
{
  return WinRTHost::GetPlatformWindowInfo() ;
}

void WinRTNoGUIPlatform::SetPlatformWindowTitle(std::string title)
{
    // No-op
}

std::optional<u32> WinRTNoGUIPlatform::ConvertHostKeyboardStringToCode(const std::string_view& str)
{
  std::optional<DWORD> converted(Win32KeyNames::GetKeyCodeForName(str));
  return converted.has_value() ? std::optional<u32>(static_cast<u32>(converted.value())) : std::nullopt;
}

std::optional<std::string> WinRTNoGUIPlatform::ConvertHostKeyboardCodeToString(u32 code)
{
  const char* converted = Win32KeyNames::GetKeyName(code);
  return converted ? std::optional<std::string>(converted) : std::nullopt;
}

void WinRTNoGUIPlatform::RunMessageLoop()
{
  while (m_message_loop_running.load(std::memory_order_acquire))
  {
    WinRTHost::ProcessEventQueue();
  }
}

void WinRTNoGUIPlatform::ExecuteInMessageLoop(std::function<void()> func)
{
  std::unique_lock<std::mutex> lk(m_event_mutex);
  m_event_queue.push_back(func);
}

void WinRTNoGUIPlatform::QuitMessageLoop()
{
  m_message_loop_running.store(false, std::memory_order_release);
}

void WinRTNoGUIPlatform::SetFullscreen(bool enabled)
{
    // No-op
}

bool WinRTNoGUIPlatform::RequestRenderWindowSize(s32 new_window_width, s32 new_window_height)
{
  return true;
}

bool WinRTNoGUIPlatform::OpenURL(const std::string_view& url)
{
  winrt::Windows::Foundation::Uri m_uri{winrt::to_hstring(url)};
  auto asyncOperation = winrt::Windows::System::Launcher::LaunchUriAsync(m_uri);
  asyncOperation.Completed([](winrt::Windows::Foundation::IAsyncOperation<bool> const& sender,
                              winrt::Windows::Foundation::AsyncStatus const asyncStatus) { return; });
}

bool WinRTNoGUIPlatform::CopyTextToClipboard(const std::string_view& text)
{
  return false;
}

std::unique_ptr<NoGUIPlatform> NoGUIPlatform::CreateWinRTPlatform()
{
  std::unique_ptr<WinRTNoGUIPlatform> ret(new WinRTNoGUIPlatform());
  if (!ret->Initialize())
    return {};

  return ret;
}

std::optional<WindowInfo> WinRTHost::GetPlatformWindowInfo()
{
  WindowInfo wi;

  if (s_corewind)
  {
    u32 width = 1920, height = 1080;
    float scale = 1.0;
    GAMING_DEVICE_MODEL_INFORMATION info = {};
    GetGamingDeviceModelInformation(&info);
    if (info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT)
    {
      HdmiDisplayInformation hdi = HdmiDisplayInformation::GetForCurrentView();
      if (hdi)
      {
        width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
        height = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
        // Our UI is based on 1080p, and we're adding a modifier to zoom in by 80%
        scale = ((float)width / 1920.0f) * 1.8f;
      }
    }

    wi.surface_width = width;
    wi.surface_height = height;
    wi.surface_scale = 1.0f;
    wi.type = WindowInfo::Type::Win32;
    wi.window_handle = reinterpret_cast<void*>(winrt::get_abi(s_corewind)); 
  }
  else
  {
    wi.type = WindowInfo::Type::Surfaceless;
  }

  return wi;
}

void WinRTHost::ProcessEventQueue()
{
  if (!m_event_queue.empty())
  {
    std::unique_lock lk(m_event_mutex);
    while (!m_event_queue.empty())
    {
      m_event_queue.front()();
      m_event_queue.pop_front();
    }
  }
}
