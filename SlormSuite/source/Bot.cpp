// Plays a floor of an expedition. Walks to the nearest open breach, fights what comes out until it
// closes, picks up what drops, and takes the door once it can be had.
#include "Suite.hpp"
#include "ReaperNames.hpp"
#include <imgui.h>
#include <deque>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <queue>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// Enemies this near are fought before anything else, and hit from this near.
	constexpr double NOTICE = 170, HIT = 110;
	// Enemies this near an open breach are gone to as well.
	constexpr double AROUND = 300;
	// Near enough to an enemy to stop walking at it.
	constexpr double CLOSE = 30;
	// A breach this little out of the way, on foot, is done in passing.
	constexpr float ON_THE_WAY = 260;
	// Standing on a breach counts from this near.
	constexpr double ON_BREACH = 28;
	// The door can be had once all but this many breaches are closed.
	constexpr int SPARE = 5;
	constexpr ULONGLONG ACT_MS = 700, GIVE_UP_MS = 120000, SKIP_MS = 45000;

	// What the hero has picked up, newest last; the same thing again is counted on its line.
	enum LootKind { LOOT_STUFF, LOOT_GEAR, LOOT_REAPER };
	struct LootLine
	{
		std::string text;
		int kind = LOOT_STUFF;
		int count = 1;
	};

	struct Farm
	{
		// Plays the floor: breach after breach, then the door. Never saved.
		bool on = false;
		int key = VK_HOME;
		// The game's click to move option is off; read each step.
		bool no_ctm = false;
		std::string status = "off";
		// With the door to be had, breaches within this of the hero are still done first.
		float extra = 500;
		// Chaos breaches are left alone.
		bool skip_chaos = true;
		// Loot lying within this of the hero, on foot, is walked to and picked up between breaches.
		bool loot = true;
		float loot_range = 450;
		// In town, walks to Almerolis and starts the next battlefield.
		bool restart = true;
		// What is started from town: the battlefield, the Great Forge, or the Slorm Temple.
		int mode = 0;
		// The temple floor a run starts on, 0 for the one the launcher has; the highest ever cleared, -1 until read.
		int temple_floor = 0, temple_cleared = -1;
		// The shrine at the end of a temple floor is used before the door is taken.
		bool temple_shrine = true;
		// Pure slorm left on the ground of a temple floor is walked onto.
		bool temple_pure = true;
		// The Great Forge is fought up to this wave, then its chest is opened and the hero heads home.
		int forge_waves = 10;
		// In the Great Forge the hero holds the middle and lets the enemies come.
		bool forge_center = true;
		// Limits, 0 for none: expeditions started before the Farmer stops in town, and battlefield
		// floors played before the door is taken home.
		int max_runs = 0, max_floors = 0;
		// A full bag sends the hero home at the next door and stops the Farmer in town.
		bool stop_full = true;
		// Expeditions started since it was switched on, floors seen in this one.
		int runs = 0, floors = 0;
		// Why it switched itself off; shown in place of "off".
		std::string stopped;
		// Bag slots taken and had, -1 until read.
		int bag_used = -1, bag_slots = 0;
		// Holds the primary skill on enemies; off for a hero whose skills fire by themselves.
		bool attack = false;
		std::deque<LootLine> loot_log;
		// What the loot log lists: reapers, gear, fragments, slormites, slormelines, slormandrites, the rest.
		bool loot_show[7] = { true, true, false, false, true, true, true };
		// How many things have been picked up.
		int loot_count = 0;
	} g_Farm;
	Feature* g_Mod = nullptr;
	// The mod that unlocks reapers as they are picked up.
	Feature* g_Unlock = nullptr;

	struct Place { double x = 0, y = 0; };
	struct Breach
	{
		Place at;
		// Not done yet: the breach is still in the room.
		bool open = false;
		RValue it;
		// Stepped on, its enemies out; the game shuts the hero in with them until it is done.
		bool active = false;
		// A chaos breach, ESP's "Breach V".
		bool chaos = false;
	};

	bool g_Has = false;
	// The hero has stood on the breach being worked on.
	bool g_Reached = false;
	Place g_Breach;
	ULONGLONG g_Since = 0, g_ActAt = 0, g_QuietAt = 0;
	bool g_Walking = false;
	int g_Fails = 0, g_Closed = -1;
	std::map<long long, ULONGLONG> g_Skip;
	bool g_Attack = false;
	Place g_AttackAt;
	std::string g_Said;

	long long Key(const Place& At) { return static_cast<long long>(At.x) * 100000 + static_cast<long long>(At.y); }
	double Far(const Place& A, const Place& B) { return hypot(A.x - B.x, A.y - B.y); }

	// The status line, logged when it changes.
	void Say(const std::string& Text)
	{
		g_Farm.status = Text;
		if (Text != g_Said)
			Log("bot: %s", Text.c_str());
		g_Said = Text;
	}

	std::vector<Breach> Breaches()
	{
		std::vector<Breach> out;
		CInstance* level = FindInstance("obj_procedural");
		RValue list = level ? Local(level, "breach_array_coords") : RValue();
		if (!list.IsArray())
			return out;
		// The breaches still in the room, by where they are.
		std::map<long long, RValue> there;
		RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_breach") });
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { object })));
		for (int i = 0; i < count; i++)
		{
			RValue instance = g_Yytk->CallBuiltin("instance_find", { object, RValue(static_cast<double>(i)) });
			there[Key({ ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("x") })), ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("y") })) })] = instance;
		}
		for (const RValue& entry : list.ToVector())
		{
			if (!entry.IsArray())
				continue;
			std::vector<RValue> parts = entry.ToVector();
			if (parts.size() < 3)
				continue;
			// The third is the breach, or -1 from when it is stepped on; it stays in the room until it is done.
			Breach breach;
			breach.at = { ToNumber(parts[0]), ToNumber(parts[1]) };
			auto found = there.find(Key(breach.at));
			breach.open = found != there.end();
			if (breach.open)
			{
				breach.it = found->second;
				breach.active = !(parts[2].m_Kind == VALUE_REF || ToNumber(parts[2]) > 0);
				breach.chaos = ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { breach.it, RValue("breach_outpost") })) > 0;
			}
			out.push_back(breach);
		}
		return out;
	}

	// The living enemies, nearest first. Townsfolk have no life and are left out.
	// What the nearest of them is, for the log.
	std::string g_Nearest;
	std::vector<Place> Enemies(CInstance* Hero, const Place& Me)
	{
		std::vector<Place> out;
		double nearest = 1e9;
		g_Nearest.clear();
		RValue parent = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_parent_lifeform") });
		double me = Local(Hero, "id").ToDouble();
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { parent })));
		for (int i = 0; i < count; i++)
		{
			RValue other = g_Yytk->CallBuiltin("instance_find", { parent, RValue(static_cast<double>(i)) });
			auto of = [&](const char* Name) { return ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { other, RValue(Name) })); };
			if (g_Yytk->CallBuiltin("variable_instance_get", { other, RValue("id") }).ToDouble() == me || of("dead_since") > 0 || of("the_health") <= 0)
				continue;
			// The game keeps some it has put away far off the map.
			if (of("x") < 0 || of("y") < 0)
				continue;
			// Those who fight for the hero are alive too: only the other side counts.
			if (ToText(g_Yytk->CallBuiltin("variable_instance_get", { other, RValue("team") })) != "bad")
				continue;
			std::string name = ToText(g_Yytk->CallBuiltin("object_get_name", { g_Yytk->CallBuiltin("variable_instance_get", { other, RValue("object_index") }) }));
			out.push_back({ of("x"), of("y") });
			if (Far(out.back(), Me) < nearest)
			{
				nearest = Far(out.back(), Me);
				g_Nearest = name;
			}
		}
		std::sort(out.begin(), out.end(), [&](const Place& A, const Place& B) { return Far(A, Me) < Far(B, Me); });
		return out;
	}

	// The nearest instance of an object to a place; false when the room has none.
	bool Nearest(const char* Object, const Place& To, Place& Found)
	{
		RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue(Object) });
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { object })));
		for (int i = 0; i < count; i++)
		{
			RValue instance = g_Yytk->CallBuiltin("instance_find", { object, RValue(static_cast<double>(i)) });
			Place at = { ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("x") })), ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("y") })) };
			if (i == 0 || Far(at, To) < Far(Found, To))
				Found = at;
		}
		return count > 0;
	}

	bool Busy()
	{
		int state = WalkStatus().state;
		return state == WALK_STARTING || state == WALK_WALKING;
	}

	// A walk asked for now, unless one is on its way or one was asked for a moment ago.
	bool Go(const Place& To, ULONGLONG Now, double Reach = 24)
	{
		if (Busy() || Now - g_ActAt < ACT_MS)
			return false;
		g_ActAt = Now;
		WalkTo(To.x, To.y, false, Reach);
		return true;
	}

	// The game is told the mouse is on a place until then.
	ULONGLONG g_HoverUntil = 0;
	Place g_Hover;

	// The door, or what is left of a breach to open the door with. The game offers either ("Open a
	// Portal") while the mouse is on it, and a click then walks the hero up and uses it; the mouse has
	// to stay there. So the hero is stood below, the mouse is put on it and kept there, and once the
	// game offers it, it is clicked. Where on it the mouse has to be is tried out.
	// On the door the mouse stays above the middle: there it is the next floor, below it the way home.
	// Door: 0 what is left of a breach, 1 the door to the next floor, 2 the door home.
	void Exit(CInstance* Self, const Place& At, int Door, const Place& Me, ULONGLONG Now)
	{
		// The door goes on with the mouse above its middle and home with it below.
		static constexpr double RESIDUE[] = { 8, 16, 0, 24 }, DOOR[] = { -8, -12, -5, -16 }, HOME[] = { 8, 12, 5, 16 };
		const double* TRIES = Door == 2 ? HOME : Door ? DOOR : RESIDUE;
		static long long key = 0;
		static ULONGLONG staged_at = 0, tried_at = 0, clicked_at = 0;
		static int tried = 0;
		static std::string said;
		// Once below it, the hero is left to walk up for a while before it is stood below again.
		long long this_key = Key(At) * 3 + Door;
		bool staged = key == this_key && Now - staged_at < 8000;
		if (!staged)
		{
			if (!(Me.y > At.y + 6 && fabs(Me.x - At.x) < 28 && Far(Me, At) < 60))
			{
				// A walk ends some 17 above where it was clicked.
				Go({ At.x, At.y + 50 }, Now);
				return;
			}
			key = this_key;
			staged_at = tried_at = Now;
			clicked_at = 0;
			tried = 0;
		}
		std::string offer = ToText(Local(Self, "activable_value"));
		if (offer.empty() && Now - tried_at > 1200)
		{
			tried = (tried + 1) % 4;
			tried_at = Now;
		}
		g_Hover = { At.x, At.y + TRIES[tried] };
		g_HoverUntil = Now + 700;
		if (offer != said)
			Log("bot: the game offers \"%s\" with the mouse %+.0f from it, the hero at %.0f %.0f", offer.c_str(), TRIES[tried], Me.x, Me.y);
		said = offer;
		if (offer.empty() || Now - tried_at < 250 || Now - clicked_at < 1500 || Busy())
			return;
		clicked_at = Now;
		WalkTo(g_Hover.x, g_Hover.y, false, 0);
	}

	// The level's floor, a cell set where the hero can walk; read once a floor.
	struct Floor
	{
		int width = 0, height = 0;
		double cell = 16;
		std::vector<char> open;
	};
	Floor g_Floor;
	constexpr float NO_WAY = 1e9f;

	void ReadFloor(CInstance* Self)
	{
		g_Floor = Floor();
		RValue grid = Local(Self, "grid_minimap");
		// 5 is GML's ds_type_grid.
		if (grid.m_Kind == VALUE_UNDEFINED || grid.m_Kind == VALUE_UNSET || ToNumber(g_Yytk->CallBuiltin("ds_exists", { grid, RValue(5.0) })) < 1)
			return;
		int width = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { grid }))), height = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { grid })));
		RValue room_width;
		if (width < 1 || height < 1 || width > 600 || height > 600 || !AurieSuccess(g_Yytk->GetBuiltin("room_width", nullptr, NULL_INDEX, room_width)))
			return;
		g_Floor.width = width;
		g_Floor.height = height;
		g_Floor.cell = ToNumber(room_width) / width;
		g_Floor.open.resize(static_cast<size_t>(width) * height);
		// Floor is 1 before it is seen and 100 after; walls are the same below zero.
		for (int down = 0; down < height; down++)
			for (int across = 0; across < width; across++)
				g_Floor.open[down * width + across] = ToNumber(g_Yytk->CallBuiltin("ds_grid_get", { grid, RValue(static_cast<double>(across)), RValue(static_cast<double>(down)) })) > 0;
		Log("bot: floor read, %d x %d cells of %.0f", width, height, g_Floor.cell);
	}

	// The floor cell a place is in, or the nearest one within a few cells; -1 for none.
	int Cell(const Place& At)
	{
		if (g_Floor.width < 1 || g_Floor.cell <= 0)
			return -1;
		int across = static_cast<int>(At.x / g_Floor.cell), down = static_cast<int>(At.y / g_Floor.cell), best = -1;
		double best_far = 0;
		for (int dy = -3; dy <= 3; dy++)
			for (int dx = -3; dx <= 3; dx++)
			{
				int x = across + dx, y = down + dy;
				if (x < 0 || y < 0 || x >= g_Floor.width || y >= g_Floor.height || !g_Floor.open[y * g_Floor.width + x])
					continue;
				double dist = hypot(dx, dy);
				if (best < 0 || dist < best_far)
				{
					best = y * g_Floor.width + x;
					best_far = dist;
				}
			}
		return best;
	}

	// How far it is on foot from a place to every cell of the floor, in the room's units.
	std::vector<float> Walked(const Place& From)
	{
		std::vector<float> dist(g_Floor.open.size(), NO_WAY);
		int start = Cell(From);
		if (start < 0)
			return dist;
		std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> next;
		dist[start] = 0;
		next.push({ 0.0f, start });
		int width = g_Floor.width, height = g_Floor.height;
		float step = static_cast<float>(g_Floor.cell);
		while (!next.empty())
		{
			auto [so_far, at] = next.top();
			next.pop();
			if (so_far > dist[at])
				continue;
			int x = at % width, y = at / width;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int nx = x + dx, ny = y + dy;
					if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= width || ny >= height || !g_Floor.open[ny * width + nx])
						continue;
					// No cutting a corner of a wall.
					if (dx && dy && (!g_Floor.open[y * width + nx] || !g_Floor.open[ny * width + x]))
						continue;
					float to = so_far + step * (dx && dy ? 1.41421f : 1.0f);
					if (to < dist[ny * width + nx])
					{
						dist[ny * width + nx] = to;
						next.push({ to, ny * width + nx });
					}
				}
		}
		return dist;
	}

	// How far it is on foot from each breach, kept for the floor: breaches do not move.
	std::map<long long, std::vector<float>> g_From;

	// The breach to do next and how far it is on foot: the first of the shortest round found through
	// as many breaches as the door still needs, with those that lie close to that round added to it.
	const Breach* Next(const std::vector<const Breach*>& Among, const Place& Me, int Needs, double& Away)
	{
		Away = 0;
		if (Among.empty())
			return nullptr;
		size_t count = Among.size();
		// Without the floor, as the crow flies.
		bool floor = g_Floor.width > 0 && Cell(Me) >= 0;
		std::vector<std::vector<float>> dist(count + 1, std::vector<float>(count, NO_WAY));
		for (size_t from = 0; from <= count; from++)
		{
			Place at = from ? Among[from - 1]->at : Me;
			std::vector<float> here;
			const std::vector<float>* walked = nullptr;
			if (floor && from)
			{
				auto kept = g_From.find(Key(at));
				if (kept == g_From.end())
					kept = g_From.emplace(Key(at), Walked(at)).first;
				walked = &kept->second;
			}
			else if (floor)
			{
				here = Walked(at);
				walked = &here;
			}
			for (size_t to = 0; to < count; to++)
			{
				int cell = floor ? Cell(Among[to]->at) : -1;
				dist[from][to] = !floor ? static_cast<float>(Far(at, Among[to]->at)) : cell < 0 ? NO_WAY : (*walked)[cell];
			}
		}
		// Nearest first, each from the one before.
		std::vector<int> order, rest;
		for (size_t i = 0; i < count; i++)
			if (dist[0][i] < NO_WAY) rest.push_back(static_cast<int>(i));
		if (rest.empty())
			return nullptr;
		size_t many = (std::min)(static_cast<size_t>((std::max)(Needs, 1)), rest.size());
		int at = -1;
		while (order.size() < many)
		{
			auto best = std::min_element(rest.begin(), rest.end(), [&](int A, int B) { return dist[at + 1][A] < dist[at + 1][B]; });
			at = *best;
			order.push_back(at);
			rest.erase(best);
		}
		auto length = [&](const std::vector<int>& Of)
		{
			float sum = dist[0][Of[0]];
			for (size_t i = 1; i < Of.size(); i++) sum += dist[Of[i - 1] + 1][Of[i]];
			return sum;
		};
		// What a breach adds to the round when put in before the one at a place in it, or after the last.
		auto adds = [&](const std::vector<int>& Of, int Breach, size_t Before)
		{
			int before = Before ? Of[Before - 1] + 1 : 0;
			if (Before >= Of.size())
				return dist[before][Breach];
			return dist[before][Breach] + dist[Breach + 1][Of[Before]] - dist[before][Of[Before]];
		};
		// Made shorter while it can be: a stretch walked the other way round, a breach moved to another
		// place in the round, or one taken out and one left out put in where it fits best.
		float now = length(order);
		for (int round = 0; round < 200 && many > 1; round++)
		{
			bool better = false;
			auto take = [&](const std::vector<int>& Tried)
			{
				float tried = length(Tried);
				if (tried + 0.5f >= now)
					return false;
				order = Tried;
				now = tried;
				return better = true;
			};
			for (size_t i = 0; i < many && !better; i++)
				for (size_t j = i + 1; j < many && !better; j++)
				{
					std::vector<int> tried = order;
					std::reverse(tried.begin() + i, tried.begin() + j + 1);
					take(tried);
				}
			for (size_t i = 0; i < many && !better; i++)
				for (size_t j = 0; j < many && !better; j++)
				{
					if (i == j)
						continue;
					std::vector<int> tried = order;
					int moved = tried[i];
					tried.erase(tried.begin() + i);
					tried.insert(tried.begin() + j, moved);
					take(tried);
				}
			for (size_t i = 0; i < many && !better; i++)
				for (size_t j = 0; j < rest.size() && !better; j++)
					for (size_t k = 0; k < many && !better; k++)
					{
						std::vector<int> tried = order;
						int out = tried[i];
						tried.erase(tried.begin() + i);
						tried.insert(tried.begin() + k, rest[j]);
						if (take(tried))
							rest[j] = out;
					}
			if (!better)
				break;
		}
		// A breach the round passes close by is done in passing, where it adds the least.
		int passing = 0;
		while (!rest.empty())
		{
			float least = NO_WAY;
			size_t which = 0, where = 0;
			for (size_t j = 0; j < rest.size(); j++)
				for (size_t k = 0; k < order.size(); k++)
				{
					float more = adds(order, rest[j], k);
					if (more < least) { least = more; which = j; where = k; }
				}
			if (least > ON_THE_WAY)
				break;
			order.insert(order.begin() + where, rest[which]);
			rest.erase(rest.begin() + which);
			passing++;
		}
		now = length(order);
		static std::string said;
		std::string route;
		char text[48];
		for (int index : order)
		{
			snprintf(text, sizeof(text), " %.0f,%.0f", Among[index]->at.x, Among[index]->at.y);
			route += text;
		}
		if (route != said)
			Log("bot: route %s%s, %.0f on foot, %d of them in passing, first is %.0f away", floor ? "" : "(no floor read) ", route.c_str(), now, passing, dist[0][order[0]]);
		said = route;
		Away = dist[0][order[0]];
		return Among[order[0]];
	}

	// Loot on the ground: walked to, then picked up with the game's own key, pressed on the hero.
	std::map<double, ULONGLONG> g_NoLoot;
	double g_Loot = 0;
	Place g_LootAt;
	ULONGLONG g_LootSince = 0, g_LootNearAt = 0, g_LootPlanned = 0, g_PickUntil = 0;
	constexpr double PICK = 44;

	// True while loot is being seen to; the breaches wait.
	bool Loot(const Place& Me, ULONGLONG Now)
	{
		if (!g_Farm.loot)
			return false;
		RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_loot") });
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { object })));
		if (count < 1)
		{
			g_Loot = 0;
			return false;
		}
		// The one being seen to, while it lies there; a new one is looked for now and then.
		bool there = false;
		std::vector<std::pair<double, Place>> lying;
		for (int i = 0; i < count; i++)
		{
			RValue instance = g_Yytk->CallBuiltin("instance_find", { object, RValue(static_cast<double>(i)) });
			double id = g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("id") }).ToDouble();
			Place at = { ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("x") })), ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("y") })) };
			if (id == g_Loot)
			{
				there = true;
				g_LootAt = at;
			}
			if (g_NoLoot.find(id) == g_NoLoot.end())
				lying.push_back({ id, at });
		}
		if (g_Loot && !there)
		{
			Log("bot: loot %.0f picked up after %.1f s", g_Loot, (Now - g_LootSince) / 1000.0);
			g_Loot = 0;
		}
		if (!g_Loot)
		{
			if (lying.empty() || Now - g_LootPlanned < 400)
				return false;
			g_LootPlanned = Now;
			bool floor = g_Floor.width > 0 && Cell(Me) >= 0;
			std::vector<float> walked = floor ? Walked(Me) : std::vector<float>();
			float best = static_cast<float>(g_Farm.loot_range);
			for (const auto& [id, at] : lying)
			{
				int cell = floor ? Cell(at) : -1;
				float away = !floor ? static_cast<float>(Far(Me, at)) : cell < 0 ? NO_WAY : walked[cell];
				if (away <= best)
				{
					best = away;
					g_Loot = id;
					g_LootAt = at;
				}
			}
			if (!g_Loot)
				return false;
			g_LootSince = Now;
			g_LootNearAt = 0;
			Log("bot: loot %.0f at %.0f %.0f, %.0f away on foot, %d lying", g_Loot, g_LootAt.x, g_LootAt.y, best, static_cast<int>(lying.size()));
		}
		Say("picking up loot");
		bool beside = Far(Me, g_LootAt) <= PICK;
		if (beside && !g_LootNearAt)
			g_LootNearAt = Now;
		// Not picked up from beside it, or not got to: left where it lies for this floor.
		if ((g_LootNearAt && Now - g_LootNearAt > 3000) || Now - g_LootSince > 12000)
		{
			Log("bot: loot %.0f at %.0f %.0f left, %s", g_Loot, g_LootAt.x, g_LootAt.y, g_LootNearAt ? "it would not be picked up" : "it could not be got to");
			g_NoLoot[g_Loot] = Now;
			g_Loot = 0;
			if (Busy()) WalkStop();
			return false;
		}
		if (beside)
			g_PickUntil = Now + 150;
		else
		{
			// A walk to somewhere else is given up first. A walk ends above where it was clicked.
			if (Busy() && Far({ WalkStatus().x, WalkStatus().y }, { g_LootAt.x, g_LootAt.y + 14 }) > 4)
				WalkStop();
			Go({ g_LootAt.x, g_LootAt.y + 14 }, Now, 20);
		}
		return true;
	}

	void Rest()
	{
		g_Has = false;
		g_Reached = false;
		g_Walking = false;
		g_Attack = false;
		g_Fails = 0;
	}
}

