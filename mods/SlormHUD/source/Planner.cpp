// Reads builds shared from slorm-planner. The whole build is packed into the link:
// six bits per character, fields in the order the planner writes them.
#include "Hud.hpp"
#include <tuple>

std::vector<Build> g_Builds;
int g_ActiveBuild = -1;

namespace
{
	const char ALPHABET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789$";

	struct Reader
	{
		std::vector<bool> bits;
		size_t at = 0;
		bool overrun = false;

		int Take(int Count)
		{
			int value = 0;
			for (int i = 0; i < Count; i++)
			{
				if (at >= bits.size()) overrun = true;
				value = value * 2 + (at < bits.size() && bits[at] ? 1 : 0);
				at++;
			}
			return value;
		}
	};
}

const char* HeroName(int Class)
{
	static const char* names[] = { "Knight", "Huntress", "Mage" };
	return Class >= 0 && Class < 3 ? names[Class] : "Unknown";
}

bool ParseBuild(const std::string& Text, Build& Out, std::string& Error)
{
	// A full link or only its last part.
	std::string key = Text;
	while (!key.empty() && isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
	size_t slash = key.find_last_of('/');
	if (slash != std::string::npos)
		key.erase(0, slash + 1);
	while (!key.empty() && isspace(static_cast<unsigned char>(key.front()))) key.erase(0, 1);
	if (key.size() < 20)
	{
		Error = "that is not a planner link";
		return false;
	}

	Reader reader;
	for (char c : key)
	{
		const char* found = strchr(ALPHABET, c);
		if (!found || !c)
		{
			Error = "that is not a planner link";
			return false;
		}
		int value = static_cast<int>(found - ALPHABET);
		for (int bit = 5; bit >= 0; bit--) reader.bits.push_back((value >> bit) & 1);
	}

	int major = reader.Take(4), minor = reader.Take(4), fix = reader.Take(6);
	auto since = [&](int Major, int Minor, int Fix) { return std::tie(major, minor, fix) >= std::tie(Major, Minor, Fix); };

	Out = {};
	Out.key = key;
	Out.hero = reader.Take(2);
	Out.level = reader.Take(since(0, 4, 0) ? 7 : 6);
	if (Out.hero > 2)
	{
		Error = "that is not a planner link";
		return false;
	}

	Out.reaper.id = reader.Take(10);
	Out.reaper.primordial = reader.Take(1) != 0;
	Out.reaper.level = reader.Take(7);
	if (since(0, 2, 0)) Out.reaper.affinity = reader.Take(7);
	// Two reapers carry extra state.
	if (Out.reaper.id == 90 && Out.reaper.primordial) reader.Take(10);
	if (Out.reaper.id == 114) reader.Take(25);
	if (since(0, 7, 0)) Out.reaper.mastery = reader.Take(7);

	if (since(0, 2, 0))
		for (Ranked& rune : Out.runes)
			if (reader.Take(1))
			{
				rune.id = reader.Take(6);
				rune.rank = reader.Take(5);
			}

	if (since(0, 5, 0) && reader.Take(1)) Out.first_node = reader.Take(10);
	for (int i = reader.Take(4); i > 0; i--) Out.nodes.push_back(reader.Take(10));
	for (int i = reader.Take(4); i > 0; i--)
	{
		Ranked legacy;
		legacy.id = reader.Take(10);
		legacy.rank = reader.Take(4);
		Out.legacies.push_back(legacy);
	}

	for (int i = reader.Take(4); i > 0; i--)
	{
		BuildSkill skill;
		skill.id = reader.Take(10);
		skill.level = reader.Take(5);
		if (reader.Take(1))
			for (int j = reader.Take(4); j > 0; j--)
			{
				Ranked upgrade;
				upgrade.id = reader.Take(10);
				upgrade.rank = reader.Take(4);
				skill.upgrades.push_back(upgrade);
			}
		Out.skills.push_back(std::move(skill));
	}

	static const char* slots[] = { "helm", "body", "shoulder", "bracer", "glove", "boot", "ring", "ring", "amulet", "belt", "cape" };
	static const char* labels[] = { "Helm", "Body", "Shoulder", "Bracer", "Glove", "Boot", "Left ring", "Right ring", "Amulet", "Belt", "Cape" };
	static const char* tiers[] = { "N", "D", "M", "R", "E" };
	for (int i = reader.Take(4); i > 0; i--)
	{
		int index = reader.Take(5);
		if (index > 10 || reader.overrun)
		{
			Error = "could not read the gear in this link";
			return false;
		}
		BuildItem item;
		item.slot = slots[index];
		item.index = index;
		item.label = labels[index];
		item.level = reader.Take(since(0, 4, 1) ? 7 : 6);
		item.reinforcement = reader.Take(8);
		if (item.level > 100) reader.Take(4);
		for (int j = reader.Take(4); j > 0; j--)
		{
			int rarity = reader.Take(3);
			const StatInfo* stat = StatById(reader.Take(9));
			BuildAffix affix;
			affix.roll = reader.Take(9);
			affix.pure = reader.Take(1) != 0;
			if (affix.pure) reader.Take(8);
			if (!stat || rarity > 6)
			{
				Error = "this link has a stat the game does not know";
				return false;
			}
			// The legendary's own stats are not rolled ones.
			if (rarity > 4)
				continue;
			affix.tier = tiers[rarity];
			affix.stat = stat->ref;
			item.affixes.push_back(affix);
		}
		// Each is stored as id plus one, zero for none.
		item.attribute = reader.Take(4) - 1;
		item.attribute_value = reader.Take(2);
		item.smith = reader.Take(4) - 1;
		item.smith_value = reader.Take(3);
		item.mastery = reader.Take(4) - 1;
		item.mastery_value = reader.Take(3);
		item.legendary = reader.Take(10) - 1;
		item.legendary_value = reader.Take(8);
		Out.items.push_back(std::move(item));
	}

	Out.ultimatum = reader.Take(5) - 1;
	Out.ultimatum_level = reader.Take(5);
	for (int& rank : Out.attributes) rank = reader.Take(since(0, 4, 1) ? 7 : 6);
	Out.support = reader.Take(10) - 1;
	Out.primary = reader.Take(10) - 1;
	Out.secondary = reader.Take(10) - 1;
	for (int& activable : Out.activables) activable = reader.Take(10) - 1;

	if (reader.overrun)
	{
		Error = "this link is cut short";
		return false;
	}
	Out.name = std::string(HeroName(Out.hero)) + " level " + std::to_string(Out.level);
	return true;
}
