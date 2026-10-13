// Dev builds only: logs how the game runs the Slormitizer and samples its odds.
#ifdef SLORM_DEV
#include "Reslormitizer.hpp"
#include <array>
#include <cmath>
#include <fstream>
using namespace Aurie;
using namespace YYTK;

static std::string g_Folder;
static PFUNC_YYGMLScript g_Result = nullptr;
static PFUNC_YYGMLScript g_Potential = nullptr;

// Scripts whose calls are logged.
static constexpr std::array TRACED = {
	"scr_crafter_activate_craft",
	"scr_crafter_craft_result",
	"scr_crafter_select_slormite",
	"scr_crafter_select_slormite_craft_nb",
	"scr_crafter_slormite_merge",
	"scr_pick_up_item",
	"scr_blacksmith_select_slormeline",
	"scr_blacksmith_select_slormeline_craft_nb",
	"scr_inventory_move_item",
	"scr_inventory_transfer",
	"scr_inventory_transfer_back",
	"scr_inventory_drop_storage",
	"scr_inventory_change_panel",
	"scr_potential_gain",
	"scr_gold_spend",
	"scr_currency_spend",
};
static std::array<PFUNC_YYGMLScript, TRACED.size()> g_Originals = {};

static std::string Show(const RValue& Value, int Depth, size_t Limit)
{
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET) return "undefined";
	if (Value.IsString()) return '"' + Value.ToString() + '"';
	if (Value.IsArray())
	{
		std::vector<RValue> parts = Value.ToVector();
		if (Depth <= 0)
			return "[" + std::to_string(parts.size()) + "]";
		std::string out = "[";
		for (size_t i = 0; i < parts.size() && i < Limit; i++)
			out += (i ? ", " : "") + Show(parts[i], Depth - 1, Limit);
		if (parts.size() > Limit) out += ", ...";
		return out + "]";
	}
	if (Value.IsStruct()) return "struct";
	if (Value.m_Kind == VALUE_PTR) return "pointer";
	if (Value.m_Kind == VALUE_REF) return "ref " + std::to_string(Value.ToDouble());
	if (Value.IsNumberConvertible())
	{
		char text[32];
		double number = Value.ToDouble();
		snprintf(text, sizeof(text), number == static_cast<long long>(number) ? "%.0f" : "%.6g", number);
		return text;
	}
	return "kind " + std::to_string(Value.m_Kind);
}

template <size_t Index>
static RValue& TraceHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	std::string_view name = TRACED[Index];
	// The panel asks for the odds and the stock every frame; only the real thing is logged.
	bool quiet = (name == "scr_crafter_craft_result" && ArgumentCount > 0 && Arguments[0] && ToNumber(*Arguments[0]) != 0)
		|| (name == "scr_currency_spend" && ArgumentCount > 2 && Arguments[2] && ToNumber(*Arguments[2]) != 0)
		|| (name == "scr_gold_spend" && ArgumentCount > 1 && Arguments[1] && ToNumber(*Arguments[1]) == 0);
	if (quiet)
		return g_Originals[Index](Self, Other, Result, ArgumentCount, Arguments);
	std::string call = "(";
	for (int i = 0; i < ArgumentCount; i++)
		call += (i ? ", " : "") + (Arguments[i] ? Show(*Arguments[i], 2, 14) : std::string("null"));
	Log("call %s%s)", TRACED[Index], call.substr(0, 700).c_str());
	if (name.find("slormite") != std::string_view::npos)
		Log("  ui: crafter_selected_slormite %s, crafter_selected_slormite_craft_nb %s, gold %s", Show(Variable(Self, "crafter_selected_slormite"), 2, 12).c_str(),
			Show(Variable(Self, "crafter_selected_slormite_craft_nb"), 2, 12).c_str(), Show(Variable(Self, "gold"), 1, 4).c_str());
	RValue& result = g_Originals[Index](Self, Other, Result, ArgumentCount, Arguments);
	Log("  %s -> %s", TRACED[Index], Show(result, 2, 20).substr(0, 700).c_str());
	return result;
}

template <size_t... Index>
static void HookAll(AurieModule* Module, std::index_sequence<Index...>)
{
	auto hook = [&](size_t At, PVOID Hook)
	{
		PFUNC_YYGMLScript script = FindScript(TRACED[At]);
		if (!script)
		{
			Log("trace: no script %s", TRACED[At]);
			return;
		}
		AurieStatus status = MmCreateHook(Module, std::string("SlormReslormitizer trace ") + TRACED[At], script, Hook, reinterpret_cast<PVOID*>(&g_Originals[At]));
		if (!AurieSuccess(status))
			Log("trace: hook failed on %s: %s", TRACED[At], AurieStatusToString(status));
	};
	(hook(Index, static_cast<PVOID>(&TraceHook<Index>)), ...);
}