static ULONGLONG g_UnlockedAt = 0;
static int g_Clicks = 0;

// The reaper just picked up, -1 for none, and when.
static double g_Reaper = -1;
static ULONGLONG g_ReaperAt = 0;

// What is picked up comes as a list whose first entry says what it is: a reaper is { rarity, which, "", "weapon", ... }.
static void Picked(const RValue& Item)
{
	if (!Item.IsArray())
		return;
	std::vector<RValue> parts = Item.ToVector();
	if (parts.empty() || !parts[0].IsArray())
		return;
	std::vector<RValue> head = parts[0].ToVector();
	if (head.size() < 4 || head[3].m_Kind != VALUE_STRING)
		return;
	// The log: gear by its rarity and slot, the rest by what it is.
	std::string what = ToText(head[3]);
	LootLine line;
	char text[96];
	if (what == "weapon")
	{
		// Named after the hero's weapon.
		int which = static_cast<int>(ToNumber(head[1]));
		CInstance* hero = FindInstance("obj_parent_hero");
		std::string kind = hero ? ToText(g_Yytk->CallBuiltin("object_get_name", { Local(hero, "object_index") })) : "";
		const char* weapon = kind == "obj_hero_huntress" ? "Bow" : kind == "obj_hero_mage" ? "Staff" : "Sword";
		std::string name = which >= 0 && which < static_cast<int>(std::size(REAPER_NAMES)) ? REAPER_NAMES[which] : "";
		size_t at = name.find('$');
		if (at != std::string::npos)
			name.replace(at, 1, weapon);
		snprintf(text, sizeof(text), "Reaper #%d", which);
		line = { name.empty() ? std::string(text) : name, LOOT_REAPER };
		Log("bot: reaper %d is %s (hero %s)", which, line.text.c_str(), kind.c_str());
	}
	else if (what == "equipment")
		line = { ToText(head[0]) + " " + ToText(head[1]), LOOT_GEAR };
	else
		line = { what, LOOT_STUFF };
	if (!line.text.empty())
		line.text[0] = static_cast<char>(toupper(static_cast<unsigned char>(line.text[0])));
	static const char* const SHOWN[] = { "weapon", "equipment", "fragment", "slormite", "slormeline", "slormandrite" };
	int shown = 6;
	for (int i = 0; i < 6; i++)
		if (what == SHOWN[i]) shown = i;
	if (g_Mod && g_Mod->enabled && g_Farm.loot_show[shown])
	{
		std::lock_guard guard(g_Lock);
		std::deque<LootLine>& log = g_Farm.loot_log;
		g_Farm.loot_count++;
		if (!log.empty() && log.back().text == line.text && line.kind != LOOT_REAPER)
			log.back().count++;
		else
			log.push_back(line);
		if (log.size() > 60)
			log.pop_front();
	}
	if (what != "weapon" || !g_Unlock || !g_Unlock->enabled)
		return;
	g_Reaper = ToNumber(head[1]);
	g_ReaperAt = GetTickCount64();
	Log("bot: reaper %.0f picked up", g_Reaper);
}

