// SlormCore.dll: the overlay every mod shares. It owns the frame hook, the window's input and
// the Dear ImGui context, draws one window, and lets mods add tabs to it through an interface.
#include <YYToolkit/YYTK_Shared.hpp>
#include <SlormCore/Overlay.hpp>
#include "Version.hpp"
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
	// Virtual-key code that shows or hides the overlay: Insert.
	int hotkey = 0x2D;
};

struct Entry
{
	std::string name;
	SlormCore::DrawFn draw;
	void* user;
};

static YYTKInterface* g_Yytk = nullptr;
static AurieModule* g_Module = nullptr;
static fs::path g_ConfigPath;
static std::string g_IniPath;
static Appearance g_Appearance;
// Set while the hotkey is being rebound, so the key press does not also toggle the overlay.
static bool g_SuppressToggle = false;
static bool g_Visible = true;
// Guards the lists. Never held while a mod draws: the mod may be holding a lock of its own.
static std::recursive_mutex g_Lock;
static std::vector<Entry> g_Tabs, g_Huds;

static bool g_Ready = false;
static ImGuiContext* g_Imgui = nullptr;
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static HWND g_Window = nullptr;
static WNDPROC g_OriginalProc = nullptr;
// Latched once per frame so the game sees a steady answer.
static bool g_HideMouse = false;
using CursorFn = BOOL(WINAPI*)(LPPOINT);
static CursorFn g_OriginalCursor = nullptr;

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[SlormCore] ") + Format).c_str(), Arguments...);
}

static void LoadConfig()
{
	std::ifstream file(g_ConfigPath);
	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream words(line);
		std::string first;
		if (!(words >> first) || first[0] == '#')
			continue;
		if (first == "visible") { int flag = 1; words >> flag; g_Visible = flag != 0; }
		else if (first == "theme") words >> g_Appearance.theme;
		else if (first == "accent") { int on = 0; words >> on >> g_Appearance.accent[0] >> g_Appearance.accent[1] >> g_Appearance.accent[2]; g_Appearance.custom_accent = on != 0; }
		else if (first == "primary") { int on = 0; words >> on >> g_Appearance.primary[0] >> g_Appearance.primary[1] >> g_Appearance.primary[2]; g_Appearance.custom_primary = on != 0; }
		else if (first == "opacity") words >> g_Appearance.opacity;
		else if (first == "scale") words >> g_Appearance.scale;
		else if (first == "hotkey") { words >> g_Appearance.hotkey; if (g_Appearance.hotkey < 8 || g_Appearance.hotkey > 254) g_Appearance.hotkey = Appearance().hotkey; }
	}
}

static void SaveConfig()
{
	std::ofstream file(g_ConfigPath);
	file << "# Written by the overlay.\n"
		<< "visible " << (g_Visible ? 1 : 0) << '\n'
		<< "theme " << g_Appearance.theme << '\n'
		<< "accent " << (g_Appearance.custom_accent ? 1 : 0) << ' ' << g_Appearance.accent[0] << ' ' << g_Appearance.accent[1] << ' ' << g_Appearance.accent[2] << '\n'
		<< "primary " << (g_Appearance.custom_primary ? 1 : 0) << ' ' << g_Appearance.primary[0] << ' ' << g_Appearance.primary[1] << ' ' << g_Appearance.primary[2] << '\n'
		<< "opacity " << g_Appearance.opacity << '\n'
		<< "scale " << g_Appearance.scale << '\n'
		<< "hotkey " << g_Appearance.hotkey << '\n';
}

// While the cursor is over the overlay, the game is told it is off screen, so nothing behind it hovers.
static BOOL WINAPI HookedCursor(LPPOINT Point)
{
	BOOL ok = g_OriginalCursor(Point);
	if (ok && Point && g_HideMouse)
		Point->x = Point->y = -20000;
	return ok;
}

static BOOL RealCursor(LPPOINT Point)
{
	return g_OriginalCursor ? g_OriginalCursor(Point) : GetCursorPos(Point);
}

