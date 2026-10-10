// Temppopotamus' Bastion without the clicks: walking up to the academy entrance starts it,
// walking up to its exit key and portal takes them, and its result screen closes itself.
#include "../Suite.hpp"
#include <imgui.h>
#include <cmath>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// The Bastion's number among the story missions.
	constexpr int MISSION = 31;
	// How near the hero has to be to the entrance, the key or the portal, in the room's units.
	constexpr double REACH = 72;

	PFUNC_YYGMLScript g_RunStart = nullptr;
	PFUNC_YYGMLScript g_CloseButton = nullptr;
	// The mission being played, -1 in town or when it was not seen starting.
	int g_Mission = -1;

	RValue& RunStartHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
	{
		g_Mission = ArgumentCount > 0 && Arguments[0] ? static_cast<int>(ToNumber(*Arguments[0])) : -1;
		Log("bastion: run %d starts", g_Mission);
		return g_RunStart(Self, Other, Result, ArgumentCount, Arguments);
	}

	// Runs one of an instance's user events, as the game does when the hero uses it.
	void UserEvent(CInstance* Instance, int Number)
	{
		RValue result;
		g_Yytk->CallBuiltinEx(result, "event_user", Instance, Instance, { RValue(static_cast<double>(Number)) });
	}

	double Distance(CInstance* From, CInstance* To)
	{
		auto at = [](CInstance* Instance, const char* Axis) { return ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { RValue(Instance), RValue(Axis) })); };
		double x = at(From, "x") - at(To, "x"), y = at(From, "y") - at(To, "y");
		return sqrt(x * x + y * y);
	}

	struct Bastion : Feature
	{
		// Back in town from a Bastion run, until its result screen is gone.
		bool returned = false;
		// The entrance only works after the hero has been away from it, or arriving next to it would loop.
		bool armed = false;
		// Out of town since the run started.
		bool away = false;
		// When the result screen was last told to close.
		ULONGLONG closed_at = 0;

		// Take the exit key and the portal when the hero reaches them.
		bool auto_exit = true;
		// Held to stay in, to pick up loot.
		int hold_key = VK_LCONTROL;
		bool skip_result = true;

		Bastion() : Feature("Auto enter Temppopotamus' Bastion", "bastion",
			"Walking up to the Mage Academy entrance starts the Bastion, without the academy's menu.") {}

		void Start() override
		{
			g_CloseButton = FindScript("scr_ui_menu_close_button");
			PFUNC_YYGMLScript run_start = FindScript("scr_run_start");
			if (!run_start || !AurieSuccess(MmCreateHook(g_ArSelfModule, "SlormSuite run start", run_start, RunStartHook, reinterpret_cast<PVOID*>(&g_RunStart))))
				Log("bastion: no run start script, the mod will do nothing");
		}

		void Town(CInstance* Self, CInstance* Hero)
		{
			if (g_Mission >= 0)
			{
				// The run has started but town is still up for a few steps.
				if (!away)
					return;
				returned = g_Mission == MISSION;
				g_Mission = -1;
				away = false;
				armed = false;
			}
			CInstance* entrance = FindInstance("obj_hub_academy_entrance");
			bool close_by = entrance && Distance(Hero, entrance) < REACH;
			if (!close_by)
				armed = true;
			std::string menu = ToText(Local(Self, "menu_show"));
			if (menu == "result")
			{
				// The game takes a click once the screen has come down; its close button is what a click runs.
				ULONGLONG now = GetTickCount64();
				if (returned && skip_result && ToText(Local(Self, "menu_position")) == "open" && ToNumber(Local(Self, "result_menu_delay_time")) >= ToNumber(Local(Self, "result_menu_delay_timer")) && now - closed_at > 200)
				{
					closed_at = now;
					Log("bastion: closing the result screen");
					CallScript(g_CloseButton, Self);
				}
				return;
			}
			if (menu != "none" || !g_RunStart)
				return;
			returned = false;
			if (close_by && armed)
			{
				armed = false;
				Log("bastion: starting from the entrance");
				RValue result, mission(static_cast<double>(MISSION));
				RValue* arguments[] = { &mission };
				RunStartHook(Self, Self, result, 1, arguments);
			}
		}

		void Tick(CInstance* Self) override
		{
			CInstance* hero = FindInstance("obj_parent_hero");
			if (!hero)
				return;
			if (RoomName() == "rm_the_hub")
			{
				Town(Self, hero);
				return;
			}
			away = true;
			if (g_Mission != MISSION || !auto_exit || (GetAsyncKeyState(hold_key) & 0x8000))
				return;
			// Picking up the key opens the portal; using the portal ends the run.
			static CInstance* taken = nullptr;
			CInstance* key = FindInstance("obj_quest_item");
			CInstance* portal = FindInstance("obj_breach_door");
			if (!key && !portal)
				taken = nullptr;
			if (key && key != taken && Distance(hero, key) < REACH)
			{
				taken = key;
				Log("bastion: taking the exit key");
				UserEvent(key, 15);
			}
			else if (!key && portal && portal != taken && Distance(hero, portal) < REACH)
			{
				taken = portal;
				Log("bastion: taking the exit portal");
				UserEvent(portal, 14);
			}
		}

		bool Draw() override
		{
			bool changed = ImGui::Checkbox("Auto exit", &auto_exit);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Walking up to the exit key and the portal takes them.");
			changed |= ImGui::Checkbox("Skip the result screen", &skip_result);
			if (auto_exit)
			{
				changed |= KeyButton("hold", &hold_key);
				ImGui::SameLine();
				ImGui::TextUnformatted("do not exit while this key is pressed");
			}
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			int value = 0;
			if (!(Words >> value))
				return;
			if (Name == "auto_exit") auto_exit = value != 0;
			else if (Name == "hold_key" && value >= 8 && value <= 254) hold_key = value;
			else if (Name == "skip_result") skip_result = value != 0;
		}

		void Save(std::ostream& File) override
		{
			File << "bastion.auto_exit " << (auto_exit ? 1 : 0) << '\n'
				<< "bastion.hold_key " << hold_key << '\n'
				<< "bastion.skip_result " << (skip_result ? 1 : 0) << '\n';
		}
	};
	Bastion g_Bastion;
}