// Runs one of an instance's user events, as the game does when the hero uses it.
static void Use(CInstance* Instance, int Number)
{
	RValue result;
	g_Yytk->CallBuiltinEx(result, "event_user", Instance, Instance, { RValue(static_cast<double>(Number)) });
}

static void Call(CInstance* Self, const char* Name, std::vector<RValue> Arguments = {})
{
	PFUNC_YYGMLScript script = FindScript(Name);
	if (script)
		CallScript(script, Self, Arguments);
	Log("bot: %s%s", Name, script ? " called" : " is not in the game");
}

// What the War Chest held comes out over a moment and is picked up before anything else is done.
// True while there is some, or may yet be.
static ULONGLONG g_ChestEmptied = 0;
static bool ChestLoot(const Place& Me, ULONGLONG Now)
{
	static ULONGLONG seen = 0;
	if (!g_ChestEmptied || Now - g_ChestEmptied > 30000)
		return false;
	Place loot;
	if (Nearest("obj_loot", Me, loot))
	{
		seen = Now;
		if (Far(loot, Me) > 30)
			Go(loot, Now, 20);
		if (Far(loot, Me) < 70)
			g_PickUntil = Now + 150;
		Say("picking up what the chest held");
		return true;
	}
	if (Now - (std::max)(seen, g_ChestEmptied) < 3000)
	{
		Say("waiting for what the chest held");
		return true;
	}
	g_ChestEmptied = 0;
	return false;
}

