// Spends the hero's slorm on ranks of ancestral legacy nodes picked at random, for the elemental
// damage the game grants by the slorm put into the legacy.
#include "../Suite.hpp"
#include "../LegacyCosts.hpp"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <random>
using namespace Aurie;
using namespace YYTK;

namespace
{
	constexpr int NODES = 150;

	// With the legacy shown, the game keeps the ranks being edited in the UI's temp_elems, a list of nodes
	// for each hero, buys a rank with scr_ui_element_invest and gives one back, for what it cost, with
	// scr_ui_element_devest; they are kept when the legacy is closed.
	PFUNC_YYGMLScript g_Invest = nullptr, g_Devest = nullptr;

	int MaxRank(int Node)
	{
		int ranks = 0;
		while (ranks < 10 && LEGACY_COSTS[Node][ranks] > 0) ranks++;
		return ranks;
	}

	// The slorm of the hero being played, -1 when it cannot be read.
	double Slorm(int& Hero)
	{
		RValue all = Global("essence");
		Hero = static_cast<int>(ToNumber(Global("current_hero")));
		if (!all.IsArray() || Hero < 0 || ToNumber(g_Yytk->CallBuiltin("array_length", { all })) <= Hero)
			return -1;
		return ToNumber(g_Yytk->CallBuiltin("array_get", { all, RValue(static_cast<double>(Hero)) }));
	}

	struct Legacy : Feature
	{
		// Slorm left alone.
		int keep = 0;
		// Slorm to get back by giving up ranks.
		int back = 0;

		std::atomic<bool> want_start = false, want_back = false, want_stop = false, running = false;
		// Giving ranks back rather than buying them.
		bool taking = false;
		std::mutex lock;
		std::string status;
		// As last read by the game's side, for the overlay.
		std::atomic<double> slorm = -1;
		std::atomic<int> open = 0;
		std::atomic<bool> shown = false;
		int hero = -1, bought = 0;
		double spent = 0;
		std::mt19937 dice{ std::random_device{}() };

		Legacy() : Feature("Auto spend slorm on Ancestral Legacy", "legacy",
			"Buttons that spend the hero's slorm on ranks of ancestral legacy nodes picked at random, for the elemental damage granted by slorm put into the legacy, and give ranks back. Pressed with the Ancestral Legacy open.") {}

		void Start() override
		{
			g_Invest = FindScript("scr_ui_element_invest");
			g_Devest = FindScript("scr_ui_element_devest");
			if (!g_Invest || !g_Devest)
				Log("legacy: no invest or devest script");
		}

		void Say(const std::string& Text)
		{
			Log("legacy: %s", Text.c_str());
			std::lock_guard guard(lock);
			status = Text;
		}

		void End(const char* Why)
		{
			char text[200];
			snprintf(text, sizeof(text), taking ? "%s: %d rank%s given up for %.0f slorm" : "%s: %d rank%s bought for %.0f slorm", Why, bought, bought == 1 ? "" : "s", spent);
			running = false;
			Say(text);
		}

