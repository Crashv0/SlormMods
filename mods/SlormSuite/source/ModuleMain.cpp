#include "Suite.hpp"
#include <unordered_map>
#include "Version.hpp"
#include <fstream>
using namespace Aurie;
using namespace YYTK;

static fs::path g_ConfigPath;

std::recursive_mutex g_Lock;

Appearance g_Appearance;
bool g_SuppressToggle = false;
bool g_Visible = true;

std::vector<Feature*>& Features()
{
	static std::vector<Feature*> list;
	return list;
}

Feature::Feature(const char* Name, const char* Key, const char* Summary) : name(Name), key(Key), summary(Summary)
{
	Features().push_back(this);
}

static void LoadConfig()
{
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
		if (first == "visible") { int flag = 1; words >> flag; g_Visible = flag != 0; continue; }
		if (first == "theme") { words >> g_Appearance.theme; continue; }
		if (first == "accent") { int on = 0; words >> on >> g_Appearance.accent[0] >> g_Appearance.accent[1] >> g_Appearance.accent[2]; g_Appearance.custom_accent = on != 0; continue; }
		if (first == "primary") { int on = 0; words >> on >> g_Appearance.primary[0] >> g_Appearance.primary[1] >> g_Appearance.primary[2]; g_Appearance.custom_primary = on != 0; continue; }
		if (first == "opacity") { words >> g_Appearance.opacity; continue; }
		if (first == "scale") { words >> g_Appearance.scale; continue; }
		if (first == "hotkey") { words >> g_Appearance.hotkey; if (g_Appearance.hotkey < 8 || g_Appearance.hotkey > 254) g_Appearance.hotkey = Appearance().hotkey; continue; }
		// A feature's lines: "key.enabled 1", "key.name value".
		size_t dot = first.find('.');
		if (dot == std::string::npos)
			continue;
		for (Feature* feature : Features())
		{
			if (first.compare(0, dot, feature->key) != 0)
				continue;
			std::string name = first.substr(dot + 1);
			if (name == "enabled") { int flag = 0; words >> flag; feature->enabled = flag != 0; }
			else feature->Load(name, words);
		}
	}
}

std::string ModFolder()
{
	return g_ConfigPath.parent_path().string();
}

void SaveConfig()
{
	std::ofstream file(g_ConfigPath);
	file << "# Written by the overlay.\n"
		<< "visible " << (g_Visible ? 1 : 0) << '\n'
		<< "theme " << g_Appearance.theme << '\n'
		<< "accent " << (g_Appearance.custom_accent ? 1 : 0) << ' ' << g_Appearance.accent[0] << ' ' << g_Appearance.accent[1] << ' ' << g_Appearance.accent[2] << '\n'
		<< "primary " << (g_Appearance.custom_primary ? 1 : 0) << ' ' << g_Appearance.primary[0] << ' ' << g_Appearance.primary[1] << ' ' << g_Appearance.primary[2] << '\n'
		<< "opacity " << g_Appearance.opacity << '\n'
		<< "scale " << g_Appearance.scale << '\n'
		<< "hotkey " << g_Appearance.hotkey << '\n';
	for (Feature* feature : Features())
	{
		file << feature->key << ".enabled " << (feature->enabled ? 1 : 0) << '\n';
		feature->Save(file);
	}
}

static void Tick(CInstance* Self)
{
	std::lock_guard guard(g_Lock);

	static bool key_was_down = false;
	bool key_down = (GetAsyncKeyState(g_Appearance.hotkey) & 0x8000) != 0;
	if (g_SuppressToggle)
	{
		// Wait for the newly bound key to be released.
		if (!key_down)
			g_SuppressToggle = false;
		key_down = true;
	}
	else if (GetForegroundWindow() == GetActiveWindow() && key_down && !key_was_down)
	{
		g_Visible = !g_Visible;
		SaveConfig();
	}
	key_was_down = key_down;

	for (Feature* feature : Features())
	{
		if (feature->enabled) feature->Tick(Self);
		else if (feature->was_enabled) feature->Off(Self);
		feature->was_enabled = feature->enabled;
	}
#ifdef SLORM_DEV
	DevTick(Self);
#endif
}

// Runs for the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
#ifdef SLORM_DEV
	DevEvent(code);
#endif
	// The hero's step finds the mouse where a walk wants it; nothing else does.
	static std::unordered_map<CCode*, bool> hero_steps;
	if (code)
	{
		auto known = hero_steps.find(code);
		if (known == hero_steps.end())
		{
			std::string_view name = code->GetName() ? code->GetName() : "";
			bool hero = name.rfind("gml_Object_obj_parent_hero_Step", 0) == 0 || (name.rfind("gml_Object_obj_hero_", 0) == 0 && name.find("_Step_") != std::string_view::npos);
			known = hero_steps.emplace(code, hero).first;
		}
		if (known->second)
		{
			WalkScript(1);
			if (!Event.CalledOriginal()) Event.Call();
			WalkScript(-1);
			return;
		}
	}
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
	Tick(self);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_ConfigPath = ModulePath.parent_path() / "SlormSuite.txt";
	LoadConfig();
	OverlaySetIniPath((ModulePath.parent_path() / "SlormSuite.layout.ini").string());
	for (Feature* feature : Features())
		feature->Start();
#ifdef SLORM_DEV
	DevStart(ModulePath.parent_path().string());
#endif

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormSuite] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMSUITE_VERSION " loaded, %d features, key %d shows or hides the overlay, config %s", static_cast<int>(Features().size()), g_Appearance.hotkey, g_ConfigPath.string().c_str());
	return AURIE_SUCCESS;
}