// The War Chest: walked up to, opened as the hero would, and emptied with its menu's button.
// True while there is one being seen to.
static bool WarChest(CInstance* Self, const Place& Me, ULONGLONG Now)
{
	static ULONGLONG acted = 0;
	static int tries = 0;
	std::string menu = ToText(Local(Self, "menu_show"));
	if (menu == "chest")
	{
		tries = 0;
		if (ToText(Local(Self, "menu_position")) == "open" && Now > acted + 500)
		{
			acted = g_ChestEmptied = Now;
			Call(Self, "scr_ui_menu_war_chest_loot");
		}
		Say("emptying the War Chest");
		return true;
	}
	// The chest stands in town empty too; what it holds is a list while it holds anything.
	CInstance* chest = Global("forge_war_chest").IsArray() ? FindInstance("obj_chest_forge") : nullptr;
	if (!chest || menu != "none")
	{
		tries = 0;
		return false;
	}
	// One that stays shut is left alone.
	if (tries >= 8)
		return false;
	Place at = { ToNumber(Local(chest, "x")), ToNumber(Local(chest, "y")) };
	Say("opening the War Chest");
	if (Far(Me, at) > 70)
	{
		Go({ at.x, at.y + 36 }, Now, 30);
		return true;
	}
	if (!Busy() && Now > acted + 1500)
	{
		acted = Now;
		tries++;
		Log("bot: using the War Chest at %.0f %.0f, try %d", at.x, at.y, tries);
		Use(chest, 15);
	}
	return true;
}

// The Great Forge: waves are started by the game itself once told to, and fought by walking at what
// comes. From the last wave wanted on, no more are started; with the room quiet the chest is opened,
// which brings the door home.
static bool g_InForge = false;
// The temple breach before the one being walked to.
static Place g_Before;
static bool g_HasBefore = false;
// A click told to the game for a button is not also one for the hero to walk or strike on, until then.
static ULONGLONG g_ButtonUntil = 0;
static void GreatForge(CInstance* Self, CInstance* Hero, ULONGLONG Now)
{
	// The forge's start button, as shares of the screen.
	static constexpr float START[] = { 0.8895f, 0.2847f };
	static Place home;
	static ULONGLONG quiet_at = 0, acted = 0, release_at = 0, between_at = 0;
	static int said_wave = -1;
	static std::string said_left;
	// Where the click being made went down, and since when the hero has been dead.
	static float clicked[2] = { START[0], START[1] };
	static ULONGLONG dead_at = 0;
	// Held at the middle, the hero goes out for a while to enemies that have stopped falling.
	static ULONGLONG fell_at = 0, hunt_until = 0;
	static int standing = 0;
	Place me = { ToNumber(Local(Hero, "x")), ToNumber(Local(Hero, "y")) };
	if (!g_InForge)
	{
		g_InForge = true;
		hunt_until = 0;
		standing = 0;
		home = me;
		// The hero comes in north of the middle. The forge's eight stands ring the room: its middle is theirs.
		RValue stand = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_empty_item") });
		int stands = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { stand })));
		double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
		for (int i = 0; i < stands; i++)
		{
			RValue it = g_Yytk->CallBuiltin("instance_find", { stand, RValue(static_cast<double>(i)) });
			double x = ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { it, RValue("x") })), y = ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { it, RValue("y") }));
			left = (std::min)(left, x), right = (std::max)(right, x), top = (std::min)(top, y), bottom = (std::max)(bottom, y);
		}
		if (stands >= 4)
			home = { (left + right) / 2, (top + bottom) / 2 };
		Log("bot: the forge's middle is %.0f %.0f, from %d stands", home.x, home.y, stands);
		quiet_at = Now;
		said_wave = -1;
		dead_at = 0;
		Log("bot: in the Great Forge at %.0f %.0f, to wave %d", me.x, me.y, g_Farm.forge_waves);
	}
	if (release_at && Now >= release_at)
	{
		release_at = 0;
		OverlayClick(clicked[0], clicked[1], false);
		OverlayReturnCursor();
	}
	char text[96];
	// The scene of a fallen hero is clicked on until the game takes him home, chest and all.
	if (ToText(Local(Hero, "actual_state")) == "dead")
	{
		static constexpr float SCENE[] = { 0.373f, 0.266f };
		if (!dead_at)
			dead_at = Now;
		if (Now - dead_at > 2500 && Now > acted + 1500 && !release_at)
		{
			acted = Now;
			release_at = Now + 90;
			clicked[0] = SCENE[0], clicked[1] = SCENE[1];
			OverlayClick(SCENE[0], SCENE[1], true);
			Log("bot: the hero is down, clicking the scene on, %.1f s after the death", (Now - dead_at) / 1000.0);
		}
		return Say("the hero died, heading home");
	}
	dead_at = 0;
	// The door comes once the chest is emptied; the hero's way home is its first use.
	if (CInstance* door = FindInstance("obj_breach_door"))
	{
		if (ChestLoot(me, Now))
			return;
		Place at = { ToNumber(Local(door, "x")), ToNumber(Local(door, "y")) };
		Say("heading home");
		if (Far(me, at) > 80)
			Go({ at.x, at.y + 30 }, Now, 30);
		else if (!Busy() && Now > acted + 1500)
		{
			acted = Now;
			Log("bot: taking the door home at %.0f %.0f", at.x, at.y);
			Use(door, 14);
		}
		return;
	}
	// Waves begun so far: each has its count of enemies.
	int waves = 0;
	CInstance* room = FindInstance("obj_procedural");
	RValue fought = room ? Local(room, "enemies_this_wave") : RValue();
	if (fought.IsArray())
		for (const RValue& enemies : fought.ToVector())
			if (ToNumber(enemies) > 0) waves++;
	bool last = waves >= g_Farm.forge_waves;
	if (waves != said_wave)
		Log("bot: forge wave %d of %d", waves, g_Farm.forge_waves);
	said_wave = waves;
	// The game starts each next wave by itself while this is set.
	bool autos = ToNumber(Global("forge_auto_wave")) > 0;
	if (autos == last)
	{
		g_Yytk->CallBuiltin("variable_global_set", { RValue("forge_auto_wave"), RValue(last ? 0.0 : 1.0) });
		Log("bot: forge waves %s start by themselves", last ? "no longer" : "now");
	}
	std::string menu = ToText(Local(Self, "menu_show"));
	bool between = room && ToNumber(Local(room, "forge_interwave")) > 0;
	if (!between)
		between_at = 0;
	else if (!between_at)
		between_at = Now;
	std::vector<Place> enemies = Enemies(Hero, me);
	// With the last wave over for a while, what still stands is not held to be an enemy.
	if (last && between && Now - between_at > 20000 && !enemies.empty())
	{
		if (g_Nearest != said_left)
			Log("bot: the last wave is long over and %d still stand, the nearest a %s; going for the chest", static_cast<int>(enemies.size()), g_Nearest.c_str());
		said_left = g_Nearest;
		enemies.clear();
		quiet_at = between_at;
	}
	if (!enemies.empty())
	{
		quiet_at = Now;
		int count = static_cast<int>(enemies.size());
		if (standing == 0 || count < standing)
		{
			fell_at = Now;
			// A few left are seen off before going back.
			if (hunt_until)
				hunt_until = count <= 6 ? Now + 12000 : Now;
		}
		standing = count;
		if (hunt_until && Now >= hunt_until)
		{
			hunt_until = 0;
			fell_at = Now;
			Log("bot: back to the forge's middle, %d standing", count);
		}
		if (g_Farm.forge_center && !hunt_until && Now - fell_at > 6000)
		{
			hunt_until = Now + 12000;
			Log("bot: none of %d fell for a while, going out to the nearest, a %s %.0f away", count, g_Nearest.c_str(), Far(enemies[0], me));
		}
		// They come to the hero; ones still far off, where they are let in, are waited for at the middle.
		if (hunt_until)
		{
			if (Far(enemies[0], me) > CLOSE)
				Go(enemies[0], Now);
		}
		else if (g_Farm.forge_center || Far(enemies[0], me) > 280)
		{
			if (Far(me, home) > (g_Farm.forge_center ? 45 : 90))
				Go(home, Now, 24);
		}
		else if (Far(enemies[0], me) > CLOSE)
			Go(enemies[0], Now);
		snprintf(text, sizeof(text), hunt_until ? "going out to the enemies left, wave %d of %d" : "fighting wave %d of %d", waves, g_Farm.forge_waves);
		return Say(text);
	}
	standing = 0;
	hunt_until = 0;
	if (last)
	{
		// Quiet for a moment with the wave over: the chest can be opened.
		if (menu == "chest" || (between && Now - quiet_at > 3000))
			if (WarChest(Self, me, Now))
				return;
		return Say("waiting for the last wave to end");
	}
	// A wave the game has not started by itself is started with its button.
	if (between && menu == "none" && Now - quiet_at > 1500 && Now - between_at > 1500 && Now > acted + 4000 && !release_at)
	{
		acted = Now;
		release_at = Now + 90;
		clicked[0] = START[0], clicked[1] = START[1];
		OverlayClick(START[0], START[1], true), g_ButtonUntil = Now + 300;
		Log("bot: clicking the forge's start button");
	}
	if (Far(me, home) > (g_Farm.forge_center ? 45 : 90))
		Go(home, Now, 24);
	snprintf(text, sizeof(text), "waiting for wave %d", waves + 1);
	Say(text);
}

// The bag is the hero's inventory after the eleven things worn; a slot holding something is an array.
static void ReadBag()
{
	static ULONGLONG read = 0;
	if (GetTickCount64() - read < 1000)
		return;
	read = GetTickCount64();
	g_Farm.bag_used = -1;
	RValue heroes = Global("inventory");
	int who = static_cast<int>(ToNumber(Global("current_hero"))), slots = static_cast<int>(ToNumber(Global("inventory_slots")));
	if (!heroes.IsArray() || who < 0 || slots < 1 || slots > 400 || ToNumber(g_Yytk->CallBuiltin("array_length", { heroes })) <= who)
		return;
	RValue bag = g_Yytk->CallBuiltin("array_get", { heroes, RValue(static_cast<double>(who)) });
	if (!bag.IsArray() || ToNumber(g_Yytk->CallBuiltin("array_length", { bag })) < 11 + slots)
		return;
	int used = 0;
	for (int slot = 0; slot < slots; slot++)
		used += g_Yytk->CallBuiltin("array_get", { bag, RValue(11.0 + slot) }).IsArray() ? 1 : 0;
	static int logged = -1;
	if (used != logged)
		Log("bot: bag %d of %d", used, slots);
	logged = used;
	g_Farm.bag_used = used;
	g_Farm.bag_slots = slots;
}