		void Tick(CInstance* Self) override
		{
			int who = -1;
			double has = Slorm(who);
			if (who != hero && running)
				End("stopped, the hero changed");
			hero = who;
			slorm = has;
			// The ranks as the open legacy has them.
			std::vector<int> ranks;
			RValue heroes = ToText(Local(Self, "menu_show")) == "elements" ? Local(Self, "temp_elems") : RValue();
			if (has >= 0 && heroes.IsArray() && ToNumber(g_Yytk->CallBuiltin("array_length", { heroes })) > who)
			{
				RValue mine = g_Yytk->CallBuiltin("array_get", { heroes, RValue(static_cast<double>(who)) });
				if (mine.IsArray())
					for (const RValue& rank : mine.ToVector())
						ranks.push_back(static_cast<int>(ToNumber(rank)));
			}
			if (ranks.size() != NODES)
				ranks.clear();
			shown = !ranks.empty();
			// Ranks that could be bought with what may be spent.
			std::vector<int> among;
			for (int node = 0; node < NODES; node++)
				if (shown && ranks[node] >= 0 && ranks[node] < MaxRank(node) && LEGACY_COSTS[node][ranks[node]] <= has - keep)
					among.push_back(node);
			open = static_cast<int>(among.size());
			if (want_stop.exchange(false) && running)
				End("stopped");
			bool start = want_start.exchange(false), give = want_back.exchange(false);
			if ((start || give) && !running)
			{
				bought = 0;
				spent = 0;
				taking = give;
				if (!(taking ? g_Devest : g_Invest))
					return Say("the game has no script for it; nothing was done");
				if (has < 0)
					return Say("no hero's slorm to read; nothing was done");
				if (!shown)
					return Say("open the Ancestral Legacy, then press again");
				if (taking && back < 1)
					return Say("set how much slorm to get back first");
				running = true;
				Say(taking ? "giving ranks back" : "spending");
			}
			if (!running)
				return;
			if (!shown)
				return End("stopped, the Ancestral Legacy was closed");
			// Nodes the hero has in use keep their ranks.
			std::vector<RValue> used;
			RValue equipped = Global("fast_check_elements_equipped");
			if (taking && equipped.IsArray())
				used = equipped.ToVector();
#ifdef SLORM_DEV
			if (taking && bought == 0)
			{
				std::string in_use;
				for (size_t node = 0; node < used.size(); node++)
					if (ToNumber(used[node]) >= 0) in_use += " " + std::to_string(node);
				Log("legacy: nodes in use:%s", in_use.c_str());
			}
#endif
			// A few ranks each step.
			for (int turn = 0; turn < 12; turn++)
			{
				if (taking && spent >= back)
					return End("done");
				among.clear();
				for (int node = 0; node < NODES; node++)
					if (taking ? ranks[node] > 0 && ranks[node] <= MaxRank(node) && !(node < static_cast<int>(used.size()) && ToNumber(used[node]) >= 0)
						: ranks[node] >= 0 && ranks[node] < MaxRank(node) && LEGACY_COSTS[node][ranks[node]] <= has - keep)
						among.push_back(node);
				if (among.empty())
					return End(taking ? "no more ranks to give up" : "done");
				int node = among[std::uniform_int_distribution<size_t>(0, among.size() - 1)(dice)], rank = ranks[node], next = rank + (taking ? -1 : 1);
				int cost = LEGACY_COSTS[node][taking ? rank - 1 : rank];
				CallScript(taking ? g_Devest : g_Invest, Self, { RValue(static_cast<double>(node)) });
				int same = -1;
				double left = Slorm(same);
#ifdef SLORM_DEV
				Log("legacy: node %d rank %d -> %d, slorm %.0f -> %.0f, cost %d", node, rank, next, has, left, cost);
#endif
				// Anything but the price means the game does not work as this does; nothing more is touched.
				if (left != has + (taking ? cost : -cost))
				{
					char text[200];
					snprintf(text, sizeof(text), "stopped, node %d rank %d changed slorm by %.0f where %d was expected", node, next, left - has, taking ? cost : -cost);
					return End(text);
				}
				bought++;
				spent += cost;
				ranks[node] = next;
				has = left;
			}
		}

		bool Draw() override
		{
			bool changed = false;
			ImGui::SetNextItemWidth(130);
			changed |= ImGui::InputInt("Keep at least this much slorm (0 = spend it all)", &keep, 0, 0);
			if (keep < 0) keep = 0;
			bool busy = running;
			if (ImGui::Button(busy ? "Stop" : "Spend slorm"))
				(busy ? want_stop : want_start) = true;
			ImGui::SameLine();
			double has = slorm;
			if (has < 0)
				ImGui::TextDisabled("no hero");
			else if (!shown)
				ImGui::TextDisabled("%.0f slorm; open the Ancestral Legacy to spend it", has);
			else
				ImGui::TextDisabled("%.0f slorm, %d node%s can take a rank", has, open.load(), open == 1 ? "" : "s");
			ImGui::SetNextItemWidth(130);
			changed |= ImGui::InputInt("Get back at least this much slorm", &back, 0, 0);
			if (back < 0) back = 0;
			ImGui::BeginDisabled(busy);
			if (ImGui::Button("Give ranks back"))
				want_back = true;
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Gives up ranks of nodes picked at random until that much slorm is back.\nNodes in use are meant to be left alone.");
			std::lock_guard guard(lock);
			if (!status.empty())
				ImGui::TextDisabled("%s", status.c_str());
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			if (Name == "keep" && Words >> keep && keep < 0)
				keep = 0;
			if (Name == "back" && Words >> back && back < 0)
				back = 0;
		}

		void Save(std::ostream& File) override
		{
			File << "legacy.keep " << keep << '\n'
				<< "legacy.back " << back << '\n';
		}
	} g_Legacy;
}
