This fork is intended to provide a (mostly) identical DuckStation experience on Xbox.

Any general feature requests should go upstream and then be pulled into this project.

Credits:
- stenzek/duckstation for the initial UWP development
- SirMangler/pcsx2 for UWPUtils.h and being a general implementation reference
- worleydl for the initial development of this

~~WinRT App.cpp is heavily linked to nogui\_host with platform abstractions stripped out.  It is my intent to create a WinRT platform to use with noguihost however I've ran into issues with debugging DX in that approach and haven't found a resolution yet.  Apologies in advance for DRY violations until this issue is resolved.~~

The project no longer relies on NoGUI in any fashion -- WinRT is it's own Host, using FSUI as the husk for most functions (as XAML doesn't work with this configuration, as far as I've tested.

