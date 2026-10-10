#include "Hud.hpp"
#include "Version.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_set>
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
bool g_Marks = true;

Live g_Live;

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
	g_Marks = true;
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
		if (first == "marks") { int flag = 1; words >> flag; g_Marks = flag != 0; continue; }
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
		<< "marks " << (g_Marks ? 1 : 0) << '\n'
		<< "theme " << g_Appearance.theme << '\n'
		<< "accent " << (g_Appearance.custom_accent ? 1 : 0) << ' ' << g_Appearance.accent[0] << ' ' << g_Appearance.accent[1] << ' ' << g_Appearance.accent[2] << '\n'
		<< "primary " << (g_Appearance.custom_primary ? 1 : 0) << ' ' << g_Appearance.primary[0] << ' ' << g_Appearance.primary[1] << ' ' << g_Appearance.primary[2] << '\n'
		<< "opacity " << g_Appearance.opacity << '\n'
		<< "scale " << g_Appearance.scale << '\n'
		<< "hotkey " << g_Appearance.hotkey << '\n';
}

static double ToNumber(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
	{
		try { return std::stod(Value.ToString()); }
		catch (...) { return 0; }
	}
	return Value.IsNumberConvertible() ? Value.ToDouble() : 0;
}

// One hero's row of a [hero][index] table, as numbers.
static std::vector<int> HeroRow(const RValue& Table, int Hero)
{
	std::vector<int> out;
	if (!Table.IsArray())
		return out;
	std::vector<RValue> heroes = Table.ToVector();
	if (Hero < 0 || Hero >= static_cast<int>(heroes.size()) || !heroes[Hero].IsArray())
		return out;
	for (const RValue& value : heroes[Hero].ToVector())
		out.push_back(static_cast<int>(ToNumber(value)));
	return out;
}

static void ReadLive(CInstance* Self)
{
	auto global = [](const char* Name) { return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name) }); };
	auto local = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };

	Live live;
	live.hero = static_cast<int>(ToNumber(global("current_hero")));
	RValue menu = local("menu_show");
	if (menu.IsString()) live.menu = menu.ToString();
	// While the legacy screen is open the UI works on its own copies, so Undo can drop them.
	if (live.menu == "elements")
	{
		live.legacy_ranks = HeroRow(local("temp_elems"), live.hero);
		live.stones = HeroRow(local("temp_elems_equipped"), live.hero);
		live.realms = HeroRow(local("temp_elems_realms_equipped"), live.hero);
	}
	if (live.legacy_ranks.empty() || live.stones.empty() || live.realms.empty())
	{
		live.legacy_ranks = HeroRow(global("elements_rank"), live.hero);
		live.stones = HeroRow(global("elements_equipped"), live.hero);
		live.realms = HeroRow(global("elements_realms"), live.hero);
	}
	live.valid = !live.legacy_ranks.empty() && !live.stones.empty() && !live.realms.empty();
	g_Live = std::move(live);
}

static int At(const std::vector<int>& Values, int Index)
{
	return Index >= 0 && Index < static_cast<int>(Values.size()) ? Values[Index] : 0;
}

// Todo is in the build and still to place; Wrong is placed and not in the build, or in it with no rank.
enum class Mark { None, Match, Partial, Wrong, Todo };
// GameMaker colours are blue, green, red from the high byte down: green, orange, red, blue.
static const double MARK_COLORS[] = { 0, 0x5AD25A, 0x2896F5, 0x3C3CF0, 0xF59646 };

