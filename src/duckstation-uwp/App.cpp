#define NOMINMAX

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

#include "core/achievements.h"
#include "core/fullscreen_ui.h"
#include "core/game_list.h"
#include "core/host.h"
#include "core/settings.h"
#include "core/system.h"
#include "common/crash_handler.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/threading.h"
#include "util/imgui_manager.h"
#include "util/input_manager.h"
#include "util/platform_misc.h"

#include "UWPUtils.h"

#include <thread>
#include <chrono>

#include "win32_key_names.h"


// Entrypoint into the emulator
#include "duckstation-nogui/nogui_host.h"
#include "winrt_nogui_platform.h"

Log_SetChannel(App);

using namespace winrt;

using namespace winrt::Windows;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Display::Core;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Composition;


//////////////////////////////////////////////////////////////////////////
// Local variable declarations
//////////////////////////////////////////////////////////////////////////
static winrt::Windows::UI::Core::CoreWindow* s_corewind = NULL;

static bool s_batch_mode = false;
static bool s_is_fullscreen = false;
static bool s_was_paused_by_focus_loss = false;

static Threading::Thread s_cpu_thread;
static Threading::KernelSemaphore s_platform_window_updated;
static std::atomic_bool s_running{false};
static std::mutex s_cpu_thread_events_mutex;
static std::condition_variable s_cpu_thread_event_done;
static std::condition_variable s_cpu_thread_event_posted;
static std::deque<std::pair<std::function<void()>, bool>> s_cpu_thread_events;
static u32 s_blocking_cpu_events_pending = 0; // TODO: Token system would work better here.

static std::mutex s_async_op_mutex;
static std::thread s_async_op_thread;
static std::string s_uwpPath;
static FullscreenUI::ProgressCallback* s_async_op_progress = nullptr;


namespace WinRTHost {
  static bool InitializeConfig();
  static bool InBatchMode();
  static std::optional<WindowInfo> GetPlatformWindowInfo();

  static void StartCPUThread();
  static void StopCPUThread();
  static void CPUThreadEntryPoint();
  static void CPUThreadMainLoop();
  static void StartAsyncOp(std::function<void(ProgressCallback*)> callback);
  static void CancelAsyncOp();
  static void AsyncOpThreadEntryPoint(std::function<void(ProgressCallback*)> callback);
  static void ProcessCPUThreadEvents(bool block);
  } // namespace WinRTHost

bool WinRTHost::InitializeConfig()
{
    // TODO: Might need thise
}

bool WinRTHost::InBatchMode()
{
    return s_batch_mode;
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
      wi.window_handle = reinterpret_cast<void*>(winrt::get_abi(*s_corewind));
    }
    else
    {
      wi.type = WindowInfo::Type::Surfaceless;
    }

    return wi;
}

void WinRTHost::StartCPUThread()
{
    s_running.store(true, std::memory_order_release);
    s_cpu_thread.Start(CPUThreadEntryPoint);
}

void WinRTHost::StopCPUThread()
{
    if (!s_cpu_thread.Joinable())
      return;

    {
      std::unique_lock lock(s_cpu_thread_events_mutex);
      s_running.store(false, std::memory_order_release);
      s_cpu_thread_event_posted.notify_one();
    }
    s_cpu_thread.Join();
}


void WinRTHost::CPUThreadEntryPoint()
{
    Threading::SetNameOfCurrentThread("CPU Thread");

    // input source setup must happen on emu thread
    ::System::Internal::ProcessStartup();


    // start the fullscreen UI and get it going
    if (Host::CreateGPUDevice(Settings::GetRenderAPIForRenderer(g_settings.gpu_renderer)) && FullscreenUI::Initialize())
    {
      // kick a game list refresh if we're not in batch mode
      if (!InBatchMode())
        Host::RefreshGameListAsync(false);

      CPUThreadMainLoop();

      Host::CancelGameListRefresh();
    }

    // finish any events off (e.g. shutdown system with save)
    ProcessCPUThreadEvents(false);

    if (::System::IsValid())
      ::System::ShutdownSystem(false);
    Host::ReleaseGPUDevice();
    Host::ReleaseRenderWindow();

    ::System::Internal::ProcessShutdown();
}

void WinRTHost::CPUThreadMainLoop()
{
    while (s_running.load(std::memory_order_acquire))
    {
      if (::System::IsRunning())
      {
        ::System::Execute();
        continue;
      }

      Host::PumpMessagesOnCPUThread();
      ::System::Internal::IdlePollUpdate();
      ::System::PresentDisplay(false);
      if (!g_gpu_device->IsVsyncEnabled())
        g_gpu_device->ThrottlePresentation();
    }
}

