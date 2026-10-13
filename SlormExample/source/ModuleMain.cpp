// The smallest mod with a tab on SlormCore's overlay. Copy this folder to start a mod.
#include <YYToolkit/YYTK_Shared.hpp>
#include <SlormCore/Overlay.hpp>
#include "Version.hpp"
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;

// Runs inside the mod's tab, each frame it is the open one.
static void DrawTab()
{
	static int clicks = 0;
	ImGui::TextUnformatted("SlormExample v" SLORMEXAMPLE_VERSION);
	if (ImGui::Button("Click"))
		clicks++;
	ImGui::SameLine();
	ImGui::Text("%d clicks", clicks);

	RValue gold = g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") });
	if (gold.IsNumberConvertible())
		ImGui::Text("Goldus: %.0f", gold.ToDouble());
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	UNREFERENCED_PARAMETER(Module);
	UNREFERENCED_PARAMETER(ModulePath);
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	if (!SlormCore::AddTab("Example", DrawTab))
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormExample] no overlay: install SlormCore");
	return AURIE_SUCCESS;
}
