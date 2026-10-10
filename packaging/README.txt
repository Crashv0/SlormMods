SlormHUD
Follow a slorm-planner build inside The Slormancer.

REQUIREMENTS
  The Slormancer on Steam (Windows, 64-bit)
  Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already
  have it. If the mod does not load, install it from
  https://aka.ms/vs/17/release/vc_redist.x64.exe

INSTALL
  1. Back up your save: %LocalAppData%\The_Slormancer
  2. Open the game folder
     (Steam: right-click the game > Manage > Browse local files)
  3. Extract this zip into it, so version.dll sits next to
     "The Slormancer.exe"
  4. Start the game from Steam as usual

USE
  Press F3 to show or hide the SlormHUD window, and F4 to show or hide the
  outlines on the game's screens; both keys can be changed on the Options
  tab. On the Imports tab, paste a slorm-planner share link and press
  Import.

  Play a character of the build's class and open the ancestral legacy
  map, the skill screen or the inventory: the build is drawn on it.
  Green matches the build, orange is partly there, magenta is still to
  place, red is not in the build.

UNINSTALL
  Delete these from the game folder:
    version.dll
    SlormHUD-README.txt
    aurie.log
    the mods folder
  The game's own files are never changed.

FILES IN THIS ZIP
  version.dll                      loads the mod framework when the game starts
  mods\Native\AurieCore.dll        Aurie Framework
  mods\Aurie\YYToolkit.dll         YYToolkit
  mods\Aurie\SlormHUD.dll     the mod
  mods\licenses\                   licenses and sources of the above

Created while playing: aurie.log, and in mods\Aurie the files
SlormHUD.txt (your settings) and SlormHUD.layout.ini.
