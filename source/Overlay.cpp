#include "Suite.hpp"
#include "Version.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
using namespace YYTK;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool g_Ready = false;
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static std::string g_IniPath;
static HWND g_Window = nullptr;
static WNDPROC g_OriginalProc = nullptr;
// Latched once per frame so the game sees a steady answer.
static bool g_HideMouse = false;
using CursorFn = BOOL(WINAPI*)(LPPOINT);
static CursorFn g_OriginalCursor = nullptr;

static BOOL RealCursor(LPPOINT Point)
{
	return g_OriginalCursor ? g_OriginalCursor(Point) : GetCursorPos(Point);
}

// While the cursor is over the overlay, the game is told it is off screen, so nothing behind it hovers.
static BOOL WINAPI HookedCursor(LPPOINT Point)
{
	BOOL ok = g_OriginalCursor(Point);
	if (ok && Point && g_HideMouse)
		Point->x = Point->y = -20000;
	return ok;
}
static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);

void OverlaySetIniPath(const std::string& Path)
{
	g_IniPath = Path;
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
bool KeyButton(const char* Id, int* Key)
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
		ImGui::Button("Press a key (Esc cancels)", { 220, 0 });
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
	else if (ImGui::Button(KeyName(*Key).c_str(), { 220, 0 }))
	{
		capturing = id;
		armed = false;
	}
	ImGui::PopID();
	return changed;
}

// About links. An empty address shows the name greyed out until there is a page to point at.
static const char* LINK_AUTHOR = "https://discord.com/users/187106829984202761";
static const char* LINK_NEXUS = "";
static const char* LINK_GITHUB = "";

