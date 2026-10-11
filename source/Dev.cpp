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
	"scr_hero_through_door",
	"scr_target_finder",
	"scr_hero_check_if_engaged",
};
// Of those, the ones called many times a step: each distinct call and result is logged once.
static constexpr std::array BUSY = {
	"scr_new_attack_state_aim",
	"scr_new_attack_state_get_cursor",
	"scr_new_attack_state_cost",
	"scr_click_to_move",
	"scr_hero_move",
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
	// The forge's offers are dumped as soon as they exist.
	if (std::string_view(TRACED[Index]) == "scr_ui_menu_forge_choice_create")
	{
		g_Dev.dump_note = "forge choices";
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
}
#endif
