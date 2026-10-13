SlormCore
The runtime that mods for The Slormancer need. Install it once; mods
are then single files added next to it.

REQUIREMENTS
  The Slormancer on Steam (Windows, 64-bit)
  Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already
  have it. If mods do not load, install it from
  https://aka.ms/vs/17/release/vc_redist.x64.exe

INSTALL
  1. Back up your save: %LocalAppData%\The_Slormancer
  2. Open the game folder
     (Steam: right-click the game > Manage > Browse local files)
  3. Extract this zip into it, so version.dll sits next to
     "The Slormancer.exe"
  4. Start the game from Steam as usual

USE
  Mods go in mods\Aurie; each mod's zip is extracted into the game
  folder the same way. Press Insert to show or hide the mods window;
  the key and the look can be changed on its Options tab. If a mod does
  not load, look in aurie.log in the game folder.

UNINSTALL
  Delete these from the game folder, which also removes every mod:
    version.dll
    SlormCore-README.txt
    aurie.log
    the mods folder
  The game's own files are never changed.

Created while playing: aurie.log, and in mods\Aurie the files
SlormCore.txt (your settings) and SlormCore.layout.ini.

FILES IN THIS ZIP
  version.dll                      loads the mod framework when the game starts
  mods\Native\AurieCore.dll        Aurie Framework
  mods\Aurie\YYToolkit.dll         YYToolkit
  mods\Aurie\SlormCore.dll         the window the mods share
  mods\licenses\                   licenses and sources of the above
