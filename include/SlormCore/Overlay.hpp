// The overlay every mod shares: SlormCore.dll owns the window, the frame hook and the input,
// and a mod adds a tab to it. Build with the Dear ImGui in vendor/imgui, which is the host's.
#pragma once
#include <Aurie/shared.hpp>
#include <imgui.h>

namespace SlormCore
{
	constexpr const char* OVERLAY_INTERFACE = "SlormCore_Overlay";

	using DrawFn = void(*)(void* User);

	struct OverlayInterface : Aurie::AurieInterfaceBase
	{
		// False when the caller's Dear ImGui is not the host's build; it must not draw then.
		virtual bool SameImGui(const char* Version, size_t Io, size_t Style, size_t Vec2, size_t Vec4, size_t Vertex, size_t Index) = 0;
		// Null until the game's first frame.
		virtual ImGuiContext* Context() = 0;
		virtual void Allocators(ImGuiMemAllocFunc* Alloc, ImGuiMemFreeFunc* Free, void** User) = 0;
		// A tab of the shared window, drawn while it is the open one.
		virtual bool AddTab(const char* Name, DrawFn Draw, void* User) = 0;
		// Drawn every frame, window shown or not.
		virtual bool AddHud(DrawFn Draw, void* User) = 0;
		// Takes back every tab and HUD added with this pair. A mod that unloads must call it.
		virtual void Remove(DrawFn Draw, void* User) = 0;
		virtual bool Visible() = 0;
		virtual void SetVisible(bool Visible) = 0;
		// The overlay is taking the mouse, or the keys for typing; the game should not act on them.
		virtual bool WantsMouse() = 0;
		virtual bool WantsKeys() = 0;
	};

	// Null when SlormCore.dll is not installed.
	inline OverlayInterface* Overlay()
	{
		static OverlayInterface* host = nullptr;
		Aurie::AurieInterfaceBase* found = nullptr;
		if (!host && Aurie::AurieSuccess(Aurie::ObGetInterface(OVERLAY_INTERFACE, found)))
			host = static_cast<OverlayInterface*>(found);
		return host;
	}

	// Points this DLL's Dear ImGui at the host's. False when there is nothing to draw on.
	inline bool Attach()
	{
		OverlayInterface* host = Overlay();
		if (!host || !host->Context())
			return false;
		static int same = -1;
		if (same < 0)
			same = host->SameImGui(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx));
		if (!same)
			return false;
		ImGuiMemAllocFunc alloc = nullptr;
		ImGuiMemFreeFunc release = nullptr;
		void* user = nullptr;
		host->Allocators(&alloc, &release, &user);
		ImGui::SetAllocatorFunctions(alloc, release, user);
		ImGui::SetCurrentContext(host->Context());
		return true;
	}

	namespace Internal
	{
		inline void Draw(void* User)
		{
			if (Attach())
				reinterpret_cast<void(*)()>(User)();
		}
	}

	// The usual way in: Draw runs with Dear ImGui ready, inside the tab.
	inline bool AddTab(const char* Name, void(*Draw)())
	{
		OverlayInterface* host = Overlay();
		return host && host->AddTab(Name, Internal::Draw, reinterpret_cast<void*>(Draw));
	}

	inline bool AddHud(void(*Draw)())
	{
		OverlayInterface* host = Overlay();
		return host && host->AddHud(Internal::Draw, reinterpret_cast<void*>(Draw));
	}

	inline void Remove(void(*Draw)())
	{
		if (OverlayInterface* host = Overlay())
			host->Remove(Internal::Draw, reinterpret_cast<void*>(Draw));
	}
}
