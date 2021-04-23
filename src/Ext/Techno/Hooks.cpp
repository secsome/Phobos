#include "Body.h"

#include "../TechnoType/Body.h"

DEFINE_HOOK(6F9E50, TechnoClass_AI, 5)
{
	GET(TechnoClass*, pThis, ECX);

	// MindControlRangeLimit
	TechnoExt::ApplyMindControlRangeLimit(pThis);
	// Interceptor
	TechnoExt::ApplyInterceptor(pThis);
	// Powered.KillSpawns
	TechnoExt::ApplyPowered_KillSpawns(pThis);
	// Spawner.LimitRange & Spawner.ExtraLimitRange
	TechnoExt::ApplySpawn_LimitRange(pThis);

	return 0;
}

DEFINE_HOOK(5218F3, InfantryClass_WhatWeaponShouldIUse_DeployFireWeapon, 6)
{
    GET(TechnoTypeClass*, pType, ECX);

    if (pType->DeployFireWeapon == -1)
        return 0x52194E;

    return 0;
}