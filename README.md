# SlormSuite

A collection of mods for The Slormancer, bundled into one. Each mod is off until turned on in the overlay (F7).

## Mods

### Auto enter Temppopotamus' Bastion

On the Mods tab. Walking up to the Mage Academy entrance starts the Bastion without the menu.

- **Auto exit** takes the exit key and the portal when you reach them.
- **Skip the result screen** closes it as soon as the game allows.
- A key (Left Ctrl by default) holds the exit back while it is down, to pick up loot.

### Auto pick Great Forge paths

On its own Forge tab. Each wave, when "Choose your Path" opens, the mod takes the path offering what stands highest in your priority lists.

- **Priority lists.** Lists of rewards and challenges, and lists inside lists. Higher lines win. Drag a line onto another to place it above; drag onto the lower half of a list's title to put it inside. A list can be set so any of its lines is as good as another.
- **Want or avoid.** Each entry is either wanted or avoided. An avoided entry rules out paths offering it for every line below it, so put it above what it should outweigh.
- **Everything the forge offers.** Challenges, cataclysms, obelisks, Siege Leader modifiers, curses, spirits, quality and quantity rewards, and the War Chest effects of Legendary paths and the rest after the Siege Leader. The add menus have a search that also reads the descriptions.
- **When nothing listed is offered.** Pick at random, leave the choice to you, always left, middle or right, or the challenge lasting the most waves. A path offered alone is always taken.
- **Delay and hold key.** A number of seconds before picking, with the pick shown on screen as a countdown; click a path yourself to overrule it. No pick is made while the hold key (Left Ctrl by default) is down.
- **Pick log.** What was taken on each wave of the run, and whether by a list, by the rule above, or by you.
- **Save and share.** Lists can be saved under a name, and exported to or imported from the clipboard as one line of text.

The countdown's color and position are under Options, Forge.

## Build

Needs VS 2022 Build Tools with the C++ workload, run from WSL.

```
./build.sh          # bin/SlormSuite.dll and the bin/version.dll proxy
./build.sh deploy   # also copies into the game's mods/Aurie (set SLORM_GAME or .env)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormSuite-v<version>.zip
```

A mod is one file in `source/features/`: a `Feature` subclass with a static instance.