static bool BagFull()
{
	return g_Farm.stop_full && g_Farm.bag_used >= 0 && g_Farm.bag_used >= g_Farm.bag_slots;
}

// Town: the result of the last expedition is closed, the hero walks to Almerolis, and the battlefield
// is started through his menu with the calls the game makes on the clicks.
static void Hub(CInstance* Self, CInstance* Hero, ULONGLONG Now)
{
	// Where Almerolis offers expeditions.
	static constexpr Place ALMEROLIS = { 2320, 505 };
	static ULONGLONG acted = 0;
	static int step = 0, asked = 0;
	static std::string seen;
	std::string menu = ToText(Local(Self, "menu_show"));
	bool open = ToText(Local(Self, "menu_position")) == "open";
#ifdef SLORM_DEV
	static bool dumped = false;
	if (!dumped)
	{
		dumped = true;
		g_Dev.dump_note = "town";
		g_Dev.want_dump = true;
	}
#endif
	if (menu != seen)
		Log("bot: town menu %s -> %s, citizen %s", seen.c_str(), menu.c_str(), ToText(Local(Self, "menu_citizen")).c_str());
	seen = menu;
	if (!g_Farm.restart)
		return Say("in town");
	if (Now < acted + 700)
		return;
	auto call = [&](const char* Name, std::vector<RValue> Arguments)
	{
		PFUNC_YYGMLScript script = FindScript(Name);
		if (script)
			CallScript(script, Self, Arguments);
		Log("bot: %s%s", Name, script ? " called" : " is not in the game");
		acted = Now;
	};
	if (menu == "result")
	{
		if (open && ToNumber(Local(Self, "result_menu_delay_time")) >= ToNumber(Local(Self, "result_menu_delay_timer")))
			call("scr_ui_menu_close_button", {});
		return Say("closing the result of the last expedition");
	}
	if (menu == "launcher")
	{
		asked = 0;
		if (!open)
			return;
		if (step == 0)
		{
			step = 1;
			// Almerolis lists the battlefield first, the Slorm Temple second and the Great Forge third.
			call("scr_ui_launcher_change_quest", { RValue(g_Farm.mode == 1 ? 2.0 : g_Farm.mode == 2 ? 1.0 : 0.0) });
			return Say(g_Farm.mode == 1 ? "choosing the Great Forge" : g_Farm.mode == 2 ? "choosing the Slorm Temple" : "choosing the battlefield");
		}
		if (step == 1 && g_Farm.mode == 2 && g_Farm.temple_floor > 0)
		{
			step = 2;
			// No floor past the one after the highest cleared.
			int floor = g_Farm.temple_cleared >= 0 ? (std::min)(g_Farm.temple_floor, g_Farm.temple_cleared + 1) : g_Farm.temple_floor;
			if (static_cast<int>(ToNumber(Local(Self, "temple_floor"))) != floor)
			{
				call("scr_ui_launcher_change_temple_floor", { RValue(static_cast<double>(floor)) });
				Log("bot: temple floor %d asked for, the launcher now has %.0f", floor, ToNumber(Local(Self, "temple_floor")));
				return Say("choosing the temple floor");
			}
		}
		step = 0;
		call("scr_run_start", {});
		g_Farm.runs++;
		Log("bot: expedition %d started", g_Farm.runs);
		acted = Now + 3000;
		return Say("starting the expedition");
	}
	step = 0;
	if (menu == "chest")
	{
		WarChest(Self, { 0, 0 }, Now);
		return;
	}
	// A town entrance on the way can put up the story missions; they are closed and the walk goes on.
	if (menu == "story")
	{
		if (open)
			call("scr_ui_menu_close_button", {});
		return Say("closing the story missions");
	}
	if (menu != "none")
		return Say("a menu is open in town");
	Place me = { ToNumber(Local(Hero, "x")), ToNumber(Local(Hero, "y")) }, npc;
	// A War Chest brought home full is emptied first, and what it held picked up.
	if (WarChest(Self, me, Now) || ChestLoot(me, Now))
		return;
	char why[96] = "";
	if (g_Farm.max_runs > 0 && g_Farm.runs >= g_Farm.max_runs)
		snprintf(why, sizeof(why), "stopped: %d expedition%s done", g_Farm.runs, g_Farm.runs == 1 ? "" : "s");
	else if (BagFull())
		snprintf(why, sizeof(why), "stopped: the bag is full, %d of %d", g_Farm.bag_used, g_Farm.bag_slots);
	if (why[0])
	{
		g_Farm.stopped = why;
		g_Farm.on = false;
		return;
	}
	if (!Nearest("obj_npc", ALMEROLIS, npc) || Far(npc, ALMEROLIS) > 90)
		return Say("cannot find Almerolis");
	// Stood below him, clear of a click on him.
	Place stand = { npc.x, npc.y + 34 };
	if (Far(me, stand) > 40)
	{
		asked = 0;
		Go(stand, Now, 30);
		return Say("walking to Almerolis");
	}
	if (Busy())
		return;
	if (asked >= 3)
		return Say("Almerolis did not open the expedition menu");
	if (asked++ == 0)
		Log("bot: at Almerolis, he is at %.0f %.0f, the hero at %.0f %.0f", npc.x, npc.y, me.x, me.y);
	call("scr_citizen_action", { RValue("Run Start") });
	acted = Now + 800;
	Say("asking Almerolis for an expedition");
}

// The hero is walked by the game's click to move, an option of the game that can be off.
static bool NoClickToMove()
{
	RValue on = Global("ctm");
	return on.m_Kind != VALUE_UNDEFINED && on.m_Kind != VALUE_UNSET && ToNumber(on) < 1;
}

static PFUNC_YYGMLScript g_PickUp = nullptr;
static RValue& PickUpHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	RValue& result = g_PickUp(Self, Other, Result, ArgumentCount, Arguments);
	if (((g_Mod && g_Mod->enabled) || (g_Unlock && g_Unlock->enabled)) && ArgumentCount > 0 && Arguments[0])
		Picked(*Arguments[0]);
	return result;
}

static void ReaperTick(CInstance* Self)
{
	// The game unlocks a new reaper on a click, with this call; it is made a moment after the pick up.
	if (g_Reaper >= 0 && GetTickCount64() - g_ReaperAt > 600)
	{
		double reaper = g_Reaper;
		g_Reaper = -1;
		PFUNC_YYGMLScript unlock = FindScript("scr_weapon_pre_evolve");
		if (unlock)
		{
			CallScript(unlock, Self, { RValue(reaper), RValue(1.0) });
			Log("bot: reaper %.0f unlocked with scr_weapon_pre_evolve(%.0f, 1)", reaper, reaper);
			g_UnlockedAt = GetTickCount64();
			g_Clicks = 0;
		}
	}
	// What the unlock shows is sent away with a click on the middle of the screen, where the hero stands.
	// It takes one within a few seconds, so it is clicked twice.
	if (g_UnlockedAt)
	{
		ULONGLONG since = GetTickCount64() - g_UnlockedAt;
		static const ULONGLONG AT[] = { 1500, 3500 };
		if (g_Clicks < 4 && since >= AT[g_Clicks / 2] + (g_Clicks % 2 ? 80 : 0))
		{
			OverlayClick(0.5f, 0.45f, g_Clicks % 2 == 0);
			if (g_Clicks % 2)
				OverlayReturnCursor();
			Log("bot: click %s to send the reaper screen away, %.1f s after the unlock", g_Clicks % 2 ? "released" : "pressed", since / 1000.0);
			g_Clicks++;
		}
		if (g_Clicks >= 4)
			g_UnlockedAt = 0;
	}
}

