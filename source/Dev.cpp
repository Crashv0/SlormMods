// Dev builds only: tools for finding out how the game does something.
#ifdef SLORM_DEV
#include "Suite.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_set>
using namespace Aurie;
using namespace YYTK;

Dev g_Dev;
static std::string g_Folder;
static int g_DumpCount = 0;
static long long g_Step = 0;

// Scripts whose calls are logged. Ones that run every step are logged when their arguments change.
static constexpr std::array TRACED = {
	"scr_ui_menus_launcher",
	"scr_ui_menus_story_launcher",
	"scr_ui_launcher_change_panel",
	"scr_ui_launcher_change_level",
	"scr_ui_launcher_change_quest",
	"scr_ui_launcher_change_environment_battlefield",
	"scr_ui_launcher_change_sub_battlefield",
	"scr_ui_launcher_change_temple_floor",
	"scr_ui_launcher_change_primordial_floor",
	"scr_ui_launcher_change_torment",
	"scr_ui_launcher_change_torment_plus",
	"scr_ui_launcher_match_level",
	"scr_ui_launcher_navigate_temple",
	"scr_ui_launcher_map_primordial",
	"scr_launcher_navigate_map",
	"scr_ui_menus_leave",
	"scr_ui_menu_leave_confirmed",
	"scr_ui_menu_close_button",
	"scr_run_end",
	"scr_run_close_result_menu",
	"scr_floor_start",
	"scr_floor_end",
	"scr_room_request",
	"scr_room_load",
	"scr_end_cleanup",
	"scr_citizen_action",
	"scr_citizens_talk_action",
	"scr_chest_open",
	"scr_breach_loot",
	"scr_story_mission_select",
	"scr_hub_load_hero",
	"scr_ui_menu_forge_choice_create",
	"scr_forge_challenge_add",
	"scr_forge_challenge_end",
	"scr_forge_challenge_remove",
	"scr_forge_challenge_rarity_data",
	"scr_forge_rewards_apply_effects",
	"scr_forge_rewards_get_upgrade_result",
	"scr_ui_menu_war_chest_add_reward",
	"scr_ui_menu_war_chest_loot",
	"scr_corrupted_slorm_get_cost_forge",
	"scr_general_skill_cast",
	"scr_general_skill_add_recast",
	"scr_new_attack_state_start",
	"scr_new_attack_state_end",
	"scr_new_attack_state_cost",
	"scr_new_attack_state_aim",
	"scr_new_attack_state_get_cursor",
	"scr_new_attack_state_start_cooldown",
	"scr_aura_toggle",
	"scr_mana_withdraw",
	"scr_click_to_move",
	"scr_hero_move",
	"scr_hero_step",
	"scr_hero_through_door",
	"scr_target_finder",
	"scr_hero_check_if_engaged",
	"scr_create_loot",
	"scr_create_loot_reaper",
	"scr_create_loot_equipment",
	"scr_weapon_new_weapon",
	"scr_inventory_check_weapon_evolve",
	"scr_weapon_pre_evolve",
	"scr_weapon_evolve",
	"scr_inventory_equip_weapon",
};
// Of those, the ones called many times a step: each distinct call and result is logged once.
static constexpr std::array BUSY = {
	"scr_new_attack_state_aim",
	"scr_new_attack_state_get_cursor",
	"scr_new_attack_state_cost",
	"scr_click_to_move",
	"scr_hero_move",
	"scr_hero_step",
	"scr_target_finder",
	"scr_hero_check_if_engaged",
};
static std::array<PFUNC_YYGMLScript, TRACED.size()> g_Originals = {};
static std::array<std::string, TRACED.size()> g_LastCall;
static std::array<long long, TRACED.size()> g_LastStep = {};

std::string Show(const RValue& Value, int Depth, size_t Limit)
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