void WinRTHost::ProcessCPUThreadEvents(bool block)
{
    std::unique_lock lock(s_cpu_thread_events_mutex);

    for (;;)
    {
      if (s_cpu_thread_events.empty())
      {
        if (!block || !s_running.load(std::memory_order_acquire))
          return;
      }

      // return after processing all events if we had one
      block = false;

      auto event = std::move(s_cpu_thread_events.front());
      s_cpu_thread_events.pop_front();
      lock.unlock();
      event.first();
      lock.lock();

      if (event.second)
      {
        s_blocking_cpu_events_pending--;
        s_cpu_thread_event_done.notify_one();
      }
    }
}


void WinRTHost::StartAsyncOp(std::function<void(ProgressCallback*)> callback)
{
    CancelAsyncOp();
    s_async_op_thread = std::thread(AsyncOpThreadEntryPoint, std::move(callback));
}

void WinRTHost::CancelAsyncOp()
{
    std::unique_lock lock(s_async_op_mutex);
    if (!s_async_op_thread.joinable())
      return;

    if (s_async_op_progress)
      s_async_op_progress->SetCancelled();

    lock.unlock();
    s_async_op_thread.join();
}

void WinRTHost::AsyncOpThreadEntryPoint(std::function<void(ProgressCallback*)> callback)
{
    Threading::SetNameOfCurrentThread("Async Op");

    FullscreenUI::ProgressCallback fs_callback("async_op");
    std::unique_lock lock(s_async_op_mutex);
    s_async_op_progress = &fs_callback;

    lock.unlock();
    callback(&fs_callback);
    lock.lock();

    s_async_op_progress = nullptr;
}

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
    std::unique_lock lock(s_cpu_thread_events_mutex);
    s_cpu_thread_events.emplace_back(std::move(function), block);
    s_cpu_thread_event_posted.notify_one();
    if (block)
      s_cpu_thread_event_done.wait(lock, []() { return s_blocking_cpu_events_pending == 0; });
}


void Host::RefreshGameListAsync(bool invalidate_cache)
{
    WinRTHost::StartAsyncOp(
      [invalidate_cache](ProgressCallback* progress) { GameList::Refresh(invalidate_cache, false, progress); });
}

void Host::CancelGameListRefresh()
{
    WinRTHost::CancelAsyncOp();
}



// Host impls
void Host::ReportErrorAsync(const std::string_view& title, const std::string_view& message)
{
  if (!title.empty() && !message.empty())
  {
    Log_ErrorPrintf("ReportErrorAsync: %.*s: %.*s", static_cast<int>(title.size()), title.data(),
                    static_cast<int>(message.size()), message.data());
  }
  else if (!message.empty())
  {
    Log_ErrorPrintf("ReportErrorAsync: %.*s", static_cast<int>(message.size()), message.data());
  }
}

bool Host::ConfirmMessage(const std::string_view& title, const std::string_view& message)
{
  if (!title.empty() && !message.empty())
  {
    Log_ErrorPrintf("ConfirmMessage: %.*s: %.*s", static_cast<int>(title.size()), title.data(),
                    static_cast<int>(message.size()), message.data());
  }
  else if (!message.empty())
  {
    Log_ErrorPrintf("ConfirmMessage: %.*s", static_cast<int>(message.size()), message.data());
  }

  return true;
}

void Host::ReportDebuggerMessage(const std::string_view& message)
{
  Log_ErrorPrintf("ReportDebuggerMessage: %.*s", static_cast<int>(message.size()), message.data());
}

std::span<const std::pair<const char*, const char*>> Host::GetAvailableLanguageList()
{
  return {};
}

bool Host::ChangeLanguage(const char* new_language)
{
  return false;
}

void Host::AddFixedInputBindings(SettingsInterface& si)
{
}

void Host::OnInputDeviceConnected(const std::string_view& identifier, const std::string_view& device_name)
{
  Host::AddKeyedOSDMessage(fmt::format("InputDeviceConnected-{}", identifier),
                           fmt::format("Input device {0} ({1}) connected.", device_name, identifier), 10.0f);
}

void Host::OnInputDeviceDisconnected(const std::string_view& identifier)
{
  Host::AddKeyedOSDMessage(fmt::format("InputDeviceConnected-{}", identifier),
                           fmt::format("Input device {} disconnected.", identifier), 10.0f);
}

s32 Host::Internal::GetTranslatedStringImpl(const std::string_view& context, const std::string_view& msg, char* tbuf,
                                            size_t tbuf_space)
{
  if (msg.size() > tbuf_space)
    return -1;
  else if (msg.empty())
    return 0;

  std::memcpy(tbuf, msg.data(), msg.size());
  return static_cast<s32>(msg.size());
}

std::optional<std::vector<u8>> Host::ReadResourceFile(const char* filename)
{
  const std::string path(Path::Combine(EmuFolders::Resources, filename));
  std::optional<std::vector<u8>> ret(FileSystem::ReadBinaryFile(path.c_str()));
  if (!ret.has_value())
    Log_ErrorPrintf("Failed to read resource file '%s'", filename);
  return ret;
}

bool Host::ResourceFileExists(const char* filename)
{
  const std::string path(Path::Combine(EmuFolders::Resources, filename));
  return FileSystem::FileExists(path.c_str());
}