static void BotTick(CInstance* Self)
{
	static bool was_on = false, key_was_down = false;
	bool key_down = !g_SuppressToggle && (GetAsyncKeyState(g_Farm.key) & 0x8000) != 0;
	if (key_down && !key_was_down)
		g_Farm.on = !g_Farm.on;
	key_was_down = key_down;
	if (!g_Farm.on)
	{
		if (was_on)
		{
			if (Busy()) WalkStop();
			Rest();
			g_Closed = -1;
			g_Floor = Floor(), g_From.clear();
			g_Skip.clear();
			g_NoLoot.clear();
			g_Loot = 0;
			Say(g_Farm.stopped.empty() ? "off" : g_Farm.stopped);
		}
		was_on = false;
		return;
	}
	if (!was_on)
	{
		g_Farm.runs = g_Farm.floors = 0;
		g_Farm.stopped.clear();
	}
	was_on = true;
	CInstance* hero = FindInstance("obj_parent_hero");
	if (hero && g_Farm.no_ctm)
	{
		if (Busy()) WalkStop();
		Rest();
		return Say("needs Click to Move turned on in the game's options");
	}
	if (!hero || RoomName() != "rm_procedural")
	{
		Rest();
		g_Closed = -1;
		g_Floor = Floor(), g_From.clear();
		g_InForge = false;
		g_Farm.floors = 0;
		if (hero && RoomName() == "rm_the_hub")
			return Hub(Self, hero, GetTickCount64());
		return Say("waiting for an expedition");
	}
	// The Great Forge shows its button; nowhere else does.
	if (ToNumber(Local(Self, "forge_show_choice_button")) > 0)
	{
		Rest();
		g_Closed = -1;
		return GreatForge(Self, hero, GetTickCount64());
	}
	g_InForge = false;
	std::string state = ToText(Local(hero, "actual_state"));
	if (state == "dead")
	{
		// Left on to go again from town.
		g_Farm.on = g_Farm.restart;
		return Say("the hero died");
	}
	// A floor being made or left.
	if (ToNumber(Local(hero, "enable_input")) < 1)
	{
		Rest();
		g_Closed = -1;
		g_Floor = Floor(), g_From.clear();
		g_Skip.clear();
		g_NoLoot.clear();
		g_Loot = 0;
		return;
	}
	ULONGLONG now = GetTickCount64();
	Place me = { ToNumber(Local(hero, "x")), ToNumber(Local(hero, "y")) };
	std::vector<Breach> breaches = Breaches();
	int total = static_cast<int>(breaches.size()), closed = 0;
	for (const Breach& breach : breaches) closed += breach.open ? 0 : 1;
	// A floor of the Slorm Temple: every breach, one room after the other, then a shrine and the door.
	CInstance* level = FindInstance("obj_procedural");
	RValue rooms = level ? Local(level, "temple_room_array") : RValue();
	bool temple = rooms.IsArray() && !rooms.ToVector().empty();
	static ULONGLONG shrine_at = 0;
	static int shrine_tries = 0;
	static double pure_had = -1;
	double pure_has = ToNumber(Global("pure_slorm"));
	if (temple && pure_had >= 0 && pure_has != pure_had)
		Log("bot: pure slorm %.0f -> %.0f (%+.0f)", pure_had, pure_has, pure_has - pure_had);
	pure_had = pure_has;
	if (g_Closed < 0)
		shrine_at = 0, shrine_tries = 0;
	int needed = temple ? (std::max)(total, 1) : (std::max)(total - SPARE, 1);
	// When the last temple breach was closed; the bridge on comes a moment after.
	static ULONGLONG closed_at = 0;
	if (closed != g_Closed)
	{
		closed_at = g_Closed < 0 ? 0 : now;
		Log("bot: %d of %d breaches closed, the door needs %d%s", closed, total, needed, temple ? ", in the Slorm Temple" : "");
		if (g_Closed < 0)
		{
			std::string all;
			char place[48];
			for (const Breach& breach : breaches)
			{
				snprintf(place, sizeof(place), " %.0f,%.0f%s", breach.at.x, breach.at.y, !breach.open ? "(closed)" : breach.chaos ? "(V)" : "");
				all += place;
			}
			Log("bot: breaches %s", all.c_str());
			// A floor not seen before is one more of this expedition.
			static long long last = 0;
			long long floor = total;
			for (const Breach& breach : breaches) floor += Key(breach.at);
			if (total > 0 && (floor != last || g_Farm.floors < 1))
				Log("bot: floor %d of this expedition", ++g_Farm.floors);
			last = floor;
		}
		// What is there to open the door with, the first time it can be opened.
		if (g_Closed >= 0 && g_Closed < needed && closed >= needed)
		{
#ifdef SLORM_DEV
			g_Dev.dump_note = "door can be had";
			g_Dev.want_dump = true;
#endif
		}
		g_Closed = closed;
	}

	// Loot comes before the next breach and the door, but not while a breach is open on the hero.
	bool shut_in = false;
	for (const Breach& breach : breaches) shut_in = shut_in || (breach.open && breach.active);
	// A temple breach just stepped on holds the hero in place for a moment; walks sent then only fail.
	static long long stepped_on = 0;
	static ULONGLONG stepped_at = 0;
	for (const Breach& breach : breaches)
		if (temple && breach.open && breach.active && Key(breach.at) != stepped_on)
		{
			stepped_on = Key(breach.at);
			stepped_at = now;
			WalkStop();
		}
	if (temple && shut_in && now - stepped_at < 1600)
		return Say("the breach is opening");
	if (!shut_in && !(g_Has && g_Reached) && Loot(me, now))
	{
		g_Walking = false;
		g_Attack = false;
		return;
	}

	// Pure slorm a temple breach leaves on the ground is taken by walking onto it.
	static std::map<long long, ULONGLONG> pure_since;
	static int pure_step = 0;
	if (g_Closed < 0)
		pure_since.clear();
	if (temple && g_Farm.temple_pure && !shut_in)
	{
		RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue("obj_slorm_anomaly") });
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { object })));
		Place pure;
		long long pure_id = 0;
		bool found = false;
		for (int i = 0; i < count; i++)
		{
			RValue instance = g_Yytk->CallBuiltin("instance_find", { object, RValue(static_cast<double>(i)) });
			Place at = { ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("x") })), ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("y") })) };
			// It drifts, so it is known by its id. One not reached in a while is left.
			long long id = static_cast<long long>(g_Yytk->CallBuiltin("variable_instance_get", { instance, RValue("id") }).ToDouble());
			auto since = pure_since.find(id);
			if (since != pure_since.end() && now - since->second > 12000)
				continue;
			if (!found || Far(at, me) < Far(pure, me))
				pure = at, pure_id = id, found = true;
		}
		if (found)
		{
			if (pure_since.emplace(pure_id, now).second)
				Log("bot: pure slorm on the ground at %.0f %.0f, %.0f away", pure.x, pure.y, Far(pure, me));
			g_Walking = false;
			g_Attack = false;
			// A hero stops 18 above where he is sent: sent below it to stand on it, then across it.
			static constexpr double ACROSS[][2] = { { 0, 18 }, { 0, 44 }, { 40, 18 }, { -40, 18 }, { 0, -8 } };
			const double* aim = ACROSS[pure_step % 5];
			if (Go({ pure.x + aim[0], pure.y + aim[1] }, now))
				pure_step++;
			return Say("picking up pure slorm");
		}
	}

	char text[128];
	Place exit;
	// The temple's shrine comes with the last breach closed. Using it gives its pure slorm and makes
	// the door; it does not take a use at once, so it is used until the door is there.
	if (temple && g_Farm.temple_shrine && closed >= total && shrine_tries < 20 && !FindInstance("obj_breach_door"))
		if (CInstance* shrine = FindInstance("obj_temple_shrine"))
		{
			Rest();
			Place at = { ToNumber(Local(shrine, "x")), ToNumber(Local(shrine, "y")) };
			Say("using the shrine");
			// It takes a use from across its room, and the ground by it cannot always be walked on: it
			// is used from where the hero is, and come up to, from his side, only if that does nothing.
			double away = Far(me, at);
			bool nearer = away > 220 || (away > 90 && shrine_tries >= 4 && shrine_tries % 4 == 0);
			if (nearer && Go({ at.x + (me.x - at.x) / away * 55, at.y + (me.y - at.y) / away * 55 + 18 }, now))
				shrine_tries += away > 220 ? 0 : 1;
			else if (away <= 220 && now - shrine_at > 1000)
			{
				shrine_at = now;
				shrine_tries++;
				Log("bot: using the shrine at %.0f %.0f from %.0f away, try %d; it has activable %s", at.x, at.y, away, shrine_tries, ToText(Local(shrine, "activable")).c_str());
				Use(shrine, 15);
			}
			return;
		}
	// The door is clicked as soon as there is one.
	if (Nearest("obj_breach_door", me, exit))
	{
		Rest();
		bool home = BagFull() || (g_Farm.max_floors > 0 && g_Farm.floors >= g_Farm.max_floors);
		snprintf(text, sizeof(text), "taking the door %s at %.0f %.0f", home ? (BagFull() ? "home with a full bag" : "home") : "to the next floor", exit.x, exit.y);
		Say(text);
		Exit(Self, exit, home ? 2 : 1, me, now);
		return;
	}

	// The breach being worked on, while it is open.
	bool opened = false;
	if (g_Has)
	{
		auto found = std::find_if(breaches.begin(), breaches.end(), [](const Breach& Of) { return Key(Of.at) == Key(g_Breach); });
		if (found == breaches.end() || !found->open)
			Rest();
		else
			opened = found->active;
	}
	if (g_Has && now - g_Since > GIVE_UP_MS)
	{
		Log("bot: the breach at %.0f %.0f took too long, left for later", g_Breach.x, g_Breach.y);
		if (!temple)
			g_Skip[Key(g_Breach)] = now + SKIP_MS;
		Rest();
	}
	if (!g_Has)
	{
		// Finding the way round takes a moment, so not every step.
		static ULONGLONG planned = 0;
		if (now - planned < 400)
			return;
		planned = now;
		if (g_Floor.width < 1 && !temple)
			ReadFloor(Self);
		// One already stepped on comes first: there is no leaving it.
		const Breach* best = nullptr;
		double away = 0;
		for (const Breach& breach : breaches)
			if (breach.open && breach.active)
				best = &breach;
		// The temple's rooms come one at a time, each with its place in the order: across, down, turn.
		// Breaches stand one to a room, so theirs are the same across and down, counted from the least.
		if (temple && !best)
		{
			std::vector<double> xs, ys;
			std::vector<int> across, down;
			auto add = [](auto& Into, auto Value) { if (std::find(Into.begin(), Into.end(), Value) == Into.end()) Into.push_back(Value); };
			std::vector<std::vector<RValue>> turns;
			for (const RValue& room : rooms.ToVector())
				if (room.IsArray() && room.ToVector().size() >= 3)
					turns.push_back(room.ToVector());
			for (const auto& turn : turns)
				add(across, static_cast<int>(ToNumber(turn[0]))), add(down, static_cast<int>(ToNumber(turn[1])));
			for (const Breach& breach : breaches)
				add(xs, breach.at.x), add(ys, breach.at.y);
			std::sort(xs.begin(), xs.end()), std::sort(ys.begin(), ys.end());
			std::sort(across.begin(), across.end()), std::sort(down.begin(), down.end());
			int least = 0;
			bool placed = xs.size() == across.size() && ys.size() == down.size();
			std::map<int, Place> by_turn;
			for (const Breach& breach : breaches)
			{
				int turn_of = 0;
				if (placed)
				{
					int column = across[std::find(xs.begin(), xs.end(), breach.at.x) - xs.begin()], row = down[std::find(ys.begin(), ys.end(), breach.at.y) - ys.begin()];
					for (const auto& turn : turns)
						if (static_cast<int>(ToNumber(turn[0])) == column && static_cast<int>(ToNumber(turn[1])) == row)
							turn_of = static_cast<int>(ToNumber(turn[2]));
				}
				if (turn_of)
					by_turn[turn_of] = breach.at;
				if (!breach.open)
					continue;
				// Without a place in the order, the nearest.
				bool sooner = !best || (turn_of && least ? turn_of < least : !least && (turn_of || Far(breach.at, me) < Far(best->at, me)));
				if (sooner)
					best = &breach, least = turn_of;
			}
			// The room before it is the one the hero leaves by a bridge.
			g_HasBefore = best && least > 1 && by_turn.count(least - 1);
			if (g_HasBefore)
				g_Before = by_turn[least - 1];
			if (best)
				Log("bot: the temple's next breach is at %.0f %.0f, turn %d of %d", best->at.x, best->at.y, least, static_cast<int>(turns.size()));
		}
		// Chaos breaches are passed over when asked, unless the door cannot be had without them.
		for (int pass = 0; pass < 2 && !best; pass++)
		{
			if (pass == 1 && (!g_Farm.skip_chaos || closed >= needed))
				break;
			std::vector<const Breach*> among;
			for (const Breach& breach : breaches)
				if (breach.open && (pass == 1 || !g_Farm.skip_chaos || !breach.chaos) && g_Skip[Key(breach.at)] < now)
					among.push_back(&breach);
			best = Next(among, me, needed - closed, away);
		}
		// With the door to be had, a breach is only worth it when it is near on foot; the door opens
		// by clicking what is left of a closed breach, the nearest of them.
		if (temple && closed >= needed)
			return Say("every breach is closed, waiting for the shrine and the door");
		if (closed >= needed && (!best || (!best->active && away > g_Farm.extra)) && Nearest("obj_breach_residue", me, exit))
		{
			g_Attack = false;
			snprintf(text, sizeof(text), "opening the door at %.0f %.0f", exit.x, exit.y);
			Say(text);
			Exit(Self, exit, 0, me, now);
			return;
		}
		if (!best)
			return Say(closed >= total ? "every breach is closed and there is no door" : "no breach can be reached for now");
		g_Has = true;
		g_Breach = best->at;
		g_Since = g_QuietAt = now;
	}

	if (Far(g_Breach, me) <= ON_BREACH)
		g_Reached = true;
	// The breach is opened first; what stands by it is fought once it is open.
	std::vector<Place> enemies = g_Reached || opened ? Enemies(hero, me) : std::vector<Place>();
	if (!enemies.empty() && (Far(enemies[0], me) <= NOTICE || Far(enemies[0], g_Breach) <= AROUND))
	{
		g_Walking = false;
		g_QuietAt = now;
		g_AttackAt = enemies[0];
		// A hero whose skills fire by themselves only has to be among the enemies.
		g_Attack = g_Farm.attack && Far(enemies[0], me) <= HIT;
		if (g_Attack && Busy())
			WalkStop();
		else if (!g_Attack && Far(enemies[0], me) > CLOSE)
			Go(enemies[0], now);
		snprintf(text, sizeof(text), "fighting by the breach at %.0f %.0f", g_Breach.x, g_Breach.y);
		return Say(text);
	}
	g_Attack = false;
	// A walk to the breach that failed: after two, the breach is left for later.
	if (g_Walking && !Busy())
	{
		g_Walking = false;
		if (WalkStatus().state == WALK_FAILED && ++g_Fails >= 2)
		{
			Log("bot: cannot walk to the breach at %.0f %.0f (%s), left for later", g_Breach.x, g_Breach.y, WalkStatus().why.c_str());
			if (!temple)
				g_Skip[Key(g_Breach)] = now + SKIP_MS;
			return Rest();
		}
	}
	if (Far(g_Breach, me) > ON_BREACH)
	{
		if (temple && g_HasBefore && now - closed_at < 1200 && !Busy())
			return Say("waiting for the bridge to the next room");
		// Temple rooms are joined by a narrow bridge from middle to middle, and a hero walks 18 above
		// where he is sent: still in the room before, he is first put on the line the bridge runs along.
		Place to = g_Breach;
		if (temple && g_HasBefore)
		{
			// A walk has to be some way to be one: along the line too, towards the room's middle
			// when far from it, and on towards the bridge otherwise.
			auto along = [](double Mine, double Middle, double Next) {
				double off = Mine - Middle;
				return Middle + (fabs(off) > 70 ? off - (off > 0 ? 60 : -60) : off + (Next > Middle ? 60 : -60));
			};
			bool level = fabs(g_Before.y - g_Breach.y) < 1;
			if (level && fabs(me.x - g_Before.x) < 250 && fabs(me.y + 18 - g_Breach.y) > 12)
				to = { along(me.x, g_Before.x, g_Breach.x), g_Breach.y };
			else if (!level && fabs(me.y + 18 - g_Before.y) < 160 && fabs(me.x - g_Breach.x) > 12)
				to = { g_Breach.x, along(me.y + 18, g_Before.y, g_Breach.y) };
		}
		// A walk at the breach that went wrong off that line is started over from the line.
		const Walk& walk = WalkStatus();
		if (Far(to, g_Breach) > 1 && Busy() && walk.tries > 1 && fabs(walk.x - g_Breach.x) < 1 && fabs(walk.y - g_Breach.y) < 1)
		{
			Log("bot: off the bridge's line at %.0f %.0f on the way to %.0f %.0f, lining up again", me.x, me.y, g_Breach.x, g_Breach.y);
			WalkStop();
		}
		if (Go(to, now))
			g_Walking = true;
		snprintf(text, sizeof(text), "walking to the breach at %.0f %.0f", g_Breach.x, g_Breach.y);
		return Say(text);
	}
	// On the breach with nothing to fight: after a while, a step across it and back.
	if (now - g_QuietAt > 6000)
	{
		g_QuietAt = now;
		Go({ g_Breach.x + (me.x <= g_Breach.x ? 40 : -40), g_Breach.y }, now);
	}
	snprintf(text, sizeof(text), "waiting on the breach at %.0f %.0f", g_Breach.x, g_Breach.y);
	Say(text);
}