template <size_t Index>
static RValue& TraceHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	static const bool busy = std::find_if(BUSY.begin(), BUSY.end(), [](const char* Name) { return std::string_view(Name) == TRACED[Index]; }) != BUSY.end();
	// The hero's step reads the mouse: a walk has it find the mouse where it wants to go.
	static const bool steps = std::string_view(TRACED[Index]) == "scr_hero_step" || std::string_view(TRACED[Index]) == "scr_click_to_move" || std::string_view(TRACED[Index]) == "scr_hero_move";
	struct Lend
	{
		bool on;
		Lend(bool On) : on(On) { if (on) WalkScript(1); }
		~Lend() { if (on) WalkScript(-1); }
	} lend(steps);
	if (g_Dev.trace && busy)
	{
		std::string call = "(";
		for (int i = 0; i < ArgumentCount; i++)
			call += (i ? ", " : "") + (Arguments[i] ? Show(*Arguments[i], 1, 6) : std::string("null"));
		RValue& result = g_Originals[Index](Self, Other, Result, ArgumentCount, Arguments);
		call += ") -> " + Show(result, 1, 6);
		static std::unordered_set<std::string> seen;
		if (seen.size() < 80 && seen.insert(call).second)
		{
			RValue object = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("object_index") });
			std::string who = object.m_Kind != VALUE_UNDEFINED && object.m_Kind != VALUE_UNSET && object.IsNumberConvertible() ? ToText(g_Yytk->CallBuiltin("object_get_name", { object })) : "?";
			Log("busy %s%s as %s", TRACED[Index], call.substr(0, 400).c_str(), who.c_str());
		}
		return result;
	}
	if (g_Dev.trace)
	{
		std::string call = "(";
		for (int i = 0; i < ArgumentCount; i++)
			call += (i ? ", " : "") + (Arguments[i] ? Show(*Arguments[i], 2, 12) : std::string("null"));
		call += ")";
		// Skip repeats of a call made every step.
		if (call != g_LastCall[Index] || g_Step - g_LastStep[Index] > 30)
			Log("call %s%s", TRACED[Index], call.substr(0, 800).c_str());
		g_LastCall[Index] = call;
		g_LastStep[Index] = g_Step;
	}
	RValue& result = g_Originals[Index](Self, Other, Result, ArgumentCount, Arguments);
	// The forge's offers are dumped as soon as they exist, and a floor as it ends.
	if (std::string_view(TRACED[Index]) == "scr_ui_menu_forge_choice_create" || std::string_view(TRACED[Index]) == "scr_floor_end")
	{
		g_Dev.dump_note = std::string_view(TRACED[Index]) == "scr_floor_end" ? "floor end" : "forge choices";
		g_Dev.want_dump = true;
	}
	return result;
}

template <size_t... Index>
static void HookAll(std::index_sequence<Index...>)
{
	auto hook = [](size_t At, PVOID Hook)
	{
		PFUNC_YYGMLScript script = FindScript(TRACED[At]);
		if (!script)
		{
			Log("trace: no script %s", TRACED[At]);
			return;
		}
		AurieStatus status = MmCreateHook(g_ArSelfModule, std::string("SlormSuite trace ") + TRACED[At], script, Hook, reinterpret_cast<PVOID*>(&g_Originals[At]));
		if (!AurieSuccess(status))
			Log("trace: hook failed on %s: %s", TRACED[At], AurieStatusToString(status));
	};
	(hook(Index, static_cast<PVOID>(&TraceHook<Index>)), ...);
}

void DevStart(const std::string& Folder)
{
	g_Folder = Folder;
	HookAll(std::make_index_sequence<TRACED.size()>());
}

// The Dev tab's test presses: set on the hero after the game has read the real keys.
static void DevPress(CInstance* Self)
{
	if (g_Dev.walk < 0)
		return;
	if (Self != FindInstance("obj_parent_hero"))
		return;
	auto set = [&](const char* Name, double Value) { g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue(Name), RValue(Value) }); };
	double x = ToNumber(Local(Self, "x")), y = ToNumber(Local(Self, "y"));
	unsigned long long now = GetTickCount64();
	if (g_Dev.walk >= 0)
	{
		static constexpr std::array DIRECTIONS = { "right_key", "left_key", "up_key", "down_key" };
		const char* key = DIRECTIONS[g_Dev.walk % DIRECTIONS.size()];
		if (!g_Dev.walk_until)
		{
			g_Dev.walk_until = now + 1000;
			Log("dev: holding %s from %.0f %.0f", key, x, y);
		}
		if (now < g_Dev.walk_until)
			set(key, 1);
		else
		{
			Log("dev: let go of %s at %.0f %.0f", key, x, y);
			g_Dev.walk = -1;
			g_Dev.walk_until = 0;
		}
	}
}

// The input script is hooked by the macros, which pass its calls on: each distinct one is logged once.
void DevInput(CInstance* Self, const RValue& Result, int ArgumentCount, RValue** Arguments)
{
	DevPress(Self);
	if (!g_Dev.trace)
		return;
	RValue object = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("object_index") });
	std::string call = object.m_Kind != VALUE_UNDEFINED && object.m_Kind != VALUE_UNSET && object.IsNumberConvertible() ? ToText(g_Yytk->CallBuiltin("object_get_name", { object })) : "?";
	call += " (";
	for (int i = 0; i < ArgumentCount; i++)
		call += (i ? ", " : "") + (Arguments[i] ? Show(*Arguments[i], 1, 6) : std::string("null"));
	call += ") -> " + Show(Result, 1, 6);
	static std::unordered_set<std::string> seen;
	if (seen.size() < 40 && seen.insert(call).second)
		Log("input scr_get_input as %s", call.substr(0, 400).c_str());
}