static std::string KeyName(int Key)
{
	if (Key >= VK_F1 && Key <= VK_F24)
		return "F" + std::to_string(Key - VK_F1 + 1);
	UINT scan = MapVirtualKeyW(Key, MAPVK_VK_TO_VSC);
	// Navigation keys share scan codes with the numpad unless flagged as extended.
	bool extended = Key == VK_INSERT || Key == VK_DELETE || Key == VK_HOME || Key == VK_END || Key == VK_PRIOR || Key == VK_NEXT
		|| Key == VK_LEFT || Key == VK_RIGHT || Key == VK_UP || Key == VK_DOWN || Key == VK_DIVIDE || Key == VK_RCONTROL || Key == VK_RMENU;
	char name[64] = {};
	if (scan && GetKeyNameTextA(static_cast<LONG>((scan << 16) | (extended ? 1 << 24 : 0)), name, sizeof(name)) > 0)
		return name;
	return "key " + std::to_string(Key);
}

// Returns a key that is down, for rebinding. Mouse buttons are skipped.
static int PressedKey()
{
	for (int key = 8; key <= 254; key++)
	{
		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2)
			continue;
		// Skip the side-neutral modifier codes; the left/right ones report the same press.
		if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)
			continue;
		if (GetAsyncKeyState(key) & 0x8000)
			return key;
	}
	return 0;
}


// A button showing a key; clicking it takes the next key pressed. True when the key changed.
static bool KeyButton(const char* Id, int* Key)
{
	// Which button is taking a key, 0 for none.
	static ImGuiID capturing = 0;
	// A key held when capture starts must be released first, or it would be taken at once.
	static bool armed = false;
	bool changed = false;
	ImGui::PushID(Id);
	ImGuiID id = ImGui::GetID("key");
	if (capturing == id)
	{
		g_SuppressToggle = true;
		ImGui::Button("Press a key (Esc cancels)");
		int key = PressedKey();
		if (!armed)
			armed = key == 0;
		else if (key == VK_ESCAPE)
			capturing = 0;
		else if (key)
		{
			*Key = key;
			capturing = 0;
			changed = true;
		}
	}
	else if (ImGui::Button(KeyName(*Key).c_str(), { ImGui::GetFontSize() * 6, 0 }))
	{
		capturing = id;
		armed = false;
	}
	ImGui::PopID();
	return changed;
}

static const char* THEME_NAMES[] = { "Dark", "Light", "Classic", "Slormancer" };

// Colours every preset derives from one accent.
static void SetAccent(ImVec4 accent)
{
	ImVec4* colors = ImGui::GetStyle().Colors;
	auto with = [&](float alpha) { return ImVec4(accent.x, accent.y, accent.z, alpha); };
	auto dim = [&](float factor) { return ImVec4(accent.x * factor, accent.y * factor, accent.z * factor, 1.0f); };
	colors[ImGuiCol_Button] = with(0.45f);
	colors[ImGuiCol_ButtonHovered] = with(0.75f);
	colors[ImGuiCol_ButtonActive] = with(1.0f);
	colors[ImGuiCol_Header] = with(0.35f);
	colors[ImGuiCol_HeaderHovered] = with(0.65f);
	colors[ImGuiCol_HeaderActive] = with(0.9f);
	colors[ImGuiCol_FrameBgHovered] = with(0.3f);
	colors[ImGuiCol_FrameBgActive] = with(0.5f);
	colors[ImGuiCol_CheckMark] = with(1.0f);
	colors[ImGuiCol_SliderGrab] = with(0.8f);
	colors[ImGuiCol_SliderGrabActive] = with(1.0f);
	colors[ImGuiCol_Tab] = dim(0.4f);
	colors[ImGuiCol_TabHovered] = with(0.8f);
	colors[ImGuiCol_TabSelected] = dim(0.7f);
	colors[ImGuiCol_TitleBgActive] = dim(0.45f);
	colors[ImGuiCol_SeparatorHovered] = with(0.7f);
	colors[ImGuiCol_SeparatorActive] = with(1.0f);
	colors[ImGuiCol_TextSelectedBg] = with(0.4f);
}

// Bars and fields: title bar, unselected tabs, input fields, table headers.
static void SetPrimary(ImVec4 primary)
{
	ImVec4* colors = ImGui::GetStyle().Colors;
	auto shade = [&](float factor, float alpha = 1.0f)
	{
		return ImVec4((std::min)(primary.x * factor, 1.0f), (std::min)(primary.y * factor, 1.0f), (std::min)(primary.z * factor, 1.0f), alpha);
	};
	colors[ImGuiCol_TitleBg] = shade(0.7f);
	colors[ImGuiCol_TitleBgActive] = shade(1.0f);
	colors[ImGuiCol_TitleBgCollapsed] = shade(0.7f, 0.8f);
	colors[ImGuiCol_Tab] = shade(0.7f);
	colors[ImGuiCol_TableHeaderBg] = shade(0.8f);
	colors[ImGuiCol_FrameBg] = shade(0.8f);
	colors[ImGuiCol_FrameBgHovered] = shade(1.15f);
	colors[ImGuiCol_FrameBgActive] = shade(1.4f);
	colors[ImGuiCol_ScrollbarGrab] = shade(1.0f);
	colors[ImGuiCol_ScrollbarGrabHovered] = shade(1.25f);
	colors[ImGuiCol_ScrollbarGrabActive] = shade(1.5f);
}

