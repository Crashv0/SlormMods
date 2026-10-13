# SlormMods

Mods for [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/). Each has its own folder, readme and releases.

| Mod | What it does |
|---|---|
| [SlormCore](SlormCore) | The runtime every mod needs. Install it first. |
| [SlormSuite](SlormSuite) | A collection of smaller mods behind one overlay. |
| [SlormReforger](SlormReforger) | Automatic reforging at Friedrich's blacksmith. |
| [SlormHUD](SlormHUD) | Follow a slorm-planner build inside the game. |
| [SlormReslormitizer](SlormReslormitizer) | Operates Rick Manalan's Slormitizer for the odds you ask for. |
| [NoSlipperyIce](NoSlipperyIce) | Removes the Slippery Ice cataclysm. |
| [SlormExample](SlormExample) | A one-file mod with a tab on the overlay, to copy when starting a mod. |

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh <mod>           # <mod>/bin/<mod>.dll
./build.sh <mod> deploy    # also copy into the game folder (set SLORM_GAME, or put it in .env)
./build.sh <mod> play      # deploy, then start the game through Steam
./build.sh <mod> package   # <mod>/dist/<mod>-v<version>.zip
```

`./build.sh` inside a mod's folder does the same for that mod. The mods share `include/` (Aurie and YYToolkit headers), `vendor/imgui/` and the SlormCore runtime that loads the framework; see [THIRD-PARTY.md](THIRD-PARTY.md).

Releases are tagged `<mod>-v<version>`.

## License

[AGPL-3.0](LICENSE).
