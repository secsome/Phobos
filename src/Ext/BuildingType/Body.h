#pragma once
#include <BuildingTypeClass.h>

#include <Helpers/Macro.h>
#include "../_Container.hpp"
#include "../../Utilities/TemplateDef.h"

class BuildingTypeExt
{
public:
	using base_type = BuildingTypeClass;

	class ExtData final : public Extension<BuildingTypeClass>
	{
	public:
		Valueable<AffectedHouse> PowersUp_Owner;
		ValueableVector<BuildingTypeClass*> PowersUp_Buildings;
		
		ValueableVector<BuildingTypeClass*> PowerPlantEnchancer_Buildings;
		Valueable<int> PowerPlantEnchancer_Amount;
		Valueable<float> PowerPlantEnchancer_Factor;

		ExtData(BuildingTypeClass* OwnerObject) : Extension<BuildingTypeClass>(OwnerObject),
			PowersUp_Owner(AffectedHouse::Owner),
			PowersUp_Buildings(),
			PowerPlantEnchancer_Buildings(),
			PowerPlantEnchancer_Amount(0),
			PowerPlantEnchancer_Factor(1.0f)
		{ }

		virtual ~ExtData() = default;

		virtual void LoadFromINIFile(CCINIClass * pINI) override;
		virtual void Initialize() override;
		virtual void CompleteInitialization();

		virtual void InvalidatePointer(void* ptr, bool bRemoved) override {
		}

		virtual void LoadFromStream(PhobosStreamReader & Stm) override;

		virtual void SaveToStream(PhobosStreamWriter & Stm) override;

	private:
		template <typename T>
		void Serialize(T& Stm);
	};

	class ExtContainer final : public Container<BuildingTypeExt> {
	public:
		ExtContainer();
		~ExtContainer();

		virtual bool Load(BuildingTypeClass* pThis, IStream* pStm) override;
	};

	static ExtContainer ExtMap;
	static bool LoadGlobals(PhobosStreamReader& Stm);
	static bool SaveGlobals(PhobosStreamWriter& Stm);

	static int GetEnchancedPower(BuildingClass* pBuilding, HouseClass* pHouse);
	static bool CanUpgrade(BuildingClass* pBuilding, BuildingTypeClass* pUpgradeType, HouseClass* pUpgradeOwner);
};