#include "Suite.hpp"
using namespace Aurie;
using namespace YYTK;

YYTKInterface* g_Yytk = nullptr;

RValue Global(const char* Name)
{
	return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name) });
}

RValue Local(CInstance* Self, const char* Name)
{
	return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) });
}

double ToNumber(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
		return atof(Value.ToString().c_str());
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET || !Value.IsNumberConvertible())
		return 0;
	return Value.ToDouble();
}

std::string ToText(const RValue& Value)
{
	return Value.IsString() ? Value.ToString() : "";
}

PFUNC_YYGMLScript FindScript(const char* Name)
{
	CScript* script = nullptr;
	std::string full = std::string("gml_Script_") + Name;
	AurieStatus status = g_Yytk->GetNamedRoutinePointer(full.c_str(), reinterpret_cast<PVOID*>(&script));
	if (!AurieSuccess(status) || !script || !script->m_Functions)
		return nullptr;
	return script->m_Functions->m_ScriptFunction;
}

RValue CallScript(PFUNC_YYGMLScript Script, CInstance* Self, std::vector<RValue> Arguments)
{
	RValue result;
	if (!Script)
		return result;
	std::vector<RValue*> pointers;
	for (RValue& argument : Arguments) pointers.push_back(&argument);
	Script(Self, Self, result, static_cast<int>(pointers.size()), pointers.data());
	return result;
}

std::string RoomName()
{
	RValue room;
	if (!AurieSuccess(g_Yytk->GetBuiltin("room", nullptr, NULL_INDEX, room)))
		return "";
	return ToText(g_Yytk->CallBuiltin("room_get_name", { room }));
}

CInstance* FindInstance(const char* Object)
{
	RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue(Object) });
	if ((object.m_Kind != VALUE_REF && ToNumber(object) < 0) || ToNumber(g_Yytk->CallBuiltin("instance_number", { object })) < 1)
		return nullptr;
	RValue found = g_Yytk->CallBuiltin("instance_find", { object, RValue(0.0) });
	CInstance* instance = nullptr;
	if (!AurieSuccess(g_Yytk->GetInstanceObject(static_cast<int32_t>(found.ToDouble()), instance)))
		return nullptr;
	return instance;
}