// A ring around the middle of a box. Fill is how much of the box the ring spans.
static void Outline(double Left, double Top, double Right, double Bottom, Mark Kind, double Fill)
{
	g_Yytk->CallBuiltin("draw_set_color", { RValue(MARK_COLORS[static_cast<int>(Kind)]) });
	// The game's shapes land most of a pixel right and down of its sprites.
	constexpr double nudge = 0.75;
	double x = (Left + Right) / 2 - nudge, y = (Top + Bottom) / 2 - nudge, radius = (Right - Left) / 2 * Fill;
	// Drawn as a strip of triangles: the game's own circle lands off the centre.
	constexpr int steps = 40;
	constexpr double width = 3;
	g_Yytk->CallBuiltin("draw_primitive_begin", { RValue(5.0) });
	for (int i = 0; i <= steps; i++)
	{
		double angle = i * 6.283185307179586 / steps, across = cos(angle), down = sin(angle);
		g_Yytk->CallBuiltin("draw_vertex", { RValue(x + across * radius), RValue(y + down * radius) });
		g_Yytk->CallBuiltin("draw_vertex", { RValue(x + across * (radius + width)), RValue(y + down * (radius + width)) });
	}
	g_Yytk->CallBuiltin("draw_primitive_end", {});
}

// Text centred under a point, edged in black so it reads over the map.
static void Label(double X, double Y, const std::string& Text, Mark Kind)
{
	static const int edge[][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
	g_Yytk->CallBuiltin("draw_set_color", { RValue(0.0) });
	for (const auto& offset : edge)
		g_Yytk->CallBuiltin("draw_text", { RValue(X + offset[0]), RValue(Y + offset[1]), RValue(Text.c_str()) });
	g_Yytk->CallBuiltin("draw_set_color", { RValue(MARK_COLORS[static_cast<int>(Kind)]) });
	g_Yytk->CallBuiltin("draw_text", { RValue(X), RValue(Y), RValue(Text.c_str()) });
}

// Outlines the ancestral legacy map against the build. The UI keeps a list of what can be
// clicked this frame, with screen rectangles: mba for legacies, mbas for stone slots.
static void MarkLegacies(CInstance* Self, const Build& Build)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	RValue legacies = get("mba"), slots = get("mbas");
	if (!legacies.IsArray() || !slots.IsArray())
		return;
	// The map is clipped to its panel; the lists are not.
	double left = ToNumber(get("tree_x")), top = ToNumber(get("tree_y"));
	double right = left + ToNumber(get("default_tree_w")), bottom = top + ToNumber(get("default_tree_h"));
	auto inside = [&](double X1, double Y1, double X2, double Y2) { return X2 >= left && X1 <= right && Y2 >= top && Y1 <= bottom; };

	// The lists stand still while the map is dragged, so shift them by how far it has moved since they last changed.
	static double seen_mark = 0, seen_x = 0, seen_y = 0;
	double pan_x = ToNumber(get("tree_off_x")), pan_y = ToNumber(get("tree_off_y")), zoom = ToNumber(get("tree_zoom"));
	std::vector<RValue> slot_list = slots.ToVector();
	double mark = static_cast<double>(slot_list.size());
	if (!slot_list.empty() && slot_list[0].IsArray())
		for (const RValue& field : slot_list[0].ToVector()) mark = mark * 31 + ToNumber(field);
	if (mark != seen_mark)
	{
		seen_mark = mark;
		seen_x = pan_x;
		seen_y = pan_y;
	}
	double shift_x = (seen_x - pan_x) * zoom, shift_y = (seen_y - pan_y) * zoom;

	RValue old_color = g_Yytk->CallBuiltin("draw_get_color", {}), old_alpha = g_Yytk->CallBuiltin("draw_get_alpha", {});
	RValue old_halign = g_Yytk->CallBuiltin("draw_get_halign", {}), old_valign = g_Yytk->CallBuiltin("draw_get_valign", {});
	g_Yytk->CallBuiltin("draw_set_alpha", { RValue(1.0) });
	// Centred, hanging from the top.
	g_Yytk->CallBuiltin("draw_set_halign", { RValue(1.0) });
	g_Yytk->CallBuiltin("draw_set_valign", { RValue(0.0) });
	// Cut the outlines off at the panel's edge. The scissor is in window pixels, the panel in GUI units.
	RValue old_scissor = g_Yytk->CallBuiltin("gpu_get_scissor", {});
	double scale_x = ToNumber(g_Yytk->CallBuiltin("window_get_width", {})) / (std::max)(1.0, ToNumber(g_Yytk->CallBuiltin("display_get_gui_width", {})));
	double scale_y = ToNumber(g_Yytk->CallBuiltin("window_get_height", {})) / (std::max)(1.0, ToNumber(g_Yytk->CallBuiltin("display_get_gui_height", {})));
	g_Yytk->CallBuiltin("gpu_set_scissor", { RValue(left * scale_x), RValue(top * scale_y), RValue((right - left) * scale_x), RValue((bottom - top) * scale_y) });

	for (const RValue& entry : legacies.ToVector())
	{
		if (!entry.IsArray())
			continue;
		// [name, ?, x, y, width, height, ?, sprite, legacy id]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 9 || !fields[0].IsString() || fields[0].ToString() != "Elements")
			continue;
		int id = static_cast<int>(ToNumber(fields[8]));
		double x = ToNumber(fields[2]) + shift_x, y = ToNumber(fields[3]) + shift_y, width = ToNumber(fields[4]), height = ToNumber(fields[5]);
		if (!inside(x, y, x + width, y + height))
			continue;
		const Ranked* wanted = nullptr;
		for (const Ranked& legacy : Build.legacies)
			if (legacy.id == id) wanted = &legacy;
		bool active = At(g_Live.realms, LegacyRealm(id)) != 0;
		int rank = At(g_Live.legacy_ranks, id);
		Mark mark = Mark::None;
		if (wanted) mark = !active ? Mark::Todo : rank == 0 ? Mark::Wrong : rank == wanted->rank ? Mark::Match : Mark::Partial;
		else if (active) mark = Mark::Wrong;
		if (mark == Mark::None)
			continue;
		Outline(x, y, x + width, y + height, mark, 1.0);
		if (!wanted)
			continue;
		// Ranks put in against the build's: "2 < 10", or the rank alone once it matches.
		std::string label = std::to_string(rank);
		if (rank != wanted->rank)
			label += (rank < wanted->rank ? " < " : " > ") + std::to_string(wanted->rank);
		Label(x + width / 2, y + height + 6, label, mark);
	}

	for (const RValue& entry : slot_list)
	{
		if (!entry.IsArray())
			continue;
		// [left, right, top, bottom, slot id, ...]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 5)
			continue;
		double x1 = ToNumber(fields[0]) + shift_x, x2 = ToNumber(fields[1]) + shift_x, y1 = ToNumber(fields[2]) + shift_y, y2 = ToNumber(fields[3]) + shift_y;
		int id = static_cast<int>(ToNumber(fields[4]));
		if (!inside(x1, y1, x2, y2))
			continue;
		bool wanted = id == Build.first_node || std::find(Build.nodes.begin(), Build.nodes.end(), id) != Build.nodes.end();
		bool placed = At(g_Live.stones, id) != 0;
		if (wanted || placed)
			Outline(x1, y1, x2, y2, !wanted ? Mark::Wrong : placed ? Mark::Match : Mark::Todo, 0.92);
	}

	if (old_scissor.IsStruct())
	{
		auto part = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_struct_get", { old_scissor, RValue(Name) }); };
		g_Yytk->CallBuiltin("gpu_set_scissor", { part("x"), part("y"), part("w"), part("h") });
	}
	g_Yytk->CallBuiltin("draw_set_halign", { old_halign });
	g_Yytk->CallBuiltin("draw_set_valign", { old_valign });
	g_Yytk->CallBuiltin("draw_set_color", { old_color });
	g_Yytk->CallBuiltin("draw_set_alpha", { old_alpha });
}