// The attack: the game is told the mouse is on the enemy, and the primary skill is held.
void BotInput(CInstance* Hero)
{
	if (!g_Farm.on)
		return;
	if (GetTickCount64() < g_ButtonUntil)
		for (const char* key : { "ctm_key", "attack_1_key" })
			g_Yytk->CallBuiltin("variable_instance_set", { RValue(Hero), RValue(key), RValue(0.0) });
	if (GetTickCount64() < g_HoverUntil)
		AimAt(g_Hover.x, g_Hover.y, 100);
	// The pick up key, tapped: a press each fifth of a second.
	if (GetTickCount64() < g_PickUntil && GetTickCount64() % 200 < 100)
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Hero), RValue("loot_key"), RValue(1.0) });
	if (!g_Attack)
		return;
	if (AimStep(g_AttackAt.x, g_AttackAt.y))
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Hero), RValue("attack_1_key"), RValue(1.0) });
}

namespace
{
	struct Farmer : Feature
	{
		Farmer() : Feature("Farm", "farmer", "Plays expedition floors for the hero: closes breaches by the shortest way round, picks up what drops, then opens the portal and takes it to the next floor.")
		{
			tab = "Farm";
			g_Mod = this;
		}

		void Start() override
		{
			PFUNC_YYGMLScript pick_up = FindScript("scr_pick_up_item");
			if (!pick_up || !AurieSuccess(MmCreateHook(g_ArSelfModule, "SlormSuite pick up", pick_up, PickUpHook, reinterpret_cast<PVOID*>(&g_PickUp))))
				Log("farmer: no pick up script, reapers are not unlocked and nothing is listed");
		}

		void Tick(CInstance* Self) override
		{
			g_Farm.no_ctm = NoClickToMove();
			ReadBag();
			// The second number the game keeps for the temple is the highest floor cleared.
			RValue temple = Global("slorm_temple_data");
			std::vector<RValue> kept = temple.IsArray() ? temple.ToVector() : std::vector<RValue>();
			g_Farm.temple_cleared = kept.size() > 1 ? static_cast<int>(ToNumber(kept[1])) : -1;
			BotTick(Self);
		}

		void Off(CInstance* Self) override
		{
			g_Farm.on = false;
			BotTick(Self);
		}

