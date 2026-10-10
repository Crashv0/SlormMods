# SlormHUD

A build planner inside [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/).

SlormHUD imports a build from a [slorm-planner](https://cayrac.github.io/slorm-planner/) share link and shows it in a window over the game: skills and upgrades, reaper and runes, ancestral legacies, attributes and gear, named from the game's own data.

It only reads. It does not edit the character, items, or save.

## Use

1. Press F7 to show or hide the window. The key can be changed on the Options tab.
2. On the Imports tab, paste a slorm-planner share link and press Import. Several builds can be kept.
3. The Build tab shows the selected build.

## How it works

The Slormancer is a GameMaker game. The mod is a plugin for [YYToolkit](https://github.com/AurieFramework/YYToolkit), which runs on the [Aurie Framework](https://github.com/AurieFramework/Aurie). The window is drawn with [Dear ImGui](https://github.com/ocornut/imgui). It runs alongside [SlormReforger](https://github.com/wistb/SlormReforger).

## Building

Needs Windows with the Visual Studio 2022 Build Tools (C++ workload). The build script runs from WSL.

```sh
./build.sh           # build bin/SlormHUD.dll and bin/version.dll
./build.sh deploy    # build, then copy the mod into the game's mods/Aurie
./build.sh package   # build, then write dist/SlormHUD-v<version>.zip
```

- `deploy` needs the game folder: export `SLORM_GAME`, or put `SLORM_GAME="..."` in a file named `.env`.
- `package` downloads the pinned Aurie and YYToolkit builds and checks each against a SHA-256 before adding it to the zip.

Where the vendored sources come from is listed in [THIRD-PARTY.md](THIRD-PARTY.md).

## License

[AGPL-3.0](LICENSE). The mod is built against Aurie and YYToolkit, which are AGPL-3.0, and the release zip includes their official builds.

## Credits

Aurie Framework and YYToolkit by the AurieFramework team. Dear ImGui by Omar Cornut. The share link format is from [slorm-planner](https://github.com/cayrac/slorm-planner) by cayrac.