// Runs after the UI has drawn.
static void DrawMarks(CInstance* Self)
{
	std::lock_guard guard(g_Lock);
	if (!g_Marks || !g_Live.valid || g_ActiveBuild < 0 || g_ActiveBuild >= static_cast<int>(g_Builds.size()))
		return;
	const Build& build = g_Builds[g_ActiveBuild];
	if (build.hero != g_Live.hero)
		return;
	RValue menu = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("menu_show") });
	RValue position = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("menu_position") });
	if (!menu.IsString() || !position.IsString() || position.ToString() != "open")
		return;
	if (menu.ToString() == "elements")
		MarkLegacies(Self, build);
}

#ifdef SLORM_DEV
bool g_WantDump = false;
std::string g_DumpNote;
std::string g_DumpResult;
static int g_DumpCount = 0;

// Names that get printed in full: the ancestral legacy screen and what it reads.
static bool Interesting(const std::string& Name)
{
	static const char* words[] = { "ele", "ancest", "gem", "realm", "stone", "legac", "aura", "hero", "class", "menu", "tree", "skill", "trait", "mba", "door", "coord" };
	std::string name = Lower(Name);
	for (const char* word : words)
		if (name.find(word) != std::string::npos) return true;
	return false;
}

static std::string Show(const RValue& Value, int Depth, size_t Limit)
{
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET) return "undefined";
	if (Value.IsString())
	{
		std::string text = Value.ToString();
		if (text.size() > 80) text = text.substr(0, 80) + "...";
		return '"' + text + '"';
	}
	if (Value.IsArray())
	{
		std::vector<RValue> parts = Value.ToVector();
		std::string out = "[" + std::to_string(parts.size()) + "]";
		if (Depth <= 0)
			return out;
		out += " {";
		for (size_t i = 0; i < parts.size() && i < Limit; i++)
			out += (i ? ", " : " ") + Show(parts[i], Depth - 1, Limit);
		if (parts.size() > Limit) out += ", ...";
		return out + " }";
	}
	if (Value.IsStruct()) return "struct";
	if (Value.m_Kind == VALUE_PTR) return "pointer";
	if (Value.m_Kind == VALUE_REF) return "ref " + std::to_string(Value.ToDouble());
	if (Value.IsNumberConvertible())
	{
		char text[32];
		double number = Value.ToDouble();
		snprintf(text, sizeof(text), number == static_cast<long long>(number) ? "%.0f" : "%g", number);
		return text;
	}
	return "kind " + std::to_string(Value.m_Kind);
}

