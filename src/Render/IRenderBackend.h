#pragma once

#include <vector>

#include <Surface.h>

class IRenderBackend
{
public:
	virtual ~IRenderBackend() noexcept = default;

	// Replacement for Prep_Direct_Draw
	virtual void Prepare() noexcept = 0;

	// Replacement for Release_Direct_Draw
	virtual void Release() noexcept = 0;

	// Replacement for GetVramAmount, return size in bytes
	virtual int GetVramAmount() noexcept { return 0x400000; } // see 0x6BD979, modern machine certainly has more than 4MB VRAM

	// Replacement for Set_Video_Mode
	virtual bool SetVideoMode(HWND hWnd, int iWidth, int iHeight, int iBpp) noexcept = 0;

	// Replacement for Reset_Video_Mode
	virtual void ResetVideoMode() noexcept = 0;

	struct ScreenResolution
	{
		unsigned int uWidth;
		unsigned int uHeight;
	};
	// Replacement for Get_Available_Display_Resolutions
	virtual std::vector<ScreenResolution> GetAvailableDisplayResolutions(unsigned int uMinWidth, unsigned int uMinHeight,
		unsigned int uMaxWidth, unsigned int uMaxHeight, unsigned int uColorBitCount) = 0;

	// Replacement for DSurface::DSurface
	// Notice that DSurface size is 0x24 bytes, in which 0x10 bytes are extra fields compared to XSurface
	// So we can store 0x10 bytes of backend-specific data in DSurface without affecting XSurface part
	// Consider to store pointers to implement backend-specific resources if needed
	virtual void CreateSurface(DSurface* pSurface, int iWidth, int iHeight, bool bSystemMem, bool bEnable3D) noexcept = 0;

	// Replacement for DSurface::Create_Primary
	virtual DSurface* CreatePrimarySurface(DSurface** ppBackSurface) noexcept = 0;

	// Replacement for Direct3D_Prep, can be used to render stuffs before general 2d render routine
	virtual void PreparePreRenderer() noexcept { }

	// Replacement for Direct3D_Init
	virtual void InitPreRenderer() noexcept { }

	// Replacement for Direct3D_Release
	virtual void ReleasePreRenderer() noexcept { }

	// Replacement for DisplayClass::Blit Direct3D part
	virtual void BlitPreRenderer() noexcept { }

	// Replacement for Profile_System
	virtual void ProfileSystem() noexcept { }

	// Replacement for Send_Statistics_Packet VID (uint32) field
	virtual long GetVideoStatisticsReport() noexcept { return -1; }

	// Get a Win32 compatible HDC for GDI operations
	virtual HDC GetDC(HWND hWnd) noexcept { return ::GetDC(hWnd); }

	// Release the HDC obtained from GetDC
	virtual int ReleaseDC(HWND hWnd, HDC hDC) noexcept { return ::ReleaseDC(hWnd, hDC); }

	// Handle Focus_Restore
	virtual void FocusRestore() noexcept { }
};
