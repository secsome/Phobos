#include "DirectDrawBackend.h"

#include <Unsorted.h>
#include <GameOptionsClass.h>
#include <MouseClass.h>

#include <Utilities/Debug.h>

DirectDrawBackend::DirectDrawBackend() noexcept
{
	hDirectDrawDLL = LoadLibraryA("DDRAW.DLL");
	pDirectDrawCreate = nullptr;
	if (hDirectDrawDLL != nullptr)
	{
		pDirectDrawCreate = reinterpret_cast<HRESULT(WINAPI*)(GUID const*, LPDIRECTDRAW*, IUnknown*)>(
			GetProcAddress(hDirectDrawDLL, "DirectDrawCreate"));
		if (pDirectDrawCreate == nullptr)
			Debug::FatalErrorAndExit("DirectDrawBackend: GetProcAddress for DirectDrawCreate failed.\n");
	}
	else
		Debug::FatalErrorAndExit("DirectDrawBackend: LoadLibraryA for DDRAW.DLL failed.\n");
}

DirectDrawBackend::~DirectDrawBackend() noexcept
{
	if (hDirectDrawDLL)
	{
		FreeLibrary(hDirectDrawDLL);
		hDirectDrawDLL = nullptr;
	}
	pDirectDrawCreate = nullptr;
}

static void(__fastcall* Process_DD_Result)(HRESULT result, int display_ok_msg) =
reinterpret_cast<void(__fastcall*)(HRESULT, int)>(0x4A3DD0);

template<typename T>
static inline void DDrawCheckError(T err, const char* function)
{
	if (err != DD_OK)
	{
		Process_DD_Result(static_cast<HRESULT>(err), true);
		Debug::FatalErrorAndExit("DirectDraw %s failed\n", function);
	}
}

void DirectDrawBackend::Prepare() noexcept
{
	Debug::LogGame("Prep direct draw.\n");
	if (DSurface::DirectDrawObject)
		return;

	HRESULT hr = pDirectDrawCreate(nullptr, &DSurface::DirectDrawObject, nullptr);

	DDrawCheckError(hr, "DirectDrawCreate");

	if (GameOptionsClass::WindowedMode)
		hr = DSurface::DirectDrawObject->SetCooperativeLevel(Game::hWnd, DDSCL_NORMAL);
	else
		hr = DSurface::DirectDrawObject->SetCooperativeLevel(Game::hWnd, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE);

	DDrawCheckError(hr, "SetCooperativeLevel");
}

void DirectDrawBackend::Release() noexcept
{
	if (DSurface::DirectDrawObject == nullptr)
		return;

	ULONG uResult = DSurface::DirectDrawObject->Release();
	DDrawCheckError(uResult, "IDirectDraw::Release");

	DSurface::DirectDrawObject = nullptr;
}

bool DirectDrawBackend::SetVideoMode(HWND hWnd, int iWidth, int iHeight, int iBpp) noexcept
{
	Prepare();

	Debug::LogGame("SetDisplayMode: %dx%dx%d\n", iWidth, iHeight, iBpp);
	HRESULT hr = DSurface::DirectDrawObject->SetDisplayMode(iWidth, iHeight, iBpp);
	DDrawCheckError(hr, "IDirectDraw::SetDisplayMode");

	Drawing::ScreenWidth = iWidth;
	Drawing::ScreenHeight = iHeight;
	Drawing::BitsPerPixel = iBpp;

	// We don't support 8bit color mode anymore
	if (iBpp != 16)
		return false;

	// Check hardware region fill and overlapped blit capability
	reinterpret_cast<void(__fastcall*)()>(0x4A3E40)();

	Debug::LogGame("Display mode set\n");
	return true;
}

void DirectDrawBackend::ResetVideoMode() noexcept
{
	if (DSurface::DirectDrawObject == nullptr)
		return;

	HRESULT hr = DSurface::DirectDrawObject->RestoreDisplayMode();
	DDrawCheckError(hr, "IDirectDraw::RestoreDisplayMode");

	Drawing::ScreenWidth = 0;
	Drawing::ScreenHeight = 0;
	Drawing::BitsPerPixel = 0;

	ULONG uResult = DSurface::DirectDrawObject->Release();
	DDrawCheckError(uResult, "IDirectDraw::Release");

	DSurface::DirectDrawObject = nullptr;
}

static HRESULT WINAPI DDEnumModesCallback(DDSURFACEDESC* pDesc, void* pContext)
{
	auto& resolutions = *reinterpret_cast<std::vector<DirectDrawBackend::ScreenResolution>*>(pContext);
	if (pDesc->ddpfPixelFormat.dwRGBBitCount == resolutions[0].uWidth)
	{
		DirectDrawBackend::ScreenResolution res;
		res.uWidth = pDesc->dwWidth;
		res.uHeight = pDesc->dwHeight;
		resolutions.push_back(res);
	}
	return DDENUMRET_OK;
}