static void ApplyAppearance()
{
	Appearance& look = g_Appearance;
	look.theme = std::clamp(look.theme, 0, 3);
	look.opacity = std::clamp(look.opacity, 0.3f, 1.0f);
	look.scale = std::clamp(look.scale, 0.75f, 2.0f);

	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();
	style.TabRounding = 0;
	if (look.theme == 1) ImGui::StyleColorsLight();
	else if (look.theme == 2) ImGui::StyleColorsClassic();
	else ImGui::StyleColorsDark();

	ImVec4* colors = style.Colors;
	if (look.theme == 3)
	{
		// The game's panels: near-black brown with dull gold edges.
		style.WindowRounding = 0;
		style.FrameRounding = 0;
		style.WindowBorderSize = 2;
		colors[ImGuiCol_WindowBg] = { 0.07f, 0.06f, 0.05f, 1.0f };
		colors[ImGuiCol_PopupBg] = { 0.09f, 0.08f, 0.07f, 1.0f };
		colors[ImGuiCol_ChildBg] = { 0.05f, 0.045f, 0.04f, 1.0f };
		colors[ImGuiCol_Border] = { 0.36f, 0.32f, 0.24f, 1.0f };
		colors[ImGuiCol_Separator] = { 0.36f, 0.32f, 0.24f, 1.0f };
		colors[ImGuiCol_FrameBg] = { 0.15f, 0.13f, 0.11f, 1.0f };
		colors[ImGuiCol_TitleBg] = { 0.10f, 0.09f, 0.07f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = { 0.10f, 0.09f, 0.07f, 0.8f };
		colors[ImGuiCol_TableHeaderBg] = { 0.14f, 0.12f, 0.10f, 1.0f };
		colors[ImGuiCol_TableRowBgAlt] = { 1.0f, 0.95f, 0.8f, 0.04f };
		colors[ImGuiCol_Text] = { 0.92f, 0.89f, 0.80f, 1.0f };
		colors[ImGuiCol_TextDisabled] = { 0.54f, 0.52f, 0.46f, 1.0f };
		SetAccent({ 0.78f, 0.62f, 0.25f, 1.0f });
	}
	if (look.custom_accent)
		SetAccent({ look.accent[0], look.accent[1], look.accent[2], 1.0f });
	if (look.custom_primary)
		SetPrimary({ look.primary[0], look.primary[1], look.primary[2], 1.0f });

	colors[ImGuiCol_WindowBg].w = look.opacity;
	colors[ImGuiCol_PopupBg].w = (std::max)(look.opacity, 0.9f);
	ImGui::GetIO().FontGlobalScale = look.scale;
}


// A slider that goes back to its default on a right click or a double click.
static bool Slider(const char* Label, float* Value, float Low, float High, const char* Format, float Default)
{
	bool changed = ImGui::SliderFloat(Label, Value, Low, High, Format);
	if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
	{
		// Let go of the slider, or the held button drags it straight back.
		ImGui::ClearActiveID();
		changed |= *Value != Default;
		*Value = Default;
	}
	return changed;
}

static bool DrawOptions()
{
	bool changed = false;
	ImGui::SeparatorText("Appearance");
	bool restyle = false;
	// Applied on release: resizing the text under the cursor makes the slider jump.
	static float scale = 0;
	ImGui::SetNextItemWidth(160);
	restyle |= ImGui::Combo("Theme", &g_Appearance.theme, THEME_NAMES, 4);
	restyle |= ImGui::Checkbox("Custom accent", &g_Appearance.custom_accent);
	if (g_Appearance.custom_accent)
	{
		ImGui::SameLine();
		restyle |= ImGui::ColorEdit3("##accent", g_Appearance.accent, ImGuiColorEditFlags_NoInputs);
	}
	restyle |= ImGui::Checkbox("Custom primary", &g_Appearance.custom_primary);
	if (g_Appearance.custom_primary)
	{
		ImGui::SameLine();
		restyle |= ImGui::ColorEdit3("##primary", g_Appearance.primary, ImGuiColorEditFlags_NoInputs);
	}
	ImGui::SetNextItemWidth(160);
	restyle |= Slider("Opacity", &g_Appearance.opacity, 0.3f, 1.0f, "%.2f", Appearance().opacity);
	ImGui::SetNextItemWidth(160);
	if (!ImGui::IsAnyItemActive() || scale == 0)
		scale = g_Appearance.scale;
	bool reset_scale = Slider("Text size", &scale, 0.75f, 2.0f, "%.2f", Appearance().scale) && !ImGui::IsItemActive();
	if (ImGui::IsItemDeactivatedAfterEdit() || reset_scale)
	{
		g_Appearance.scale = scale;
		restyle = true;
	}
	if (ImGui::Button("Reset appearance"))
	{
		int hotkey = g_Appearance.hotkey;
		g_Appearance = Appearance();
		g_Appearance.hotkey = hotkey;
		scale = 0;
		restyle = true;
	}
	if (restyle)
		ApplyAppearance();

	ImGui::SeparatorText("Hotkeys");
	changed |= KeyButton("overlay", &g_Appearance.hotkey);
	ImGui::SameLine();
	ImGui::TextUnformatted("shows or hides the overlay");

	ImGui::SeparatorText("About");
	ImGui::Text("SlormCore v" SLORMCORE_VERSION ", Dear ImGui %s", IMGUI_VERSION);
	ImGui::TextLinkOpenURL("GitHub", "https://github.com/crashv0/SlormMods");
	return changed || restyle;
}

static void DrawWindow()
{
	bool open = true;
	ImGui::SetNextWindowPos({ 40, 40 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.8f });
	if (!ImGui::Begin("Slormancer Mods###SlormCore", &open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	bool changed = false;
	if (ImGui::BeginTabBar("tabs"))
	{
		std::vector<Entry> tabs;
		{
			std::lock_guard guard(g_Lock);
			tabs = g_Tabs;
		}
		for (size_t i = 0; i < tabs.size(); i++)
		{
			// The id keeps two mods' tabs of one name apart.
			if (!ImGui::BeginTabItem((tabs[i].name + "###tab" + std::to_string(i)).c_str()))
				continue;
			ImGui::PushID(static_cast<int>(i));
			tabs[i].draw(tabs[i].user);
			ImGui::PopID();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Options"))
		{
			if (tabs.empty())
				ImGui::TextDisabled("No installed mod uses the overlay yet.");
			changed |= DrawOptions();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
	if (!open)
	{
		g_Visible = false;
		changed = true;
	}
	if (changed)
		SaveConfig();
}

static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	static bool saw_char = false;
	bool mouse = Message >= WM_MOUSEFIRST && Message <= WM_MOUSELAST;
	bool keyboard = Message == WM_KEYDOWN || Message == WM_KEYUP || Message == WM_CHAR;
	if (Message == WM_CHAR)
		saw_char = true;

	if (g_Ready && g_Visible)
	{
		// Mouse position and buttons are polled per frame; wheel and keys come from here.
		if (!mouse || Message == WM_MOUSEWHEEL)
			ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam);

		ImGuiIO& io = ImGui::GetIO();
		// Without WM_CHAR, type digits from key presses.
		if (Message == WM_KEYDOWN && io.WantTextInput && !saw_char)
		{
			if (WParam >= '0' && WParam <= '9') io.AddInputCharacter(static_cast<unsigned>(WParam));
			else if (WParam >= VK_NUMPAD0 && WParam <= VK_NUMPAD9) io.AddInputCharacter(static_cast<unsigned>('0' + WParam - VK_NUMPAD0));
			else if (WParam == VK_OEM_PERIOD || WParam == VK_DECIMAL) io.AddInputCharacter('.');
			else if (WParam == VK_OEM_MINUS || WParam == VK_SUBTRACT) io.AddInputCharacter('-');
		}

		// Keep clicks and typing on the overlay away from the game.
		if ((mouse && io.WantCaptureMouse) || (keyboard && io.WantTextInput))
			return 0;
	}
	return CallWindowProcW(g_OriginalProc, Window, Message, WParam, LParam);
}

static void Hotkey()
{
	static bool key_was_down = false;
	bool key_down = (GetAsyncKeyState(g_Appearance.hotkey) & 0x8000) != 0;
	if (g_SuppressToggle)
	{
		// Wait for the newly bound key to be released.
		if (!key_down)
			g_SuppressToggle = false;
		key_down = true;
	}
	else if (GetForegroundWindow() == g_Window && key_down && !key_was_down)
	{
		g_Visible = !g_Visible;
		SaveConfig();
	}
	key_was_down = key_down;
}

static void Frame(IDXGISwapChain* swapchain, UINT flags)
{
	if (!swapchain || (flags & DXGI_PRESENT_TEST))
		return;

	if (!g_Ready)
	{
		DXGI_SWAP_CHAIN_DESC description = {};
		if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&g_Device))) || FAILED(swapchain->GetDesc(&description)))
			return;
		g_Device->GetImmediateContext(&g_Context);

		g_Imgui = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = g_IniPath.c_str();
		ApplyAppearance();
		g_Window = description.OutputWindow;
		ImGui_ImplWin32_Init(g_Window);
		g_OriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
		ImGui_ImplDX11_Init(g_Device, g_Context);
		g_Ready = true;
		Log("overlay ready");
	}

	ID3D11Texture2D* buffer = nullptr;
	ID3D11RenderTargetView* view = nullptr;
	if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
		return;
	D3D11_TEXTURE2D_DESC size = {};
	buffer->GetDesc(&size);
	HRESULT created = g_Device->CreateRenderTargetView(buffer, nullptr, &view);
	buffer->Release();
	if (FAILED(created))
		return;

	Hotkey();
	g_HideMouse = false;
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	// The mouse is polled: window messages do not reliably carry it in this game.
	// The back buffer can be a different size from the window, so scale to it.
	ImGuiIO& io = ImGui::GetIO();
	RECT client = {};
	POINT cursor = {};
	GetClientRect(g_Window, &client);
	RealCursor(&cursor);
	ScreenToClient(g_Window, &cursor);
	io.DisplaySize = { static_cast<float>(size.Width), static_cast<float>(size.Height) };
	if (client.right > 0 && client.bottom > 0 && GetForegroundWindow() == g_Window)
	{
		io.AddMousePosEvent(cursor.x * static_cast<float>(size.Width) / client.right, cursor.y * static_cast<float>(size.Height) / client.bottom);
		io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
	}
	ImGui::NewFrame();
	// The game draws its own cursor under the overlay.
	io.MouseDrawCursor = io.WantCaptureMouse;
	if (g_Visible)
		DrawWindow();
	std::vector<Entry> huds;
	{
		std::lock_guard guard(g_Lock);
		huds = g_Huds;
	}
	for (Entry& hud : huds)
		hud.draw(hud.user);
	ImGui::Render();
	g_HideMouse = io.WantCaptureMouse;

	ID3D11RenderTargetView* old_view = nullptr;
	ID3D11DepthStencilView* old_depth = nullptr;
	g_Context->OMGetRenderTargets(1, &old_view, &old_depth);
	g_Context->OMSetRenderTargets(1, &view, nullptr);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	g_Context->OMSetRenderTargets(1, &old_view, old_depth);
	if (old_view) old_view->Release();
	if (old_depth) old_depth->Release();
	view->Release();
}

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_OriginalPresent = nullptr;