// The hero's variables for moving, besides every one named as a key.
static constexpr std::array WATCHED = {
	"actual_state", "enable_input", "remember_xaxis", "remember_yaxis",
	"click_to_move_coords", "click_to_move_path", "click_to_move_path_grid", "click_to_move_state",
	"ctm_target_x", "ctm_target_y", "ctm_attack", "ctm_attack_next", "ctm_attack_once", "ctm_attack_ignore", "ctm_activable",
	"airborne_target_x", "airborne_target_y",
};

// Logs those when they change, with where the hero is.
static void WatchHero()
{
	static CInstance* hero = nullptr;
	static std::vector<std::string> names, last;
	CInstance* now = g_Dev.watch ? FindInstance("obj_parent_hero") : nullptr;
	if (!now)
	{
		hero = nullptr;
		return;
	}
	bool fresh = now != hero;
	// The keys appear after the hero's first steps in a room, so the names are read again for a while.
	static long long read_at = 0;
	if (fresh || (g_Step - read_at > 120 && names.size() < 40))
	{
		size_t before = fresh ? 0 : names.size();
		std::vector<std::string> old_names = fresh ? std::vector<std::string>() : names, old_last = last;
		hero = now;
		read_at = g_Step;
		names.clear();
		RValue all = g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(now) });
		if (all.IsArray())
			for (const RValue& entry : all.ToVector())
			{
				std::string name = ToText(entry);
				bool key = name.size() > 4 && name.compare(name.size() - 4, 4, "_key") == 0;
				if (key || std::find_if(WATCHED.begin(), WATCHED.end(), [&](const char* Watched) { return name == Watched; }) != WATCHED.end())
					names.push_back(name);
			}
		std::sort(names.begin(), names.end());
		last.assign(names.size(), "");
		for (size_t i = 0; i < names.size(); i++)
		{
			auto old = std::find(old_names.begin(), old_names.end(), names[i]);
			last[i] = old != old_names.end() ? old_last[old - old_names.begin()] : Show(Local(now, names[i].c_str()), 1, 6);
		}
		if (names.size() != before)
			Log("hero watch: %d variables in %s", static_cast<int>(names.size()), RoomName().c_str());
	}
	std::string changes;
	for (size_t i = 0; i < names.size(); i++)
	{
		std::string value = Show(Local(now, names[i].c_str()), 1, 6);
		if (value != last[i])
			changes += (changes.empty() ? "" : ", ") + names[i] + " " + last[i] + " -> " + value;
		last[i] = value;
	}
	if (!changes.empty())
		Log("hero %lld at %.0f %.0f speed %.2f %.2f: %s", g_Step, ToNumber(Local(now, "x")), ToNumber(Local(now, "y")), ToNumber(Local(now, "h_speed")), ToNumber(Local(now, "v_speed")), changes.substr(0, 700).c_str());
}

// Each event of each object, logged the first time it runs.
void DevEvent(CCode* Code)
{
	static std::unordered_set<CCode*> seen;
	if (g_Dev.trace && Code && seen.insert(Code).second && Code->GetName())
		Log("event %s", Code->GetName());
}

