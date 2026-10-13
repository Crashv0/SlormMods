#!/usr/bin/env bash
# ./build.sh <mod>          build <mod>/bin/<mod>.dll and the bin/version.dll proxy
# ./build.sh <mod> deploy   build, then copy into the game's mods/Aurie
# ./build.sh <mod> play     deploy, then start the game through Steam
# ./build.sh <mod> package  build, then write <mod>/dist/<mod>-v<version>.zip
# A mod is the name of its folder, such as SlormSuite. Each folder's own build.sh passes its name.
# SLORM_DEV=1 builds a mod's dev variant. Not for release.
set -euo pipefail
cd "$(dirname "$0")"

MOD="${1:-}"
MOD="${MOD%/}"
ACTION="${2:-}"
[ -n "$MOD" ] && [ -f "$MOD/$MOD.vcxproj" ] || { echo "usage: ./build.sh <mod> [deploy|play|package]; mods: $(ls -d */*.vcxproj | grep -v '^proxy/' | cut -d/ -f1 | tr '\n' ' ')" >&2; exit 1; }

# "deploy" and "play" need the game folder: export SLORM_GAME, or put SLORM_GAME="..." in .env (untracked).
[ -f .env ] && . ./.env
GAME="${SLORM_GAME:-}"
# vswhere lives in a fixed place under Program Files (x86); ask Windows where that is. VSWHERE overrides it.
if [ -z "${VSWHERE:-}" ]; then
	PROGRAM_FILES=$(cmd.exe /c 'echo %ProgramFiles(x86)%' 2>/dev/null | tr -d '\r')
	VSWHERE="$(wslpath -u "$PROGRAM_FILES")/Microsoft Visual Studio/Installer/vswhere.exe"
fi

[ -f "$VSWHERE" ] || { echo "vswhere not found: install VS 2022 Build Tools with the C++ workload" >&2; exit 1; }
MSBUILD_WIN=$("$VSWHERE" -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | tr -d '\r' | head -1)
[ -n "$MSBUILD_WIN" ] || { echo "MSBuild not found" >&2; exit 1; }

VARIANT=release
if [ -n "${SLORM_DEV:-}" ]; then
	[ "$ACTION" != package ] || { echo "a dev build cannot be packaged" >&2; exit 1; }
	VARIANT=dev
	# cl reads extra options from CL.
	export CL=/DSLORM_DEV WSLENV="${WSLENV:+$WSLENV:}CL"
fi
# MSBuild does not see the define change, so rebuild when the variant does.
mkdir -p "$MOD/obj"
[ "$(cat "$MOD/obj/variant" 2>/dev/null)" = "$VARIANT" ] || find "$MOD/source" -name '*.cpp' -exec touch {} +
echo "$VARIANT" > "$MOD/obj/variant"

"$(wslpath -u "$MSBUILD_WIN")" "$(wslpath -w "$MOD/$MOD.vcxproj")" -nologo -v:minimal -p:Configuration=Release -p:Platform=x64
"$(wslpath -u "$MSBUILD_WIN")" "$(wslpath -w proxy/Proxy.vcxproj)" -nologo -v:minimal -p:Configuration=Release -p:Platform=x64

if [ "$ACTION" = deploy ] || [ "$ACTION" = play ]; then
	[ -d "$GAME/mods/Aurie" ] || { echo "set SLORM_GAME to the game folder (it must contain mods/Aurie)" >&2; exit 1; }
	# The list is read whole: a grep that stops early would fail the pipe.
	running() { [ "$(tasklist.exe 2>/dev/null | grep -ci "^The Slormancer.exe")" != 0 ]; }
	if running; then echo "the game is running; the build goes in when it closes"; else echo "the game is closed"; fi
	until cp "$MOD/bin/$MOD.dll" "$GAME/mods/Aurie/" 2>/dev/null; do
		sleep 3
	done
	echo "deployed to $GAME/mods/Aurie"
	if [ "$ACTION" = play ]; then
		# Steam's id for the game is kept next to its exe; Steam itself sits three folders up.
		APP=$(tr -d '\r\n' < "$GAME/steam_appid.txt")
		launch() {
			if [ -f "$GAME/../../../steam.exe" ]; then
				(cd "$GAME/../../.." && ./steam.exe -applaunch "$APP" >/dev/null 2>&1 &)
			else
				(cd /mnt/c && cmd.exe /c start "" "steam://rungameid/$APP" >/dev/null 2>&1) || true
			fi
		}
		# Steam ignores a launch made while it is still closing the game down, so ask again until it shows.
		for try in 1 2 3; do
			launch
			for wait in 1 2 3 4 5 6 7 8 9 10; do
				running && { echo "the game is running with the new build"; exit 0; }
				sleep 3
			done
		done
		echo "the game did not start; start it from Steam" >&2
		exit 1
	fi
fi

if [ "$ACTION" = package ]; then
	VERSION=$(sed -n 's/^#define [A-Z]*_VERSION "\(.*\)"/\1/p' "$MOD/source/Version.hpp")
	[ -n "$VERSION" ] || { echo "no version in $MOD/source/Version.hpp" >&2; exit 1; }
	mkdir -p "$MOD/dist" redist
	rm -f "$MOD/dist/$MOD-v$VERSION.zip"

	# The frameworks are not kept in the repo: fetch the pinned official builds and check them.
	fetch() {
		[ -f "redist/$1" ] || curl -fsSL -o "redist/$1" "$2"
		echo "$3  redist/$1" | sha256sum -c --quiet - || { echo "redist/$1 is not the pinned build; delete it and retry" >&2; exit 1; }
	}
	fetch AurieCore.dll https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AurieCore.dll \
		18e3a1de980f487a6b3858b673d2030e96984dd96de3a047b43a263a5ba829ae
	fetch YYToolkit.dll https://github.com/AurieFramework/YYToolkit/releases/download/v5.0.0c/YYToolkit.dll \
		ae7809f136f9222e5f49393d7ce7c3ad4c375c171308e2414660946d4ba0378a
	# Laid out to be extracted straight into the game folder.
	python3 - "$MOD" "$MOD/dist/$MOD-v$VERSION.zip" <<'PY'
import sys, zipfile
mod, target = sys.argv[1], sys.argv[2]
files = {
    "version.dll": "bin/version.dll",
    f"{mod}-README.txt": f"{mod}/packaging/README.txt",
    "mods/Native/AurieCore.dll": "redist/AurieCore.dll",
    "mods/Aurie/YYToolkit.dll": "redist/YYToolkit.dll",
    f"mods/Aurie/{mod}.dll": f"{mod}/bin/{mod}.dll",
    "mods/licenses/NOTICE.txt": f"{mod}/packaging/NOTICE.txt",
    "mods/licenses/AGPL-3.0.txt": "LICENSE",
}
# Only the mods with an overlay are built with Dear ImGui.
if "imgui" in open(f"{mod}/{mod}.vcxproj").read():
    files["mods/licenses/DearImGui-MIT.txt"] = "vendor/imgui/LICENSE.txt"
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, source in files.items():
        archive.write(source, name)
PY
	echo "wrote $MOD/dist/$MOD-v$VERSION.zip"
fi