static HRESULT WINAPI HookedPresent(IDXGISwapChain* Swapchain, UINT Sync, UINT Flags)
{
	Frame(Swapchain, Flags);
	return g_OriginalPresent(Swapchain, Sync, Flags);
}

// YYToolkit 5.0.0c never installs its own Present hook, so EVENT_FRAME does not fire.
static bool Install()
{
	RValue info = g_Yytk->CallBuiltin("os_get_info", {});
	RValue chain = g_Yytk->CallBuiltin("ds_map_find_value", { info, RValue("video_d3d11_swapchain") });
	g_Yytk->CallBuiltin("ds_map_destroy", { info });

	IDXGISwapChain* swapchain = static_cast<IDXGISwapChain*>(chain.m_Pointer);
	if (!swapchain)
		return false;
	PVOID* table = *reinterpret_cast<PVOID**>(swapchain);
	AurieStatus status = MmCreateHook(g_Module, "SlormCore Present", table[8], HookedPresent, reinterpret_cast<PVOID*>(&g_OriginalPresent));
	if (!AurieSuccess(status) || !g_OriginalPresent)
		return false;

	status = MmCreateHook(g_Module, "SlormCore GetCursorPos", GetCursorPos, HookedCursor, reinterpret_cast<PVOID*>(&g_OriginalCursor));
	if (!AurieSuccess(status))
		DbgPrintEx(LOG_SEVERITY_WARNING, "[SlormCore] cursor hook failed: %s", AurieStatusToString(status));
	return true;
}