static void Link(const char* Label, const char* Url)
{
	if (Url[0])
	{
		ImGui::TextLinkOpenURL(Label, Url);
		return;
	}
	ImGui::BeginDisabled();
	ImGui::TextLink(Label);
	ImGui::EndDisabled();
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

static bool DrawTweaks()
{
	bool changed = false;
	if (Features().empty())
		ImGui::TextDisabled("No tweaks yet.");
	for (Feature* feature : Features())
	{
		ImGui::PushID(feature->key);
		changed |= ImGui::Checkbox(feature->name, &feature->enabled);
		if (ImGui::IsItemHovered() && feature->summary[0])
			ImGui::SetTooltip("%s", feature->summary);
		if (feature->enabled)
		{
			ImGui::Indent();
			changed |= feature->Draw();
			ImGui::Unindent();
		}
		ImGui::PopID();
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
	ImGui::TextUnformatted("SlormSuite v" SLORMSUITE_VERSION " by");
	ImGui::SameLine();
	Link("Crash", LINK_AUTHOR);
	Link("Nexus Mods", LINK_NEXUS);
	ImGui::SameLine();
	Link("GitHub", LINK_GITHUB);
	return changed || restyle;
}

#ifdef SLORM_DEV
static void DrawDev()
{
	ImGui::TextDisabled("Dev build. Dumps go to mods/Aurie/SlormSuite.dump.txt, the trace to aurie.log.");
	ImGui::TextDisabled("F9 dumps too, without moving the mouse.");
	ImGui::Checkbox("Trace script calls and first-time events", &g_Dev.trace);
	static char words[256] = "";
	if (!words[0])
		snprintf(words, sizeof(words), "%s", g_Dev.dump_words.c_str());
	ImGui::SetNextItemWidth(420);
	if (ImGui::InputText("printed in full", words, sizeof(words)))
		g_Dev.dump_words = words;
	static char note[64] = "";
	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##note", "what is on screen", note, sizeof(note));
	ImGui::SameLine();
	if (ImGui::Button("Dump"))
	{
		g_Dev.dump_note = note;
		g_Dev.want_dump = true;
	}
	if (!g_Dev.result.empty())
		ImGui::TextUnformatted(g_Dev.result.c_str());
}
#endif

static void DrawWindow()
{
	std::lock_guard guard(g_Lock);
	bool open = true;
	ImGui::SetNextWindowPos({ 40, 40 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.8f });
	// The id after ### keeps the saved position when the title changes.
	if (!ImGui::Begin("SlormSuite v" SLORMSUITE_VERSION "###SlormSuite", &open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	bool changed = false;
	if (ImGui::BeginTabBar("tabs"))
	{
		if (ImGui::BeginTabItem("Tweaks"))
		{
			changed |= DrawTweaks();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Options"))
		{
			changed |= DrawOptions();
			ImGui::EndTabItem();
		}
#ifdef SLORM_DEV
		if (ImGui::BeginTabItem("Dev"))
		{
			DrawDev();
			ImGui::EndTabItem();
		}
#endif
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

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = g_IniPath.empty() ? nullptr : g_IniPath.c_str();
		ApplyAppearance();
		g_Window = description.OutputWindow;
		ImGui_ImplWin32_Init(g_Window);
		g_OriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
		ImGui_ImplDX11_Init(g_Device, g_Context);
		g_Ready = true;
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormSuite] overlay ready");
	}

	if (!g_Visible)
	{
		g_HideMouse = false;
		return;
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
	static bool logged = false;
	if (!logged)
	{
		logged = true;
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormSuite] back buffer %ux%u, window %ldx%ld", size.Width, size.Height, client.right, client.bottom);
	}
	if (client.right > 0 && client.bottom > 0 && GetForegroundWindow() == g_Window)
	{
		io.AddMousePosEvent(cursor.x * static_cast<float>(size.Width) / client.right, cursor.y * static_cast<float>(size.Height) / client.bottom);
		io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
	}
	ImGui::NewFrame();
	// The game draws its own cursor under the overlay.
	ImGui::GetIO().MouseDrawCursor = ImGui::GetIO().WantCaptureMouse;
	DrawWindow();
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

static void LogOnce(bool& Done, const char* Text)
{
	if (Done)
		return;
	Done = true;
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormSuite] %s", Text);
}

static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	static bool saw_any = false, saw_mouse = false, saw_raw = false, saw_char = false;
	bool mouse = Message >= WM_MOUSEFIRST && Message <= WM_MOUSELAST;
	bool keyboard = Message == WM_KEYDOWN || Message == WM_KEYUP || Message == WM_CHAR;
	LogOnce(saw_any, "window messages seen");
	if (mouse) LogOnce(saw_mouse, "mouse messages seen");
	if (Message == WM_INPUT) LogOnce(saw_raw, "raw input seen");
	if (Message == WM_CHAR) LogOnce(saw_char, "char messages seen");

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

bool OverlayWantsMouse()
{
	return g_Ready && g_Visible && ImGui::GetIO().WantCaptureMouse;
}

bool OverlayWantsKeys()
{
	return g_Ready && g_Visible && ImGui::GetIO().WantTextInput;
}

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_OriginalPresent = nullptr;

static HRESULT WINAPI HookedPresent(IDXGISwapChain* Swapchain, UINT Sync, UINT Flags)
{
	Frame(Swapchain, Flags);
	return g_OriginalPresent(Swapchain, Sync, Flags);
}

// YYToolkit 5.0.0c never installs its own Present hook, so EVENT_FRAME does not fire.
bool OverlayInstall(YYTKInterface* Yytk)
{
	RValue info = Yytk->CallBuiltin("os_get_info", {});
	RValue chain = Yytk->CallBuiltin("ds_map_find_value", { info, RValue("video_d3d11_swapchain") });
	Yytk->CallBuiltin("ds_map_destroy", { info });

	IDXGISwapChain* swapchain = static_cast<IDXGISwapChain*>(chain.m_Pointer);
	if (!swapchain)
		return false;
	PVOID* table = *reinterpret_cast<PVOID**>(swapchain);
	Aurie::AurieStatus status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormSuite Present", table[8], HookedPresent, reinterpret_cast<PVOID*>(&g_OriginalPresent));
	if (!Aurie::AurieSuccess(status) || !g_OriginalPresent)
		return false;

	status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormSuite GetCursorPos", GetCursorPos, HookedCursor, reinterpret_cast<PVOID*>(&g_OriginalCursor));
	if (!Aurie::AurieSuccess(status))
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_WARNING, "[SlormSuite] cursor hook failed: %s", Aurie::AurieStatusToString(status));
	return true;
}
