#include "CaptureManager.h"

#include <Utilities/Macro.h>

#include <Ext/TechnoType/Body.h>
#include <HouseClass.h>
#include <AnimClass.h>

bool CaptureManager::CanCapture(
	CaptureManagerClass* pManager,
	TechnoClass* pTarget
)
{
	if (pManager->MaxControlNodes == 1)
		return pManager->CanCapture(pTarget);

	auto pTechnoTypeExt = TechnoTypeExt::ExtMap.Find(pManager->Owner->GetTechnoType());
	if (pTechnoTypeExt && pTechnoTypeExt->MultiMindControl_ReleaseVictim)
	{
		// I hate Ares' completely rewritten things - secsome
		pManager->MaxControlNodes += 1;
		bool result = pManager->CanCapture(pTarget);
		pManager->MaxControlNodes -= 1;
		return result;
	}

	return pManager->CanCapture(pTarget);
}

bool CaptureManager::FreeUnit
(
	CaptureManagerClass* pManager,
	TechnoClass* pTarget,
	bool bSilent
)
{
	if (pTarget)
	{
		for (int i = pManager->ControlNodes.Count - 1; i >= 0; --i)
		{
			const auto pNode = pManager->ControlNodes[i];
			if (pTarget == pNode->Unit)
			{
				if (pTarget->MindControlRingAnim) {
					pTarget->MindControlRingAnim->UnInit();
					pTarget->MindControlRingAnim = nullptr;
				}

				if (!bSilent) {
					int nSound = pTarget->GetTechnoType()->MindClearedSound;

					if (nSound == -1)
						nSound = RulesClass::Instance->MindClearedSound;

					if (nSound != -1)
						VocClass::PlayIndexAtPos(nSound, pTarget->GetCoords());

				}

				// Fix : Player defeated should not get this unit.
				auto pOriginOwner = pNode->OriginalOwner->Defeated ?
					HouseClass::FindNeutral() :
					pNode->OriginalOwner;

				pTarget->SetOwningHouse(pOriginOwner);
				pManager->DecideUnitFate(pTarget);
				pTarget->MindControlledBy = nullptr;

				if (pNode)
					GameDelete(pNode);

				pManager->ControlNodes.RemoveItem(i);

				return true;
			}
		}
	}

	return false;
}

bool CaptureManager::CaptureUnit
(
	CaptureManagerClass* pManager,
	TechnoClass* pTarget,
	bool bRemoveFirst,
	AnimTypeClass* pControlledAnimType
)
{
	if (CaptureManager::CanCapture(pManager, pTarget) && pManager->MaxControlNodes > 0)
	{
		if (!pManager->InfiniteMindControl)
		{
			if (pManager->ControlNodes.Count == pManager->MaxControlNodes)
			{
				if (bRemoveFirst || pManager->MaxControlNodes == 1)
					CaptureManager::FreeUnit(pManager, pManager->ControlNodes[0]->Unit);

			}
		}

		auto pControlNode = GameCreate<ControlNode>();
		if (pControlNode)
		{
			pControlNode->OriginalOwner = pTarget->Owner;
			pControlNode->Unit = pTarget;

			pManager->ControlNodes.AddItem(pControlNode);
			pControlNode->LinkDrawTimer.Start(RulesClass::Instance->MindControlAttackLineFrames);

			if (pTarget->SetOwningHouse(pManager->Owner->Owner))
			{
				pTarget->MindControlledBy = pManager->Owner;
				pManager->DecideUnitFate(pTarget);

				auto const pBld = abstract_cast<BuildingClass*>(pTarget);
				auto const pType = pTarget->GetTechnoType();
				CoordStruct location = pTarget->GetCoords();

				location.Z += pBld ?
					pBld->Type->Height * Unsorted::LevelHeight :
					pType->MindControlRingOffset;

				if (auto const pAnimType = pControlledAnimType)
				{
					if (auto const pAnim = GameCreate<AnimClass>(pAnimType, location))
					{
						pTarget->MindControlRingAnim = pAnim;
						pAnim->SetOwnerObject(pTarget);

						if (pBld)
							pAnim->ZAdjust = -1024;

					}
				}

				return true;
			}
		}
	}

	return false;
}

bool CaptureManager::CaptureUnit
(
	CaptureManagerClass* pManager,
	TechnoClass* pTechno,
	AnimTypeClass* pControlledAnimType
)
{
	if (!pTechno) {
		return false;
	}

	const auto pTarget = pTechno->AbsDerivateID & AbstractFlags::Techno ? pTechno : nullptr;

	bool bRemoveFirst = false;
	if ( auto pTechnoTypeExt = TechnoTypeExt::ExtMap.Find(pManager->Owner->GetTechnoType()) )
		bRemoveFirst = pTechnoTypeExt->MultiMindControl_ReleaseVictim;

	return CaptureManager::CaptureUnit(pManager, pTarget, bRemoveFirst, pControlledAnimType);
}

void CaptureManager::DecideUnitFate
(
	CaptureManagerClass* pManager,
	FootClass* pFoot
)
{
	// to be implemented (if needed). - secsome
}

// =============================
// Hooks

bool __fastcall CaptureManagerClass_CaptureUnit(CaptureManagerClass* pThis, void*, TechnoClass* pTechno) {
	return CaptureManager::CaptureUnit(pThis, pTechno);
}

bool __fastcall CaptureManagerClass_FreeUnit(CaptureManagerClass* pThis, void*, TechnoClass* pTechno) {
	return CaptureManager::FreeUnit(pThis, pTechno);
}

bool __fastcall TechnoClass_CanFire_CanCapture(CaptureManagerClass* pThis, void*, TechnoClass* pTechno) {
	return CaptureManager::CanCapture(pThis, pTechno);
}

DEFINE_POINTER_LJMP(0x471D40, CaptureManagerClass_CaptureUnit); // Replace vanilla funcion
DEFINE_POINTER_LJMP(0x471FF0, CaptureManagerClass_FreeUnit);    // Replace vanilla funcion
DEFINE_POINTER_CALL(0x6FCB3B, TechnoClass_CanFire_CanCapture);  // Replace one call
