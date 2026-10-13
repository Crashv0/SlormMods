# SlormMods

Mods for [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/). Each has its own folder, readme and releases.

| Mod | What it does |
|---|---|
| [SlormSuite](SlormSuite) | A collection of smaller mods behind one overlay. |
| [NoSlipperyIce](NoSlipperyIce) | Removes the Slippery Ice cataclysm. |

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh <mod>           # <mod>/bin/<mod>.dll and the bin/version.dll proxy
./build.sh <mod> deploy    # also copy into the game's mods/Aurie (set SLORM_GAME, or put it in .env)
./build.sh <mod> play      # deploy, then start the game through Steam
./build.sh <mod> package   # <mod>/dist/<mod>-v<version>.zip
```

`./build.sh` inside a mod's folder does the same for that mod. The mods share `include/` (Aurie and YYToolkit headers), `vendor/imgui/` and the `proxy/` that loads the framework; see [THIRD-PARTY.md](THIRD-PARTY.md).

Releases are tagged `<mod>-v<version>`.

## License

[AGPL-3.0](LICENSE).
