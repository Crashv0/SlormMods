# SlormSuite

A collection of tweaks for The Slormancer, bundled into one mod. Each tweak is off until turned on in the overlay (F7).

## Build

Needs VS 2022 Build Tools with the C++ workload, run from WSL.

```
./build.sh          # bin/SlormSuite.dll and the bin/version.dll proxy
./build.sh deploy   # also copies into the game's mods/Aurie (set SLORM_GAME or .env)
./build.sh package  # dist/SlormSuite-v<version>.zip
```

A tweak is one file in `source/features/`: a `Feature` subclass with a static instance.
