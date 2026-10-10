#include "Hud.hpp"
#include "Version.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
static fs::path g_ConfigPath;

std::recursive_mutex g_Lock;

Appearance g_Appearance;
bool g_SuppressToggle = false;
bool g_Visible = true;

static bool g_KeyWasDown = false;

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[SlormHUD] ") + Format).c_str(), Arguments...);
}

static void LoadConfig()
{
	g_Builds.clear();
	g_ActiveBuild = -1;
	g_Visible = true;
	g_Appearance = Appearance();

	std::ifstream file(g_ConfigPath);
	if (!file)
		return;

	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream words(line);
		std::string first;
		if (!(words >> first) || first[0] == '#')
			continue;
		if (first == "build")
		{
			// build KEY NAME
			std::string key, name, error;
			words >> key;
			std::getline(words >> std::ws, name);
			Build build;
			if (ParseBuild(key, build, error))
			{
				if (!name.empty()) build.name = name;
				g_Builds.push_back(std::move(build));
			}
			continue;
		}
		if (first == "active_build") { words >> g_ActiveBuild; continue; }
		if (first == "visible") { int flag = 1; words >> flag; g_Visible = flag != 0; continue; }
		if (first == "theme") { words >> g_Appearance.theme; continue; }
		if (first == "accent") { int on = 0; words >> on >> g_Appearance.accent[0] >> g_Appearance.accent[1] >> g_Appearance.accent[2]; g_Appearance.custom_accent = on != 0; continue; }
		if (first == "primary") { int on = 0; words >> on >> g_Appearance.primary[0] >> g_Appearance.primary[1] >> g_Appearance.primary[2]; g_Appearance.custom_primary = on != 0; continue; }
		if (first == "opacity") { words >> g_Appearance.opacity; continue; }
		if (first == "scale") { words >> g_Appearance.scale; continue; }
		if (first == "hotkey") { words >> g_Appearance.hotkey; if (g_Appearance.hotkey < 8 || g_Appearance.hotkey > 254) g_Appearance.hotkey = VK_F7; continue; }
	}
	if (g_ActiveBuild >= static_cast<int>(g_Builds.size()))
		g_ActiveBuild = g_Builds.empty() ? -1 : 0;
}

void SaveConfig()
{
	std::ofstream file(g_ConfigPath);
	file << "# Written by the overlay. One build per line: build KEY NAME\n";
	for (const Build& build : g_Builds)
		file << "build " << build.key << ' ' << build.name << '\n';
	file << "active_build " << g_ActiveBuild << '\n'
		<< "visible " << (g_Visible ? 1 : 0) << '\n'
		<< "theme " << g_Appearance.theme << '\n'
		<< "accent " << (g_Appearance.custom_accent ? 1 : 0) << ' ' << g_Appearance.accent[0] << ' ' << g_Appearance.accent[1] << ' ' << g_Appearance.accent[2] << '\n'
		<< "primary " << (g_Appearance.custom_primary ? 1 : 0) << ' ' << g_Appearance.primary[0] << ' ' << g_Appearance.primary[1] << ' ' << g_Appearance.primary[2] << '\n'
		<< "opacity " << g_Appearance.opacity << '\n'
		<< "scale " << g_Appearance.scale << '\n'
		<< "hotkey " << g_Appearance.hotkey << '\n';
}

static void Tick(CInstance* Self, CInstance* Other)
{
	std::lock_guard guard(g_Lock);

	bool key_down = (GetAsyncKeyState(g_Appearance.hotkey) & 0x8000) != 0;
	if (g_SuppressToggle)
	{
		// Wait for the newly bound key to be released.
		if (!key_down)
			g_SuppressToggle = false;
		key_down = true;
	}
	else if (key_down && !g_KeyWasDown && GetForegroundWindow() == GetActiveWindow())
	{
		g_Visible = !g_Visible;
		SaveConfig();
	}
	g_KeyWasDown = key_down;
}

// Runs once per frame, after the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
	if (code != step_code || !code)
	{
		if (step_code || !code || !code->GetName() || std::string_view(code->GetName()) != "gml_Object_obj_ui_next_gen_Step_0")
			return;
		step_code = code;
	}

	// The swap chain only exists once the game is running.
	static bool installed = false;
	if (!installed)
	{
		installed = true;
		Log("step callback active, overlay hook %s", OverlayInstall(g_Yytk) ? "installed" : "FAILED");
	}

	// Input aimed at the overlay must not reach the game's UI.
	if (OverlayWantsMouse())
	{
		g_Yytk->CallBuiltin("mouse_clear", { RValue(1.0) });
		g_Yytk->CallBuiltin("mouse_clear", { RValue(2.0) });
	}
	if (OverlayWantsKeys())
		g_Yytk->CallBuiltin("io_clear", {});

	// Another mod may have run the event already; running it twice doubles every click.
	if (!Event.CalledOriginal()) Event.Call();
	Tick(self, other);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_ConfigPath = ModulePath.parent_path() / "SlormHUD.txt";
	// Builds in the config are read against the stat table.
	LoadGameData();
	LoadConfig();
	OverlaySetIniPath((ModulePath.parent_path() / "SlormHUD.layout.ini").string());

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormHUD] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMHUD_VERSION " loaded, %d stats, %d builds, key %d shows/hides the overlay, config %s", static_cast<int>(g_Stats.size()), static_cast<int>(g_Builds.size()), g_Appearance.hotkey, g_ConfigPath.string().c_str());
	return AURIE_SUCCESS;
}