		bool Draw() override
		{
			bool changed = false;
			ImGui::BeginDisabled(!enabled);
			ImGui::Checkbox("Run the expedition", &g_Farm.on);
			ImGui::EndDisabled();
			ImGui::SameLine();
			changed |= KeyButton("run", &g_Farm.key);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", g_Farm.status.c_str());
			if (enabled && g_Farm.no_ctm)
				ImGui::TextColored({ 1.0f, 0.45f, 0.35f, 1.0f }, "Click to Move is off in the game's options. The hero cannot be walked without it.");
			changed |= ImGui::Checkbox("In town, go to Almerolis and start", &g_Farm.restart);
			ImGui::SameLine();
			// Almerolis' four, in his order; the mode each stands for, -1 for one the Farmer cannot play yet.
			static const char* const EXPEDITIONS[] = { "Battlefield", "The Slorm Temple", "The Great Forge", "The Netherworld" };
			static constexpr int MODES[] = { 0, 2, 1, -1 };
			ImGui::SetNextItemWidth(170 * g_Appearance.scale);
			if (ImGui::BeginCombo("##expedition", EXPEDITIONS[g_Farm.mode == 1 ? 2 : g_Farm.mode == 2 ? 1 : 0]))
			{
				for (int i = 0; i < 4; i++)
				{
					std::string label = MODES[i] < 0 ? std::string(EXPEDITIONS[i]) + " (not yet)" : EXPEDITIONS[i];
					if (ImGui::Selectable(label.c_str(), MODES[i] == g_Farm.mode, MODES[i] < 0 ? ImGuiSelectableFlags_Disabled : 0))
					{
						g_Farm.mode = MODES[i];
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			// What the chosen expedition has to set.
			if (g_Farm.mode == 1)
			{
				ImGui::Indent();
				ImGui::SetNextItemWidth(160 * g_Appearance.scale);
				changed |= ImGui::SliderInt("Fight to this wave, open War Chest and head home", &g_Farm.forge_waves, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
				changed |= ImGui::Checkbox("Keep hero at center", &g_Farm.forge_center);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("The hero holds the center of the forge and lets the enemies come,\ngoing out to them for a while when none has fallen for 6 seconds.\nOff, it walks at the nearest ones.");
				ImGui::Unindent();
			}
			else
			{
				ImGui::Indent();
				ImGui::SetNextItemWidth(160 * g_Appearance.scale);
				changed |= ImGui::SliderInt("Head home after this many floors (0 = keep going)", &g_Farm.max_floors, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Floors are counted from where Farm was switched on.");
				ImGui::Unindent();
			}
			ImGui::SetNextItemWidth(160 * g_Appearance.scale);
			changed |= ImGui::SliderInt("Stop after this many expeditions (0 = no limit)", &g_Farm.max_runs, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
			if (g_Farm.on && g_Farm.max_runs > 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%d started", g_Farm.runs);
			}
			changed |= ImGui::Checkbox("Stop when the bag is full", &g_Farm.stop_full);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("On the Battlefield and in the Slorm Temple the hero takes the next door home.\nIn town it stops instead of starting again.");
			if (g_Farm.bag_used >= 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%d of %d", g_Farm.bag_used, g_Farm.bag_slots);
			}
			if (!ImGui::BeginTabBar("farmer"))
				return changed;
			if (ImGui::BeginTabItem("Battlefield"))
			{
				ImGui::SeparatorText("Breaches");
				changed |= ImGui::Checkbox("Skip Breach V", &g_Farm.skip_chaos);
				ImGui::SetNextItemWidth(90);
				changed |= ImGui::InputFloat("Once the portal can be opened, still do breaches within this", &g_Farm.extra, 0, 0, "%.0f");
				ImGui::SeparatorText("Loot");
				// Long lines wrap; the box never scrolls sideways.
				if (ImGui::BeginChild("loot", { 0, 110 * g_Appearance.scale }, ImGuiChildFlags_Borders))
				{
					static int shown = 0;
					for (const LootLine& line : g_Farm.loot_log)
					{
						std::string text = line.count > 1 ? line.text + " x" + std::to_string(line.count) : line.text;
						if (line.kind == LOOT_REAPER)
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.2f, 1.0f));
						else if (line.kind == LOOT_STUFF)
							ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						ImGui::TextWrapped("%s", text.c_str());
						if (line.kind != LOOT_GEAR)
							ImGui::PopStyleColor();
					}
					if (g_Farm.loot_log.empty())
						ImGui::TextDisabled("Nothing picked up yet.");
					// Kept at the newest line as lines come in.
					if (shown != g_Farm.loot_count)
						ImGui::SetScrollHereY(1.0f);
					shown = g_Farm.loot_count;
				}
				ImGui::EndChild();
				if (!g_Farm.loot_log.empty() && ImGui::SmallButton("Clear"))
					g_Farm.loot_log.clear();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Temple"))
			{
				ImGui::SetNextItemWidth(90 * g_Appearance.scale);
				changed |= ImGui::InputInt("Start on this floor (0 = the one the launcher has)", &g_Farm.temple_floor, 0, 0);
				g_Farm.temple_floor = (std::min)((std::max)(g_Farm.temple_floor, 0), 3000);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Each expedition starts on this floor and climbs from it.\nNo higher than the floor after the highest one cleared.");
				if (g_Farm.temple_cleared >= 0)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("highest cleared %d", g_Farm.temple_cleared);
				}
				changed |= ImGui::Checkbox("Use the shrine at the end of each floor", &g_Farm.temple_shrine);
				changed |= ImGui::Checkbox("Pick up pure slorm left on the ground", &g_Farm.temple_pure);
				ImGui::TextDisabled("Floors played before heading home are set above, with the expedition.");
				ImGui::EndTabItem();
			}
			// The mods whose tabs sit in this one.
			for (Feature* feature : Features())
			{
				if (!feature->under || std::string_view(feature->under) != tab || !ImGui::BeginTabItem(feature->tab))
					continue;
				changed |= DrawFeature(feature);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Settings"))
			{
				ImGui::SeparatorText("Loot");
				changed |= ImGui::Checkbox("Pick up loot within", &g_Farm.loot);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90);
				changed |= ImGui::InputFloat("##lootrange", &g_Farm.loot_range, 0, 0, "%.0f");
				ImGui::SeparatorText("Loot log");
				ImGui::TextDisabled("Listed when picked up:");
				static const char* const SHOWN[] = { "Reapers", "Gear", "Fragments", "Slormites", "Slormelines", "Slormandrites", "Anything else" };
				for (int i = 0; i < 7; i++)
				{
					if (i % 4)
						ImGui::SameLine();
					changed |= ImGui::Checkbox(SHOWN[i], &g_Farm.loot_show[i]);
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			int flag = 0;
			if (Name == "key") { Words >> g_Farm.key; if (g_Farm.key < 8 || g_Farm.key > 254) g_Farm.key = VK_HOME; }
			else if (Name == "extra") Words >> g_Farm.extra;
			else if (Name == "loot_range") Words >> g_Farm.loot_range;
			else if (Name == "skip_chaos") { Words >> flag; g_Farm.skip_chaos = flag != 0; }
			else if (Name == "mode") { Words >> g_Farm.mode; g_Farm.mode = g_Farm.mode == 1 || g_Farm.mode == 2 ? g_Farm.mode : 0; }
			else if (Name == "temple_floor") { Words >> g_Farm.temple_floor; g_Farm.temple_floor = (std::min)((std::max)(g_Farm.temple_floor, 0), 3000); }
			else if (Name == "temple_shrine") { Words >> flag; g_Farm.temple_shrine = flag != 0; }
			else if (Name == "temple_pure") { Words >> flag; g_Farm.temple_pure = flag != 0; }
			else if (Name == "forge_waves") { Words >> g_Farm.forge_waves; g_Farm.forge_waves = (std::min)((std::max)(g_Farm.forge_waves, 1), 100); }
			else if (Name == "forge_center") { Words >> flag; g_Farm.forge_center = flag != 0; }
			else if (Name == "max_runs") { Words >> g_Farm.max_runs; g_Farm.max_runs = (std::min)((std::max)(g_Farm.max_runs, 0), 100); }
			else if (Name == "max_floors") { Words >> g_Farm.max_floors; g_Farm.max_floors = (std::min)((std::max)(g_Farm.max_floors, 0), 100); }
			else if (Name == "stop_full") { Words >> flag; g_Farm.stop_full = flag != 0; }
			else if (Name == "restart") { Words >> flag; g_Farm.restart = flag != 0; }
			else if (Name == "loot") { Words >> flag; g_Farm.loot = flag != 0; }
			// Once a setting of this mod, now a mod of its own.
			else if (Name == "reaper") { Words >> flag; if (flag && g_Unlock) g_Unlock->enabled = true; }
			else if (Name == "loot_show")
				for (bool& show : g_Farm.loot_show)
					if (Words >> flag) show = flag != 0;
		}

		void Save(std::ostream& File) override
		{
			File << "farmer.key " << g_Farm.key << '\n'
				<< "farmer.skip_chaos " << (g_Farm.skip_chaos ? 1 : 0) << '\n'
				<< "farmer.extra " << g_Farm.extra << '\n'
				<< "farmer.restart " << (g_Farm.restart ? 1 : 0) << '\n'
				<< "farmer.temple_floor " << g_Farm.temple_floor << '\n'
				<< "farmer.temple_shrine " << (g_Farm.temple_shrine ? 1 : 0) << '\n'
				<< "farmer.temple_pure " << (g_Farm.temple_pure ? 1 : 0) << '\n'
				<< "farmer.mode " << g_Farm.mode << '\n'
				<< "farmer.forge_waves " << g_Farm.forge_waves << '\n'
				<< "farmer.forge_center " << (g_Farm.forge_center ? 1 : 0) << '\n'
				<< "farmer.max_runs " << g_Farm.max_runs << '\n'
				<< "farmer.max_floors " << g_Farm.max_floors << '\n'
				<< "farmer.stop_full " << (g_Farm.stop_full ? 1 : 0) << '\n'
				<< "farmer.loot " << (g_Farm.loot ? 1 : 0) << '\n'
				<< "farmer.loot_range " << g_Farm.loot_range << '\n'
				<< "farmer.loot_show";
			for (bool show : g_Farm.loot_show)
				File << ' ' << (show ? 1 : 0);
			File << '\n';
		}
	} g_Farmer;

	struct ReaperUnlock : Feature
	{
		ReaperUnlock() : Feature("Auto unlock reaper drops", "reaper_unlock", "A reaper picked up is unlocked at once, without opening the character screen, and its screen is clicked away.")
		{
			g_Unlock = this;
		}

		void Tick(CInstance* Self) override { ReaperTick(Self); }
	} g_ReaperUnlock;
}
