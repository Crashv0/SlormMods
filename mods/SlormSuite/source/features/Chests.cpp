// Opens and searches chests as the hero comes close enough to use them.
#include "../Suite.hpp"
#include <imgui.h>
using namespace Aurie;
using namespace YYTK;

namespace
{
	struct Chests : Feature
	{
		bool ordinary = true;
		// The Great Forge's.
		bool war_chest = true;

		// The chest last used and the state it was in, so each state is used once per visit.
		double used = -1;
		double used_state = -1;
		ULONGLONG used_at = 0;
		// Set on using the War Chest, until its menu is up and told to hand over.
		bool looting = false;
		ULONGLONG away_since = 0;
		PFUNC_YYGMLScript loot = nullptr;
		// The chest_type of the chest a warlord leaves.
		static constexpr double WARLORD = 7;

		Chests() : Feature("Auto open chests", "chests",
			"Opens and searches a chest when the hero is close enough to do it by hand.") {}

		void Start() override
		{
			loot = FindScript("scr_ui_menu_war_chest_loot");
		}

		void Tick(CInstance* Self) override
		{
			std::string menu = ToText(Local(Self, "menu_show"));
			ULONGLONG now = GetTickCount64();
			if (looting)
			{
				if (now - used_at > 5000)
					looting = false;
				// What the menu's open button runs.
				else if (menu.find("chest") != std::string::npos && ToText(Local(Self, "menu_position")) == "open" && now - used_at > 300)
				{
					looting = false;
					Log("chests: looting the War Chest from menu %s", menu.c_str());
					CallScript(loot, Self);
				}
				return;
			}
			// The game names what the hero can use right now; nothing in reach leaves the prompt empty.
			if (ToText(Local(Self, "activable_value")).empty())
			{
				// Out of reach for a while, not just hidden behind a menu: the next visit counts anew.
				if (menu != "none") away_since = 0;
				else if (!away_since) away_since = now;
				else if (now - away_since > 1000) used = -1;
				return;
			}
			away_since = 0;
			RValue target = Local(Self, "activable_obj");
#ifdef SLORM_DEV
			{
				static std::string last;
				std::string seen = "prompt " + ToText(Local(Self, "activable_value")) + ", target " + Show(target, 0, 0) + ", exists " + Show(g_Yytk->CallBuiltin("instance_exists", { target }), 0, 0)
					+ ", object " + Show(g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("object_index") }), 0, 0);
				if (seen != last)
				{
					last = seen;
					Log("chests: %s", seen.c_str());
				}
			}
#endif
			// A reference to the instance, or its id on older saves of the variable.
			if (target.m_Kind != VALUE_REF && (target.m_Kind == VALUE_UNDEFINED || target.m_Kind == VALUE_UNSET || !target.IsNumberConvertible()))
				return;
			if (ToNumber(g_Yytk->CallBuiltin("instance_exists", { target })) < 1)
				return;
			RValue object = g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("object_index") });
			if (object.m_Kind == VALUE_UNDEFINED || object.m_Kind == VALUE_UNSET || !object.IsNumberConvertible())
				return;
			std::string name = ToText(g_Yytk->CallBuiltin("object_get_name", { object }));
			if (!(name == "obj_chest" && ordinary) && !(name == "obj_chest_forge" && war_chest))
				return;
			// A warlord's chest offers a pick of equipment: left to the player.
			if (name == "obj_chest" && (ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("chest_type") })) == WARLORD
				|| g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("war_rewards") }).IsArray()))
				return;
			double id = ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("id") }));
			if (id <= 0) id = target.ToDouble();
			double state = ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { target, RValue("chest_open") }));
			if ((id == used && state == used_state) || now - used_at < 250)
				return;
			CInstance* chest = nullptr;
			if (!AurieSuccess(g_Yytk->GetInstanceObject(static_cast<int32_t>(id), chest)) || !chest)
				return;
			used = id;
			used_state = state;
			used_at = now;
			looting = name == "obj_chest_forge" && loot;
			Log("chests: using %s %.0f in state %.0f", name.c_str(), id, state);
			// What the game runs when the hero uses it.
			RValue result;
			g_Yytk->CallBuiltinEx(result, "event_user", chest, chest, { RValue(15.0) });
		}

		bool Draw() override
		{
			bool changed = ImGui::Checkbox("Chests", &ordinary);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("The chests found on expeditions: opened, then searched.");
			changed |= ImGui::Checkbox("War Chest", &war_chest);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("The Great Forge's chest.");
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			int value = 0;
			if (!(Words >> value))
				return;
			if (Name == "ordinary") ordinary = value != 0;
			else if (Name == "war_chest") war_chest = value != 0;
		}

		void Save(std::ostream& File) override
		{
			File << "chests.ordinary " << (ordinary ? 1 : 0) << '\n'
				<< "chests.war_chest " << (war_chest ? 1 : 0) << '\n';
		}
	};
	Chests g_Chests;
}
