#include "Render.h"

#include <Helpers/Macro.h>
#include <Memory.h>
#include <Surface.h>
#include <PacketClass.h>

#include "DirectDrawBackend.h"

IRenderBackend* g_pRenderBackend;

DEFINE_HOOK(0x4A3FD0, RenderBackends_Prepare, 0x8)
{
	g_pRenderBackend->Prepare();
	return 0x4A4019;
}

DEFINE_HOOK(0x4A40C0, RenderBackends_Release, 0x5)
{
	g_pRenderBackend->Release();
	return 0x4A412B;
}

DEFINE_HOOK(0x4A4130, RenderBackends_GetVramAmount, 0x5)
{
	g_pRenderBackend->GetVramAmount();
	return 0x4A42E6;
}

DEFINE_HOOK(0x4A42F0, RenderBackends_SetVideoMode, 0x5)
{
	GET(HWND, hWnd, ECX);
	GET(int, iWidth, EDX);
	GET_STACK(int, iHeight, 0x4);
	GET_STACK(int, iBpp, 0x8);
	bool bResult = g_pRenderBackend->SetVideoMode(hWnd, iWidth, iHeight, iBpp);
	R->AL(bResult);
	return 0x4A438A;
}

DEFINE_HOOK(0x4A44F0, RenderBackends_ResetVideoMode, 0x5)
{
	g_pRenderBackend->ResetVideoMode();
	return 0x4A45BA;
}

// 4A45C0 Get_Free_Video_Memory never called, skip
// 4A4620 Get_Video_Hardware_Capabilities only used in Primary Surface creation, skip
// 4A4700 Wait_Vert_Blank never called, skip
// 4A4780 Set_DD_Palette seems useless, skip

DEFINE_HOOK(0x4A4900, RenderBackends_GetAvailableDisplayResolutions, 0x8)
{
	GET(unsigned int, uMinWidth, ECX);
	GET(unsigned int, uMinHeight, EDX);
	GET_STACK(unsigned int, uMaxWidth, 0x4);
	GET_STACK(unsigned int, uMaxHeight, 0x8);
	GET_STACK(unsigned int, uColorBitCount, 0xC);

	auto vResolutions = g_pRenderBackend->GetAvailableDisplayResolutions(uMinWidth, uMinHeight,
		uMaxWidth, uMaxHeight, uColorBitCount);
	auto pResolutions = GameCreate<unsigned int>(2 * vResolutions.size() + 1);
	for (size_t i = 0; i < vResolutions.size(); ++i)
	{
		pResolutions[i * 2] = vResolutions[i].uWidth;
		pResolutions[i * 2 + 1] = vResolutions[i].uHeight;
	}
	pResolutions[vResolutions.size() * 2] = 0; // Termination

	R->EAX(pResolutions);
	return 0x4A4A8C;
}

// DSurface::DSurface
DEFINE_HOOK(0x4BA5A0, RenderBackends_CreateSurface, 0x5)
{
	GET(DSurface*, pSurface, ECX);
	GET_STACK(int, iWidth, 0x4);
	GET_STACK(int, iHeight, 0x8);
	GET_STACK(bool, bSystemMem, 0xC);
	GET_STACK(bool, bEnable3D, 0x10);

	g_pRenderBackend->CreateSurface(pSurface, iWidth, iHeight, bSystemMem, bEnable3D);

	R->EAX(pSurface);
	return 0x4BA69E;
}

// DSurface::Create_Primary
DEFINE_HOOK(0x4BA770, RenderBackends_CreatePrimary, 0x5)
{
	GET(DSurface**, ppBackSurface, ECX);
	DSurface* pSurface = g_pRenderBackend->CreatePrimarySurface(ppBackSurface);
	R->EAX(pSurface);
	return 0x4BAB98;
}

DEFINE_HOOK(0x6BDD7A, RenderBackends_PreparePreRenderer, 0x5)
{
	g_pRenderBackend->PreparePreRenderer();
	return 0x6BDD98;
}

