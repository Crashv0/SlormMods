// Presses skills for the hero by rules: each macro has conditions and a sequence of steps, and the
// macros are tried from the top. The game's input script is hooked; after it has read the real keys,
// the keys a running macro wants are set on the hero, so the game casts as if they were pressed.
#include "../Suite.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// The actions a macro can press: the hero's variable for each, as the game's input script fills it.
	constexpr int SLOTS = 7;
	constexpr std::array<const char*, SLOTS> KEYS = { "attack_1_key", "attack_2_key", "support_key", "aura_1_key", "aura_2_key", "aura_3_key", "aura_4_key" };
	constexpr std::array<const char*, SLOTS> SLOT_NAMES = { "Primary", "Secondary", "Support", "Aura 1", "Aura 2", "Aura 3", "Aura 4" };
	constexpr const char* SLOT_LIST = "Primary\0Secondary\0Support\0Aura 1\0Aura 2\0Aura 3\0Aura 4\0";
	constexpr const char* PACKED = "SSM1";

	enum Kind { ALL, ANY, NONE, LIFE_PCT, MANA_PCT, LIFE, MANA, COOLDOWN, CASTING, COMBAT, ENEMIES, SINCE, MOVING, KINDS };
	struct KindInfo
	{
		const char* name;
		const char* unit;
		// Yes or no instead of a number.
		bool flag;
		const char* tip;
	};
	constexpr std::array<KindInfo, KINDS> KIND = { {
		{ "all of", "", false, "True when every line inside is." },
		{ "any of", "", false, "True when at least one line inside is." },
		{ "none of", "", false, "True when no line inside is." },
		{ "life %", "%", false, "The hero's life, as a share of the most it can be." },
		{ "mana %", "%", false, "The hero's mana, as a share of the most it can be." },
		{ "life", "", false, "The hero's life." },
		{ "mana", "", false, "The hero's mana." },
		{ "cooldown", "s", false, "Seconds until the skill can be used again; 0 when it is ready." },
		{ "casting", "", true, "The hero is in the middle of an attack or a cast." },
		{ "in combat", "", true, "Enemies are fighting the hero." },
		{ "enemies within", "", false, "How many living enemies are within the range of the hero. The range is in the numbers ESP shows for distances." },
		{ "seconds since this macro fired", "s", false, "How long ago this macro last finished; very large before its first time." },
		{ "moving", "", true, "The hero is changing place." },
	} };
	constexpr std::array<const char*, 6> COMPARERS = { "<", "<=", ">", ">=", "=", "!=" };

	struct Cond
	{
		int uid = 0;
		int kind = ALL;
		int slot = 0;
		int cmp = 1;
		float value = 50;
		// For enemies within: how far, in a tenth of the room's units, as ESP shows distances.
		float range = 20;
		std::vector<Cond> kids;
	};

	enum StepKind { PRESS, WAIT, WAIT_UNTIL };
	struct Step
	{
		int uid = 0;
		int kind = PRESS;
		int slot = 0;
		// Held this long for a press, 0 for one tap; waited this long for a wait; given up after this long for a wait until.
		float seconds = 0;
		Cond until;
	};

	enum Trigger { AUTOMATIC, WHILE_HELD, TOGGLED };
	struct Macro
	{
		int uid = 0;
		std::string name = "new macro";
		bool enabled = true;
		int trigger = AUTOMATIC;
		int key = 0;
		// 0 either, 1 only standing still, 2 only moving.
		int movement = 0;
		float wait = 1;
		// Only when every skill it presses is off cooldown.
		bool ready = true;
		Cond root;
		std::vector<Step> steps;

		// Switched on by its key, for the toggled trigger.
		bool on = false;
		bool key_was_down = false;
		ULONGLONG fired_at = 0;
		int fired = 0;
	};

	// What the conditions read, taken from the hero once a step.
	struct State
	{
		bool valid = false;
		double life = 0, life_max = 0, mana = 0, mana_max = 0;
		double cooldown[SLOTS] = {};
		bool casting = false, combat = false, moving = false;
		// How far each living enemy is from the hero, in the room's units.
		std::vector<double> enemies;
		// Where the hero is on screen and how long one of the room's units is there, as shares of the screen.
		float across = 0, down = 0, unit_across = 0, unit_down = 0;
	};

	int g_NextUid = 1;
	int Uid() { return g_NextUid++; }

	PFUNC_YYGMLScript g_GetInput = nullptr;
	void Stepped(CInstance* Hero);
	Feature* g_Mod = nullptr;

	RValue& GetInputHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
	{
		RValue& result = g_GetInput(Self, Other, Result, ArgumentCount, Arguments);
		// Other things than the hero run the script too.
		if (g_Mod && g_Mod->enabled && Self == FindInstance("obj_parent_hero"))
			Stepped(Self);
		return result;
	}

	void Tip(const char* Text)
	{
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			return;
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
		ImGui::TextUnformatted(Text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	struct Macros : Feature
	{
		std::vector<Macro> macros;
		State state;
		// Stops every macro while set; flipped by its key.
		bool paused = false;
		int pause_key = VK_F5;
		bool pause_was_down = true;

		// The macro whose steps are being run, 0 for none, and where it has come.
		int running = 0;
		int step = 0;
		int step_frames = 0;
		ULONGLONG step_at = 0;
		// A frame without a key between two presses, so the game sees two.
		bool gap = false;
		// Asked for by the test button.
		int test = 0;
		double last_x = 0, last_y = 0;
		bool in_town = false;
		// Draws the ranges of the enemies within conditions around the hero.
		bool show_ranges = false;

		char save_name[48] = "";
		int saved_at = 0;
		std::string note;
		bool confirm_delete = false;

		Macros() : Feature("Macros", "macros", "Presses skills for the hero by rules: conditions that must hold, then a sequence of presses and waits.")
		{
			tab = "Macros";
			g_Mod = this;
		}

		void Start() override
		{
			PFUNC_YYGMLScript input = FindScript("scr_get_input");
			if (!input || !AurieSuccess(MmCreateHook(g_ArSelfModule, "SlormSuite input", input, GetInputHook, reinterpret_cast<PVOID*>(&g_GetInput))))
				Log("macros: no input script, nothing will be pressed");
		}

		// ---- reading the game ----

		void Read(CInstance* Hero)
		{
			auto number = [&](const char* Name) { return ToNumber(Local(Hero, Name)); };
			auto list = [&](const char* Name) { RValue value = Local(Hero, Name); return value.IsArray() ? value.ToVector() : std::vector<RValue>(); };
			state.life = number("the_health");
			state.life_max = number("final_the_max_health");
			if (state.life_max <= 0) state.life_max = number("the_max_health");
			state.mana = number("the_mana");
			state.mana_max = number("final_the_max_mana");
			if (state.mana_max <= 0) state.mana_max = number("the_max_mana");
			state.casting = number("attack_state") != -1 || number("aura_state") != -1;
			state.combat = number("engaged_by_enemies") > 0;
			double x = number("x"), y = number("y");
			state.moving = state.valid && (fabs(x - last_x) > 0.05 || fabs(y - last_y) > 0.05);
			last_x = x;
			last_y = y;
			// A timer counts up from 0 to its end while the skill cools down, in steps of the game.
			auto cooldowns = [&](const char* Time, const char* Timer, int First, int Count)
			{
				std::vector<RValue> time = list(Time), timer = list(Timer);
				for (int i = 0; i < Count; i++)
				{
					double at = i < static_cast<int>(time.size()) ? ToNumber(time[i]) : 0, end = i < static_cast<int>(timer.size()) ? ToNumber(timer[i]) : 0;
					state.cooldown[First + i] = at > 0 && end > at ? (end - at) / 60 : 0;
				}
			};
			cooldowns("general_cooldown_time", "general_cooldown_timer", 0, 3);
			cooldowns("actives_cd_time", "actives_cd_timer", 3, 4);
			state.valid = state.life_max > 0;
			state.enemies.clear();
			if (!Uses(ENEMIES) && !show_ranges)
				return;
			RValue parent = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_parent_lifeform") });
			// An id is a reference, not a number.
			double me = Local(Hero, "id").ToDouble();
			int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { parent })));
			for (int i = 0; i < count; i++)
			{
				RValue other = g_Yytk->CallBuiltin("instance_find", { parent, RValue(static_cast<double>(i)) });
				auto of = [&](const char* Name) { return ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { other, RValue(Name) })); };
				if (g_Yytk->CallBuiltin("variable_instance_get", { other, RValue("id") }).ToDouble() == me || of("dead_since") > 0)
					continue;
				double dx = of("x") - x, dy = of("y") - y;
				state.enemies.push_back(sqrt(dx * dx + dy * dy));
			}
			if (!show_ranges)
				return;
			// Where the room falls on screen, for the circles.
			RValue camera = g_Yytk->CallBuiltin("view_get_camera", { RValue(0.0) });
			double view_x = ToNumber(g_Yytk->CallBuiltin("camera_get_view_x", { camera })), view_y = ToNumber(g_Yytk->CallBuiltin("camera_get_view_y", { camera }));
			double view_w = ToNumber(g_Yytk->CallBuiltin("camera_get_view_width", { camera })), view_h = ToNumber(g_Yytk->CallBuiltin("camera_get_view_height", { camera }));
			if (view_w <= 0 || view_h <= 0)
				return;
			state.across = static_cast<float>((x - view_x) / view_w);
			state.down = static_cast<float>((y - view_y) / view_h);
			state.unit_across = static_cast<float>(1 / view_w);
			state.unit_down = static_cast<float>(1 / view_h);
		}

		double Value(const Cond& Of, const Macro& In) const
		{
			switch (Of.kind)
			{
			case LIFE_PCT: return state.life_max > 0 ? state.life / state.life_max * 100 : 0;
			case MANA_PCT: return state.mana_max > 0 ? state.mana / state.mana_max * 100 : 0;
			case LIFE: return state.life;
			case MANA: return state.mana;
			case COOLDOWN: return state.cooldown[std::clamp(Of.slot, 0, SLOTS - 1)];
			case CASTING: return state.casting;
			case COMBAT: return state.combat;
			case ENEMIES:
			{
				int count = 0;
				for (double distance : state.enemies)
					if (distance <= Of.range * 10) count++;
				return count;
			}
			case SINCE: return In.fired_at ? (GetTickCount64() - In.fired_at) / 1000.0 : 99999;
			case MOVING: return state.moving;
			}
			return 0;
		}

		bool Pass(const Cond& Of, const Macro& In) const
		{
			if (Of.kind <= NONE)
			{
				int passed = 0;
				for (const Cond& kid : Of.kids)
					if (Pass(kid, In)) passed++;
				if (Of.kind == ALL) return passed == static_cast<int>(Of.kids.size());
				if (Of.kind == ANY) return passed > 0 || Of.kids.empty();
				return passed == 0;
			}
			double value = Value(Of, In), than = Of.value;
			if (KIND[Of.kind].flag)
				return (value != 0) == (Of.value != 0);
			switch (Of.cmp)
			{
			case 0: return value < than;
			case 1: return value <= than;
			case 2: return value > than;
			case 3: return value >= than;
			case 4: return fabs(value - than) < 0.0001;
			default: return fabs(value - than) >= 0.0001;
			}
		}

		// Why a macro is not starting right now; null when it can.
		const char* Blocked(const Macro& Of) const
		{
			if (!Of.enabled) return "off";
			if (Of.steps.empty()) return "no steps";
			if (Of.trigger == WHILE_HELD && !(Of.key && (GetAsyncKeyState(Of.key) & 0x8000))) return "its key is not held";
			if (Of.trigger == TOGGLED && !Of.on) return "switched off by its key";
			if (Of.movement == 1 && state.moving) return "the hero is moving";
			if (Of.movement == 2 && !state.moving) return "the hero is standing still";
			if (Of.fired_at && GetTickCount64() - Of.fired_at < static_cast<ULONGLONG>(Of.wait * 1000)) return "waiting between uses";
			if (Of.ready)
				for (const Step& each : Of.steps)
					if (each.kind == PRESS && state.cooldown[std::clamp(each.slot, 0, SLOTS - 1)] > 0) return "a skill is on cooldown";
			if (!Pass(Of.root, Of)) return "conditions not met";
			return nullptr;
		}

		// Whether any condition of any macro is of a kind.
		static bool Holds(const Cond& Of, int Kind_)
		{
			if (Of.kind == Kind_)
				return true;
			for (const Cond& kid : Of.kids)
				if (Holds(kid, Kind_)) return true;
			return false;
		}

		bool Uses(int Kind_) const
		{
			for (const Macro& each : macros)
			{
				if (Holds(each.root, Kind_))
					return true;
				for (const Step& step_ : each.steps)
					if (step_.kind == WAIT_UNTIL && Holds(step_.until, Kind_)) return true;
			}
			return false;
		}

		static void Ranges(const Cond& Of, std::vector<float>& Into)
		{
			if (Of.kind == ENEMIES && std::find(Into.begin(), Into.end(), Of.range) == Into.end())
				Into.push_back(Of.range);
			for (const Cond& kid : Of.kids)
				Ranges(kid, Into);
		}

		// ---- running ----

		Macro* Find(int Uid)
		{
			for (Macro& each : macros)
				if (each.uid == Uid) return &each;
			return nullptr;
		}

		void Begin(Macro& Of)
		{
			running = Of.uid;
			step = 0;
			step_frames = 0;
			step_at = GetTickCount64();
			gap = false;
		}

		void Next()
		{
			step++;
			step_frames = 0;
			step_at = GetTickCount64();
		}

		void Run(CInstance* Hero)
		{
			Macro* macro = Find(running);
			if (!macro)
			{
				running = 0;
				return;
			}
			ULONGLONG now = GetTickCount64();
			// Waits can end and the next step start in the same frame; a press takes the frame.
			for (int guard = 0; guard < 16; guard++)
			{
				if (step >= static_cast<int>(macro->steps.size()))
				{
					macro->fired_at = now;
					macro->fired++;
					running = 0;
					return;
				}
				const Step& at = macro->steps[step];
				double elapsed = (now - step_at) / 1000.0;
				if (at.kind == PRESS)
				{
					if (gap)
					{
						gap = false;
						return;
					}
					if (step_frames > 0 && elapsed >= at.seconds)
					{
						Next();
						gap = true;
						continue;
					}
					step_frames++;
					g_Yytk->CallBuiltin("variable_instance_set", { RValue(Hero), RValue(KEYS[std::clamp(at.slot, 0, SLOTS - 1)]), RValue(1.0) });
					return;
				}
				gap = false;
				if (at.kind == WAIT)
				{
					if (elapsed < at.seconds)
						return;
					Next();
					continue;
				}
				// Waiting until: given up on when it takes too long.
				if (Pass(at.until, *macro))
				{
					Next();
					continue;
				}
				if (elapsed >= at.seconds)
				{
					macro->fired_at = now;
					running = 0;
				}
				return;
			}
		}

		void Step_(CInstance* Hero)
		{
			Read(Hero);
			bool mine = GetForegroundWindow() == GetActiveWindow();
			bool pause_down = mine && pause_key && (GetAsyncKeyState(pause_key) & 0x8000);
			if (pause_down && !pause_was_down)
			{
				paused = !paused;
				running = 0;
			}
			pause_was_down = pause_down;
			for (Macro& each : macros)
			{
				bool down = mine && each.trigger == TOGGLED && each.key && (GetAsyncKeyState(each.key) & 0x8000);
				if (down && !each.key_was_down) each.on = !each.on;
				each.key_was_down = down;
			}
			// Not in town.
			in_town = RoomName() == "rm_the_hub";
			if (!state.valid || paused || in_town || OverlayWantsKeys())
			{
				running = 0;
				return;
			}
			// Nothing is pressed into a menu.
			CInstance* ui = FindInstance("obj_ui_next_gen");
			if (!ui || ToText(Local(ui, "menu_show")) != "none")
			{
				running = 0;
				return;
			}
			if (test)
			{
				if (Macro* asked = Find(test))
					if (!asked->steps.empty()) Begin(*asked);
				test = 0;
			}
			// The highest macro that can start does, over a lower one that is running.
			for (Macro& each : macros)
			{
				if (each.uid == running)
					break;
				if (!Blocked(each))
				{
					Begin(each);
					break;
				}
			}
			if (running)
				Run(Hero);
		}

		void Hud() override
		{
			ImDrawList* draw = ImGui::GetForegroundDrawList();
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			if (paused)
			{
				const char* text = "Macros paused";
				float size = ImGui::GetFontSize() * 1.2f;
				ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
				ImVec2 at(screen.x / 2 - extent.x / 2, 12);
				draw->AddRectFilled({ at.x - 8, at.y - 4 }, { at.x + extent.x + 8, at.y + extent.y + 4 }, IM_COL32(0, 0, 0, 170), 4);
				draw->AddText(ImGui::GetFont(), size, at, IM_COL32(255, 200, 90, 255), text);
			}
			if (!show_ranges || !state.valid || state.unit_across <= 0)
				return;
			std::vector<float> ranges;
			for (const Macro& each : macros)
			{
				Ranges(each.root, ranges);
				for (const Step& step_ : each.steps)
					if (step_.kind == WAIT_UNTIL) Ranges(step_.until, ranges);
			}
			ImVec2 hero(state.across * screen.x, state.down * screen.y);
			for (float range : ranges)
			{
				int count = 0;
				for (double distance : state.enemies)
					if (distance <= range * 10) count++;
				ImVec2 radius(range * 10 * state.unit_across * screen.x, range * 10 * state.unit_down * screen.y);
				ImU32 color = count ? IM_COL32(110, 230, 110, 220) : IM_COL32(255, 200, 90, 200);
				draw->AddEllipse(hero, radius, IM_COL32(0, 0, 0, 160), 0, 0, 4);
				draw->AddEllipse(hero, radius, color, 0, 0, 2);
				char text[48];
				snprintf(text, sizeof(text), "%.0f: %d", range, count);
				ImVec2 at(hero.x - ImGui::CalcTextSize(text).x / 2, hero.y - radius.y - ImGui::GetFontSize() - 2);
				draw->AddText({ at.x + 1, at.y + 1 }, IM_COL32(0, 0, 0, 220), text);
				draw->AddText(at, color, text);
			}
		}

		// ---- text form, for the config, saved sets and sharing ----

		static void WriteCond(std::vector<std::string>& Lines, const Cond& Of, int Depth, char Tag)
		{
			char text[96];
			snprintf(text, sizeof(text), "%c %d %d %d %d %g %g", Tag, Depth, Of.kind, Of.slot, Of.cmp, Of.value, Of.range);
			Lines.push_back(text);
			for (const Cond& kid : Of.kids)
				WriteCond(Lines, kid, Depth + 1, Tag);
		}

		std::vector<std::string> Lines() const
		{
			std::vector<std::string> lines;
			char text[160];
			for (const Macro& each : macros)
			{
				snprintf(text, sizeof(text), "m %d %d %d %d %g %d ", each.enabled ? 1 : 0, each.trigger, each.key, each.movement, each.wait, each.ready ? 1 : 0);
				lines.push_back(text + each.name);
				WriteCond(lines, each.root, 0, 'c');
				for (const Step& step_ : each.steps)
				{
					snprintf(text, sizeof(text), "s %d %d %g", step_.kind, step_.slot, step_.seconds);
					lines.push_back(text);
					if (step_.kind == WAIT_UNTIL)
						WriteCond(lines, step_.until, 0, 'u');
				}
			}
			return lines;
		}

		// Where the next condition line goes, by its depth.
		std::vector<Cond*> path;

		void ReadLine(const std::string& Tag, std::istringstream& Words)
		{
			if (Tag == "m")
			{
				Macro macro;
				int enabled = 1, ready = 1;
				Words >> enabled >> macro.trigger >> macro.key >> macro.movement >> macro.wait >> ready;
				std::getline(Words >> std::ws, macro.name);
				while (!macro.name.empty() && (macro.name.back() == ' ' || macro.name.back() == '\r' || macro.name.back() == '\n')) macro.name.pop_back();
				macro.enabled = enabled != 0;
				macro.ready = ready != 0;
				macro.trigger = std::clamp(macro.trigger, 0, 2);
				macro.movement = std::clamp(macro.movement, 0, 2);
				macro.wait = std::clamp(macro.wait, 0.0f, 3600.0f);
				if (macro.key < 0 || macro.key > 254) macro.key = 0;
				if (macro.name.empty()) macro.name = "macro";
				macro.uid = Uid();
				macro.root.uid = Uid();
				macros.push_back(macro);
				path.assign(1, &macros.back().root);
			}
			else if (Tag == "s" && !macros.empty())
			{
				Step step_;
				Words >> step_.kind >> step_.slot >> step_.seconds;
				step_.kind = std::clamp(step_.kind, 0, 2);
				step_.slot = std::clamp(step_.slot, 0, SLOTS - 1);
				step_.seconds = std::clamp(step_.seconds, 0.0f, 3600.0f);
				step_.uid = Uid();
				step_.until.uid = Uid();
				macros.back().steps.push_back(step_);
				path.assign(1, &macros.back().steps.back().until);
			}
			else if ((Tag == "c" || Tag == "u") && !path.empty())
			{
				int depth = 0;
				Cond cond;
				if (!(Words >> depth >> cond.kind >> cond.slot >> cond.cmp >> cond.value) || depth < 0 || depth > static_cast<int>(path.size()) || depth > 12)
					return;
				if (cond.kind < 0 || cond.kind >= KINDS)
					return;
				cond.slot = std::clamp(cond.slot, 0, SLOTS - 1);
				cond.cmp = std::clamp(cond.cmp, 0, static_cast<int>(COMPARERS.size()) - 1);
				// Lines from before the range was kept counted the game's own yes or no.
				if (!(Words >> cond.range) && cond.kind == ENEMIES)
				{
					cond.cmp = 3;
					cond.value = 1;
				}
				cond.range = std::clamp(cond.range, 1.0f, 200.0f);
				cond.uid = Uid();
				if (depth == 0)
				{
					// The top of a tree is always a group.
					if (cond.kind > NONE) return;
					path[0]->kind = cond.kind;
					path.resize(1);
					return;
				}
				Cond* parent = path[depth - 1];
				if (parent->kind > NONE)
					return;
				parent->kids.push_back(cond);
				path.resize(depth);
				path.push_back(&parent->kids.back());
			}
		}

		std::string Pack() const
		{
			std::string text = PACKED;
			for (const std::string& line : Lines())
				text += " | " + line;
			return text;
		}

		bool Unpack(std::string Text)
		{
			size_t start = Text.find(PACKED);
			if (start == std::string::npos)
				return false;
			Text = Text.substr(start + strlen(PACKED));
			std::vector<Macro> old = macros;
			macros.clear();
			path.clear();
			running = 0;
			size_t from = 0;
			while (from <= Text.size())
			{
				size_t to = Text.find('|', from);
				std::istringstream words(Text.substr(from, to == std::string::npos ? std::string::npos : to - from));
				std::string tag;
				if (words >> tag) ReadLine(tag, words);
				if (to == std::string::npos) break;
				from = to + 1;
			}
			path.clear();
			if (macros.empty())
			{
				macros = old;
				return false;
			}
			return true;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			int value = 0;
			if (Name == "list")
			{
				macros.clear();
				path.clear();
			}
			else if (Name == "show_ranges")
			{
				if (Words >> value) show_ranges = value != 0;
			}
			else if (Name == "pause_key")
			{
				if (Words >> value && value >= 0 && value <= 254) pause_key = value;
			}
			else
				ReadLine(Name, Words);
		}

		void Save(std::ostream& File) override
		{
			File << "macros.pause_key " << pause_key << '\n' << "macros.show_ranges " << (show_ranges ? 1 : 0) << '\n' << "macros.list 1\n";
			for (const std::string& line : Lines())
				File << "macros." << line << '\n';
		}

		// ---- the tab ----

		static std::filesystem::path Folder()
		{
			return std::filesystem::path(ModFolder()) / "SlormSuite.macros";
		}

		static std::vector<std::string> Saved()
		{
			std::vector<std::string> names;
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(Folder(), error))
				if (entry.path().extension() == ".txt") names.push_back(entry.path().stem().string());
			std::sort(names.begin(), names.end());
			return names;
		}

		// A new line for a tree, with a sensible start for its kind.
		static Cond Fresh(int Kind_)
		{
			Cond cond;
			cond.uid = Uid();
			cond.kind = Kind_;
			if (KIND[Kind_].flag) cond.value = 1;
			else if (Kind_ == COOLDOWN) { cond.cmp = 4; cond.value = 0; }
			else if (Kind_ == SINCE) { cond.cmp = 3; cond.value = 5; }
			else if (Kind_ == ENEMIES) { cond.cmp = 3; cond.value = 1; }
			else if (Kind_ == LIFE || Kind_ == MANA) { cond.cmp = 1; cond.value = 1000; }
			return cond;
		}

		// One line of a tree and what is inside it. True when it asked to be removed.
		bool DrawCond(Cond& Of, const Macro& In, bool Top, bool& Changed)
		{
			bool remove = false;
			ImGui::PushID(Of.uid);
			float unit = ImGui::GetFontSize();
			if (Of.kind <= NONE)
			{
				ImGui::SetNextItemWidth(unit * 6);
				Changed |= ImGui::Combo("##group", &Of.kind, "all of\0any of\0none of\0");
				Tip(KIND[Of.kind].tip);
				ImGui::SameLine();
				if (ImGui::SmallButton("+ condition")) ImGui::OpenPopup("add");
				if (ImGui::BeginPopup("add"))
				{
					for (int kind = 0; kind < KINDS; kind++)
					{
						if (ImGui::Selectable(KIND[kind].name))
						{
							Of.kids.push_back(Fresh(kind));
							Changed = true;
						}
						Tip(KIND[kind].tip);
					}
					ImGui::EndPopup();
				}
				if (!Top)
				{
					ImGui::SameLine();
					remove = ImGui::SmallButton("x");
				}
				if (state.valid)
				{
					ImGui::SameLine();
					bool pass = Pass(Of, In);
					ImGui::TextColored(pass ? ImVec4(0.45f, 0.9f, 0.45f, 1) : ImVec4(0.95f, 0.45f, 0.4f, 1), pass ? "true" : "false");
				}
				ImGui::Indent();
				for (size_t i = 0; i < Of.kids.size();)
				{
					if (DrawCond(Of.kids[i], In, false, Changed))
					{
						Of.kids.erase(Of.kids.begin() + i);
						Changed = true;
					}
					else
						i++;
				}
				if (Of.kids.empty())
					ImGui::TextDisabled("nothing inside: always true");
				ImGui::Unindent();
				ImGui::PopID();
				return remove;
			}
			const KindInfo& info = KIND[Of.kind];
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(info.name);
			Tip(info.tip);
			if (Of.kind == COOLDOWN)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(unit * 7);
				Changed |= ImGui::Combo("##slot", &Of.slot, SLOT_LIST);
			}
			if (Of.kind == ENEMIES)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(unit * 4);
				Changed |= ImGui::InputFloat("##range", &Of.range, 0, 0, "%g");
				Of.range = std::clamp(Of.range, 1.0f, 200.0f);
				Tip("The range, in the numbers ESP shows for distances. Tick Show ranges above to see it drawn.");
				ImGui::SameLine();
				ImGui::TextUnformatted("count");
			}
			ImGui::SameLine();
			if (info.flag)
			{
				int yes = Of.value != 0 ? 0 : 1;
				ImGui::SetNextItemWidth(unit * 5);
				if (ImGui::Combo("##is", &yes, "is yes\0is no\0"))
				{
					Of.value = yes == 0 ? 1.0f : 0.0f;
					Changed = true;
				}
			}
			else
			{
				ImGui::SetNextItemWidth(unit * 3.5f);
				Changed |= ImGui::Combo("##cmp", &Of.cmp, "<\0<=\0>\0>=\0=\0!=\0");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(unit * 6);
				Changed |= ImGui::InputFloat("##value", &Of.value, 0, 0, "%g");
				if (info.unit[0])
				{
					ImGui::SameLine();
					ImGui::TextUnformatted(info.unit);
				}
			}
			ImGui::SameLine();
			remove = ImGui::SmallButton("x");
			if (state.valid)
			{
				ImGui::SameLine();
				bool pass = Pass(Of, In);
				double value = Value(Of, In);
				char text[48];
				if (info.flag) snprintf(text, sizeof(text), "now %s", value != 0 ? "yes" : "no");
				else if (value >= 99999) snprintf(text, sizeof(text), "now never");
				else snprintf(text, sizeof(text), "now %.4g", value);
				ImGui::TextColored(pass ? ImVec4(0.45f, 0.9f, 0.45f, 1) : ImVec4(0.95f, 0.45f, 0.4f, 1), "%s", text);
			}
			ImGui::PopID();
			return remove;
		}

		void DrawSteps(Macro& Of, bool& Changed)
		{
			float unit = ImGui::GetFontSize();
			int move_from = -1, move_to = -1, remove = -1;
			for (int i = 0; i < static_cast<int>(Of.steps.size()); i++)
			{
				Step& at = Of.steps[i];
				ImGui::PushID(at.uid);
				bool now = running == Of.uid && step == i;
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(now ? ImVec4(0.45f, 0.9f, 0.45f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%d.", i + 1);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(unit * 7);
				Changed |= ImGui::Combo("##kind", &at.kind, "press\0wait\0wait until\0");
				ImGui::SameLine();
				if (at.kind == PRESS)
				{
					ImGui::SetNextItemWidth(unit * 7);
					Changed |= ImGui::Combo("##slot", &at.slot, SLOT_LIST);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(unit * 5);
					Changed |= ImGui::InputFloat("s held", &at.seconds, 0, 0, "%g");
					Tip("How long the key stays down. 0 is a single tap.");
				}
				else if (at.kind == WAIT)
				{
					ImGui::SetNextItemWidth(unit * 5);
					Changed |= ImGui::InputFloat("s", &at.seconds, 0, 0, "%g");
				}
				else
				{
					ImGui::SetNextItemWidth(unit * 5);
					Changed |= ImGui::InputFloat("s at most", &at.seconds, 0, 0, "%g");
					Tip("The macro stops here if the conditions below have not come true by then.");
				}
				at.seconds = std::clamp(at.seconds, 0.0f, 3600.0f);
				ImGui::SameLine();
				if (ImGui::ArrowButton("up", ImGuiDir_Up) && i > 0) { move_from = i; move_to = i - 1; }
				ImGui::SameLine();
				if (ImGui::ArrowButton("down", ImGuiDir_Down) && i + 1 < static_cast<int>(Of.steps.size())) { move_from = i; move_to = i + 1; }
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) remove = i;
				if (at.kind == WAIT_UNTIL)
				{
					ImGui::Indent();
					if (!at.until.uid) at.until.uid = Uid();
					DrawCond(at.until, Of, true, Changed);
					ImGui::Unindent();
				}
				ImGui::PopID();
			}
			if (move_from >= 0)
			{
				std::swap(Of.steps[move_from], Of.steps[move_to]);
				Changed = true;
			}
			if (remove >= 0)
			{
				Of.steps.erase(Of.steps.begin() + remove);
				Changed = true;
			}
			if (Of.steps.empty())
				ImGui::TextDisabled("no steps: this macro does nothing");
			if (ImGui::SmallButton("+ step"))
			{
				Step fresh;
				fresh.uid = Uid();
				fresh.until.uid = Uid();
				if (!Of.steps.empty()) fresh.slot = Of.steps.back().slot;
				Of.steps.push_back(fresh);
				Changed = true;
			}
		}

		void DrawMacro(Macro& Of, bool& Changed)
		{
			float unit = ImGui::GetFontSize();
			ImGui::SeparatorText("Before it starts");
			ImGui::SetNextItemWidth(unit * 12);
			char name[48];
			snprintf(name, sizeof(name), "%s", Of.name.c_str());
			if (ImGui::InputText("name", name, sizeof(name)))
			{
				Of.name = name;
				// The text form keeps a macro on one line, split at bars.
				std::replace(Of.name.begin(), Of.name.end(), '|', '/');
				Changed = true;
			}
			ImGui::SetNextItemWidth(unit * 12);
			Changed |= ImGui::Combo("trigger", &Of.trigger, "automatic\0while a key is held\0switched by a key\0");
			Tip("Automatic: whenever its conditions hold.\nWhile a key is held: only with that key down.\nSwitched by a key: the key turns it on and off.");
			if (Of.trigger != AUTOMATIC)
			{
				ImGui::SameLine();
				Changed |= KeyButton("key", &Of.key);
				if (Of.trigger == TOGGLED)
				{
					ImGui::SameLine();
					ImGui::TextDisabled(Of.on ? "(on)" : "(off)");
				}
			}
			ImGui::SetNextItemWidth(unit * 12);
			Changed |= ImGui::Combo("movement", &Of.movement, "don't care\0only standing still\0only moving\0");
			ImGui::SetNextItemWidth(unit * 12);
			Changed |= ImGui::InputFloat("s between uses", &Of.wait, 0, 0, "%g");
			Of.wait = std::clamp(Of.wait, 0.0f, 3600.0f);
			Tip("How long after finishing before it may start again.");
			Changed |= ImGui::Checkbox("Only when its skills are off cooldown", &Of.ready);
			Tip("Every skill its steps press must be ready, or it does not start.");

			ImGui::SeparatorText("Conditions");
			if (!Of.root.uid) Of.root.uid = Uid();
			DrawCond(Of.root, Of, true, Changed);

			ImGui::SeparatorText("Steps");
			DrawSteps(Of, Changed);

			ImGui::Spacing();
			if (ImGui::Button("Test fire"))
				test = Of.uid;
			Tip("Runs its steps once now, whatever its conditions say. Needs the hero out of town and no menu open.");
			ImGui::SameLine();
			if (!state.valid) ImGui::TextDisabled("no live data: start playing to see values");
			else if (running == Of.uid) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1), "running step %d", step + 1);
			else if (paused) ImGui::TextDisabled("all macros are paused");
			else if (in_town) ImGui::TextDisabled("macros rest in town");
			else if (const char* why = Blocked(Of)) ImGui::TextDisabled("not starting: %s", why);
			else ImGui::TextDisabled("starting");
			ImGui::SameLine();
			ImGui::TextDisabled("  fired %d times", Of.fired);
		}

		bool DrawShare()
		{
			bool changed = false;
			float unit = ImGui::GetFontSize();
			ImGui::SetNextItemWidth(unit * 10);
			ImGui::InputTextWithHint("##name", "name", save_name, sizeof(save_name));
			ImGui::SameLine();
			if (ImGui::Button("Save") && save_name[0])
			{
				std::error_code error;
				std::filesystem::create_directories(Folder(), error);
				std::string name = save_name;
				for (char& c : name)
					if (strchr("\\/:*?\"<>|", c)) c = '_';
				std::ofstream file(Folder() / (name + ".txt"), std::ios::trunc);
				file << Pack() << '\n';
				note = file ? "saved as " + name : "could not save";
			}
			Tip("Saves every macro under this name, over any saved before under it.\nThe files are in SlormSuite.macros, next to the mod.");
			std::vector<std::string> saved = Saved();
			if (!saved.empty())
			{
				saved_at = std::clamp(saved_at, 0, static_cast<int>(saved.size()) - 1);
				ImGui::SetNextItemWidth(unit * 10);
				if (ImGui::BeginCombo("##saved", saved[saved_at].c_str()))
				{
					for (int i = 0; i < static_cast<int>(saved.size()); i++)
						if (ImGui::Selectable(saved[i].c_str(), i == saved_at))
						{
							saved_at = i;
							confirm_delete = false;
						}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::Button("Load"))
				{
					std::ifstream file(Folder() / (saved[saved_at] + ".txt"));
					std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
					bool read = Unpack(text);
					note = read ? "loaded " + saved[saved_at] : "nothing readable in " + saved[saved_at];
					changed |= read;
				}
				Tip("Replaces every macro with the saved ones.");
				ImGui::SameLine();
				if (ImGui::Button(confirm_delete ? "Really delete?" : "Delete"))
				{
					if (confirm_delete)
					{
						std::error_code error;
						std::filesystem::remove(Folder() / (saved[saved_at] + ".txt"), error);
						note = "deleted " + saved[saved_at];
					}
					confirm_delete = !confirm_delete;
				}
			}
			if (ImGui::Button("Export"))
			{
				ImGui::SetClipboardText(Pack().c_str());
				note = "copied to the clipboard";
			}
			Tip("Copies every macro to the clipboard as one line of text.");
			ImGui::SameLine();
			if (ImGui::Button("Import"))
			{
				const char* text = ImGui::GetClipboardText();
				bool read = text && Unpack(text);
				note = read ? "imported from the clipboard" : "no macros on the clipboard";
				changed |= read;
			}
			Tip("Replaces every macro with the ones on the clipboard.");
			if (!note.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", note.c_str());
			}
			return changed;
		}

		bool Draw() override
		{
			bool changed = false;
			changed |= KeyButton("pause", &pause_key);
			ImGui::SameLine();
			ImGui::TextUnformatted(paused ? "pauses and resumes every macro (paused now)" : "pauses and resumes every macro");

			changed |= ImGui::Checkbox("Show ranges", &show_ranges);
			Tip("Draws a circle around the hero for each range an enemies within condition uses, with the number of enemies inside it.");

			ImGui::SeparatorText("Macros");
			ImGui::TextDisabled("Tried from the top; a higher one that can start takes over from a lower one. Drag a title to reorder.");
			int moved = 0, onto = 0, remove = 0;
			for (Macro& each : macros)
			{
				ImGui::PushID(each.uid);
				changed |= ImGui::Checkbox("##on", &each.enabled);
				ImGui::SameLine();
				bool live = running == each.uid;
				if (live) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.9f, 0.45f, 1));
				bool open = ImGui::TreeNodeEx("macro", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap, "%s", each.name.c_str());
				if (live) ImGui::PopStyleColor();
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("macro", &each.uid, sizeof(int));
					ImGui::TextUnformatted(each.name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("macro"))
					{
						moved = *static_cast<const int*>(payload->Data);
						onto = each.uid;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 1.5f + ImGui::GetCursorPosX());
				if (ImGui::SmallButton("x")) ImGui::OpenPopup("remove");
				if (ImGui::BeginPopup("remove"))
				{
					if (ImGui::Selectable("Remove this macro")) remove = each.uid;
					ImGui::EndPopup();
				}
				if (open)
				{
					DrawMacro(each, changed);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (moved && onto && moved != onto)
			{
				auto from = std::find_if(macros.begin(), macros.end(), [&](const Macro& m) { return m.uid == moved; });
				if (from != macros.end())
				{
					Macro taken = *from;
					macros.erase(from);
					auto to = std::find_if(macros.begin(), macros.end(), [&](const Macro& m) { return m.uid == onto; });
					macros.insert(to, taken);
					changed = true;
				}
			}
			if (remove)
			{
				macros.erase(std::remove_if(macros.begin(), macros.end(), [&](const Macro& m) { return m.uid == remove; }), macros.end());
				if (running == remove) running = 0;
				changed = true;
			}
			if (macros.empty())
				ImGui::TextDisabled("no macros yet");
			if (ImGui::Button("Add macro"))
			{
				Macro fresh;
				fresh.uid = Uid();
				fresh.root.uid = Uid();
				macros.push_back(fresh);
				changed = true;
			}

			ImGui::SeparatorText("Save and Share");
			changed |= DrawShare();
			return changed;
		}
	};
	Macros g_Macros;

	void Stepped(CInstance* Hero)
	{
		std::lock_guard guard(g_Lock);
		g_Macros.Step_(Hero);
	}
}