static void DumpNames(std::ofstream& File, const RValue& Names, const std::function<RValue(const std::string&)>& Get)
{
	if (!Names.IsArray())
	{
		File << "(no names)\n";
		return;
	}
	std::vector<std::string> names;
	for (const RValue& name : Names.ToVector())
		if (name.IsString()) names.push_back(name.ToString());
	std::sort(names.begin(), names.end());
	for (const std::string& name : names)
	{
		bool full = Interesting(name);
		File << name << " = " << Show(Get(name), full ? 3 : 1, full ? 400 : 6) << '\n';
	}
}

static void Dump(CInstance* Self)
{
	std::ofstream file(g_ConfigPath.parent_path() / "SlormHUD.dump.txt", g_DumpCount ? std::ios::app : std::ios::trunc);
	g_DumpCount++;
	file << "===== dump " << g_DumpCount << ": " << g_DumpNote << " =====\n";
	file << "--- globals ---\n";
	// -5 is GML's "global".
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(-5.0) }),
		[](const std::string& Name) { return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name.c_str()) }); });
	file << "--- obj_ui_next_gen ---\n";
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(Self) }),
		[&](const std::string& Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name.c_str()) }); });
	RValue width = g_Yytk->CallBuiltin("display_get_gui_width", {}), height = g_Yytk->CallBuiltin("display_get_gui_height", {});
	file << "--- gui " << Show(width, 0, 0) << " x " << Show(height, 0, 0) << " ---\n";
	file << "mouse_gui = " << Show(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { RValue(0.0) }), 0, 0) << ", "
		<< Show(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { RValue(0.0) }), 0, 0) << '\n';
	RValue surface = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tree_surf") });
	if (ToNumber(g_Yytk->CallBuiltin("surface_exists", { surface })) != 0)
		file << "tree_surf_size = " << Show(g_Yytk->CallBuiltin("surface_get_width", { surface }), 0, 0) << ", "
			<< Show(g_Yytk->CallBuiltin("surface_get_height", { surface }), 0, 0) << '\n';
	g_DumpResult = "dump " + std::to_string(g_DumpCount) + " written";
	Log("%s", g_DumpResult.c_str());
}
#endif

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

	// A few times a second is plenty for a comparison.
	static int wait = 0;
	if (--wait <= 0)
	{
		wait = 4;
		ReadLive(Self);
	}