std::vector<DirectDrawBackend::ScreenResolution> DirectDrawBackend::GetAvailableDisplayResolutions(
	unsigned int uMinWidth, unsigned int uMinHeight, unsigned int uMaxWidth,
	unsigned int uMaxHeight, unsigned int uColorBitCount)
{
	std::vector<ScreenResolution> result;
	result.emplace_back(uColorBitCount, uColorBitCount);

	DSurface::DirectDrawObject->EnumDisplayModes(0, nullptr, &result, DDEnumModesCallback);
	result[0].uWidth = result[0].uHeight = 0;  // Remove the first slaved entry

	auto filter = [=](const ScreenResolution& res) {
		return
			res.uWidth < uMinWidth || res.uWidth > uMaxWidth ||
			res.uHeight < uMinHeight || res.uHeight > uMaxHeight;
	};
	auto it = std::remove_if(result.begin(), result.end(), filter);
	result.erase(it, result.end());

	std::sort(result.begin(), result.end(), [](const ScreenResolution& l, const ScreenResolution& r) {
		if (l.uWidth < r.uWidth)
			return true;
		else if (l.uWidth == r.uWidth)
			return l.uHeight < r.uHeight;
		else return false;
	});

	return result;
}

void DirectDrawBackend::CreateSurface(DSurface* pSurface, int iWidth, int iHeight, bool bSystemMem, bool bEnable3D) noexcept
{
	pSurface->Width = iWidth;
	pSurface->Height = iHeight;
	pSurface->LockCount = 0;
	pSurface->BytesPerPixel = 0;
	pSurface->LockPtr = nullptr;
	pSurface->IsPrimary = 0;
	pSurface->IsVideoRam = 0;
	pSurface->SurfacePtr = 0;
	pSurface->Description = 0;
	*reinterpret_cast<size_t*>(pSurface) = 0x7E85D4; // DSurface vtable
	pSurface->Description = GameCreate<DDSURFACEDESC>();
	if (pSurface->Description)
	{
		memset(pSurface->Description, 0, sizeof(DDSURFACEDESC));
		pSurface->Description->dwSize = sizeof(DDSURFACEDESC);
		pSurface->Description->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		pSurface->Description->dwWidth = iWidth;
		pSurface->Description->dwHeight = iHeight;
		if (bSystemMem)
			pSurface->Description->ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;
		DSurface::DirectDrawObject->CreateSurface(pSurface->Description, &pSurface->SurfacePtr, nullptr);
		if (pSurface->SurfacePtr)
		{
			memset(pSurface->Description, 0, sizeof(DDSURFACEDESC));
			pSurface->Description->dwSize = sizeof(DDSURFACEDESC);
			pSurface->SurfacePtr->GetSurfaceDesc(pSurface->Description);
			pSurface->BytesPerPixel = (pSurface->Description->ddpfPixelFormat.dwRGBBitCount + 7) >> 3;
			pSurface->IsVideoRam = (pSurface->Description->ddsCaps.dwCaps & DDCAPS_OVERLAYSTRETCH) != 0;
		}
	}
}

static inline void SetupColorShift(int shift, int& lshift, int& rshift)
{
	lshift = 0;
	for (int index = 0; index < 16; ++index)
	{
		if (shift & 0x01)
			break;
		shift >>= 1;
		++lshift;
	}
	rshift = 0;
	for (int index = 0; index < 8; ++index)
	{
		if (shift & 0x80)
			break;
		shift <<= 1;
		++rshift;
	}
}

static inline void DetermineColorMode()
{
	const auto& rl = Drawing::RedShiftRight;
	const auto& rr = Drawing::RedShiftLeft;
	const auto& bl = Drawing::BlueShiftRight;
	const auto& br = Drawing::BlueShiftLeft;
	const auto& gl = Drawing::GreenShiftRight;
	const auto& gr = Drawing::GreenShiftLeft;

	if (br == 0 && bl == 3 && gr == 5 && gl == 3 && rr == 10 && rl == 3)
		Drawing::ColorMode = RGBMode::RGB555;
	else if (br == 0 && bl == 2 && gr == 6 && gl == 3 && rr == 11 && rl == 3)
		Drawing::ColorMode = RGBMode::RGB556;
	else if (br == 0 && bl == 3 && gr == 5 && gl == 2 && rr == 11 && rl == 3)
		Drawing::ColorMode = RGBMode::RGB565;
	else if (br == 0 && bl == 3 && gr == 5 && gl == 3 && rr == 11 && rl == 2)
		Drawing::ColorMode = RGBMode::RGB655;
	else
		Drawing::ColorMode = RGBMode::Invalid;
}

