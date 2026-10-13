#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

extern YYTK::YYTKInterface* g_Yytk;

template <typename... Args>
void Log(const char* Format, Args... Arguments)
{
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, (std::string("[SlormReslormitizer] ") + Format).c_str(), Arguments...);
}

// Numbers the game keeps as text count too; 0 for anything else.
double ToNumber(const YYTK::RValue& Value);
std::string ToText(const YYTK::RValue& Value);
// -1 for anything but an array.
int Length(const YYTK::RValue& Array);
YYTK::RValue At(const YYTK::RValue& Array, int Index);
void Put(const YYTK::RValue& Array, int Index, const YYTK::RValue& Value);
// A global, or failing that a variable of the UI object.
YYTK::RValue Variable(YYTK::CInstance* Self, const char* Name);
// A game script by its name without the gml_Script_ prefix; null when the game has none.
YYTK::PFUNC_YYGMLScript FindScript(const char* Name);
YYTK::RValue CallScript(YYTK::PFUNC_YYGMLScript Script, YYTK::CInstance* Self, std::vector<YYTK::RValue> Arguments = {});

// Overlay appearance, saved with the rest of the config.
struct Appearance
{
	int theme = 0;
	bool custom_accent = false;
	float accent[3] = { 0.78f, 0.62f, 0.25f };
	// Title bar, tabs, input fields and table headers.
	bool custom_primary = false;
	float primary[3] = { 0.16f, 0.29f, 0.48f };
	float opacity = 0.95f;
	float scale = 1.0f;
	// Virtual-key code that shows or hides the overlay.
	int hotkey = 0x77;
};

// Material ids as the game numbers them: 0-4 are the Slormelines.
enum Material { FATE = 5, NEGATION = 6, TRUE_POTENTIAL = 7, APTITUDE = 8, HARMONY = 9, VIRTUE = 10, MATERIAL_COUNT = 11 };
const char* MaterialName(int Id);
// Slormites come in tiers 1 to 15.
constexpr int SLORMITE_TIERS = 15;
const char* SlormiteName(int Tier);

// What the Slormitizer shows for what is in it.
struct Odds
{
	double chance = 0, critical = 0, critical_potential = 0, twice = 0, failure = 0, cost = 0;
};

struct Loaded
{
	int slot = 0;
	int id = 0;
	double count = 0;
};

// The Slormitizer as last read from the game.
struct Machine
{
	// Rick Manalan's panel is open.
	bool open = false;
	bool has_item = false;
	int level = 0;
	double potential = 0;
	// 0 without a slormite.
	int tier = 0;
	double slormites = 0;
	std::vector<Loaded> loaded;
	Odds shown;
	// The same without Slormandrites of Fate, Aptitude, Harmony and Virtue.
	Odds base;
	// The chance of success before failure cuts it, without those four.
	double raw_chance = 0;
	double stock[MATERIAL_COUNT] = {};
	double slormite_stock[SLORMITE_TIERS + 1] = {};
	double gold = 0;
};

// The Slormandrites to load for the targets.
struct Plan
{
	bool possible = false;
	std::string why;
	int count[MATERIAL_COUNT] = {};
	Odds odds;
	// Goldus the Slormandrites would cost to buy.
	double value = 0;
};

struct Settings
{
	int chance = 100;
	int critical = 100;
	int twice = 100;
	int failure = 0;
	// Repeating stops at this reinforce level.
	int until_level = 15;
	// The slormite tier to use; 0 for whichever is in the machine.
	int slormite = 0;
	// Put more of them in when the machine runs out.
	bool reload = true;
	// With none left, merge lower slormites into it.
	bool merge_up = false;
};

// Guards everything below; the game step writes it, the overlay reads and edits it.
extern std::recursive_mutex g_Lock;
extern Appearance g_Appearance;
// Set while the hotkey is being rebound, so the key press does not also toggle the overlay.
extern bool g_SuppressToggle;
// With Rick's panel open the overlay shows unless hidden with the hotkey (g_Visible);
// elsewhere it shows only when opened with the hotkey (g_Manual).
extern bool g_Visible;
extern bool g_Manual;
extern Settings g_Settings;
extern Machine g_Machine;
extern Plan g_Plan;
extern bool g_Running;
// Asked for by the overlay, done on the next step.
extern bool g_WantLoad, g_WantOnce, g_WantRun, g_WantStop, g_WantMerge;
extern std::string g_Status;
extern std::deque<std::string> g_Notes;

inline bool OverlayShown()
{
	return g_Machine.open ? g_Visible : g_Manual;
}
// The loaded Slormandrites match the plan.
bool PlanLoaded();
// There is enough in the machine, or chosen, to start.
bool CanStart();
// What is shown meets the targets.
bool TargetsMet();
void SaveConfig();

bool OverlayInstall(YYTK::YYTKInterface* Yytk);
bool OverlayWantsMouse();
bool OverlayWantsKeys();
void OverlaySetIniPath(const std::string& Path);

#ifdef SLORM_DEV
// Dev builds only: logs how the game runs the Slormitizer and samples its odds.
constexpr int PROBE_KEY = 0x79;
void ProbeStart(Aurie::AurieModule* Module, const std::string& Folder);
void ProbeTick(YYTK::CInstance* Self);
#endif
