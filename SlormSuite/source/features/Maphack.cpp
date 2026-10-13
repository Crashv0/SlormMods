// Shows the whole map on the minimap and the large map, without walking it first.
#include "../Suite.hpp"
#include <imgui.h>
#include <algorithm>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// How far around the hero the game shows icons, in the room's units; this covers any map.
	constexpr double EVERYWHERE = 100000;
	// The game uncovers the cells within this many of the hero's.
	constexpr int REACH = 16;
	// A sweep leaves nothing unseen; a few more are tried in case the game was not ready.
	constexpr int SWEEPS = 5;

	PFUNC_YYGMLScript g_Maximap = nullptr;
	Feature* g_Mod = nullptr;
	// Set by a check that found cells the game has not uncovered.
	bool g_Sweep = false;
	double g_Grid = -1;

	// The game marks each cell of the map: 1 floor and -1 wall not yet seen, 100 and -100 once seen and painted.
	bool Holds(const RValue& Grid, double Left, double Top, double Right, double Bottom, std::initializer_list<double> Values)
	{
		for (double value : Values)
			if (ToNumber(g_Yytk->CallBuiltin("ds_grid_value_exists", { Grid, RValue(Left), RValue(Top), RValue(Right), RValue(Bottom), RValue(value) })) > 0)
				return true;
		return false;
	}

	// The script that uncovers the map, whichever map is shown. It does so around the hero when its timer
	// runs out, about once a second; so the hero is stood in turn all over the map with the timer run out.
	RValue& MaximapHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
	{
		CInstance* hero = g_Sweep && g_Mod && g_Mod->enabled ? FindInstance("obj_parent_hero") : nullptr;
		RValue grid(g_Grid);
		// 5 is GML's ds_type_grid.
		if (!hero || ToNumber(g_Yytk->CallBuiltin("ds_exists", { grid, RValue(5.0) })) < 1)
			return g_Maximap(Self, Other, Result, ArgumentCount, Arguments);
		g_Sweep = false;
		int width = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { grid })));
		int height = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { grid })));
		RValue room_width;
		double cell = width > 0 && AurieSuccess(g_Yytk->GetBuiltin("room_width", nullptr, NULL_INDEX, room_width)) ? ToNumber(room_width) / width : 16;
		auto set = [](CInstance* Of, const char* Name, const RValue& Value) { g_Yytk->CallBuiltin("variable_instance_set", { RValue(Of), RValue(Name), Value }); };
		RValue x = Local(hero, "x"), y = Local(hero, "y"), time = Local(Self, "maxi_time"), timer = Local(Self, "maxi_timer");
		for (int left = 0; left < width; left += REACH * 2)
			for (int top = 0; top < height; top += REACH * 2)
			{
				if (!Holds(grid, left, top, (std::min)(left + REACH * 2, width - 1), (std::min)(top + REACH * 2, height - 1), { 1, -1 }))
					continue;
				set(hero, "x", RValue((left + REACH + 0.5) * cell));
				set(hero, "y", RValue((top + REACH + 0.5) * cell));
				set(Self, "maxi_time", timer);
				RValue unused;
				g_Maximap(Self, Other, unused, ArgumentCount, Arguments);
			}
		set(hero, "x", x);
		set(hero, "y", y);
		set(Self, "maxi_time", time);
		return g_Maximap(Self, Other, Result, ArgumentCount, Arguments);
	}

	struct Maphack : Feature
	{
		// What the game had the icon range at.
		double fov = 0;
		int sweeps = 0;
		ULONGLONG checked_at = 0;

		Maphack() : Feature("Reveal map", "maphack",
			"The minimap and the large map show the whole map and its icons from the start, instead of what the hero has walked past.")
		{
			g_Mod = this;
			// Its switch is on the ESP tab.
			listed = false;
		}

		void Start() override
		{
			PFUNC_YYGMLScript maximap = FindScript("scr_ui_maximap");
			if (!maximap || !AurieSuccess(MmCreateHook(g_ArSelfModule, "SlormSuite maximap", maximap, MaximapHook, reinterpret_cast<PVOID*>(&g_Maximap))))
				Log("maphack: no map script, the map stays covered");
		}

		void Tick(CInstance* Self) override
		{
			// Icons are shown for what lies within this of the hero; the game sets it anew on each map.
			double now_fov = ToNumber(Local(Self, "minimap_fov"));
			if (now_fov > 0 && now_fov < EVERYWHERE)
			{
				fov = now_fov;
				g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("minimap_fov"), RValue(EVERYWHERE) });
			}
			ULONGLONG now = GetTickCount64();
			if (now - checked_at < 500)
				return;
			checked_at = now;
			RValue grid = Local(Self, "grid_minimap");
			if (ToNumber(g_Yytk->CallBuiltin("ds_exists", { grid, RValue(5.0) })) < 1)
				return;
			g_Grid = grid.ToDouble();
			double right = ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { grid })) - 1, bottom = ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { grid })) - 1;
			// Nothing seen yet: a new map.
			if (!Holds(grid, 0, 0, right, bottom, { 100, -100 }))
				sweeps = 0;
			if (!g_Sweep && sweeps < SWEEPS && Holds(grid, 0, 0, right, bottom, { 1, -1 }))
			{
				g_Sweep = true;
				sweeps++;
				Log("maphack: sweep %d", sweeps);
			}
		}

		void Off(CInstance* Self) override
		{
			g_Sweep = false;
			if (fov > 0 && ToNumber(Local(Self, "minimap_fov")) >= EVERYWHERE)
				g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("minimap_fov"), RValue(fov) });
		}
	};
	Maphack g_Maphack;
}
