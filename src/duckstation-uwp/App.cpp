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

#include "common/file_system.h"

#include "UWPUtils.h"

// Entrypoint into the emulator
#include "duckstation-nogui/nogui_host.h"
#include "winrt_nogui_platform.h"

using namespace winrt;

using namespace winrt::Windows;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Display::Core;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Composition;

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
      NoGUIHost::externalRun(UWP::GetLocalFolder());
  }

  void SetWindow(CoreWindow const& window) { window.CharacterReceived({this, &App::OnKeyInput}); }

  void OnKeyInput(const IInspectable&, const winrt::Windows::UI::Core::CharacterReceivedEventArgs& args)
  {
    NoGUIHost::ProcessPlatformKeyEvent(static_cast<s32>(args.KeyCode()), !args.KeyStatus().IsKeyReleased);
  }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  winrt::init_apartment();

  CoreApplication::Run(make<App>());

  winrt::uninit_apartment();
}