static bool Interesting(const std::string& Name)
{
	std::string name = Lower(Name), word;
	std::istringstream words(Lower(g_Dev.dump_words));
	while (std::getline(words, word, ','))
	{
		while (!word.empty() && word.front() == ' ') word.erase(0, 1);
		while (!word.empty() && word.back() == ' ') word.pop_back();
		if (!word.empty() && name.find(word) != std::string::npos)
			return true;
	}
	return false;
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

// A grid of the game as a picture: a letter for each value, the hero as @.
static void DumpGrid(std::ofstream& File, const std::string& Name, const RValue& Id, double HeroX, double HeroY)
{
	File << "--- grid " << Name << " = " << Show(Id, 0, 0) << " ---\n";
	// 5 is GML's ds_type_grid.
	if (Id.m_Kind == VALUE_UNDEFINED || Id.m_Kind == VALUE_UNSET || !Id.IsNumberConvertible() || ToNumber(g_Yytk->CallBuiltin("ds_exists", { Id, RValue(5.0) })) < 1)
	{
		File << "(no grid)\n";
		return;
	}
	int width = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { Id }))), height = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { Id })));
	RValue room_width, room_height;
	g_Yytk->GetBuiltin("room_width", nullptr, NULL_INDEX, room_width);
	g_Yytk->GetBuiltin("room_height", nullptr, NULL_INDEX, room_height);
	double cell_across = width > 0 ? ToNumber(room_width) / width : 0, cell_down = height > 0 ? ToNumber(room_height) / height : 0;
	File << width << " x " << height << " cells of " << cell_across << " x " << cell_down << ", room " << ToNumber(room_width) << " x " << ToNumber(room_height) << '\n';
	if (width < 1 || height < 1 || width > 600 || height > 600)
		return;
	static const std::string letters = "#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789";
	std::map<std::string, std::pair<char, int>> legend;
	std::vector<std::string> order;
	int hero_across = cell_across > 0 ? static_cast<int>(HeroX / cell_across) : -1, hero_down = cell_down > 0 ? static_cast<int>(HeroY / cell_down) : -1;
	for (int down = 0; down < height; down++)
	{
		std::string row;
		for (int across = 0; across < width; across++)
		{
			std::string value = Show(g_Yytk->CallBuiltin("ds_grid_get", { Id, RValue(static_cast<double>(across)), RValue(static_cast<double>(down)) }), 1, 4);
			auto found = legend.find(value);
			if (found == legend.end())
			{
				// 0 is a dot; values past the last letter share the question mark.
				char letter = value == "0" ? '.' : order.size() < letters.size() ? letters[order.size()] : '?';
				if (value != "0") order.push_back(value);
				found = legend.emplace(value, std::make_pair(letter, 0)).first;
			}
			found->second.second++;
			row += across == hero_across && down == hero_down ? '@' : found->second.first;
		}
		File << row << '\n';
	}
	for (const auto& [value, entry] : legend)
		File << entry.first << " = " << value << "  x" << entry.second << '\n';
}

static void Dump(CInstance* Self)
{
	std::ofstream file(fs::path(g_Folder) / "SlormSuite.dump.txt", g_DumpCount ? std::ios::app : std::ios::trunc);
	g_DumpCount++;
	file << "===== dump " << g_DumpCount << ": " << g_Dev.dump_note << " =====\n";
	RValue room;
	if (AurieSuccess(g_Yytk->GetBuiltin("room", nullptr, NULL_INDEX, room)))
		file << "room = " << Show(room, 0, 0) << " " << Show(g_Yytk->CallBuiltin("room_get_name", { room }), 0, 0) << '\n';

	// Every instance in the room, counted by object; -3 is GML's "all".
	std::map<std::string, std::vector<RValue>> objects;
	int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { RValue(-3.0) })));
	for (int i = 0; i < count; i++)
	{
		RValue instance = g_Yytk->CallBuiltin("instance_find", { RValue(-3.0), RValue(static_cast<double>(i)) });
		RValue object = g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("object_index") });
		// object_get_name raises an error on anything but a number.
		if (object.m_Kind == VALUE_UNDEFINED || object.m_Kind == VALUE_UNSET || !object.IsNumberConvertible())
			continue;
		objects[ToText(g_Yytk->CallBuiltin("object_get_name", { object }))].push_back(instance);
	}
	file << "--- instances ---\n";
	for (const auto& [name, instances] : objects)
		file << name << " x" << instances.size() << '\n';
	// Where each one is, in the room's units.
	file << "--- places ---\n";
	for (const auto& [name, instances] : objects)
	{
		file << name << ":";
		for (size_t i = 0; i < instances.size() && i < 80; i++)
			file << ' ' << Show(g_Yytk->CallBuiltin("variable_instance_get", { instances[i], RValue("x") }), 0, 0) << ',' << Show(g_Yytk->CallBuiltin("variable_instance_get", { instances[i], RValue("y") }), 0, 0);
		file << (instances.size() > 80 ? " ...\n" : "\n");
	}
	// The map, and every grid the level's object holds.
	CInstance* hero = FindInstance("obj_parent_hero");
	double hero_x = hero ? ToNumber(Local(hero, "x")) : -1, hero_y = hero ? ToNumber(Local(hero, "y")) : -1;
	RValue camera = g_Yytk->CallBuiltin("view_get_camera", { RValue(0.0) });
	file << "--- view ---\nhero " << hero_x << ' ' << hero_y << ", view at " << ToNumber(g_Yytk->CallBuiltin("camera_get_view_x", { camera })) << ' ' << ToNumber(g_Yytk->CallBuiltin("camera_get_view_y", { camera }))
		<< " of " << ToNumber(g_Yytk->CallBuiltin("camera_get_view_width", { camera })) << " x " << ToNumber(g_Yytk->CallBuiltin("camera_get_view_height", { camera })) << '\n';
	DumpGrid(file, "obj_ui_next_gen.grid_minimap", Local(Self, "grid_minimap"), hero_x, hero_y);
	if (CInstance* level = FindInstance("obj_procedural"))
	{
		RValue names = g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(level) });
		std::vector<std::string> grids;
		if (names.IsArray())
			for (const RValue& name : names.ToVector())
				if (ToText(name).rfind("grid_", 0) == 0) grids.push_back(ToText(name));
		std::sort(grids.begin(), grids.end());
		for (const std::string& name : grids)
			DumpGrid(file, "obj_procedural." + name, Local(level, name.c_str()), hero_x, hero_y);
	}

	file << "--- globals ---\n";
	// -5 is GML's "global".
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(-5.0) }),
		[](const std::string& Name) { return Global(Name.c_str()); });
	file << "--- obj_ui_next_gen ---\n";
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(Self) }),
		[&](const std::string& Name) { return Local(Self, Name.c_str()); });
	// The first instance of each object the words name, and of the first two kinds of enemy.
	RValue lifeform = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_parent_lifeform") });
	int enemies = 0;
	for (const auto& [name, instances] : objects)
	{
		if (name == "obj_ui_next_gen")
			continue;
		bool enemy = false;
		if (!Interesting(name))
		{
			RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue(name.c_str()) });
			enemy = enemies < 2 && ToNumber(g_Yytk->CallBuiltin("object_is_ancestor", { object, lifeform })) > 0;
			if (!enemy)
				continue;
			enemies++;
		}
		const RValue& instance = instances[0];
		file << "--- " << name << " ---\n";
		for (const char* place : { "x", "y" })
			file << place << " = " << Show(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue(place) }), 0, 0) << '\n';
		DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { instance }),
			[&](const std::string& Name) { return g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue(Name.c_str()) }); });
	}
	g_Dev.result = "dump " + std::to_string(g_DumpCount) + " written";
	Log("%s: %s", g_Dev.result.c_str(), g_Dev.dump_note.c_str());
}