DSurface* DirectDrawBackend::CreatePrimarySurface(DSurface** ppBackSurface) noexcept
{
	// In YR, ppBackSurface is always nullptr
	UNREFERENCED_PARAMETER(ppBackSurface);

	Debug::LogGame("DSurface::Create_Primary\n");

	VideoCaps eCaps = Drawing::GetHardwareCapabilities();
	if (eCaps & VideoCaps::StretchBlits)
		DSurface::AllowStretchBlits = true;
	else
		DSurface::AllowStretchBlits = false;
	Debug::LogGame("DSurface::AllowStretchBlits = %s\n", DSurface::AllowStretchBlits ? "true" : "false");

	if (eCaps & VideoCaps::ColorFill)
		DSurface::AllowHWFill = true;
	else
		DSurface::AllowHWFill = false;
	Debug::LogGame("DSurface::AllowHWFill = %s\n", DSurface::AllowHWFill ? "true" : "false");

	DSurface* surface = GameCreate<DSurface>();

	surface->Description->dwFlags = DDSD_CAPS;
	surface->Description->ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	HRESULT result = DSurface::DirectDrawObject->CreateSurface(surface->Description, &surface->SurfacePtr, NULL);
	DDrawCheckError(result, "CreatePrimarySurface");

	if (result == DD_OK)
	{
		memset(surface->Description, 0, sizeof(DDSURFACEDESC));
		surface->Description->dwSize = sizeof(DDSURFACEDESC);
		surface->SurfacePtr->GetSurfaceDesc(surface->Description);
		surface->BytesPerPixel = (surface->Description->ddpfPixelFormat.dwRGBBitCount + 7) / 8;
		surface->IsPrimary = true;
		surface->Width = surface->Description->dwWidth;
		surface->Height = surface->Description->dwHeight;
		DSurface::PaletteSurface = surface->SurfacePtr;

		// Attach a clipper object to the surface so that it can cooperate
		// with the system GDI. This only comes into play if there are going
		// to be GDI graphical elements on top of the surface (normally this
		// isn't the case for full screen games). It doesn't hurt to attach
		// a clipper object anyway -- just in case.
		if (DSurface::DirectDrawObject->CreateClipper(0, &DSurface::Clipper, NULL) == DD_OK)
		{
			if (DSurface::Clipper->SetHWnd(0, GetActiveWindow()) == DD_OK)
				surface->SurfacePtr->SetClipper(DSurface::Clipper);
		}

		// Fetch the pixel format for the surface.
		memcpy(&DSurface::PixelFormat, &surface->Description->ddpfPixelFormat, sizeof(DDPIXELFORMAT));

		SetupColorShift(DSurface::PixelFormat.dwRBitMask, Drawing::RedShiftLeft, Drawing::RedShiftRight);
		SetupColorShift(DSurface::PixelFormat.dwGBitMask, Drawing::GreenShiftLeft, Drawing::GreenShiftRight);
		SetupColorShift(DSurface::PixelFormat.dwBBitMask, Drawing::BlueShiftLeft, Drawing::BlueShiftRight);

		// Create the halfbright mask.
		Drawing::HalfbrightMask = (unsigned short)Drawing::RGB_To_Int(127, 127, 127);
		Drawing::QuarterbrightMask = (unsigned short)Drawing::RGB_To_Int(63, 63, 63);
		Drawing::EighthbrightMask = (unsigned short)Drawing::RGB_To_Int(31, 31, 31);

		DetermineColorMode();
		Debug::LogGame("DSurface::Create_Primary done\n");
	}
	else
	{
		GameDelete(surface);
		surface = nullptr;
	}

	return surface;
}

void DirectDrawBackend::FocusRestore() noexcept
{
	if (GameOptionsClass::WindowedMode)
		return;

	if (DSurface::Alternate)
		DSurface::Alternate->RestoreCheck();
	if (DSurface::Hidden)
		DSurface::Hidden->RestoreCheck();
	if (DSurface::Composite && DSurface::Composite->RestoreCheck())
	{
		if (!Game::bSpecialFlag || Unsorted::CurrentFrame > 16)
			DSurface::Composite->Fill(0);
	}
	if (DSurface::Tile && DSurface::Tile->RestoreCheck())
	{
		if (!Game::bSpecialFlag || Unsorted::CurrentFrame > 16)
			DSurface::Tile->Fill(0);
	}
	if (DSurface::Sidebar && DSurface::Sidebar->RestoreCheck())
	{
		MouseClass::Instance.SidebarBackgroundNeedsRedraw = true;
		if (!Game::bSpecialFlag || Unsorted::CurrentFrame > 16)
			DSurface::Sidebar->Fill(0);
	}
	if (DSurface::Primary && DSurface::Primary->RestoreCheck())
	{
		if (!Game::bSpecialFlag || Unsorted::CurrentFrame > 16)
			DSurface::Primary->FillRect(DSurface::WindowBounds, 0);
	}
}
