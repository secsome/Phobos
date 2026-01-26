#pragma once

#include "IRenderBackend.h"

// DirectDraw-based render backend
// This is exactly the same as the original YR rendering backend
// Based on https://github.dev/electronicarts/CnC_Renegade

class DirectDrawBackend final : public IRenderBackend
{
public:
	DirectDrawBackend() noexcept;

	virtual ~DirectDrawBackend() noexcept override;

	// Replacement for Prep_Direct_Draw
	virtual void Prepare() noexcept override;

	// Replacement for Release_Direct_Draw
	virtual void Release() noexcept override;

	// Replacement for Set_Video_Mode
	virtual bool SetVideoMode(HWND hWnd, int iWidth, int iHeight, int iBpp) noexcept override;

	// Replacement for Reset_Video_Mode
	virtual void ResetVideoMode() noexcept override;

	// Replacement for Get_Available_Display_Resolutions
	virtual std::vector<ScreenResolution> GetAvailableDisplayResolutions(unsigned int uMinWidth, unsigned int uMinHeight,
		unsigned int uMaxWidth, unsigned int uMaxHeight, unsigned int uColorBitCount) override;

	// Replacement for DSurface::DSurface
	// Notice that DSurface size is 0x24 bytes, in which 0x10 bytes are extra fields compared to XSurface
	// So we can store 0x10 bytes of backend-specific data in DSurface without affecting XSurface part
	// Consider to store pointers to implement backend-specific resources if needed
	virtual void CreateSurface(DSurface* pSurface, int iWidth, int iHeight, bool bSystemMem, bool bEnable3D) noexcept override;

	// Replacement for DSurface::Create_Primary
	virtual DSurface* CreatePrimarySurface(DSurface** ppBackSurface) noexcept override;

	// Handle Focus_Restore
	virtual void FocusRestore() noexcept override;
private:
	HMODULE hDirectDrawDLL;
	HRESULT(WINAPI* pDirectDrawCreate)(GUID const*, LPDIRECTDRAW*, IUnknown*);
};
