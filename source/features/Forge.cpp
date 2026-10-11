// The Great Forge: picks each wave's path by the player's priority lists.
#include "../Suite.hpp"
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// Something a path can offer. Challenges and War Chest effects keep the game's own numbers,
	// cataclysms come 100 higher, and a reward is 1000 + 10 x its War Chest slot + its kind.
	struct Entry
	{
		int id;
		const char* kind;
		const char* label;
		// What it does, in the game's words.
		const char* tip;
	};
	constexpr Entry CATALOG[] = {
		{ 1010, "Loot", "Random Equipment", "Adds a random piece of Equipment to the War Chest." },
		{ 1020, "Loot", "Random Slormite", "Adds a random Slormite to the War Chest." },
		{ 1030, "Loot", "Random Slormeline or Slormandrite", "Adds a random Slormeline or Slormandrite to the War Chest." },
		{ 1040, "Loot", "Random Fragment", "Adds a random Fragment to the War Chest." },
		{ 1011, "Quality and quantity", "Equipment Quality", "Raises the quality of the Equipment in the War Chest when it is looted.\nLost when defeated." },
		{ 1021, "Quality and quantity", "Slormite Quality", "Raises the quality of the Slormites in the War Chest when it is looted.\nLost when defeated." },
		{ 1031, "Quality and quantity", "Slormeline Quality", "Raises the quality of the Slormelines in the War Chest when it is looted.\nLost when defeated." },
		{ 1041, "Quality and quantity", "Fragment Quality", "Raises the quality of the Fragments in the War Chest when it is looted.\nLost when defeated." },
		{ 1051, "Quality and quantity", "Goldus Quantity", "Raises the quantity of Goldus in the War Chest.\nLost when defeated." },
		{ 1061, "Quality and quantity", "Slorm Quantity", "Raises the quantity of Slorm in the War Chest.\nLost when defeated." },
		{ 1070, "Quality and quantity", "War Chest Quantity", "Increases the quantity of everything inside the War Chest when it is looted.\nLost when defeated." },
		{ 1080, "Quality and quantity", "War Chest Quality", "Increases the overall quality of the Equipment, Slormites, Slormelines and Fragments inside the War Chest when it is looted.\nLost when defeated." },
		{ 1000, "Spirits", "Spirit of Astorias", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1001, "Spirits", "Spirit of Adrianne", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1002, "Spirits", "Spirit of Beigarth", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1003, "Spirits", "Spirit of Cory", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1004, "Spirits", "Spirit of Smaloron", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1005, "Spirits", "Spirit of Fulgurorn", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 1006, "Spirits", "Spirit of Hagan", "Reapersmith Spirits of this type, spent at Cory Ironbender.\nSpirits of your current Slorm Reaper's type get a bonus." },
		{ 6, "Spirit effects", "All spirit types equal the highest", "The amount of each type of Reapersmith Spirit inside your War Chest becomes equal to the highest amount among the types of Reapersmith Spirits inside your War Chest." },
		{ 20, "Spirit effects", "35% chance of a spirit per kill", "For the rest of the Expedition, you have 35% Chance to add +1 of a random type of Reapersmith Spirit to your War Chest everytime you kill an enemy." },
		{ 22, "Spirit effects", "Rewards +100% with every spirit type", "For the rest of the Expedition, if there is at least one Reapersmith Spirit of each type inside your War Chest, following Rewards are increased by 100%." },
		{ 25, "Spirit effects", "Spirits increased by Slorm Quantity", "Increases the amount of every Reapersmith Spirit in your War Chest by your current Slorm Quantity." },
		{ 29, "Spirit effects", "Spirits increased by Fragment Quality", "Increases the amount of every Reapersmith Spirit in your War Chest by your current Fragment Quality." },
		{ 26, "Spirit effects", "All spirits become my Reaper's type", "Converts all the Reapersmith Spirits in your War Chest into the type of Reapersmith Spirit of your current Slorm Reaper." },
		{ 27, "Spirit effects", "All spirits become my most-held type", "Converts all the Reapersmith Spirits in your War Chest into the type of Reapersmith Spirit that you have the most inside your War Chest." },
		{ 28, "Spirit effects", "All spirits become Goldus", "Converts all the Reapersmith Spirits in your War Chest into Goldus (35 Goldus per Reapersmith Spirit)." },
		{ 31, "Spirit effects", "+35 spirits per Allied Obelisk each wave", "You add +35 Reapersmith Spirits of a random type to your War Chest for every Allied Obelisk left alive everytime you complete a Wave." },
		{ 34, "Spirit effects", "Spirit rewards only, +200%", "Following Rewards can only be Reapersmith Spirits.\nFollowing Rewards are increased by 200%." },
		{ 4, "War Chest effects", "Chest Quantity equals Chest Quality", "For the rest of the Expedition, your War Chest Quantity is equal to your War Chest Quality." },
		{ 5, "War Chest effects", "+5% Chest Quality per loot type", "For the rest of the Expedition, you gain +5% War Chest Quality for every type of loot inside your War Chest." },
		{ 14, "War Chest effects", "Equipment Quality into Chest Quality", "Converts your current Equipment Quality into War Chest Quality." },
		{ 15, "War Chest effects", "Slorm Quantity into Chest Quantity", "Converts your current Slorm Quantity into War Chest Quantity." },
		{ 18, "War Chest effects", "+2% Chest Quality per wave", "For the rest of the Expedition, you gain +2% War Chest Quality everytime you complete a Wave." },
		{ 19, "War Chest effects", "+1% Chest Quantity per wave", "For the rest of the Expedition, you gain +1% War Chest Quantity everytime you complete a Wave." },
		{ 30, "War Chest effects", "Chest contents increased by Chest Quality", "The total of every loot inside your War Chest is immediately increased by your current War Chest Quality." },
		{ 21, "War Chest effects", "Normal challenge rewards +100%", "For the rest of the Expedition, Rewards from Normal Challenges are increased by 100%." },
		{ 35, "War Chest effects", "Keep bonuses when defeated", "If you are defeated inside The Great Forge, you will get to loot your War Chest without losing all its bonuses and multipliers." },
		{ 7, "Loot effects", "Goldus equal to the Slorm in the chest", "You gain Goldus equal to 100% of the Slorm inside your War Chest." },
		{ 16, "Loot effects", "+3% Goldus Quantity per Elite kill", "For the rest of the Expedition, you add +3% Goldus Quantity to your War Chest everytime you kill an Elite enemy." },
		{ 17, "Loot effects", "+2% Goldus in the chest per wave", "For the rest of the Expedition, the amount of Goldus inside your War Chest is increased by 2% everytime you complete a Wave." },
		{ 23, "Loot effects", "All Quality into Goldus Quantity", "Converts your current Equipment Quality, Slormite Quality, Slormelines Quality and Fragment Quality into Goldus Quantity." },
		{ 8, "Loot effects", "+10 Slorm per spirit", "You immediately add +10 Slorm to your War Chest for every Reapersmith Spirit inside your War Chest." },
		{ 13, "Loot effects", "25% of Slorm becomes Elder Slorm", "When looting your War Chest, 25% of the Slorm you should loot is converted into Elder Slorm." },
		{ 9, "Loot effects", "+1% Equipment Quality per Equipment", "You immediately gain +1% Equipment Quality for every Equipment inside your War Chest." },
		{ 33, "Loot effects", "+3% Equipment Quality per wave with a hostile Obelisk", "For the rest of the Expedition, you gain +3% Equipment Quality everytime you complete a Wave with at least one active hostile Obelisk." },
		{ 10, "Loot effects", "+2% Slormite Quality per Slormite", "You immediately gain +2% Slormite Quality for every Slormite inside your War Chest." },
		{ 11, "Loot effects", "+3% Slormeline Quality per Slormeline", "You immediately gain +3% Slormeline Quality for every Slormeline inside your War Chest." },
		{ 24, "Loot effects", "Slorm and Goldus Quantity into Slormeline Quality", "Converts your Slorm Quantity and Goldus Quantity into Slormeline Quality." },
		{ 12, "Loot effects", "+1% Fragment Quality per Slormite and Slormeline", "You immediately gain +1% Fragment Quality for every Slormite and Slormeline inside your War Chest." },
		{ 32, "Allied Obelisk", "Allied Obelisk rewards +200%", "For the rest of the Expedition, Allied Obelisks' Rewards are increased by 200%." },
		{ 36, "Allied Obelisk", "Allied Obelisk: +9 spirits per wave", "Allied Obelisk adds +9 Reapersmith Spirits of a random type to your War Chest everytime you complete a Wave." },
		{ 37, "Allied Obelisk", "Allied Obelisk: +3% Goldus Quantity per wave", "Allied Obelisk adds +3% Goldus Quantity to your War Chest everytime you complete a Wave." },
		{ 38, "Allied Obelisk", "Allied Obelisk: +2% Chest Quality per wave", "Allied Obelisk adds +2% War Chest Quality to your War Chest everytime you complete a Wave." },
		{ 39, "Allied Obelisk", "Allied Obelisk: +2% Chest Quantity per wave", "Allied Obelisk adds +2% War Chest Quantity to your War Chest everytime you complete a Wave." },
		{ 40, "Allied Obelisk", "Allied Obelisk: +4% Equipment Quality per wave", "Allied Obelisk adds +4% Equipment Quality to your War Chest everytime you complete a Wave." },
		{ 1, "Challenges", "Greater Strength", "Enemies deal +15% Increased Damage." },
		{ 2, "Challenges", "Greater Vitality", "Enemies have +30% Max Life." },
		{ 3, "Challenges", "Greater Speed", "Enemies have +5% Attack Speed and +10% Movement Speed." },
		{ 68, "Challenges", "\"Set up the Barricades!\"", "Each Wave, enemies will come from 2 different locations." },
		{ 69, "Challenges", "\"Open the Gates!\"", "Each Wave, enemies will come from 4 different locations!" },
		{ 70, "Challenges", "\"Let. Them. Come.\"", "Each Wave, enemies will come from 6 different locations!!" },
		{ 71, "Challenges", "\"Is That All?!\"", "Each Wave, enemies will come from 1 additional locations." },
		{ 47, "Challenges", "Neverending Strength", "Enemies gain a 2% Increased Damage Multiplier everytime you complete a Wave." },
		{ 48, "Challenges", "Neverending Vitality", "Enemies gain a 4% Max Life Multiplier everytime you complete a Wave." },
		{ 49, "Challenges", "Neverending Speed", "Enemies gain a 2% Attack Speed Multiplier everytime you complete a Wave." },
		{ 50, "Challenges", "Overcrowding", "The following Wave has +25% more enemies. (50% at Epic)" },
		{ 51, "Challenges", "Ace Combat", "The following Wave has 3 additional Elite enemies. (6 at Epic)" },
		{ 52, "Challenges", "Highlander", "The following Wave has an Invincible Elite enemy that only dies once you've killed every other enemy." },
		{ 53, "Challenges", "Attrition Battle", "The Duration of all upcoming Challenges is increased by 2 Waves." },
		{ 54, "Challenges", "Cursed Reward", "Adds a random Curse that will affect an upcoming Challenge." },
		{ 55, "Challenges", "Curse: life from Goldus Quantity", "Enemies have more Max Life.\nMax Life is equal to 25% of your current Goldus Quantity." },
		{ 56, "Challenges", "Curse: damage from spirits", "Enemies have more Skill and Elemental Damage.\nDamage is equal to the total of all the Reapersmith Spirits inside your War Chest." },
		{ 57, "Challenges", "Curse: attack speed from Chest Quantity", "Enemies have more Attack Speed.\nAttack Speed is equal to your current War Chest Quantity." },
		{ 58, "Challenges", "Upgrade: next is Magic", "The next Challenge is of Magic rarity." },
		{ 59, "Challenges", "Upgrade: next is Rare", "The next Challenge is of Rare rarity." },
		{ 60, "Challenges", "Upgrade: next is Epic", "The next Challenge is of Epic rarity." },
		{ 61, "Challenges", "Upgrade: next is Legendary", "The next Challenge is of Legendary rarity." },
		{ 116, "Cataclysms", "Assassins", "Enemies have +5% Evasion and +10% Critical Strike Chance." },
		{ 117, "Cataclysms", "Assimilation", "Enemies receive 15% Decreased Damage for each yard between you and them." },
		{ 103, "Cataclysms", "Avenger", "Enemies gain +3% Increased Damage whenever another enemy dies within a 5 yard radius." },
		{ 122, "Cataclysms", "Double Down", "Enemies have +50% Recast Chance." },
		{ 135, "Cataclysms", "Endless Fire", "A slow yet massive Fireball is constantly following you, dealing 100 Fire Damage to Heroes and Minions hit." },
		{ 143, "Cataclysms", "Equilibrium", "Damage dealt by Fire, Ice, Lightning, Light and Shadow Effects is increased by 25%." },
		{ 141, "Cataclysms", "Evolving Wizards", "Enemies have +25% Elemental Resistance and deal +25% Elemental Damage." },
		{ 110, "Cataclysms", "Explosive", "Enemies explode upon dying, dealing 10% of their Base Max Life as Skill Damage after 3 second within a 1.5 yard radius." },
		{ 130, "Cataclysms", "Fire Rage", "Enemies summon 1 large Fire waves upon dying, dealing 18 Fire Damage." },
		{ 125, "Cataclysms", "Fire Strike", "Enemies deal extra Fire Damage and have a chance to apply Burn for a few seconds." },
		{ 111, "Cataclysms", "Floor is Lava", "Enemies leave an area of Molten Lava upon dying, lasting 3 seconds and dealing 15 Fire Damage per second to Heroes and Minions standing on it." },
		{ 144, "Cataclysms", "Flow Interruption", "You have -25% Life and Mana Regeneration." },
		{ 101, "Cataclysms", "Frenzy", "Enemies have +25% Attack Speed and +50% Movement Speed." },
		{ 119, "Cataclysms", "Fury of the Storm", "Enemies trigger a Lightning Bolt upon dying, dealing 18 Lightning Damage to Heroes and Minions hit." },
		{ 115, "Cataclysms", "Gargantuan", "Enemies are bigger and deal +200% Increased Damage until they receive damage." },
		{ 100, "Cataclysms", "Gravity", "Your Projectiles have -50% Projectile Range and Speed." },
		{ 121, "Cataclysms", "Hermetic", "Enemies have +25% Resistance to Damage over Time." },
		{ 112, "Cataclysms", "Ice Bombs", "An Ice Bomb appears every 2 seconds.\nIce Bomb explodes after 2 seconds, dealing 90 Ice Damage and Freezing Heroes and Minions hit by the explosion for the next 1.5 seconds within a 2 yard radius." },
		{ 136, "Cataclysms", "Ice Ray", "Every 15 seconds, an Ice Ray falls from the sky directly toward you, dealing up to 150 Ice Damage. This Damage cannot be fatal.\nA few moments before it lands, Bryan creates a Protection Dome. Get into the dome to prevent the Ice Ray from dealing damage to you." },
		{ 126, "Cataclysms", "Ice Strike", "Enemies deal extra Ice Damage and have a chance to apply Chill for a few seconds." },
		{ 105, "Cataclysms", "Invulnerability", "Enemies gain a Shield equal to 100% of their Max Life." },
		{ 106, "Cataclysms", "Iron Health", "Enemies have +25% Max Life." },
		{ 113, "Cataclysms", "Light Beam", "Light Beams continuously appear, dealing 29 Light Damage in a small area to Heroes and Minions hit." },
		{ 128, "Cataclysms", "Light Strike", "Enemies deal extra Light Damage and have a chance to apply Daze, Slow, Armor Broken or Elemental Resistance Broken for a few seconds." },
		{ 138, "Cataclysms", "Light Totem", "A Light Totem appears every 10 seconds.\nThe Light Totem lasts 6 seconds and casts 3 waves of 5 Light Projectiles before disappearing." },
		{ 127, "Cataclysms", "Lightning Strike", "Enemies deal extra Lightning Damage and have a chance to apply Shock for a few seconds." },
		{ 123, "Cataclysms", "Multiplication", "Enemies have +1 Additional Projectiles." },
		{ 142, "Cataclysms", "Nullifying Shield", "Enemies ignore the first 1 hits." },
		{ 124, "Cataclysms", "Proliferation", "Enemies have +25% Increased Area Size." },
		{ 118, "Cataclysms", "Reconstruction", "Enemies not receiving damage for 3 seconds restore their full Life." },
		{ 109, "Cataclysms", "Regeneration", "Non-Elite Enemies regenerate 10% of their Max Life per second.\nElite Enemies regenerate 2% of their Max Life per second." },
		{ 102, "Cataclysms", "Reincarnation", "Enemies have 25% Chance to be revived once upon dying." },
		{ 137, "Cataclysms", "Relentless Lightning", "Lightning Bolts are constantly bouncing from the Edges of the World dealing 50 Lightning Damage to Heroes and Minions hit." },
		{ 114, "Cataclysms", "Replicant", "Enemies have 30% Chance to summon a copy of themselves when they reach 20% Life." },
		{ 133, "Cataclysms", "Seal of Blindness", "Seals of Blindness are moving randomly across the area.\nStanding on a Seal of Blindness reduces your Field of Vision." },
		{ 139, "Cataclysms", "Shadow Benediction", "Every 4 seconds, a random enemy nearby becomes Chosen by the Shadows for the next 3 seconds.\nWhen Chosen by the Shadows ends, if the enemy is still alive, it deals 100 Shadow Damage to you and applies Shadow Confusion." },
		{ 134, "Cataclysms", "Shadow Clone", "Enemies create a Shadow Clone upon dying that casts 1 attacks before vanishing." },
		{ 120, "Cataclysms", "Shadow Curse", "Every 3 seconds, a Shadow Symbol appears next to you.\nThe Shadow Symbol lasts 1.5 seconds and deals 21 Shadow Damage the first time you walk onto it." },
		{ 129, "Cataclysms", "Shadow Strike", "Enemies deal extra Shadow Damage and have a chance to apply Shadow Confusion for a few seconds." },
		{ 132, "Cataclysms", "Side Storm", "Whenever they attack, Enemies create 2 Lightning Bolts on their sides dealing 22 Lightning Damage to Heroes and Minions hit." },
		{ 131, "Cataclysms", "Slippery Ice", "The entire area is covered in Slippery Ice.\nWhile standing on Slippery Ice area, you slip..." },
		{ 107, "Cataclysms", "Tenacious", "Enemies have +50% Tenacity, granting resistance to Crowd Control and Knockback." },
		{ 108, "Cataclysms", "To Each his Own", "You have -25% Life Leech." },
		{ 104, "Cataclysms", "Unified", "Enemies have +3% Increased Damage and +3% Movement Speed for every other enemy within a 3 yard radius." },
		{ 140, "Cataclysms", "Wild Beasts", "Enemies have +25% Armor and deal +25% Skill Damage." },
		{ 62, "Obelisks", "Obelisk of Transmutation", "Spawns an Obelisk of Transmutation nearby.\nWhile Obelisk of Transmutation is active, everytime an enemy dies, it restores Life equal to its Max Life to another random enemy.\nDestroyed, it becomes an Allied Obelisk that rewards you each Wave." },
		{ 63, "Obelisks", "Obelisk of Destruction", "Spawns an Obelisk of Destruction nearby.\nWhile Obelisk of Destruction is active, it continuously casts Deadly Missiles in random directions.\nDestroyed, it becomes an Allied Obelisk that rewards you each Wave." },
		{ 64, "Obelisks", "Obelisk of Illusion", "Spawns an Obelisk of Illusion nearby.\nWhile Obelisk of Illusion is active, you can only choose between 2 Challenges instead of 3.\nDestroyed, it becomes an Allied Obelisk that rewards you each Wave." },
		{ 65, "Obelisks", "Obelisk of Annihilation", "Spawns an Obelisk of Annihilation nearby.\nWhile Obelisk of Annihilation is active, Enemies deal Increased Damage equal to the Percent of Life Left on Obelisk of Annihilation.\nDestroyed, it becomes an Allied Obelisk that rewards you each Wave." },
		{ 66, "Obelisks", "Obelisk of Revocation", "Spawns an Obelisk of Revocation nearby.\nWhile Obelisk of Revocation is active, you cannot restore Life.\nDestroyed, it becomes an Allied Obelisk that rewards you each Wave." },
		{ 41, "Siege Leader", "The Siege Leader : Brutality", "The Siege Leader has +20% Increased Damage." },
		{ 42, "Siege Leader", "The Siege Leader : Dominance", "The Siege Leader has +40% Max Life." },
		{ 43, "Siege Leader", "The Siege Leader : Alacrity", "The Siege Leader has +10% Attack Speed." },
		{ 67, "Siege Leader", "The Siege Leader : Quiver", "The Siege Leader has +1 Additional Projectile." },
		{ 44, "Siege Leader", "The Siege Leader : Barrage", "The Siege Leader has new Attack Patterns." },
		{ 45, "Siege Leader", "The Siege Leader : Escort", "The Siege Leader is escorted by 2 Elite Enemies." },
		{ 46, "Siege Leader", "The Siege Leader : Reinforcements", "The Siege Leader continuously calls upon allies during the fight." },
	};

	const Entry* Find(int Id)
	{
		for (const Entry& entry : CATALOG)
			if (entry.id == Id) return &entry;
		return nullptr;
	}

	const char* Label(int Id)
	{
		const Entry* entry = Find(Id);
		return entry ? entry->label : "?";
	}

	void ShowTip(const char* Text)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
		ImGui::TextUnformatted(Text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	void Tip(const char* Text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ShowTip(Text);
	}

	int g_NextUid = 1;

	// A line of the priority tree: an entry of the catalog, or a list holding more lines.
	struct Node
	{
		int uid = g_NextUid++;
		// The catalog entry; -1 for a list.
		int id = -1;
		std::string name;
		// An entry to stay away from: paths offering it are passed over from this line down.
		bool avoid = false;
		// A list picks its lines top to bottom; when off, any of them is as good as another.
		bool ordered = true;
		std::vector<Node> children;
	};

	Node List(const char* Name, std::initializer_list<int> Ids)
	{
		Node list;
		list.name = Name;
		for (int id : Ids)
		{
			Node line;
			line.id = id;
			list.children.push_back(line);
		}
		return list;
	}

	Node* Find(Node& From, int Uid)
	{
		if (From.uid == Uid)
			return &From;
		for (Node& child : From.children)
			if (Node* found = Find(child, Uid)) return found;
		return nullptr;
	}

	bool Take(Node& Parent, int Uid, Node& Out)
	{
		for (size_t i = 0; i < Parent.children.size(); i++)
		{
			if (Parent.children[i].uid == Uid)
			{
				Out = std::move(Parent.children[i]);
				Parent.children.erase(Parent.children.begin() + i);
				return true;
			}
			if (Take(Parent.children[i], Uid, Out))
				return true;
		}
		return false;
	}

	bool PlaceBefore(Node& Parent, int Uid, const Node& What)
	{
		for (size_t i = 0; i < Parent.children.size(); i++)
		{
			if (Parent.children[i].uid == Uid)
			{
				Parent.children.insert(Parent.children.begin() + i, What);
				return true;
			}
			if (PlaceBefore(Parent.children[i], Uid, What))
				return true;
		}
		return false;
	}

	void Used(const Node& From, std::set<int>& Ids)
	{
		if (From.id >= 0) Ids.insert(From.id);
		for (const Node& child : From.children) Used(child, Ids);
	}

	void Collect(const Node& From, std::vector<const Node*>& Lines)
	{
		if (From.id >= 0) Lines.push_back(&From);
		for (const Node& child : From.children) Collect(child, Lines);
	}

	// The tree as steps from first to last; the lines of one step are as good as each other.
	void Flatten(const Node& List, std::vector<std::vector<const Node*>>& Steps)
	{
		for (const Node& child : List.children)
		{
			if (child.id < 0 && child.ordered)
			{
				Flatten(child, Steps);
				continue;
			}
			std::vector<const Node*> step;
			Collect(child, step);
			if (!step.empty()) Steps.push_back(step);
		}
	}

	PFUNC_YYGMLScript g_Choose = nullptr;

	// What each path on offer holds, as catalog ids, left to right.
	std::vector<std::vector<int>> Offers()
	{
		std::vector<std::vector<int>> offers;
		CInstance* room = FindInstance("obj_procedural");
		if (!room)
			return offers;
		RValue paths = Local(room, "forge_challenge_array");
		if (!paths.IsArray())
			return offers;
		for (const RValue& path : paths.ToVector())
		{
			std::vector<int> ids;
			std::vector<RValue> fields = path.IsArray() ? path.ToVector() : std::vector<RValue>();
			if (!fields.empty()) ids.push_back(static_cast<int>(ToNumber(fields[0])));
			// The reward is "slot.kind" as text, and -1 when the path has none.
			if (fields.size() > 4 && fields[4].IsString())
			{
				std::string reward = fields[4].ToString();
				size_t dot = reward.find('.');
				int kind = dot == std::string::npos ? 0 : atoi(reward.c_str() + dot + 1);
				ids.push_back(1000 + atoi(reward.c_str()) * 10 + kind);
			}
			// As a number it is the War Chest's own quantity (7) or quality (8), or one of its effects,
			// which the next field then names: 9, or 10 for what an Obelisk gives once allied.
			else if (fields.size() > 5 && fields[4].IsNumberConvertible() && ToNumber(fields[4]) >= 7)
			{
				int slot = static_cast<int>(ToNumber(fields[4]));
				ids.push_back(slot >= 9 ? static_cast<int>(ToNumber(fields[5])) : 1000 + slot * 10);
			}
			offers.push_back(ids);
		}
		return offers;
	}

	// The wave the paths on offer are for, from how many have been fought.
	int Wave()
	{
		int wave = 1;
		CInstance* room = FindInstance("obj_procedural");
		RValue fought = room ? Local(room, "enemies_this_wave") : RValue();
		if (fought.IsArray())
			for (const RValue& enemies : fought.ToVector())
				if (ToNumber(enemies) > 0) wave++;
		return wave;
	}

	// A path as it reads on screen: where it stands and what it holds.
	std::string Describe(const std::vector<std::vector<int>>& Offers, int Path, bool Capitals = false)
	{
		static const char* const THREE[] = { "left", "middle", "right" };
		static const char* const TWO[] = { "left", "right" };
		std::string text = Offers.size() == 3 ? THREE[Path] : Offers.size() == 2 ? TWO[Path] : "only";
		if (Capitals)
			for (char& c : text) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
		for (size_t i = 0; i < Offers[Path].size(); i++)
		{
			const Entry* entry = Find(Offers[Path][i]);
			// 0 is the rest after the Siege Leader, which is no challenge.
			text += (i ? ", " : ": ") + (entry ? std::string(entry->label) : Offers[Path][i] == 0 ? "Well-Deserved Rest" : "#" + std::to_string(Offers[Path][i]));
		}
		return text;
	}

	// The paths taken this run, the player's own included.
	struct Pick
	{
		int wave;
		std::string text;
		const char* by;
	};
	std::vector<Pick> g_Picks;
	// Who is behind the pick being made: the player, unless the mod says otherwise.
	const char* g_By = "you";

	RValue& ChooseHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
	{
		int path = ArgumentCount > 0 && Arguments[0] ? static_cast<int>(ToNumber(*Arguments[0])) : -1;
		std::vector<std::vector<int>> offers = Offers();
		if (path >= 0 && path < static_cast<int>(offers.size()))
		{
			std::lock_guard guard(g_Lock);
			int wave = Wave();
			// An earlier wave than the last means a new run; the same wave, a pick that did not take.
			if (!g_Picks.empty() && wave < g_Picks.back().wave) g_Picks.clear();
			if (!g_Picks.empty() && wave == g_Picks.back().wave) g_Picks.pop_back();
			g_Picks.push_back({ wave, Describe(offers, path), g_By });
			Log("forge: wave %d, %s (%s)", wave, g_Picks.back().text.c_str(), g_By);
		}
		return g_Choose(Self, Other, Result, ArgumentCount, Arguments);
	}

	// The lists as one line of text, for the clipboard and for saved files:
	// "SSF1 70 -109 {+Loot|1061 1051} {~Any|1000 1001}", - for avoid, + or ~ for a list in order or not.
	const char* const PACKED = "SSF1";

	std::string Pack(const Node& List)
	{
		std::string text;
		for (const Node& line : List.children)
		{
			if (!text.empty()) text += ' ';
			if (line.id >= 0)
			{
				text += (line.avoid ? "-" : "") + std::to_string(line.id);
				continue;
			}
			std::string name = line.name;
			for (char& c : name)
				if (c == '|' || c == '{' || c == '}' || c == '\n' || c == '\r') c = ' ';
			text += std::string("{") + (line.ordered ? '+' : '~') + name + '|' + Pack(line) + '}';
		}
		return text;
	}

	// Reads lines into a list up to its closing brace or the end; false when the text is not well formed.
	bool Unpack(const char*& At, Node& List, int Depth)
	{
		for (;;)
		{
			while (*At == ' ' || *At == '\t' || *At == '\r' || *At == '\n') At++;
			if (!*At)
				return Depth == 0;
			if (*At == '}')
			{
				At++;
				return Depth > 0;
			}
			Node line;
			if (*At == '{')
			{
				At++;
				if ((*At != '+' && *At != '~') || Depth >= 8)
					return false;
				line.ordered = *At++ == '+';
				const char* bar = strchr(At, '|');
				if (!bar || bar - At > 47)
					return false;
				line.name.assign(At, bar);
				At = bar + 1;
				if (!Unpack(At, line, Depth + 1))
					return false;
				List.children.push_back(line);
				continue;
			}
			line.avoid = *At == '-';
			if (line.avoid) At++;
			if (*At < '0' || *At > '9')
				return false;
			line.id = static_cast<int>(strtol(At, const_cast<char**>(&At), 10));
			// Entries this version does not know are left out.
			if (Find(line.id)) List.children.push_back(line);
		}
	}

	// How well an entry answers a search, 0 for not at all. Every word has to be found: in the
	// name scores highest, then the group, then the description, then the name with letters skipped.
	int Match(const Entry& Entry, const std::string& Query)
	{
		std::string label = Lower(Entry.label), kind = Lower(Entry.kind), tip = Lower(Entry.tip);
		std::istringstream words(Lower(Query));
		std::string word;
		int score = 0;
		while (words >> word)
		{
			size_t at = label.find(word);
			if (at != std::string::npos)
				score += at == 0 || label[at - 1] == ' ' ? 50 : 40;
			else if (kind.find(word) != std::string::npos)
				score += 20;
			else if (tip.find(word) != std::string::npos)
				score += 10;
			else
			{
				// A typo or shorthand: its letters in order somewhere in the name.
				size_t next = 0;
				for (char c : word)
				{
					next = label.find(c, next);
					if (next == std::string::npos)
						return 0;
					next++;
				}
				score += 1;
			}
		}
		return score;
	}

	// How many waves each path's challenge lasts, left to right; -1 for the rest of the Expedition.
	std::vector<int> Durations()
	{
		std::vector<int> waves;
		CInstance* room = FindInstance("obj_procedural");
		RValue paths = room ? Local(room, "forge_challenge_array") : RValue();
		if (!paths.IsArray())
			return waves;
		for (const RValue& path : paths.ToVector())
		{
			std::vector<RValue> fields = path.IsArray() ? path.ToVector() : std::vector<RValue>();
			waves.push_back(fields.size() > 1 ? static_cast<int>(ToNumber(fields[1])) : -1);
		}
		return waves;
	}

	struct Forge : Feature
	{
		// The top list; its lines come first to last.
		Node root;

		// What to do when no path offers a wanted line.
		enum Fallback { Random, Wait, Left, Middle, Right, Longest, FALLBACKS };
		int fallback = Random;
		// Leave the choice to the player rather than take a path with an avoided line.
		bool never_avoided = false;

		// The lists before the last load or import, for taking it back.
		Node before;
		bool can_undo = false;
		std::string share_note;
		char save_name[40] = "";
		// The saved lists found on disk, and the one chosen.
		std::vector<std::string> saved;
		int saved_at = -1;
		bool sure_delete = false;
		// Whether the disk has been read for saved lists yet.
		bool looked = false;

		static std::filesystem::path SavedFolder()
		{
			return std::filesystem::path(ModFolder()) / "SlormSuite.forge";
		}

		void FindSaved()
		{
			std::string chosen = saved_at >= 0 && saved_at < static_cast<int>(saved.size()) ? saved[saved_at] : "";
			saved.clear();
			std::error_code error;
			for (const auto& file : std::filesystem::directory_iterator(SavedFolder(), error))
				if (file.path().extension() == ".txt") saved.push_back(file.path().stem().string());
			std::sort(saved.begin(), saved.end());
			saved_at = -1;
			for (size_t i = 0; i < saved.size(); i++)
				if (saved[i] == chosen) saved_at = static_cast<int>(i);
		}

		// Replaces the lists with packed text; false when it is not a list.
		bool Replace(const std::string& Text)
		{
			size_t start = Text.find_first_not_of(" \t\r\n");
			size_t length = strlen(PACKED);
			if (start == std::string::npos || Text.compare(start, length, PACKED) != 0)
				return false;
			Node read;
			const char* at = Text.c_str() + start + length;
			if (!Unpack(at, read, 0))
				return false;
			before = root;
			can_undo = true;
			root.children = read.children;
			return true;
		}

		bool DrawSharing()
		{
			bool changed = false;
			ImGui::SeparatorText("Save and Share");
			if (!looked)
			{
				looked = true;
				FindSaved();
			}

			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
			ImGui::InputTextWithHint("##name", "name to save as", save_name, sizeof(save_name));
			ImGui::SameLine();
			std::string name = save_name;
			for (char& c : name)
				if (!isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '_') c = '_';
			while (!name.empty() && name.back() == ' ') name.pop_back();
			ImGui::BeginDisabled(name.empty());
			if (ImGui::Button("Save"))
			{
				std::error_code error;
				std::filesystem::create_directories(SavedFolder(), error);
				std::ofstream file(SavedFolder() / (name + ".txt"));
				file << PACKED << ' ' << Pack(root) << '\n';
				share_note = file ? "Saved as " + name + "." : "Could not write the file.";
				file.close();
				FindSaved();
			}
			ImGui::EndDisabled();
			Tip("Saves the lists under this name, over any saved before under it.\nThe files are in SlormSuite.forge, next to the mod.");

			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
			if (ImGui::BeginCombo("##saved", saved_at >= 0 ? saved[saved_at].c_str() : saved.empty() ? "nothing saved" : "saved lists"))
			{
				for (int i = 0; i < static_cast<int>(saved.size()); i++)
					if (ImGui::Selectable(saved[i].c_str(), saved_at == i))
					{
						saved_at = i;
						sure_delete = false;
					}
				ImGui::EndCombo();
			}
			ImGui::BeginDisabled(saved_at < 0);
			ImGui::SameLine();
			if (ImGui::Button("Load"))
			{
				std::ifstream file(SavedFolder() / (saved[saved_at] + ".txt"));
				std::stringstream text;
				text << file.rdbuf();
				changed = Replace(text.str());
				share_note = changed ? "Loaded " + saved[saved_at] + "." : "That file holds no lists.";
			}
			Tip("Replaces the lists with the saved ones.");
			ImGui::SameLine();
			if (ImGui::Button(sure_delete ? "Really delete?" : "Delete"))
			{
				if (sure_delete)
				{
					std::error_code error;
					std::filesystem::remove(SavedFolder() / (saved[saved_at] + ".txt"), error);
					share_note = "Deleted " + saved[saved_at] + ".";
					FindSaved();
				}
				sure_delete = !sure_delete;
			}
			ImGui::EndDisabled();

			if (ImGui::Button("Export"))
			{
				ImGui::SetClipboardText((std::string(PACKED) + ' ' + Pack(root)).c_str());
				share_note = "Copied to the clipboard.";
			}
			Tip("Copies the lists to the clipboard as one line of text, to send to someone or keep.");
			ImGui::SameLine();
			if (ImGui::Button("Import"))
			{
				const char* text = ImGui::GetClipboardText();
				changed = text && Replace(text);
				share_note = changed ? "Imported from the clipboard." : "The clipboard holds no lists.";
			}
			Tip("Replaces the lists with the ones pasted from the clipboard, as an export copied them.");
			if (can_undo)
			{
				ImGui::SameLine();
				if (ImGui::Button("Undo"))
				{
					std::swap(root, before);
					share_note = "Back to the lists from before.";
					changed = true;
				}
				Tip("Swaps back to the lists from before the last load or import.");
			}
			if (!share_note.empty()) ImGui::TextDisabled("%s", share_note.c_str());
			return changed;
		}

		// What the overlay asked for this frame, done once the tree has been drawn.
		enum class Do { Nothing, Remove, Before, Into, Add, AddList };
		Do todo = Do::Nothing;
		int todo_from = 0, todo_to = 0;

		Forge() : Feature("Auto pick Great Forge paths", "forge",
			"Each wave, takes the path offering what stands highest in your lists.\nWhen none of the three is listed, takes one at random.")
		{
			tab = "Forge";
			under = "Farmer";
			appearance = true;
			root.children.push_back(List("Loot", { 1061, 1051, 1010 }));
			root.children.push_back(List("Spirits", { 1000, 1001, 1002, 1003, 1004, 1005, 1006 }));
		}

		// Seconds the pick is shown on screen before it is made; 0 picks at once.
		float delay = 0;
		// The countdown line: its color, and where its middle sits as a percent of the screen.
		float notice_color[3] = { 1.0f, 0.86f, 0.47f };
		float notice_at[2] = { 50, 10 };
		// The line is shown as an example until then, while its options are being changed.
		ULONGLONG preview_until = 0;

		// Whether the open path menu has been dealt with.
		bool decided = false;
		// The pick waiting out the delay, -1 for none, with when it is due and how it reads on screen.
		int pending = -1;
		ULONGLONG due_at = 0;
		ULONGLONG last_tick = 0;
		// No pick is made while this key is down.
		int hold_key = VK_LCONTROL;
		bool held = false;
		std::string pending_text;

		void Start() override
		{
			PFUNC_YYGMLScript choose = FindScript("scr_ui_menu_forge_choice_choose");
			if (!choose || !AurieSuccess(MmCreateHook(g_ArSelfModule, "SlormSuite forge choose", choose, ChooseHook, reinterpret_cast<PVOID*>(&g_Choose))))
				Log("forge: no script to pick a path, the mod will do nothing");
		}

		// Whether the last choice came from the lists or from the rule for when they offer nothing.
		const char* reason = "list";

		void TakePath(CInstance* Self, int Path)
		{
			RValue result, path(static_cast<double>(Path));
			RValue* arguments[] = { &path };
			g_By = reason;
			ChooseHook(Self, Self, result, 1, arguments);
			g_By = "you";
		}

		// The path to take, counted from the left; -1 leaves it to the player.
		int Choose(const std::vector<std::vector<int>>& Offers)
		{
			auto offered = [&](int Path, const std::vector<const Node*>& Step, bool Avoid)
			{
				for (const Node* line : Step)
					if (line->avoid == Avoid && std::find(Offers[Path].begin(), Offers[Path].end(), line->id) != Offers[Path].end()) return true;
				return false;
			};
			// A single path is no choice: it is taken whatever the lists say.
			if (Offers.size() == 1)
			{
				reason = "only path";
				return 0;
			}
			// A rest whose rewards cannot be read is left to the player rather than taken blind.
			for (const auto& ids : Offers)
				if (ids.size() == 1 && ids[0] == 0) return -1;
			std::vector<int> open;
			for (int i = 0; i < static_cast<int>(Offers.size()); i++) open.push_back(i);
			std::vector<std::vector<const Node*>> steps;
			Flatten(root, steps);
			for (const auto& step : steps)
			{
				std::vector<int> kept, wanted;
				for (int path : open)
					if (!offered(path, step, true)) kept.push_back(path);
				// With every path avoided, the avoiding gives way unless told never to.
				if (!kept.empty()) open = kept;
				else if (never_avoided) return -1;
				for (int path : open)
					if (offered(path, step, false)) wanted.push_back(path);
				if (!wanted.empty())
				{
					reason = "list";
					return wanted[rand() % wanted.size()];
				}
			}
			reason = "nothing listed";
			if (fallback == Wait)
				return -1;
			if (fallback == Left)
				return open.front();
			if (fallback == Middle)
				return open[open.size() / 2];
			if (fallback == Right)
				return open.back();
			if (fallback == Longest)
			{
				// Challenges that never end are not counted; with only those, any path will do.
				std::vector<int> waves = Durations(), longest;
				int most = -1;
				for (int path : open)
				{
					int lasts = path < static_cast<int>(waves.size()) ? waves[path] : -1;
					if (lasts < 0 || lasts < most)
						continue;
					if (lasts > most) longest.clear();
					most = lasts;
					longest.push_back(path);
				}
				if (!longest.empty())
					return longest[rand() % longest.size()];
			}
			return open[rand() % open.size()];
		}

		void Tick(CInstance* Self) override
		{
			// The menu closing ends a pending pick: the player chose first.
			if (ToText(Local(Self, "menu_show")) != "forge" || ToText(Local(Self, "menu_position")) != "open")
			{
				decided = false;
				pending = -1;
				return;
			}
			ULONGLONG now = GetTickCount64();
			// One try per menu, so a refused pick is left to the player.
			if (!decided)
			{
				decided = true;
				std::vector<std::vector<int>> offers = Offers();
				int path = offers.empty() || !g_Choose ? -1 : Choose(offers);
				std::string text;
				for (const auto& ids : offers)
				{
					text += text.empty() ? "" : " | ";
					for (int id : ids) text += std::string(Label(id)) + " (" + std::to_string(id) + ") ";
				}
				Log("forge: offered %s-> %d", text.c_str(), path);
#ifdef SLORM_DEV
				if (CInstance* room = FindInstance("obj_procedural"))
					for (const char* name : { "forge_challenge_array", "forge_reward_array", "forge_selected_super_reward", "forge_wave_rest", "boss_wave" })
						Log("forge: %s = %s", name, Show(Local(room, name), 3, 20).c_str());
#endif
				if (path >= 0)
				{
					pending_text = Describe(offers, path, true);
					pending = path;
					due_at = now + static_cast<ULONGLONG>(delay * 1000);
				}
				last_tick = now;
			}
			if (pending < 0)
				return;
			// With the key held the countdown stands still.
			held = (GetAsyncKeyState(hold_key) & 0x8000) != 0;
			if (held) due_at += now - last_tick;
			last_tick = now;
			if (held || now < due_at)
				return;
			int path = pending;
			pending = -1;
			TakePath(Self, path);
		}

		void Hud() override
		{
			ULONGLONG now = GetTickCount64();
			char text[256];
			if (pending >= 0 && held)
				snprintf(text, sizeof(text), "SlormSuite picks %s once the key is let go", pending_text.c_str());
			else if (pending >= 0 && delay > 0)
				snprintf(text, sizeof(text), "SlormSuite picks %s in %.1f s", pending_text.c_str(), due_at > now ? (due_at - now) / 1000.0 : 0.0);
			else if (now < preview_until)
				snprintf(text, sizeof(text), "SlormSuite picks LEFT: Light Totem, Slorm Quantity in %.1f s", delay);
			else
				return;
			ImDrawList* draw = ImGui::GetForegroundDrawList();
			float size = ImGui::GetFontSize() * 1.5f;
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			float wrap = screen.x * 0.4f;
			ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, wrap, text);
			// Kept on screen whatever the percents say.
			ImVec2 at(screen.x * notice_at[0] / 100 - extent.x / 2, screen.y * notice_at[1] / 100 - extent.y / 2);
			at.x = std::clamp(at.x, 12.0f, (std::max)(12.0f, screen.x - extent.x - 12));
			at.y = std::clamp(at.y, 8.0f, (std::max)(8.0f, screen.y - extent.y - 8));
			draw->AddRectFilled({ at.x - 12, at.y - 8 }, { at.x + extent.x + 12, at.y + extent.y + 8 }, IM_COL32(0, 0, 0, 190), 6);
			draw->AddText(ImGui::GetFont(), size, at, ImGui::GetColorU32(ImVec4(notice_color[0], notice_color[1], notice_color[2], 1)), text, nullptr, wrap);
		}

		void Ask(Do What, int From, int To)
		{
			todo = What;
			todo_from = From;
			todo_to = To;
		}

		// The catalog as menus; the id clicked, or -1.
		int CatalogMenu(const std::set<int>& Taken)
		{
			int picked = -1;
			static char search[48] = "";
			if (ImGui::IsWindowAppearing())
			{
				search[0] = 0;
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
			bool enter = ImGui::InputTextWithHint("##search", "search names and descriptions", search, sizeof(search), ImGuiInputTextFlags_EnterReturnsTrue);
			if (search[0])
			{
				std::vector<std::pair<int, const Entry*>> found;
				for (const Entry& entry : CATALOG)
					if (int score = Match(entry, search)) found.push_back({ score, &entry });
				std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
				if (found.empty())
					ImGui::TextDisabled("Nothing found.");
				if (found.size() > 14) found.resize(14);
				for (const auto& [score, entry] : found)
				{
					bool free = !Taken.count(entry->id);
					ImGui::PushID(entry->id);
					if (ImGui::MenuItem(entry->label, entry->kind, false, free))
						picked = entry->id;
					ImGui::PopID();
					Tip(entry->tip);
				}
				// Enter takes the best one still free.
				if (enter)
				{
					for (const auto& [score, entry] : found)
						if (picked < 0 && !Taken.count(entry->id)) picked = entry->id;
					if (picked >= 0) ImGui::CloseCurrentPopup();
				}
				return picked;
			}
			ImGui::Separator();
			const char* open = nullptr;
			bool shown = false;
			for (const Entry& entry : CATALOG)
			{
				if (!open || strcmp(open, entry.kind) != 0)
				{
					if (shown) ImGui::EndMenu();
					open = entry.kind;
					shown = ImGui::BeginMenu(entry.kind);
				}
				if (!shown)
					continue;
				if (ImGui::MenuItem(entry.label, nullptr, false, !Taken.count(entry.id)))
					picked = entry.id;
				Tip(entry.tip);
			}
			if (shown) ImGui::EndMenu();
			return picked;
		}

		void DropTarget(const Node& Line, bool IsList, float Top, float Bottom)
		{
			if (!ImGui::BeginDragDropTarget())
				return;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("forge"))
			{
				// The lower half of a list's title drops into it, anything else lands above the line.
				bool into = IsList && ImGui::GetMousePos().y > (Top + Bottom) / 2;
				Ask(into ? Do::Into : Do::Before, *static_cast<const int*>(payload->Data), Line.uid);
			}
			ImGui::EndDragDropTarget();
		}

		bool DrawLine(Node& Line, const std::set<int>& Taken)
		{
			bool changed = false;
			ImGui::PushID(Line.uid);
			if (Line.id >= 0)
			{
				if (ImGui::SmallButton("x")) Ask(Do::Remove, Line.uid, 0);
				ImGui::SameLine();
				bool avoid = Line.avoid;
				if (avoid) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
				if (ImGui::SmallButton(avoid ? "avoid" : "want "))
				{
					Line.avoid = !Line.avoid;
					changed = true;
				}
				Tip("want: take a path offering this.\navoid: pass over paths offering this, for every line below.\nPut an avoided entry above what it should outweigh.");
				ImGui::SameLine();
				ImGui::Selectable(Label(Line.id));
				if (avoid) ImGui::PopStyleColor();
				float top = ImGui::GetItemRectMin().y, bottom = ImGui::GetItemRectMax().y;
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("forge", &Line.uid, sizeof(int));
					ImGui::TextUnformatted(Label(Line.id));
					ImGui::EndDragDropSource();
				}
				bool hovered = ImGui::IsItemHovered();
				DropTarget(Line, false, top, bottom);
				if (hovered && !ImGui::GetDragDropPayload())
					if (const Entry* entry = Find(Line.id)) ShowTip(entry->tip);
				ImGui::PopID();
				return changed;
			}

			bool menu = ImGui::SmallButton("...");
			ImGui::SameLine();
			bool open = ImGui::TreeNodeEx("##list", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow, "%s%s", Line.name.c_str(), Line.ordered ? "" : "  (any of these)");
			float top = ImGui::GetItemRectMin().y, bottom = ImGui::GetItemRectMax().y;
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("forge", &Line.uid, sizeof(int));
				ImGui::TextUnformatted(Line.name.c_str());
				ImGui::EndDragDropSource();
			}
			DropTarget(Line, true, top, bottom);

			if (menu) ImGui::OpenPopup("list menu");
			if (ImGui::BeginPopup("list menu"))
			{
				char name[48];
				strncpy_s(name, Line.name.c_str(), _TRUNCATE);
				ImGui::SetNextItemWidth(180);
				if (ImGui::InputText("name", name, sizeof(name)))
				{
					Line.name = name;
					changed = true;
				}
				changed |= ImGui::Checkbox("Top to bottom", &Line.ordered);
				Tip("On: a higher line of this list beats a lower one.\nOff: any line of this list is as good as another.");
				if (ImGui::BeginMenu("Add"))
				{
					int picked = CatalogMenu(Taken);
					if (picked >= 0) Ask(Do::Add, picked, Line.uid);
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Add a list inside")) Ask(Do::AddList, 0, Line.uid);
				if (ImGui::MenuItem("Remove this list")) Ask(Do::Remove, Line.uid, 0);
				ImGui::EndPopup();
			}

			if (open)
			{
				if (Line.children.empty())
					ImGui::TextDisabled("empty");
				for (Node& child : Line.children)
					changed |= DrawLine(child, Taken);
				ImGui::TreePop();
			}
			ImGui::PopID();
			return changed;
		}

		bool Apply()
		{
			Do what = todo;
			todo = Do::Nothing;
			Node moved;
			switch (what)
			{
			case Do::Nothing:
				return false;
			case Do::Remove:
				return Take(root, todo_from, moved);
			case Do::Add:
			case Do::AddList:
			{
				Node* list = Find(root, todo_to);
				if (!list || list->id >= 0)
					return false;
				Node line;
				if (what == Do::Add) line.id = todo_from;
				else line.name = "New list";
				list->children.push_back(line);
				return true;
			}
			default:
			{
				Node* from = Find(root, todo_from);
				// A list cannot go inside itself.
				if (!from || todo_from == todo_to || Find(*from, todo_to) || !Take(root, todo_from, moved))
					return false;
				if (what == Do::Before)
					return PlaceBefore(root, todo_to, moved);
				Node* list = Find(root, todo_to);
				if (!list || list->id >= 0)
					return false;
				list->children.push_back(moved);
				return true;
			}
			}
		}

		bool Draw() override
		{
			bool changed = false;
			std::set<int> taken;
			Used(root, taken);

			ImGui::SeparatorText("Priority");
			if (ImGui::Button("Add list")) Ask(Do::AddList, 0, root.uid);
			ImGui::SameLine();
			if (ImGui::Button("Add entry")) ImGui::OpenPopup("add entry");
			if (ImGui::BeginPopup("add entry"))
			{
				int picked = CatalogMenu(taken);
				if (picked >= 0) Ask(Do::Add, picked, root.uid);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			Tip("Higher lines are picked first.\nA line set to avoid rules out paths offering it for the lines below.\nDrag a line onto another to place it above that one.\nDrag onto the lower half of a list's title to put it inside.\nThe ... button renames a list, adds to it or removes it.");

			if (root.children.empty())
				ImGui::TextDisabled("Nothing listed: every path is picked at random.");
			for (Node& line : root.children)
				changed |= DrawLine(line, taken);
			// Somewhere to drop a line below the last list.
			if (ImGui::GetDragDropPayload())
			{
				ImGui::Selectable("move to the bottom");
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("forge"))
						Ask(Do::Into, *static_cast<const int*>(payload->Data), root.uid);
					ImGui::EndDragDropTarget();
				}
			}
			changed |= Apply();
			changed |= DrawSharing();

			ImGui::SeparatorText("Picking");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			changed |= ImGui::SliderFloat("seconds before picking", &delay, 0, 10, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			Tip("0 picks at once.\nOtherwise the pick is shown on screen for this long first; click a path yourself to overrule it.");
			changed |= KeyButton("hold", &hold_key);
			ImGui::SameLine();
			ImGui::TextUnformatted("do not pick while this key is held");
			Tip("Held, the countdown stands still and the pick is shown on screen.\nHold it before the paths appear to stop an instant pick.");

			ImGui::SeparatorText("When nothing listed is offered");
			static const char* const NAMES[FALLBACKS] = { "Pick at random", "Leave the choice to me", "Always left", "Always middle", "Always right", "Longest duration" };
			static const char* const TIPS[FALLBACKS] = {
				"Takes any of the paths that are not avoided.",
				"Does nothing; the wave waits for your pick.",
				"Takes the leftmost path that is not avoided.",
				"Takes the middle path. With an avoided path left out, or only two on offer, the right one of the two.",
				"Takes the rightmost path that is not avoided.",
				"Takes the path whose challenge lasts the most waves, at random between equals.\nChallenges lasting the rest of the Expedition are passed over; with only those on offer, any is taken.",
			};
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			if (ImGui::BeginCombo("##fallback", NAMES[fallback]))
			{
				for (int i = 0; i < FALLBACKS; i++)
				{
					if (ImGui::Selectable(NAMES[i], fallback == i))
					{
						fallback = i;
						changed = true;
					}
					Tip(TIPS[i]);
				}
				ImGui::EndCombo();
			}
			changed |= ImGui::Checkbox("Never take an avoided path", &never_avoided);
			Tip("On: when every path offers something you avoid, the choice is left to you.\nOff: the least bad one is taken, by the rule above.");

			ImGui::SeparatorText("Picks this run");
			if (g_Picks.empty())
				ImGui::TextDisabled("None yet.");
			else if (ImGui::BeginChild("picks", { ImGui::GetFontSize() * 30, ImGui::GetTextLineHeightWithSpacing() * 8 }, ImGuiChildFlags_Borders))
			{
				// Newest first.
				for (size_t i = g_Picks.size(); i-- > 0;)
				{
					const Pick& pick = g_Picks[i];
					ImGui::TextWrapped("Wave %d  %s  (%s)", pick.wave, pick.text.c_str(), pick.by);
				}
			}
			if (!g_Picks.empty()) ImGui::EndChild();
			return changed;
		}

		bool DrawAppearance() override
		{
			bool changed = false;
			ImGui::SeparatorText("Countdown line");
			ImGui::TextDisabled("Shown while a pick waits: for its seconds, or for the hold key.");
			bool touched = false;
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			changed |= ImGui::SliderFloat("across", &notice_at[0], 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
			touched |= ImGui::IsItemActive() || ImGui::IsItemHovered();
			Tip("Where the middle of the countdown line sits, from the left edge of the screen to the right.");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
			changed |= ImGui::SliderFloat("down", &notice_at[1], 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
			touched |= ImGui::IsItemActive() || ImGui::IsItemHovered();
			Tip("Where the middle of the countdown line sits, from the top of the screen to the bottom.");
			changed |= ImGui::ColorEdit3("color", notice_color, ImGuiColorEditFlags_NoInputs);
			touched |= ImGui::IsItemActive() || ImGui::IsItemHovered() || ImGui::IsPopupOpen("picker", ImGuiPopupFlags_AnyPopupId);
			if (touched) preview_until = GetTickCount64() + 1500;
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			// The saved tree replaces the built-in one, even when it is empty.
			if (Name == "lists")
			{
				root.children.clear();
				return;
			}
			if (Name == "fallback")
			{
				int value = 0;
				if (Words >> value && value >= 0 && value < FALLBACKS) fallback = value;
				return;
			}
			if (Name == "delay")
			{
				float value = 0;
				if (Words >> value && value >= 0 && value <= 10) delay = value;
				return;
			}
			if (Name == "notice_at")
			{
				float x = 0, y = 0;
				if (Words >> x >> y)
				{
					notice_at[0] = std::clamp(x, 0.0f, 100.0f);
					notice_at[1] = std::clamp(y, 0.0f, 100.0f);
				}
				return;
			}
			if (Name == "notice_color")
			{
				float r = 0, g = 0, b = 0;
				if (Words >> r >> g >> b)
				{
					notice_color[0] = std::clamp(r, 0.0f, 1.0f);
					notice_color[1] = std::clamp(g, 0.0f, 1.0f);
					notice_color[2] = std::clamp(b, 0.0f, 1.0f);
				}
				return;
			}
			if (Name == "hold_key")
			{
				int value = 0;
				if (Words >> value && value >= 8 && value <= 254) hold_key = value;
				return;
			}
			if (Name == "never_avoided")
			{
				int value = 0;
				if (Words >> value) never_avoided = value != 0;
				return;
			}
			if (Name != "line")
				return;
			int depth = 0;
			char kind = 0;
			if (!(Words >> depth >> kind))
				return;
			Node* parent = &root;
			for (int i = 0; i < depth && !parent->children.empty() && parent->children.back().id < 0; i++)
				parent = &parent->children.back();
			Node line;
			if (kind == 'i')
			{
				// Entries this version does not know are left out.
				if (!(Words >> line.id) || !Find(line.id))
					return;
				int avoid = 0;
				Words >> avoid;
				line.avoid = avoid != 0;
			}
			else
			{
				int ordered = 1;
				Words >> ordered;
				line.ordered = ordered != 0;
				std::getline(Words >> std::ws, line.name);
			}
			parent->children.push_back(line);
		}

		void SaveLines(std::ostream& File, const Node& From, int Depth)
		{
			for (const Node& line : From.children)
			{
				File << "forge.line " << Depth << ' ';
				if (line.id >= 0) File << "i " << line.id << ' ' << (line.avoid ? 1 : 0) << '\n';
				else File << "g " << (line.ordered ? 1 : 0) << ' ' << line.name << '\n';
				SaveLines(File, line, Depth + 1);
			}
		}

		void Save(std::ostream& File) override
		{
			File << "forge.fallback " << fallback << '\n'
				<< "forge.delay " << delay << '\n'
				<< "forge.notice_at " << notice_at[0] << ' ' << notice_at[1] << '\n'
				<< "forge.notice_color " << notice_color[0] << ' ' << notice_color[1] << ' ' << notice_color[2] << '\n'
				<< "forge.hold_key " << hold_key << '\n'
				<< "forge.never_avoided " << (never_avoided ? 1 : 0) << '\n'
				<< "forge.lists 1\n";
			SaveLines(File, root, 0);
		}
	};
	Forge g_Forge;
}