DEFINE_HOOK(0x4C16A0, RenderBackEnds_InitPreRenderer, 0x6)
{
	g_pRenderBackend->InitPreRenderer();
	return 0x4C16B6;
}

DEFINE_HOOK(0x6BB8E0, RenderBackends_ReleasePreRenderer, 0x5)
{
	g_pRenderBackend->ReleasePreRenderer();
	return 0x6BB8F1;
}

DEFINE_HOOK(0x4F45BB, RenderBackends_BlitPreRenderer, 0x8)
{
	g_pRenderBackend->BlitPreRenderer();
	return 0x4F474E;
}

DEFINE_HOOK(0x5355D0, RenderBackends_ProfileSystem, 0x5)
{
	g_pRenderBackend->ProfileSystem();
	return 0x535A38;
}

DEFINE_HOOK(0x6C84C9, RenderBackends_SendStatisticsPacket, 0x5)
{
	auto pField = GameCreate<FieldClass>();
	if (pField == nullptr)
	{
		R->EAX(0);
		return 0x6C8541;
	}

	long lReport = g_pRenderBackend->GetVideoStatisticsReport();
	R->EAX(pField);
	R->ESI(lReport);
	return 0x6C8512;
}

DEFINE_HOOK(0x5E286D, RenderBackends_GetDC1, 0x7)
{
	GET(HWND, hWnd, EBX);
	HDC dc = g_pRenderBackend->GetDC(hWnd);
	R->EAX(dc);
	return 0x5E2874;
}

DEFINE_HOOK(0x5E298E, RenderBackends_ReleaseDC1, 0x8)
{
	GET(HWND, hWnd, EBX);
	GET(HDC, hDC, EDX);

	int iResult = g_pRenderBackend->ReleaseDC(hWnd, hDC);
	R->EAX(iResult);
	return 0x5E2996;
}

DEFINE_HOOK(0x6191C4, RenderBackends_GetDC2, 0x7)
{
	GET(HWND, hWnd, EAX);
	HDC dc = g_pRenderBackend->GetDC(hWnd);
	R->EAX(dc);
	return 0x6191CB;
}

DEFINE_HOOK(0x6191DC, RenderBackends_ReleaseDC2, 0x8)
{
	GET_STACK(HWND, hWnd, STACK_OFFSET(0x2D40, 0x4));
	GET(HDC, hDC, ESI);

	int iResult = g_pRenderBackend->ReleaseDC(hWnd, hDC);
	R->EAX(iResult);
	return 0x6191EB;
}

DEFINE_HOOK(0x6194CB, RenderBackends_GetDC3, 0x8)
{
	GET(HWND, hWnd, ECX);
	HDC dc = g_pRenderBackend->GetDC(hWnd);

	// Stolen bytes
	R->ref_Stack<DWORD>(STACK_OFFSET(0x2D40, -0x2C94)) = R->EBX();

	R->EAX(dc);
	return 0x6194D9;
}

DEFINE_HOOK(0x619A18, RenderBackends_ReleaseDC3, 0xB)
{
	GET_STACK(HWND, hWnd, STACK_OFFSET(0x2D3C, 0x4));
	GET_STACK(HDC, hDC, STACK_OFFSET(0x2D3C, -0x2D00));

	int iResult = g_pRenderBackend->ReleaseDC(hWnd, hDC);
	R->EAX(iResult);
	return 0x619B47;
}

DEFINE_HOOK(0x77747A, RenderBackends_FocusRestore, 0x5)
{
	g_pRenderBackend->FocusRestore();
	return 0x777575;
}

// Setup after setting up COMs
DEFINE_HOOK(0x6BD9B5, RenderBackends_BackendCTOR, 0x5)
{
	// TODO: implement backend create/selection logic
	g_pRenderBackend = GameCreate<DirectDrawBackend>();
	return 0;
}

// Cleanup before releasing COMs
DEFINE_HOOK(0x6BEAC3, RenderBackends_BackendDTOR, 0x6)
{
	if (g_pRenderBackend != nullptr)
	{
		GameDelete(g_pRenderBackend);
		g_pRenderBackend = nullptr;
	}
	return 0;
}
