This fork is only meant to stage minimal code and configuration needed for building UWP binaries.  Any general feature requests should go upstream and then be pulled into this project.

Credits:
- stenzek/duckstation for inspiration of original UWP app and reuse of UWP downloaders.
- SirMangler/pcsx2 for UWPUtils.h

WinRT App.cpp is heavily linked to nogui\_host with platform abstractions stripped out.  It is my intent to create a WinRT platform to use with noguihost however I've ran into issues with debugging DX in that approach and haven't found a resolution yet.  Apologies in advance for DRY violations until this issue is resolved.