void DevTick(CInstance* Self)
{
	g_Step++;
	static bool key_was_down = false;
	bool key_down = (GetAsyncKeyState(DUMP_KEY) & 0x8000) != 0;
	if (key_down && !key_was_down)
	{
		g_Dev.dump_note = "key";
		g_Dev.want_dump = true;
	}
	key_was_down = key_down;
	if (g_Dev.want_dump)
	{
		g_Dev.want_dump = false;
		Dump(Self);
	}
	static bool walk_was_down = false, mouse_was_down = false;
	bool walk_down = (GetAsyncKeyState(WALK_KEY) & 0x8000) != 0, mouse_down = (GetAsyncKeyState(WALK_MOUSE_KEY) & 0x8000) != 0;
	if (walk_down && !walk_was_down)
		WalkTo(g_Dev.walk_offset[0], g_Dev.walk_offset[1], true);
	if (mouse_down && !mouse_was_down)
	{
		RValue x, y;
		if (AurieSuccess(g_Yytk->GetBuiltin("mouse_x", nullptr, NULL_INDEX, x)) && AurieSuccess(g_Yytk->GetBuiltin("mouse_y", nullptr, NULL_INDEX, y)))
			WalkTo(ToNumber(x), ToNumber(y));
	}
	walk_was_down = walk_down;
	mouse_was_down = mouse_down;
	static bool mark_was_down = false, back_was_down = false;
	bool mark_down = (GetAsyncKeyState(MARK_KEY) & 0x8000) != 0, back_down = (GetAsyncKeyState(WALK_MARK_KEY) & 0x8000) != 0;
	CInstance* hero = FindInstance("obj_parent_hero");
	if (hero && (g_Dev.mark_want || (mark_down && !mark_was_down)))
	{
		g_Dev.mark_x = ToNumber(Local(hero, "x"));
		g_Dev.mark_y = ToNumber(Local(hero, "y"));
		g_Dev.marked = true;
		Log("dev: marked %.0f %.0f in %s", g_Dev.mark_x, g_Dev.mark_y, RoomName().c_str());
	}
	g_Dev.mark_want = false;
	if (back_down && !back_was_down && g_Dev.marked)
		WalkTo(g_Dev.mark_x, g_Dev.mark_y);
	mark_was_down = mark_down;
	back_was_down = back_down;
	WatchHero();
}
#endif