#ifdef SLORM_DEV
	static bool dump_key_was_down = false;
	bool dump_key = (GetAsyncKeyState(DUMP_KEY) & 0x8000) != 0;
	if (dump_key && !dump_key_was_down)
	{
		g_DumpNote = "key";
		g_WantDump = true;
	}
	dump_key_was_down = dump_key;
	if (g_WantDump)
	{
		g_WantDump = false;
		Dump(Self);
	}
#endif
}

// Runs for the UI object's Step event, and for its Draw GUI event to outline the build.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
#ifdef SLORM_DEV
	// Each event of the UI objects, logged once, to find the ones that draw.
	static std::unordered_set<CCode*> seen;
	if (code && seen.insert(code).second && code->GetName() && strstr(code->GetName(), "_ui_"))
		Log("event %s", code->GetName());
#endif
	static CCode* draw_code = nullptr;
	if (code && code == draw_code)
	{
		// Another mod may have run the event already; running it twice doubles every click.
		if (!Event.CalledOriginal()) Event.Call();
		DrawMarks(self);
		return;
	}
	if (!draw_code && code && code->GetName() && std::string_view(code->GetName()) == "gml_Object_obj_ui_next_gen_Draw_64")
	{
		draw_code = code;
		return;
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

#ifdef SLORM_DEV
	// When the overlay takes input from the game, and when it lets go.
	static bool took_mouse = false, took_keys = false;
	if (OverlayWantsMouse() != took_mouse || OverlayWantsKeys() != took_keys)
	{
		took_mouse = OverlayWantsMouse();
		took_keys = OverlayWantsKeys();
		Log("input: mouse %s, keys %s", took_mouse ? "taken" : "free", took_keys ? "taken" : "free");
	}
	// What the game sees on each click.
	static bool was_down = false;
	bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down != was_down)
	{
		was_down = down;
		auto number = [](const char* Name, std::vector<RValue> Arguments) { return g_Yytk->CallBuiltin(Name, Arguments).ToDouble(); };
		Log("click %s: overlay mouse %d keys %d visible %d, game held %.0f pressed %.0f released %.0f at %.0f,%.0f",
			down ? "down" : "up", OverlayWantsMouse() ? 1 : 0, OverlayWantsKeys() ? 1 : 0, g_Visible ? 1 : 0,
			number("mouse_check_button", { RValue(1.0) }), number("mouse_check_button_pressed", { RValue(1.0) }),
			number("mouse_check_button_released", { RValue(1.0) }),
			number("device_mouse_x_to_gui", { RValue(0.0) }), number("device_mouse_y_to_gui", { RValue(0.0) }));
	}
#endif
	// Input aimed at the overlay must not reach the game's UI.
	if (OverlayWantsMouse())
	{
		g_Yytk->CallBuiltin("mouse_clear", { RValue(1.0) });
		g_Yytk->CallBuiltin("mouse_clear", { RValue(2.0) });
	}
	if (OverlayWantsKeys())
		g_Yytk->CallBuiltin("io_clear", {});

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