std::optional<std::string> Host::ReadResourceFileToString(const char* filename)
{
  const std::string path(Path::Combine(EmuFolders::Resources, filename));
  std::optional<std::string> ret(FileSystem::ReadFileToString(path.c_str()));
  if (!ret.has_value())
    Log_ErrorPrintf("Failed to read resource file to string '%s'", filename);
  return ret;
}

std::optional<std::time_t> Host::GetResourceFileTimestamp(const char* filename)
{
  const std::string path(Path::Combine(EmuFolders::Resources, filename));
  FILESYSTEM_STAT_DATA sd;
  if (!FileSystem::StatFile(path.c_str(), &sd))
  {
    Log_ErrorPrintf("Failed to stat resource file '%s'", filename);
    return std::nullopt;
  }

  return sd.ModificationTime;
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
  return WinRTHost::GetPlatformWindowInfo();
}

void Host::ReleaseRenderWindow()
{
    // No-op
}

void Host::OnSystemStarting()
{
  // TODO: Revisit
  //s_was_paused_by_focus_loss = false;
}

void Host::OnSystemStarted()
{
}

void Host::OnSystemPaused()
{
}

void Host::OnSystemResumed()
{
}

void Host::OnSystemDestroyed()
{
}

void Host::OnIdleStateChanged()
{
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{

}

void Host::OpenURL(const std::string_view& url)
{

}

bool Host::CopyTextToClipboard(const std::string_view& text)
{
  return false;
}

void Host::OnPerformanceCountersUpdated()
{
  // noop
}

void Host::OnGameChanged(const std::string& disc_path, const std::string& game_serial, const std::string& game_name)
{
  Log_VerbosePrintf("Host::OnGameChanged(\"%s\", \"%s\", \"%s\")", disc_path.c_str(), game_serial.c_str(),
                    game_name.c_str());
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
  // noop
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
  // noop
}

void Host::OnAchievementsRefreshed()
{
  // noop
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
  // noop
}

void Host::SetMouseMode(bool relative, bool hide_cursor)
{
  // noop
}

void Host::PumpMessagesOnCPUThread()
{
  WinRTHost::ProcessCPUThreadEvents(false);
  //NoGUIHost::ProcessCPUThreadPlatformMessages(); // TODO: May need something like this
}

bool Host::IsFullscreen()
{
  return true;
}

void Host::SetFullscreen(bool enabled)
{

}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Settings& old_settings)
{
}

void Host::CommitBaseSettingChanges()
{
  //NoGUIHost::SaveSettings();
}


std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
  return WinRTHost::GetPlatformWindowInfo();
}

void Host::RequestExit(bool allow_confirm)
{
  if (::System::IsValid())
  {
    Host::RunOnCPUThread([]() { ::System::ShutdownSystem(g_settings.save_state_on_exit); });
  }

  // clear the running flag, this'll break out of the main CPU loop once the VM is shutdown.
  s_running.store(false, std::memory_order_release);
}

void Host::RequestSystemShutdown(bool allow_confirm, bool save_state)
{
  if (::System::IsValid())
  {
    Host::RunOnCPUThread([save_state]() { ::System::ShutdownSystem(save_state); });
  }
}


std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view& str)
{
  std::optional<DWORD> converted(Win32KeyNames::GetKeyCodeForName(str));
  return converted.has_value() ? std::optional<u32>(static_cast<u32>(converted.value())) : std::nullopt;

}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
  const char* converted = Win32KeyNames::GetKeyName(code);
  return converted ? std::optional<std::string>(converted) : std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
  return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()


struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
  IFrameworkView CreateView() { return *this; }

  void Initialize(CoreApplicationView const& v)
  {
    v.Activated({this, &App::OnActivate});
  }

  void OnActivate(const winrt::Windows::ApplicationModel::Core::CoreApplicationView&,
                  const winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs& args)
  {
      // TODO: Bring this back for launchpass folks
  }

  void Load(hstring const&) {}

  void Uninitialize() {}

  void Run() { 
    CoreWindow window = CoreWindow::GetForCurrentThread();
    s_corewind = &window;
    window.Activate();

    CrashHandler::Install();

    // the rest of initialization happens on the CPU thread.
    WinRTHost::StartCPUThread();

    while (s_running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    WinRTHost::CancelAsyncOp();
    WinRTHost::StopCPUThread();

    // Ensure log is flushed.
    Log::SetFileOutputParams(false, nullptr);

    //s_base_settings_interface.reset();
  }

  void SetWindow(CoreWindow const& window) { window.CharacterReceived({this, &App::OnKeyInput}); }

  void OnKeyInput(const IInspectable&, const winrt::Windows::UI::Core::CharacterReceivedEventArgs& args)
  {
    //NoGUIHost::ProcessPlatformKeyEvent(static_cast<s32>(args.KeyCode()), !args.KeyStatus().IsKeyReleased);
  }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  winrt::init_apartment();

  CoreApplication::Run(make<App>());

  winrt::uninit_apartment();
}