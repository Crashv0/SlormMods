# SlormCore

The runtime that mods for [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/) need. Install it once; a mod is then a single DLL in `mods\Aurie`.

It holds:

- a `version.dll` proxy that starts the mod framework when the game does;
- the pinned builds of [Aurie](https://github.com/AurieFramework/Aurie) (v2.0.2) and [YYToolkit](https://github.com/AurieFramework/YYToolkit) (v5.0.0c) the mods are built against;
- `SlormCore.dll`, one overlay window that every mod adds its tab to. Insert shows or hides it.

The game's own files are never changed.

## Install

1. Back up your save: `%LocalAppData%\The_Slormancer`
2. Download the latest `SlormCore` zip from [Releases](https://github.com/crashv0/SlormMods/releases).
3. Extract it into the game folder, so `version.dll` sits next to `The Slormancer.exe`.
4. Start the game from Steam as usual.

To uninstall, delete `version.dll` and the `mods` folder; that removes every mod too.

## For mod authors

Build against the headers in [`include/`](../include) and ship only your DLL, to be placed in `mods\Aurie`. Name the SlormCore version you built against as a requirement, and do not bundle `version.dll`, Aurie, YYToolkit or `SlormCore.dll`.

[SlormExample](../SlormExample) is a whole mod in one file; copy the folder to start. A tab on the overlay is one call:

```cpp
#include <SlormCore/Overlay.hpp>

static void DrawTab()
{
	ImGui::TextUnformatted("Hello");
}

// in ModuleInitialize
SlormCore::AddTab("My mod", DrawTab);
```

- Compile the Dear ImGui in [`vendor/imgui`](../vendor/imgui) into your DLL (the four core files, no backend). It must be the version SlormCore was built with; a mod built with another gets an empty tab and a line in `aurie.log`.
- Do not hook the frame, the window or the mouse yourself, and do not create a Dear ImGui context: the overlay does all of it.
- `SlormCore::AddHud` draws every frame, whether the window is shown or not.
- `SlormCore::Overlay()->WantsMouse()` and `WantsKeys()` say when the overlay is taking input.
- The draw call runs in the game's frame. Guard what it shares with your game-step code.

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh          # bin/SlormCore.dll and bin/version.dll
./build.sh deploy   # also copy them, Aurie and YYToolkit into the game folder (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh package  # dist/SlormCore-v<version>.zip
```

See [THIRD-PARTY.md](../THIRD-PARTY.md). Licensed under AGPL-3.0.
