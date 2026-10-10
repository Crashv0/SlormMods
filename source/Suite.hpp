#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

// Overlay appearance, saved with the rest of the config.
struct Appearance
{
	int theme = 2;
	bool custom_accent = false;
	float accent[3] = { 0.78f, 0.62f, 0.25f };
	// Title bar, tabs, input fields and table headers.
	bool custom_primary = false;
	float primary[3] = { 0.16f, 0.29f, 0.48f };
	float opacity = 1.0f;
	float scale = 1.0f;
	// Virtual-key code that shows or hides the overlay.
	int hotkey = 0x76;
};

// Guards everything below; the game step writes it, the overlay reads and edits it.
extern std::recursive_mutex g_Lock;

extern Appearance g_Appearance;
// Set while the hotkey is being rebound, so the key press does not also toggle the overlay.
extern bool g_SuppressToggle;
extern bool g_Visible;

void SaveConfig();
// Where the mod's files sit: the folder of its DLL.
std::string ModFolder();
// A button showing a key; clicking it takes the next key pressed. True when the key changed.
bool KeyButton(const char* Id, int* Key);

// One mod of the suite. Each lives in its own file under features/ and adds itself to the list.
struct Feature
{
	// Shown in the overlay.
	const char* name;
	// Starts its lines in the config; no spaces.
	const char* key;
	const char* summary;
	// Names its own tab in the overlay; without one it is a line of the Mods tab.
	const char* tab = nullptr;
	bool enabled = false;

	Feature(const char* Name, const char* Key, const char* Summary);
	virtual ~Feature() = default;
	// Once at load, after the game's scripts can be looked up.
	virtual void Start() {}
	// Each step of the game's UI object while enabled.
	virtual void Tick(YYTK::CInstance* Self) {}
	// Its settings in the overlay, shown while enabled or in its own tab; true when something to save changed.
	virtual bool Draw() { return false; }
	// Set by a feature with looks to choose: it gets a tab of its own under Options, drawn by this.
	bool appearance = false;
	virtual bool DrawAppearance() { return false; }
	// Drawn over the game each frame while enabled, with the overlay shown or not.
	virtual void Hud() {}
	// One "key.Name ..." line of the config.
	virtual void Load(const std::string& Name, std::istringstream& Words) {}
	// Writes its settings as "key.name value" lines.
	virtual void Save(std::ostream& File) {}
};
std::vector<Feature*>& Features();

// The game, as the features reach it.
extern YYTK::YYTKInterface* g_Yytk;
YYTK::RValue Global(const char* Name);
YYTK::RValue Local(YYTK::CInstance* Self, const char* Name);
// Numbers the game keeps as text count too; 0 for anything else.
double ToNumber(const YYTK::RValue& Value);
std::string ToText(const YYTK::RValue& Value);
// The room the game is in, as it names it: "rm_the_hub" in town.
std::string RoomName();
// The first instance of an object by name; null when the room has none.
YYTK::CInstance* FindInstance(const char* Object);
// A game script by its name without the gml_Script_ prefix; null when the game has none.
YYTK::PFUNC_YYGMLScript FindScript(const char* Name);
// Runs it as the UI object would.
YYTK::RValue CallScript(YYTK::PFUNC_YYGMLScript Script, YYTK::CInstance* Self, std::vector<YYTK::RValue> Arguments = {});

template <typename... Args>
void Log(const char* Format, Args... Arguments)
{
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, (std::string("[SlormSuite] ") + Format).c_str(), Arguments...);
}

#ifdef SLORM_DEV
// Dev builds only: dumps of the game's variables and a trace of its script calls.
struct Dev
{
	bool want_dump = false;
	std::string dump_note;
	// Names holding any of these words are printed in full; comma separated.
	std::string dump_words = "menu,launch,room,run,floor,breach,portal,temple,forge,hub,citizen,mission,level,chest,challenge,choice,wave,war_,reward,procedural,controller";
	std::string result;
	bool trace = true;
};
extern Dev g_Dev;
// Dumps without moving the mouse.
constexpr int DUMP_KEY = 0x78;
void DevStart(const std::string& Folder);
void DevTick(YYTK::CInstance* Self);
void DevEvent(YYTK::CCode* Code);
std::string Show(const YYTK::RValue& Value, int Depth, size_t Limit);
#endif

inline std::string Lower(std::string Text)
{
	for (char& c : Text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return Text;
}

bool OverlayInstall(YYTK::YYTKInterface* Yytk);
bool OverlayWantsMouse();
bool OverlayWantsKeys();
void OverlaySetIniPath(const std::string& Path);