void ProbeStart(AurieModule* Module, const std::string& Folder)
{
	g_Folder = Folder;
	g_Result = FindScript("scr_crafter_craft_result");
	g_Potential = FindScript("scr_get_potential");
	HookAll(Module, std::make_index_sequence<TRACED.size()>());
	// The hooks sit on the scripts; calls made here go around them.
	for (size_t i = 0; i < TRACED.size(); i++)
		if (std::string_view(TRACED[i]) == "scr_crafter_craft_result" && g_Originals[i])
			g_Result = g_Originals[i];
}

// A field keeps the kind it had: the game stores most numbers as text.
static RValue Like(const RValue& Old, double Number)
{
	if (!Old.IsString())
		return RValue(Number);
	char text[32];
	snprintf(text, sizeof(text), "%.0f", Number);
	return RValue(text);
}

static void Probe(CInstance* Self)
{
	std::ofstream out(g_Folder + "\\SlormReslormitizer.probe.txt", std::ios::app);
	out << "==== probe\n";
	for (const char* name : { "current_hero", "character_slots", "inventory_slots", "material_slots", "storage_slots", "gold", "menu_citizen", "merchant_panel" })
		out << name << " = " << Show(Variable(Self, name), 1, 8) << '\n';

	RValue names = g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(Self) });
	for (const RValue& name : names.ToVector())
	{
		std::string text = ToText(name);
		if (text.find("craft") != std::string::npos || text.find("slormite") != std::string::npos || text.find("reinforce") != std::string::npos)
			out << "ui." << text << " = " << Show(g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), name }), 3, 16) << '\n';
	}

	RValue values = Variable(Self, "values");
	for (const char* key : { "Slormite Apply Multiplier", "Slormite Max Tier", "Slormite Purchase Price 1", "Slormite Purchase Price 2", "Slormite Purchase Price 3", "Slormite Purchase Price 5",
		"Slormite Purchase Price 10", "Slormite Purchase Price 15", "Slormeline Fragment Normal Purchase Price", "Slormeline Fragment Magic Purchase Price", "Slormeline Fragment Rare Purchase Price",
		"Slormeline Fragment Epic Purchase Price", "Slormeline Fragment Legendary Purchase Price", "Slormandrite of Fate Purchase Price", "Slormandrite of Negation Purchase Price",
		"Slormandrite of True Potential Purchase Price", "Slormandrite of Aptitude Purchase Price", "Slormandrite of Harmony Purchase Price", "Slormandrite of Virtue Purchase Price",
		"Craft Slormite Underlevel", "Craft Slormite Overlevel", "Craft Slormandrite of Fate Effect", "Craft Slormandrite of Virtue Effect", "Craft Slormandrite of Aptitude Effect",
		"Craft Slormandrite of Harmony Effect", "Craft Normal Slormeline Reinforce Value", "Craft Legendary Slormeline Reinforce Value", "Potential Initial Victims", "Potential Victims Multiplier",
		"Potential legendary Multiplier", "Slormite Reinforce Chance Diviser", "Fragments for Slormeline" })
		out << "values[" << key << "] = " << Show(g_Yytk->CallBuiltin("ds_map_find_value", { values, RValue(key) }), 1, 8) << '\n';

	RValue hero = At(Variable(Self, "inventory"), static_cast<int>(ToNumber(Variable(Self, "current_hero"))));
	int count = Length(hero);
	out << "hero slots " << count << '\n';
	// What is in the Slormitizer: the item, the slormite, and the modifiers.
	int item_slot = -1, slormite_slot = -1;
	std::vector<int> modifier_slots;
	for (int i = 0; i < count; i++)
	{
		RValue slot = At(hero, i);
		bool empty = Length(slot) < 1;
		if (i >= count - 40 || (i >= 70 && i < 120))
			out << "slot " << i << " = " << Show(slot, 2, 14) << '\n';
		if (empty || i < count - 12)
			continue;
		RValue header = At(slot, 0);
		if (ToText(At(header, 3)) == "equipment") item_slot = i;
		else if (ToText(At(header, 3)) == "slormite" && ToNumber(At(header, 1)) == 1) slormite_slot = i;
		else if (ToText(At(header, 3)) == "slormite") modifier_slots.push_back(i);
	}
	out << "item slot " << item_slot << ", slormite slot " << slormite_slot << ", modifiers " << modifier_slots.size() << '\n';
	out << "preview = " << Show(CallScript(g_Result, Self, { RValue(1.0) }), 2, 24) << '\n';
	if (item_slot < 0 || slormite_slot < 0)
	{
		out << "no item or no slormite in the Slormitizer; nothing sampled\n";
		return;
	}

	RValue item = At(hero, item_slot);
	RValue item_header = At(item, 0), slormite_header = At(At(hero, slormite_slot), 0);
	out << "potential = " << Show(CallScript(g_Potential, Self, { item }), 2, 12) << '\n';
	RValue old_state = At(item_header, 5), old_tier = At(slormite_header, 2);
	std::vector<RValue> modifier_headers, old_ids, old_counts;
	for (int slot : modifier_slots)
	{
		modifier_headers.push_back(At(At(hero, slot), 0));
		old_ids.push_back(At(modifier_headers.back(), 2));
		old_counts.push_back(At(modifier_headers.back(), 4));
	}

	auto set_state = [&](int Level, double Victims)
	{
		char text[64];
		snprintf(text, sizeof(text), "%d-%.0f", Level, Victims);
		Put(item_header, 5, RValue(text));
	};
	auto set_modifier = [&](size_t Which, double Id, double Count)
	{
		Put(modifier_headers[Which], 2, Like(old_ids[Which], Id));
		Put(modifier_headers[Which], 4, Like(old_counts[Which], Count));
	};
	auto preview = [&]() { return Show(CallScript(g_Result, Self, { RValue(1.0) }), 1, 24); };
	auto clear_modifiers = [&]() { for (size_t m = 0; m < modifier_headers.size(); m++) set_modifier(m, ToNumber(old_ids[m]), 0); };

	// A share of the victims the level asks for.
	auto set_share = [&](int Level, double Share)
	{
		set_state(Level, 0);
		set_state(Level, floor(ToNumber(At(CallScript(g_Potential, Self, { item }), 2)) * Share));
	};
	auto set_tier = [&](int Tier) { Put(slormite_header, 2, Like(old_tier, Tier)); };
	for (size_t m = 0; m < modifier_headers.size(); m++)
		out << "modifier " << m << " in slot " << modifier_slots[m] << " = " << Show(modifier_headers[m], 1, 12) << '\n';

	clear_modifiers();
	out << "-- E: slormite tier, level, share of potential; modifier counts 0\n";
	for (int tier : { 8, 15 })
		for (int level = tier - 5; level <= tier + 3; level++)
			for (double share : { 0.0, 0.2, 0.5, 1.0 })
			{
				set_tier(tier);
				set_share(level, share);
				out << "E " << tier << ' ' << level << ' ' << share << " | " << preview() << '\n';
			}

	if (!modifier_headers.empty())
	{
		out << "-- C: level, modifier id, count; slormite tier 8, potential 40%, other modifiers 0\n";
		set_tier(8);
		for (int level : { 5, 7, 8, 9 })
			for (int id = 0; id <= 10; id++)
				for (double amount : { 0.0, 1.0, 2.0, 3.0, 10.0 })
				{
					set_share(level, 0.4);
					set_modifier(0, id, amount);
					out << "C " << level << ' ' << id << ' ' << amount << " | " << preview() << '\n';
				}
		clear_modifiers();
	}
	if (modifier_headers.size() >= 2)
	{
		out << "-- D: level, first id, count, second id, count; slormite tier 8, potential 40%\n";
		for (int level : { 7, 8 })
			for (auto [first, second] : { std::pair{ 5, 9 }, std::pair{ 5, 10 }, std::pair{ 8, 5 }, std::pair{ 10, 9 }, std::pair{ 4, 5 }, std::pair{ 5, 5 }, std::pair{ 0, 3 }, std::pair{ 2, 9 } })
				for (double a : { 0.0, 1.0, 2.0 })
					for (double b : { 0.0, 1.0, 2.0 })
					{
						set_share(level, 0.4);
						set_modifier(0, first, a);
						set_modifier(1, second, b);
						out << "D " << level << ' ' << first << ' ' << a << ' ' << second << ' ' << b << " | " << preview() << '\n';
					}
	}

	Put(item_header, 5, old_state);
	Put(slormite_header, 2, old_tier);
	for (size_t m = 0; m < modifier_headers.size(); m++)
	{
		Put(modifier_headers[m], 2, old_ids[m]);
		Put(modifier_headers[m], 4, old_counts[m]);
	}
	out << "restored; preview = " << preview() << '\n';
}

void ProbeTick(CInstance* Self)
{
	static bool was_down = false;
	bool down = (GetAsyncKeyState(PROBE_KEY) & 0x8000) != 0 && GetForegroundWindow() == GetActiveWindow();
	if (down && !was_down)
	{
		Log("probe: sampling the Slormitizer");
		Probe(Self);
		Log("probe: written to SlormReslormitizer.probe.txt");
	}
	was_down = down;
}
#endif
