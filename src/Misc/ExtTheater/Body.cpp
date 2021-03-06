#include "Body.h"
#include "TheaterProto.h"
#include <Helpers/Macro.h>

#include <ScenarioClass.h>
#include <MixFileClass.h>
#include <Theater.h>

#include "trim.h"
#include "../../Utilities/Debug.h"

void TheaterExt::Init()
{
	Debug::Log("Init custom theater %s\n", ID);
	if (!strlen(ID)) {
		strcpy_s(ID, sizeof(ID), TheaterProto::Array[0].name);
	}

	if (_stricmp(ID, TheaterExt::nameLast) == 0) {
		return;
	}

	if (protectFlags == 0) {
		VirtualProtect(Theater::Array, sizeof(Theater) * 6, PAGE_EXECUTE_READWRITE, &protectFlags);
	}

	TheaterExt::LoadFromINI();
	ScenarioClass::Instance->Theater = static_cast<TheaterType>(TheaterExt::slot);

	auto slotPrt = &Theater::Array[slot];
	strcpy_s(nameLast, ID);
	strcpy_s(slotPrt->Extension, suffix);
	strcpy_s(slotPrt->Letter, letter);
	slotPrt->RadarTerrainBrightness = radarBrightness;

	TheaterExt::LoadSpecificMixs();
}

void TheaterExt::LoadFromINI()
{
	specificMixNames.Clear();
	specificMixNames_buff[0] = 0;

	slot = TheaterProto::FindIndexOrDefault(ID);

	CCINIClass* pINI = TheaterExt::OpenConfig(ID);
	if (pINI) {
		const char* section = "THEATER";

		slot = pINI->ReadInteger(section, "Slot", slot);
		auto proto = TheaterProto::GetOrDefault(slot);

		pINI->ReadString(section, "Suffix", proto->suffix, suffix);
		pINI->ReadString(section, "Letter", proto->letter, letter);
		pINI->ReadString(section, "ControlFile", proto->controlFileName, controlFileName);
		pINI->ReadString(section, "Palette.Overlay", proto->palOverlay, palOverlay);
		pINI->ReadString(section, "Palette.Unit", proto->palUnit, palUnit);
		pINI->ReadString(section, "Palette.ISO", proto->palISO, palISO);
		pINI->ReadString(section, "MixArray", proto->specificMix, specificMixNames_buff);
		radarBrightness = (float)pINI->ReadDouble(section, "RadarBrightness", proto->radarBrightness);

		Phobos::CloseConfig(pINI);
	}
	else {
		auto proto = TheaterProto::GetOrDefault(slot);

		strcpy_s(suffix, proto->suffix);
		strcpy_s(letter, proto->letter);
		strcpy_s(controlFileName, proto->controlFileName);
		strcpy_s(palOverlay, proto->palOverlay);
		strcpy_s(palUnit, proto->palUnit);
		strcpy_s(palISO, proto->palISO);
		strcpy_s(specificMixNames_buff, proto->specificMix);
		radarBrightness = (float)proto->radarBrightness;
	}

	// Parse MixArray
	if (specificMixNames_buff[0]) {
		char* context = nullptr;
		char* cur = strtok_s(specificMixNames_buff, Phobos::readDelims, &context);
		while (cur) {
			specificMixNames.AddItem(Trim::FullTrim(cur));
			cur = strtok_s(nullptr, Phobos::readDelims, &context);
		}
	}
}

void TheaterExt::LoadSpecificMixs()
{
	TheaterExt::UnloadSpecificMixs();
	Debug::Log("Load specific Theater Mixfiles:\n");
	for (int i = 0; i < specificMixNames.Count; i++)
	{
		char* pName = specificMixNames.GetItem(i);
		Debug::Log("\t%s\n", pName);
		specificMixFiles.AddItem(
			GameCreate<MixFileClass>(pName)
		);
	}
	Debug::Log("\n");
}

void TheaterExt::UnloadSpecificMixs()
{
	Debug::Log("Unload specific Theater Mixfiles:\n");
	for (int i = 0; i < specificMixFiles.Count; i++)
	{
		MixFileClass* pMix = specificMixFiles.GetItem(i);
		Debug::Log("\t%s\n", pMix->FileName);
		GameDelete(pMix);
	}
	specificMixFiles.Clear();
	Debug::Log("\n");
}
