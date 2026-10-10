# SlormSuite

A collection of mods for The Slormancer, bundled into one. Each mod is off until turned on in the overlay (F7).

## Mods

### Auto enter Temppopotamus' Bastion

On the Mods tab. Walking up to the Mage Academy entrance starts the Bastion without the menu.

- **Auto exit** takes the exit key and the portal when you reach them; holding a key (Left Ctrl) keeps you in to pick up loot.
- **Skip the result screen** closes it as soon as the game allows.

### Auto pick Great Forge paths

On the Forge tab. When "Choose your Path" opens, the mod takes the path that stands highest in your priority lists.

- **Priority lists.** Rewards and challenges in lists, and lists inside lists, reordered by dragging. Higher lines win.
- **Want or avoid.** An avoided entry rules out its paths for every line below it.
- **Nothing listed.** Pick at random, leave it to you, always left, middle or right, or the longest challenge.
- **Delay and hold key.** A countdown shows the pick before it is made; holding a key (Left Ctrl) pauses it.
- **Pick log.** What was taken on each wave, and by what.
- **Save and share.** Lists are saved by name, and exported or imported as one line of text.

### Mark drawings on screen

On the ESP tab. Marks every open breach on the map.

- **Tiers.** Breach I, II, III by waves and Breach V for Chaos, each set to Mark, Avoid or Hide.
- **Markers.** A line from the hero, a circle, and name and distance text, colored by kind of breach.
- **Nearest breach.** An arrow by the hero pointing at it.
- **Hide key.** F8 shows or hides every drawing.

## Build

Needs VS 2022 Build Tools with the C++ workload, run from WSL.

```
./build.sh          # bin/SlormSuite.dll and the bin/version.dll proxy
./build.sh deploy   # also copies into the game's mods/Aurie (set SLORM_GAME or .env)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormSuite-v<version>.zip
```

A mod is one file in `source/features/`: a `Feature` subclass with a static instance.