struct Host : SlormCore::OverlayInterface
{
	AurieStatus Create() override { return AURIE_SUCCESS; }
	void Destroy() override {}
	void QueryVersion(short& Major, short& Minor, short& Patch) override
	{
		Major = SLORMCORE_MAJOR;
		Minor = SLORMCORE_MINOR;
		Patch = SLORMCORE_PATCH;
	}

	bool SameImGui(const char* Version, size_t Io, size_t Style, size_t Vec2, size_t Vec4, size_t Vertex, size_t Index) override
	{
		bool same = Version && !strcmp(Version, IMGUI_VERSION) && Io == sizeof(ImGuiIO) && Style == sizeof(ImGuiStyle)
			&& Vec2 == sizeof(ImVec2) && Vec4 == sizeof(ImVec4) && Vertex == sizeof(ImDrawVert) && Index == sizeof(ImDrawIdx);
		if (!same)
			DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormCore] a mod was built with Dear ImGui %s, not %s; its tabs stay empty", Version ? Version : "?", IMGUI_VERSION);
		return same;
	}

	ImGuiContext* Context() override { return g_Ready ? g_Imgui : nullptr; }

	void Allocators(ImGuiMemAllocFunc* Alloc, ImGuiMemFreeFunc* Free, void** User) override
	{
		ImGui::GetAllocatorFunctions(Alloc, Free, User);
	}

	bool AddTab(const char* Name, SlormCore::DrawFn Draw, void* User) override
	{
		if (!Name || !Name[0] || !Draw)
			return false;
		std::lock_guard guard(g_Lock);
		g_Tabs.push_back({ Name, Draw, User });
		Log("tab added: %s", Name);
		return true;
	}

	bool AddHud(SlormCore::DrawFn Draw, void* User) override
	{
		if (!Draw)
			return false;
		std::lock_guard guard(g_Lock);
		g_Huds.push_back({ "", Draw, User });
		return true;
	}

	void Remove(SlormCore::DrawFn Draw, void* User) override
	{
		std::lock_guard guard(g_Lock);
		auto same = [&](const Entry& Of) { return Of.draw == Draw && Of.user == User; };
		std::erase_if(g_Tabs, same);
		std::erase_if(g_Huds, same);
	}

	bool Visible() override { return g_Visible; }

	void SetVisible(bool Visible) override
	{
		g_Visible = Visible;
		SaveConfig();
	}

	bool WantsMouse() override { return g_Ready && g_Visible && ImGui::GetIO().WantCaptureMouse; }
	bool WantsKeys() override { return g_Ready && g_Visible && ImGui::GetIO().WantTextInput; }
};

