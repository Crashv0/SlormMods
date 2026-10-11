# SlormSuite

A collection of mods for The Slormancer, bundled into one. Each mod is off until turned on in the overlay (F7).

## Mods

### Macros

On the Macros tab. Presses skills for the hero by rules; the game still applies its own cost, cooldown and animation checks.

- **Priority.** Macros are tried from the top, and a higher one that can start takes over from a lower one.
- **Trigger.** Automatic, while a key is held, or switched by a key, with an option for standing still or moving and a wait between uses.
- **Conditions.** A tree of all of / any of / none of over life, mana, cooldowns, casting, in combat, enemies within a range and time since the macro fired, each with its live value.
- **Steps.** Press a skill or aura (tap or hold), wait, or wait until conditions hold.
- **Pause key.** F5 pauses and resumes every macro. Nothing is pressed in town or in a menu.
- **Save and share.** Saved by name, and exported or imported as one line of text.

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
- **Reveal map.** The minimap and the large map show the whole map and its icons from the start of a floor. It has its own switch and works with the drawings off.

### Auto open chests

On the Mods tab. Uses a chest when the hero is close enough to do it by hand.

- **Chests** on expeditions are opened, then searched.
- **War Chest** in the Great Forge is opened and emptied, once per visit.

### Auto enter Temppopotamus' Bastion

On the Mods tab. Walking up to the Mage Academy entrance starts the Bastion without the menu.

- **Auto exit** takes the exit key and the portal when you reach them; holding a key (Left Ctrl) keeps you in to pick up loot.
- **Skip the result screen** closes it as soon as the game allows.

## Build

Needs VS 2022 Build Tools with the C++ workload, run from WSL.

```
./build.sh          # bin/SlormSuite.dll and the bin/version.dll proxy
./build.sh deploy   # also copies into the game's mods/Aurie (set SLORM_GAME or .env)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormSuite-v<version>.zip
```

A mod is one file in `source/features/`: a `Feature` subclass with a static instance.