static Host g_Host;

// Runs for the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	CCode* code = std::get<2>(Event.Arguments());
	if (code != step_code || !code)
	{
		if (step_code || !code || !code->GetName() || std::string_view(code->GetName()) != "gml_Object_obj_ui_next_gen_Step_0")
			return;
		step_code = code;
	}

	// The swap chain only exists once the game is running.
	static bool installed = false;
	if (!installed)
	{
		installed = true;
		Log("frame hook %s", Install() ? "installed" : "FAILED");
	}

	// Input aimed at the overlay must not reach the game's UI.
	if (g_Host.WantsMouse())
	{
		g_Yytk->CallBuiltin("mouse_clear", { RValue(1.0) });
		g_Yytk->CallBuiltin("mouse_clear", { RValue(2.0) });
	}
	if (g_Host.WantsKeys())
		g_Yytk->CallBuiltin("io_clear", {});
}

// Runs as the DLL is loaded, before any mod's ModuleInitialize, so every mod finds the interface.
EXPORTED AurieStatus ModuleEntrypoint(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	UNREFERENCED_PARAMETER(ModulePath);
	return ObCreateInterface(Module, &g_Host, SlormCore::OVERLAY_INTERFACE);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Module = Module;
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_ConfigPath = ModulePath.parent_path() / "SlormCore.txt";
	g_IniPath = (ModulePath.parent_path() / "SlormCore.layout.ini").string();
	LoadConfig();

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormCore] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMCORE_VERSION " loaded, key %d shows or hides the overlay", g_Appearance.hotkey);
	return AURIE_SUCCESS;
}
