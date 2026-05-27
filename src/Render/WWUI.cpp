#include "WWUI.h"

#include "Functions.h"

#include <BitFont.h>
#include <OwnerDraw.h>
#include <Drawing.h>
#include <PCX.h>
#include <Phobos.h>
#include <RulesClass.h>
#include <SessionClass.h>
#include <StringTable.h>
#include <UI.h>
#include <Unsorted.h>
#include <VocClass.h>

#include <Memory.h>
#include <Surface.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <mbstring.h>
#include <iterator>
#include <new>
#include <string>
#include <vector>

namespace
{
	constexpr int DialogProcWindowLongIndex = 4;
	constexpr int PaintStateExtraIndex = 61;
	constexpr int SavedFontExtraIndex = 56;
	constexpr int SavedBkModeExtraIndex = 57;
	constexpr int SavedBkColorExtraIndex = 58;
	constexpr int SavedTextColorExtraIndex = 59;

	WWWinData* FindOwnerDrawData(HWND hWnd)
	{
		if (!OwnerDraw::Dialogs.size())
			return nullptr;

		return OwnerDraw::Dialogs.try_get(hWnd);
	}

	WNDPROC FindWindowProc(OwnerDraw::HwndProcDict& procs, HWND hWnd)
	{
		if (!procs.size())
			return nullptr;

		if (const auto pProc = procs.try_get(hWnd))
			return *pProc;

		return nullptr;
	}

	bool IsEmpty(const WideWstring& text)
	{
		return text.GetLength() == 0;
	}

	const wchar_t* GetWideTextBuffer(const WideWstring& text)
	{
		return text.Buffer ? text.Buffer : L"";
	}

	WideWstring QueryTooltipText(HWND parentHwnd, HWND controlHwnd, LPARAM hitCode)
	{
		OwnerDrawTooltipRequest request;
		request.ControlHwnd = controlHwnd;
		request.HitCode = hitCode;

		::SendMessageA(parentHwnd, WW_GETTOOLTIPTEXT, 0, reinterpret_cast<LPARAM>(&request));

		return request.Text;
	}

	std::vector<OwnerDrawWindowMessageKey>& ActiveWindowMessages()
	{
		static std::vector<OwnerDrawWindowMessageKey> messages;
		return messages;
	}

	bool AllowsRecursiveMessage(UINT message)
	{
		return message == WM_COMMAND
			|| message == WM_SYSKEYDOWN
			|| message == WM_SYSKEYUP
			|| message == WM_SYSCOMMAND
			|| message == WM_SYSCHAR;
	}

	class WindowMessageGuardScope
	{
	public:
		WindowMessageGuardScope(HWND hWnd, UINT message) :
			Key { message, hWnd }
		{
		}

		bool Enter()
		{
			auto& messages = ActiveWindowMessages();
			const auto it = std::find(messages.begin(), messages.end(), this->Key);

			if (it != messages.end())
			{
				if (!AllowsRecursiveMessage(this->Key.Message))
					return false;

				messages.erase(it);
			}

			messages.push_back(this->Key);
			this->Active = true;
			return true;
		}

		void Release()
		{
			if (!this->Active)
				return;

			auto& messages = ActiveWindowMessages();
			const auto it = std::find(messages.begin(), messages.end(), this->Key);
			if (it != messages.end())
				messages.erase(it);

			this->Active = false;
		}

		~WindowMessageGuardScope()
		{
			this->Release();
		}

	private:
		OwnerDrawWindowMessageKey Key;
		bool Active { false };
	};

	HWND GetActiveWindowStackTop()
	{
		const int count = OwnerDraw::ActiveWindowStackCount;
		if (count <= 0 || !OwnerDraw::ActiveWindowStack)
			return nullptr;

		return OwnerDraw::ActiveWindowStack[count - 1];
	}

	void ResizeActiveWindowStack(int capacity)
	{
		if (capacity < 10)
			capacity = 10;

		auto pItems = static_cast<HWND*>(YRMemory::Allocate(sizeof(HWND) * capacity));
		std::memset(pItems, 0, sizeof(HWND) * capacity);

		const int copyCount = std::min(OwnerDraw::ActiveWindowStackCount, capacity);
		if (OwnerDraw::ActiveWindowStack && copyCount > 0)
			std::memcpy(pItems, OwnerDraw::ActiveWindowStack, sizeof(HWND) * copyCount);

		if (OwnerDraw::ActiveWindowStack)
			YRMemory::Deallocate(OwnerDraw::ActiveWindowStack);

		OwnerDraw::ActiveWindowStack = pItems;
		OwnerDraw::ActiveWindowStackCapacity = capacity;

		if (OwnerDraw::ActiveWindowStackCount > capacity)
			OwnerDraw::ActiveWindowStackCount = capacity;
	}

	void EnsureActiveWindowStackCapacity(int required)
	{
		if (required <= OwnerDraw::ActiveWindowStackCapacity)
			return;

		int capacity = OwnerDraw::ActiveWindowStackCapacity * 2;
		if (capacity < required)
			capacity = required;

		ResizeActiveWindowStack(capacity);
	}

	void MaybeShrinkActiveWindowStack()
	{
		const int capacity = OwnerDraw::ActiveWindowStackCapacity;
		const int count = OwnerDraw::ActiveWindowStackCount;

		if (capacity <= 10 || count * 3 > capacity)
			return;

		ResizeActiveWindowStack(std::max(capacity / 2, 10));
	}

	void RemoveActiveWindow(HWND hWnd)
	{
		for (int index = 0; index < OwnerDraw::ActiveWindowStackCount; )
		{
			if (OwnerDraw::ActiveWindowStack[index] != hWnd)
			{
				++index;
				continue;
			}

			const int last = OwnerDraw::ActiveWindowStackCount - 1;
			if (index < last)
			{
				std::memmove(
					&OwnerDraw::ActiveWindowStack[index],
					&OwnerDraw::ActiveWindowStack[index + 1],
					sizeof(HWND) * (last - index));
			}

			OwnerDraw::ActiveWindowStackCount = last;
			MaybeShrinkActiveWindowStack();
		}
	}

	LRESULT BringOwnerDrawWindowToTop(HWND hWnd, WPARAM wParam, LPARAM lParam)
	{
		const HWND previousTop = GetActiveWindowStackTop();
		const HWND target = wParam ? reinterpret_cast<HWND>(wParam) : hWnd;

		RemoveActiveWindow(target);

		if (lParam)
		{
			EnsureActiveWindowStackCapacity(OwnerDraw::ActiveWindowStackCount + 1);
			OwnerDraw::ActiveWindowStack[OwnerDraw::ActiveWindowStackCount++] = target;

			OwnerDraw::AboutToCallSetWindowPos = 1;
			::SetWindowPos(target, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
			OwnerDraw::AboutToCallSetWindowPos = 0;
		}

		return reinterpret_cast<LRESULT>(previousTop);
	}

	bool IsActiveWindowMessageBlocked(HWND hWnd, UINT message)
	{
		if (OwnerDraw::ActiveWindowStackCount <= 0)
			return false;

		const HWND activeTop = GetActiveWindowStackTop();

		bool belongsToActiveWindow = ::GetParent(hWnd) == nullptr;
		if (::GetWindowLongA(hWnd, GWL_ID) <= 0)
			belongsToActiveWindow = true;

		for (HWND walker = hWnd; walker; walker = ::GetParent(walker))
		{
			if (walker == activeTop)
			{
				belongsToActiveWindow = true;
				break;
			}
		}

		bool allowedOutsideActiveWindow = false;
		if (message < WM_MOUSEFIRST || message > WM_MBUTTONDBLCLK)
			allowedOutsideActiveWindow = true;
		if (message >= WM_NCMOUSEMOVE && message <= WM_NCMBUTTONDBLCLK)
			allowedOutsideActiveWindow = false;
		if (message >= WM_KEYFIRST && message <= WM_KEYLAST)
			allowedOutsideActiveWindow = false;
		if (message == WM_SYSKEYUP || message == WM_SYSKEYDOWN || message == WM_SYSCOMMAND || message == WM_SYSCHAR)
			allowedOutsideActiveWindow = true;
		if (message == WW_UNKNOWN49B || message == WM_TIMER || message == WW_LB_GETCELLTEXT)
			allowedOutsideActiveWindow = false;

		return !belongsToActiveWindow && !allowedOutsideActiveWindow;
	}

	bool HandleWindowPosChanging(HWND hWnd, LPARAM lParam, LRESULT& result)
	{
		if (OwnerDraw::AboutToCallSetWindowPos == 1 || OwnerDraw::ActiveWindowStackCount <= 0)
			return false;

		int index = -1;
		for (int i = 0; i < OwnerDraw::ActiveWindowStackCount; ++i)
		{
			if (OwnerDraw::ActiveWindowStack[i] == hWnd)
			{
				index = i;
				break;
			}
		}

		if (index < 0)
			return false;

		auto pPosition = reinterpret_cast<WINDOWPOS*>(lParam);
		if (!pPosition)
		{
			result = 0;
			return true;
		}

		if (index == OwnerDraw::ActiveWindowStackCount - 1
			&& pPosition->hwndInsertAfter
			&& !(pPosition->flags & SWP_NOZORDER))
		{
			pPosition->hwndInsertAfter = nullptr;
		}
		else
		{
			pPosition->flags |= SWP_NOZORDER | SWP_NOOWNERZORDER;
		}

		::InvalidateRect(hWnd, nullptr, FALSE);
		result = 0;
		return true;
	}

	using ScalarDeletingDestructor = void* (__thiscall*)(Surface*, unsigned int);

	void DeleteSurfaceObject(Surface*& pSurface)
	{
		if (!pSurface)
			return;

		const auto pDestructor = (*reinterpret_cast<ScalarDeletingDestructor**>(pSurface))[0];
		pDestructor(pSurface, 1);
		pSurface = nullptr;
	}

	void RestoreAndClearTooltipIfNeeded(HWND hWnd, UINT message)
	{
		auto& tooltip = OwnerDraw::TooltipBlitState;

		if (hWnd != tooltip.OwnerHwnd || !tooltip.Active)
			return;

		if (message != WM_NCDESTROY && message != WM_SHOWWINDOW && message != WM_KILLFOCUS)
			return;

		const bool restoredBackground = !tooltip.BackgroundRestored && tooltip.BackingSurface;
		if (!tooltip.BackgroundRestored)
		{
			OwnerDraw::RestoreTooltipBackground();
			if (restoredBackground)
				RenderDX::UpdateScreen(DSurface::Primary);
		}

		DeleteSurfaceObject(tooltip.BackingSurface);
		tooltip.Active = 0;
		tooltip.BackgroundRestored = 0;
	}

	void InsetSurfaceRect(RectangleStruct& rect, int x, int y)
	{
		rect.X += x;
		rect.Y += y;
		rect.Width -= 2 * x;
		rect.Height -= 2 * y;
	}

	void CopyAlternateToPrimary(const RectangleStruct& destRect, const RectangleStruct& sourceRect)
	{
		if (!DSurface::Primary || !DSurface::Alternate)
			return;

		DSurface::Primary->Lock(0, 0);
		DSurface::Alternate->Lock(0, 0);
		DSurface::Primary->CopyFromPart(
			const_cast<RectangleStruct*>(&destRect),
			DSurface::Alternate,
			const_cast<RectangleStruct*>(&sourceRect),
			false,
			true);
		DSurface::Alternate->Unlock();
		DSurface::Primary->Unlock();
	}

	constexpr int ScrollBarButtonHeight = 22;
	constexpr int ScrollBarMinimumThumbHeight = 14;
	constexpr int ScrollBarInitialRepeatMs = 500;
	constexpr int ScrollBarRepeatMs = 25;

	int ConvertRGBToSurfaceColor(COLORREF color)
	{
		if (color == static_cast<COLORREF>(-1))
			return -1;

		return Drawing::RGB_To_Int(GetRValue(color), GetGValue(color), GetBValue(color));
	}

	COLORREF AverageColor(COLORREF first, COLORREF second)
	{
		return RGB(
			(GetRValue(first) + GetRValue(second)) / 2,
			(GetGValue(first) + GetGValue(second)) / 2,
			(GetBValue(first) + GetBValue(second)) / 2);
	}

	WORD BlendSurfacePixel(WORD destination, WORD source, int alpha)
	{
		const int inverseAlpha = 255 - alpha;
		const WORD redMask = OwnerDraw::ColorShiftRed;
		const WORD greenMask = OwnerDraw::ColorShiftGreen;
		const WORD blueMask = OwnerDraw::ColorShiftBlue;

		return static_cast<WORD>(
			((((source & redMask) * alpha + (destination & redMask) * inverseAlpha) >> 8) & redMask)
			| ((((source & greenMask) * alpha + (destination & greenMask) * inverseAlpha) >> 8) & greenMask)
			| ((((source & blueMask) * alpha + (destination & blueMask) * inverseAlpha) >> 8) & blueMask));
	}

	WORD BlendSurfacePixelTowardMasks(WORD destination, int alpha)
	{
		const int inverseAlpha = 255 - alpha;
		const WORD redMask = OwnerDraw::ColorShiftRed;
		const WORD greenMask = OwnerDraw::ColorShiftGreen;
		const WORD blueMask = OwnerDraw::ColorShiftBlue;

		return static_cast<WORD>(
			(((redMask * alpha + (destination & redMask) * inverseAlpha) >> 8) & redMask)
			| (((greenMask * alpha + (destination & greenMask) * inverseAlpha) >> 8) & greenMask)
			| (((blueMask * alpha + (destination & blueMask) * inverseAlpha) >> 8) & blueMask));
	}

	void BlendFillRect(const RectangleStruct& rect, Surface* pSurface, WORD color, int alpha)
	{
		if (!pSurface || alpha <= 0 || rect.Width <= 0 || rect.Height <= 0)
			return;

		auto pPixels = static_cast<WORD*>(pSurface->Lock(0, 0));
		if (!pPixels)
			return;

		const int pitch = pSurface->GetPitch() / 2;
		const int left = std::max(rect.X, 0);
		const int top = std::max(rect.Y, 0);
		const int right = std::min(rect.X + rect.Width, pSurface->GetWidth());
		const int bottom = std::min(rect.Y + rect.Height, pSurface->GetHeight());

		for (int y = top; y < bottom; ++y)
		{
			auto pLine = &pPixels[y * pitch + left];
			for (int x = left; x < right; ++x)
			{
				*pLine = BlendSurfacePixel(*pLine, color, alpha);
				++pLine;
			}
		}

		pSurface->Unlock();
	}

	void BlendGradientRect(const RectangleStruct& rect, Surface* pSurface, WORD color, int widthScale)
	{
		if (!pSurface || rect.Width <= 0 || rect.Height <= 0)
			return;

		int fillWidth = static_cast<int>((static_cast<long long>(rect.Width) * widthScale) >> 16);
		if (fillWidth < 0)
			return;

		if (!fillWidth)
			fillWidth = 1;

		auto pPixels = static_cast<WORD*>(pSurface->Lock(0, 0));
		if (!pPixels)
			return;

		const int pitch = pSurface->GetPitch() / 2;
		const int quarterHeight = rect.Height / 4;
		bool useGradient = true;

		for (int y = 0; y < rect.Height; ++y)
		{
			if (y == 3 * quarterHeight)
				useGradient = true;

			if (y == quarterHeight)
				useGradient = false;

			auto pLine = &pPixels[(rect.Y + y) * pitch + rect.X];
			int alphaNumerator = 255;

			for (int x = 0; x < fillWidth; ++x)
			{
				if (useGradient)
				{
					const int alpha = (alphaNumerator / rect.Width) & 0xFF;
					*pLine = BlendSurfacePixel(*pLine, color, alpha);
				}
				else
				{
					*pLine = color;
				}

				++pLine;
				alphaNumerator += 255;
			}
		}

		pSurface->Unlock();
	}

	bool DrawAlphaLine(DSurface* pSurface, Point2D start, Point2D end, WORD color, BYTE alpha)
	{
		if (!pSurface)
			return false;

		if (start.Y == end.Y)
		{
			if (start.X > end.X)
				std::swap(start.X, end.X);

			auto pPixels = static_cast<WORD*>(pSurface->Lock(start.X, start.Y));
			if (!pPixels)
				return false;

			for (int x = start.X; x <= end.X; ++x)
			{
				*pPixels = BlendSurfacePixel(*pPixels, color, alpha);
				++pPixels;
			}

			pSurface->Unlock();
			return true;
		}

		if (start.X == end.X)
		{
			const int step = start.Y <= end.Y ? pSurface->GetPitch() : -pSurface->GetPitch();
			const int count = std::abs(end.Y - start.Y) + 1;
			auto pPixelBytes = static_cast<BYTE*>(pSurface->Lock(start.X, start.Y));
			if (!pPixelBytes)
				return false;

			for (int i = 0; i < count; ++i)
			{
				auto pPixel = reinterpret_cast<WORD*>(pPixelBytes);
				*pPixel = BlendSurfacePixel(*pPixel, color, alpha);
				pPixelBytes += step;
			}

			pSurface->Unlock();
			return true;
		}

		return false;
	}

	bool DrawAlphaBeveledRect(
		DSurface* pSurface,
		const RectangleStruct& rect,
		bool raised,
		int thickness,
		BYTE leftAlpha,
		BYTE topAlpha,
		BYTE rightAlpha,
		BYTE bottomAlpha)
	{
		if (!pSurface || thickness <= 0)
			return false;

		const WORD topLeftColor = raised ? 0xFFFF : 0;
		const WORD bottomRightColor = raised ? 0 : 0xFFFF;
		bool result = raised;

		for (int layer = 0; layer < thickness; ++layer)
		{
			Point2D start { rect.X + layer, rect.Y + layer };
			Point2D end { rect.X + rect.Width - layer - 2, rect.Y + layer };
			result = DrawAlphaLine(pSurface, start, end, topLeftColor, topAlpha);

			start = { rect.X + layer, rect.Y + rect.Height - layer - 1 };
			end = { rect.X + rect.Width - layer - 1, start.Y };
			result = DrawAlphaLine(pSurface, start, end, bottomRightColor, bottomAlpha);

			start = { rect.X + layer, rect.Y + layer + 1 };
			end = { start.X, rect.Y + rect.Height - layer - 1 };
			result = DrawAlphaLine(pSurface, start, end, topLeftColor, leftAlpha);

			start = { rect.X + rect.Width - layer - 1, rect.Y + layer };
			end = { start.X, rect.Y + rect.Height - layer - 2 };
			result = DrawAlphaLine(pSurface, start, end, bottomRightColor, rightAlpha);
		}

		return result;
	}

	int DrawBeveledBorder(Surface* pSurface, const RectangleStruct& rect, int thickness, int color)
	{
		if (!pSurface || thickness <= 0)
			return thickness - 1;

		int lineColor = color;
		if (lineColor == -1 && OwnerDraw::DefaultBorderColor != static_cast<COLORREF>(-1))
			lineColor = ConvertRGBToSurfaceColor(OwnerDraw::DefaultBorderColor);

		const int lightColor = ConvertRGBToSurfaceColor(OwnerDraw::BevelLightColor);
		const int shadowColor = ConvertRGBToSurfaceColor(OwnerDraw::BevelShadowColor);
		const int averageColor = ConvertRGBToSurfaceColor(AverageColor(OwnerDraw::BevelLightColor, OwnerDraw::BevelShadowColor));

		const int leftBase = rect.X - thickness;
		const int topBase = rect.Y - thickness;
		const int rightBase = leftBase + rect.Width + 2 * thickness - 1;
		const int bottomBase = topBase + rect.Height + 2 * thickness - 1;

		for (int layer = 0; layer < thickness; ++layer)
		{
			int topLeftColor = lineColor;
			int bottomRightColor = lineColor;

			if (thickness == 2)
			{
				topLeftColor = layer == 0 ? lightColor : shadowColor;
				bottomRightColor = layer == 0 ? shadowColor : lightColor;
			}

			const int left = leftBase + layer;
			const int top = topBase + layer;
			const int right = rightBase - layer;
			const int bottom = bottomBase - layer;

			Point2D start { left, top };
			Point2D end { right - 1, top };
			pSurface->DrawLine(&start, &end, topLeftColor);

			start = { left, top + 1 };
			end = { left, bottom };
			pSurface->DrawLine(&start, &end, topLeftColor);

			start = { left, bottom };
			end = { right, bottom };
			pSurface->DrawLine(&start, &end, bottomRightColor);

			start = { right, top };
			end = { right, bottom - 1 };
			pSurface->DrawLine(&start, &end, bottomRightColor);

			if (thickness == 2)
			{
				Point2D corner { right, top };
				pSurface->SetPixel(&corner, averageColor);
				corner = { left, bottom };
				pSurface->SetPixel(&corner, averageColor);
			}
		}

		return 0;
	}

	BSurface* GetPCXSurface(const char* pFilename)
	{
		return PCX::Instance.GetSurface(pFilename, nullptr);
	}

	bool BlitTiledPCX(const RectangleStruct& rect, Surface* pDestination, Surface* pSource, int offsetX, int offsetY)
	{
		if (!pDestination || !pSource || rect.Width <= 0 || rect.Height <= 0)
			return false;

		auto pDestPixels = static_cast<WORD*>(pDestination->Lock(0, 0));
		if (!pDestPixels)
			return false;

		auto pSourcePixels = static_cast<WORD*>(pSource->Lock(0, 0));
		if (!pSourcePixels)
		{
			pDestination->Unlock();
			return false;
		}

		const int destPitch = pDestination->GetPitch() / 2;
		const int sourcePitch = pSource->GetPitch() / 2;
		const int sourceWidth = pSource->GetWidth();
		const int sourceHeight = pSource->GetHeight();
		const int sourceStartX = offsetX + std::max((sourceWidth - rect.Width) / 2, 0);
		int sourceY = offsetY + std::max((sourceHeight - rect.Height) / 2, 0);

		for (int y = 0; y < rect.Height; ++y)
		{
			int sourceX = sourceStartX;
			auto pDestLine = &pDestPixels[rect.X + destPitch * (rect.Y + y)];
			for (int x = 0; x < rect.Width; ++x)
			{
				*pDestLine++ = pSourcePixels[sourcePitch * (sourceY % sourceHeight) + (sourceX % sourceWidth)];
				++sourceX;
			}

			++sourceY;
		}

		pSource->Unlock();
		pDestination->Unlock();
		return true;
	}

	bool CopySurfacePart(Surface* pDestination, const RectangleStruct& toRect, Surface* pSource, const RectangleStruct& fromRect)
	{
		if (!pDestination || !pSource)
			return false;

		auto dest = toRect;
		auto source = fromRect;
		return pDestination->CopyFromPart(&dest, pSource, &source, false, true);
	}

	void DrawPCXCopy(Surface* pDestination, const RectangleStruct& rect, BSurface* pPCX)
	{
		if (!pPCX)
			return;

		RectangleStruct sourceRect { 0, 0, pPCX->GetWidth(), pPCX->GetHeight() };
		RectangleStruct destRect { rect.X, rect.Y, pPCX->GetWidth(), pPCX->GetHeight() };
		CopySurfacePart(pDestination, destRect, pPCX, sourceRect);
	}

	void DrawScrollArrow(Surface* pSurface, const RectangleStruct& rect, bool isUp, bool pressed, bool useGreyArt)
	{
		char filename[32] {};
		std::snprintf(filename, sizeof(filename), isUp ? "guparrow%c.pcx" : "gdnarrow%c.pcx", pressed ? 'p' : 'r');

		const char* pFilename = useGreyArt ? filename : filename + 1;
		DrawPCXCopy(pSurface, rect, GetPCXSurface(pFilename));
	}

	void BlendScrollBarCache(OwnerDrawDialogElement& data, int width, int height)
	{
		auto pPixels = static_cast<WORD*>(data.CacheSurface->Lock(0, 0));
		if (!pPixels)
			return;

		const int pixelCount = width * height;
		const int alpha = static_cast<unsigned char>(data.Alpha);

		for (int i = 0; i < pixelCount; ++i)
		{
			pPixels[i] = BlendSurfacePixelTowardMasks(pPixels[i], alpha);
		}

		data.CacheSurface->Unlock();
	}

	void EnsureScrollBarCache(
		OwnerDrawDialogElement& data,
		OwnerDrawDialogElement* pParentData,
		const RectangleStruct& localRect,
		const RectangleStruct& parentSourceRect)
	{
		if (data.CacheSurface)
		{
			if (data.CacheSurface->GetWidth() != localRect.Width || data.CacheSurface->GetHeight() != localRect.Height)
				DeleteSurfaceObject(data.CacheSurface);
		}

		if (data.CacheSurface || localRect.Width <= 0 || localRect.Height <= 0)
			return;

		data.CacheSurface = GameCreate<BSurface>(localRect.Width, localRect.Height);
		++OwnerDraw::CachedSurfaceCount;

		if (pParentData && pParentData->CacheSurface)
			CopySurfacePart(data.CacheSurface, localRect, pParentData->CacheSurface, parentSourceRect);

		BlendScrollBarCache(data, localRect.Width, localRect.Height);
	}

	void PaintScrollBar(
		HWND hWnd,
		OwnerDrawDialogElement& data,
		const RECT& clientRect,
		const RECT& scrollBarRect,
		int thumbTop,
		int thumbBottom,
		bool upPressed,
		bool downPressed)
	{
		if (!DSurface::Alternate)
			return;

		RectangleStruct localRect { 0, 0, clientRect.right, clientRect.bottom };
		RectangleStruct destRect { scrollBarRect.left, scrollBarRect.top, clientRect.right, clientRect.bottom };

		const HWND parentHwnd = ::GetParent(hWnd);
		auto pParentData = parentHwnd ? FindOwnerDrawData(parentHwnd) : nullptr;

		RECT parentRect {};
		if (parentHwnd)
			OwnerDraw::GetRectangle(parentHwnd, &parentRect);

		RectangleStruct parentSourceRect = localRect;
		if (pParentData && pParentData->CacheSurface)
		{
			parentSourceRect.X = scrollBarRect.left - parentRect.left;
			parentSourceRect.Y = scrollBarRect.top - parentRect.top;
		}

		EnsureScrollBarCache(data, pParentData, localRect, parentSourceRect);

		if (pParentData && pParentData->CacheSurface)
			CopySurfacePart(DSurface::Alternate, destRect, pParentData->CacheSurface, parentSourceRect);

		const bool disabled = data.ScrollBarDisabled();
		int borderColor = ConvertRGBToSurfaceColor(disabled ? OwnerDraw::AltBorderColor : OwnerDraw::DefaultBorderColor);
		if (disabled && OwnerDraw::AltBorderColor == static_cast<COLORREF>(-1))
			borderColor = -1;

		DrawBeveledBorder(DSurface::Alternate, destRect, 2, borderColor);

		RectangleStruct thumbRect
		{
			scrollBarRect.left,
			scrollBarRect.top + thumbTop,
			clientRect.right,
			thumbBottom - thumbTop
		};

		if (auto pGripMiddle = GetPCXSurface(disabled ? "gsbgripm.pcx" : "sbgripm.pcx"))
		{
			auto middleRect = thumbRect;
			middleRect.Width = pGripMiddle->GetWidth();
			BlitTiledPCX(middleRect, DSurface::Alternate, pGripMiddle, 0, 0);
		}

		if (auto pGripTop = GetPCXSurface(disabled ? "gsbgript.pcx" : "sbgript.pcx"))
			DrawPCXCopy(DSurface::Alternate, thumbRect, pGripTop);

		if (auto pGripBottom = GetPCXSurface(disabled ? "gsbgripb.pcx" : "sbgripb.pcx"))
		{
			RectangleStruct bottomRect
			{
				thumbRect.X,
				scrollBarRect.top + thumbBottom - pGripBottom->GetHeight(),
				thumbRect.Width,
				thumbRect.Height
			};

			DrawPCXCopy(DSurface::Alternate, bottomRect, pGripBottom);
		}

		RectangleStruct upButtonRect { scrollBarRect.left, scrollBarRect.top, clientRect.right, ScrollBarButtonHeight };
		RectangleStruct upButtonSource { 0, 0, clientRect.right, ScrollBarButtonHeight };
		CopySurfacePart(DSurface::Alternate, upButtonRect, data.CacheSurface, upButtonSource);

		const BYTE bevelAlpha = static_cast<BYTE>(OwnerDraw::ScrollButtonBevelAlpha);
		DrawAlphaBeveledRect(DSurface::Alternate, upButtonRect, !upPressed, 2, bevelAlpha, bevelAlpha, bevelAlpha, bevelAlpha);
		DrawScrollArrow(DSurface::Alternate, upButtonRect, true, upPressed, disabled);

		RectangleStruct downButtonRect
		{
			scrollBarRect.left,
			scrollBarRect.bottom - ScrollBarButtonHeight,
			clientRect.right,
			ScrollBarButtonHeight
		};
		RectangleStruct downButtonSource { 0, clientRect.bottom - ScrollBarButtonHeight, clientRect.right, ScrollBarButtonHeight };
		CopySurfacePart(DSurface::Alternate, downButtonRect, data.CacheSurface, downButtonSource);
		DrawAlphaBeveledRect(DSurface::Alternate, downButtonRect, !downPressed, 2, bevelAlpha, bevelAlpha, bevelAlpha, bevelAlpha);
		DrawScrollArrow(DSurface::Alternate, downButtonRect, false, downPressed, disabled);
	}

	void NotifyChildren(HWND hWnd, UINT message)
	{
		OwnerDrawHWNDVector children {};
		::EnumChildWindows(hWnd, OwnerDraw::CollectChildHwndProc, reinterpret_cast<LPARAM>(&children));

		for (int i = 0; i < children.Count; ++i)
			::SendMessageA(children.Items[i], message, 0, 0);

		if (children.Items)
			YRMemory::Deallocate(children.Items);
	}

	void RepaintChildWindows(HWND hWnd, HWND ownerHwnd, const RECT& ownerDrawRect)
	{
		OwnerDrawHWNDVector children {};
		::EnumChildWindows(hWnd, OwnerDraw::CollectChildHwndProc, reinterpret_cast<LPARAM>(&children));

		HWND comboDropHwnd = nullptr;

		for (int i = 0; i < children.Count; ++i)
		{
			const HWND childHwnd = children.Items[i];
			const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, childHwnd);

			if (pOriginalWndProc == OwnerDraw::ComboDropWindowHandler)
			{
				comboDropHwnd = childHwnd;
				continue;
			}

			::InvalidateRect(childHwnd, nullptr, FALSE);
			::UpdateWindow(childHwnd);
		}

		if (comboDropHwnd)
		{
			::InvalidateRect(comboDropHwnd, nullptr, FALSE);
			::UpdateWindow(comboDropHwnd);
		}
		else if (OwnerDraw::ComboDropActiveDropHwnd && OwnerDraw::ComboDropActiveParentHwnd == ownerHwnd)
		{
			RECT dropRect {};
			OwnerDraw::GetRectangle(OwnerDraw::ComboDropActiveDropHwnd, &dropRect);

			RECT intersect {};
			if (::IntersectRect(&intersect, &ownerDrawRect, &dropRect))
			{
				::InvalidateRect(OwnerDraw::ComboDropActiveDropHwnd, nullptr, FALSE);
				::UpdateWindow(OwnerDraw::ComboDropActiveDropHwnd);
			}
		}

		if (children.Items)
			YRMemory::Deallocate(children.Items);
	}

	void RepaintOverlappingPreviousSibling(HWND hWnd, HWND ownerHwnd, const RECT& ownerDrawRect, int paintCopyMode)
	{
		if (paintCopyMode <= 0 || (hWnd != ownerHwnd && OwnerDraw::PaintDepth != 1))
			return;

		for (HWND sibling = ownerHwnd; sibling; )
		{
			sibling = ::GetWindow(sibling, GW_HWNDPREV);
			if (!sibling)
				break;

			if (!::GetWindowLongA(sibling, DialogProcWindowLongIndex))
				continue;

			RECT siblingRect {};
			OwnerDraw::GetRectangle(sibling, &siblingRect);

			RECT intersect {};
			if (::IntersectRect(&intersect, &ownerDrawRect, &siblingRect))
			{
				::InvalidateRect(sibling, nullptr, FALSE);
				::UpdateWindow(sibling);
				break;
			}
		}
	}

	void RestoreTooltipBackgroundForPaint(const RECT& ownerDrawRect, bool& redrawTooltip)
	{
		auto& tooltip = OwnerDraw::TooltipBlitState;

		const RECT tooltipRect
		{
			tooltip.Rect.X,
			tooltip.Rect.Y,
			tooltip.Rect.X + tooltip.Rect.Width + 1,
			tooltip.Rect.Y + tooltip.Rect.Height + 1
		};

		RECT intersect {};
		if (OwnerDraw::PaintDepth != 1
			|| !::IntersectRect(&intersect, &ownerDrawRect, &tooltipRect)
			|| !tooltip.Active
			|| tooltip.BackgroundRestored
			|| !tooltip.BackingSurface
			|| !DSurface::Primary)
		{
			return;
		}

		RectangleStruct targetRect { tooltip.Rect.X, tooltip.Rect.Y, tooltip.Rect.Width, tooltip.Rect.Height };
		RectangleStruct sourceRect { 0, 0, tooltip.Rect.Width, tooltip.Rect.Height };
		DSurface::Primary->CopyFromPart(&targetRect, tooltip.BackingSurface, &sourceRect, false, true);
		RenderDX::UpdateScreen(DSurface::Primary);
		tooltip.BackgroundRestored = 1;
		redrawTooltip = true;
	}

	void AccumulatePaintBounds(const RECT& ownerDrawRect)
	{
		if (OwnerDraw::PaintLeft >= ownerDrawRect.left)
			OwnerDraw::PaintLeft = ownerDrawRect.left;
		if (OwnerDraw::PaintTop >= ownerDrawRect.top)
			OwnerDraw::PaintTop = ownerDrawRect.top;
		if (OwnerDraw::PaintRight <= ownerDrawRect.right)
			OwnerDraw::PaintRight = ownerDrawRect.right;
		if (OwnerDraw::PaintBottom <= ownerDrawRect.bottom)
			OwnerDraw::PaintBottom = ownerDrawRect.bottom;
	}

	struct PaintRoot
	{
		HWND ProbeHwnd {};
		HWND OwnerHwnd {};
		OwnerDrawDialogElement* Data {};
	};

	PaintRoot FindPaintRoot(HWND hWnd)
	{
		HWND ownerHwnd = hWnd;
		HWND probeHwnd = hWnd;

		while (probeHwnd)
		{
			ownerHwnd = probeHwnd;
			if (::GetWindowLongA(probeHwnd, DialogProcWindowLongIndex))
				break;

			probeHwnd = ::GetParent(probeHwnd);
		}

		return PaintRoot
		{
			probeHwnd,
			ownerHwnd,
			probeHwnd ? FindOwnerDrawData(ownerHwnd) : nullptr
		};
	}

	LRESULT CallSelectedHandler(WNDPROC pSelectedWndProc, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!pSelectedWndProc)
			return 0;

		return ::CallWindowProcA(pSelectedWndProc, hWnd, message, wParam, lParam);
	}

	LRESULT DispatchPaintMessage(
		HWND hWnd,
		UINT message,
		WPARAM wParam,
		LPARAM lParam,
		WNDPROC pSelectedWndProc,
		const RECT& ownerDrawRect,
		int& paintCopyMode,
		bool& redrawTooltip)
	{
		auto pData = FindOwnerDrawData(hWnd);
		if (!pData)
			return CallSelectedHandler(pSelectedWndProc, hWnd, message, wParam, lParam);

		if (pData->NeedsControlImage)
		{
			::ValidateRect(hWnd, nullptr);
			return CallSelectedHandler(pSelectedWndProc, hWnd, WW_SCROLLBAR_UPDATETHUMB, wParam, lParam);
		}

		RestoreTooltipBackgroundForPaint(ownerDrawRect, redrawTooltip);
		AccumulatePaintBounds(ownerDrawRect);

		const auto root = FindPaintRoot(hWnd);
		int rootPaintState = 0;

		if (root.ProbeHwnd == hWnd)
		{
			if (!root.Data)
			{
				::ValidateRect(hWnd, nullptr);
				return 1;
			}

			rootPaintState = root.Data->Extra[PaintStateExtraIndex] < 1 ? 1 : root.Data->Extra[PaintStateExtraIndex];
		}
		else
		{
			if (!root.Data)
			{
				::ValidateRect(hWnd, nullptr);
				RepaintOverlappingPreviousSibling(hWnd, root.OwnerHwnd, ownerDrawRect, paintCopyMode);
				return 1;
			}

			rootPaintState = root.Data->Extra[PaintStateExtraIndex];
		}

		paintCopyMode = rootPaintState;
		LRESULT result = 1;

		if (rootPaintState < 1)
		{
			::ValidateRect(hWnd, nullptr);
		}
		else
		{
			result = CallSelectedHandler(pSelectedWndProc, hWnd, message, wParam, lParam);
			if (root.Data)
				root.Data->Extra[PaintStateExtraIndex] = rootPaintState;

			RepaintChildWindows(hWnd, root.OwnerHwnd, ownerDrawRect);
		}

		RepaintOverlappingPreviousSibling(hWnd, root.OwnerHwnd, ownerDrawRect, rootPaintState);
		return result;
	}

	void FinishPaint(HWND hWnd, OwnerDrawDialogElement* pData, int paintCopyMode, int windowOffsetX, int windowOffsetY)
	{
		if (!pData)
			return;

		if (::GetWindowLongA(hWnd, DialogProcWindowLongIndex) && OwnerDraw::PaintDepth > 1)
			pData->Extra[PaintStateExtraIndex] = 2;

		if (--OwnerDraw::PaintDepth != 0)
			return;

		if (!pData->NeedsControlImage && paintCopyMode >= 1 && !OwnerDraw::IsWebBrowserVisible())
		{
			const int paintLeft = OwnerDraw::PaintLeft;
			const int paintTop = OwnerDraw::PaintTop;
			const int paintWidth = OwnerDraw::PaintRight - OwnerDraw::PaintLeft;
			const int paintHeight = OwnerDraw::PaintBottom - OwnerDraw::PaintTop;

			if (paintWidth > 0 && paintHeight > 0)
			{
				RectangleStruct sourceRect { paintLeft, paintTop, paintWidth, paintHeight };
				RectangleStruct destRect { paintLeft + windowOffsetX, paintTop + windowOffsetY, paintWidth, paintHeight };

				if (pData->Extra[PaintStateExtraIndex] == 1)
				{
					pData->Extra[PaintStateExtraIndex] = 2;

					if (::GetWindowLongA(hWnd, DialogProcWindowLongIndex) && OwnerDraw::RunOpenAnimationIfNeeded(hWnd))
						pData->Extra[PaintStateExtraIndex] = 3;

					CopyAlternateToPrimary(destRect, sourceRect);
					NotifyChildren(hWnd, WW_EDIT_RESTOREFOCUS);
				}
				else
				{
					char className[0x80] {};
					::GetClassNameA(hWnd, className, sizeof(className));

					if (!std::strcmp(className, "ComboBox"))
					{
						InsetSurfaceRect(sourceRect, -1, -1);
						InsetSurfaceRect(destRect, -1, -1);
					}

					CopyAlternateToPrimary(destRect, sourceRect);
				}
			}
		}

		OwnerDraw::PaintRight = 0;
		OwnerDraw::PaintLeft = 0xFFFFFF;
		OwnerDraw::PaintTop = 0xFFFFFF;
		OwnerDraw::PaintBottom = 0;
	}

	void ReleaseElementText(OwnerDrawDialogElement& data)
	{
		if (!data.TextBuffer)
			return;

		YRMemory::Deallocate(data.TextBuffer);
		data.TextBuffer = nullptr;
	}

	void SetElementTextA(OwnerDrawDialogElement& data, const char* pText)
	{
		ReleaseElementText(data);

		if (pText && *pText)
		{
			const auto length = std::strlen(pText);
			data.TextBuffer = static_cast<wchar_t*>(YRMemory::Allocate(sizeof(wchar_t) * (length + 1)));
			std::swprintf(data.TextBuffer, length + 1, L"%hs", pText);
		}

		data.HasText = 1;
	}

	bool SetElementTextW(OwnerDrawDialogElement& data, const wchar_t* pText)
	{
		const bool changed = (!data.TextBuffer && pText)
			|| (data.TextBuffer && !pText)
			|| (data.TextBuffer && pText && std::wcscmp(data.TextBuffer, pText));

		ReleaseElementText(data);

		if (pText && *pText)
		{
			const auto length = std::wcslen(pText);
			data.TextBuffer = static_cast<wchar_t*>(YRMemory::Allocate(sizeof(wchar_t) * (length + 1)));
			std::wcscpy(data.TextBuffer, pText);
		}

		data.HasText = 0;
		return changed;
	}

	void CopyTextA(const OwnerDrawDialogElement& data, WPARAM length, LPARAM lParam)
	{
		if (!lParam)
			return;

		auto pBuffer = reinterpret_cast<char*>(lParam);
		*pBuffer = '\0';

		if (data.TextBuffer)
		{
			OwnerDraw::WideToCharString(pBuffer, data.TextBuffer, length);
			if (length)
				pBuffer[length - 1] = '\0';
		}
	}

	void CopyTextW(const OwnerDrawDialogElement& data, WPARAM length, LPARAM lParam)
	{
		if (!lParam)
			return;

		auto pBuffer = reinterpret_cast<wchar_t*>(lParam);
		*pBuffer = L'\0';

		if (data.TextBuffer)
		{
			std::wcsncpy(pBuffer, data.TextBuffer, length);
			if (length)
				pBuffer[length - 1] = L'\0';
		}
	}

	bool IsNativeTextMessage(UINT message, LRESULT& result)
	{
		switch (message)
		{
		case WM_SETTEXT:
			result = 0;
			return true;

		case WM_GETTEXT:
		case LB_GETTEXT:
			result = 0;
			return true;

		case CB_ADDSTRING:
		case CB_FINDSTRING:
		case CB_FINDSTRINGEXACT:
		case CB_SELECTSTRING:
		case CB_INSERTSTRING:
		case CB_GETLBTEXT:
		case LB_ADDSTRING:
		case LB_FINDSTRING:
		case LB_FINDSTRINGEXACT:
		case LB_SELECTSTRING:
		case LB_INSERTSTRING:
			result = -1;
			return true;

		default:
			return false;
		}
	}

	bool HandleElementTextMessage(
		OwnerDrawDialogElement& data,
		HWND hWnd,
		UINT message,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT& result,
		bool& callSelectedHandler)
	{
		switch (message)
		{
		case WW_SETHASTEXT:
			result = data.HasText == 0;
			callSelectedHandler = false;
			return true;

		case WW_GETTEXTA:
			CopyTextA(data, wParam, lParam);
			return true;

		case WW_GETTEXTW:
			CopyTextW(data, wParam, lParam);
			return true;

		case WW_SETUNKNOWNPROP50:
			data.LParam3 = lParam;
			return true;

		case WW_SETTEXTA:
			SetElementTextA(data, reinterpret_cast<const char*>(lParam));
			return true;

		case WW_SETUNKNOWNPROP30:
			result = data.Erase2;
			data.Erase2 = wParam;
			callSelectedHandler = false;
			return true;

		case WW_SETTEXTW:
		{
			const bool changed = SetElementTextW(data, reinterpret_cast<const wchar_t*>(lParam));
			if (changed && data.DrawMode == 1 && data.AnimationActive)
			{
				::KillTimer(hWnd, 0);
				data.AnimationActive = false;
				::SendMessageA(hWnd, WW_STATIC_REVEALTEXTS, 0, 0);
			}
			return true;
		}

		default:
			return false;
		}
	}

	void UpdateTooltipTextOnMouseMove(HWND hWnd, LPARAM lParam)
	{
		const HWND parentHwnd = ::GetParent(hWnd);
		const HWND tooltipHwnd = parentHwnd ? ::GetDlgItem(parentHwnd, OwnerDraw::TooltipText) : nullptr;
		if (!tooltipHwnd)
			return;

		WideWstring tooltipText;

		const LPARAM pointParam = MAKELPARAM(LOWORD(lParam), HIWORD(lParam));
		const auto hitCode = ::SendMessageA(hWnd, WW_QUERYTOOLTIPHIT, 0, pointParam);
		tooltipText = QueryTooltipText(parentHwnd, hWnd, hitCode);

		if (IsEmpty(tooltipText))
		{
			tooltipText = QueryTooltipText(parentHwnd, hWnd, -1);

			if (IsEmpty(tooltipText))
			{
				if (const auto label = OwnerDraw::GetTooltipStringLabel(parentHwnd, hWnd))
				{
					tooltipText = StringTable::LoadString(
						label,
						nullptr,
						"D:\\ra2mdpost\\ownrdraw.cpp",
						1957);
				}
				else
				{
					tooltipText = L"";
				}
			}
		}

		::SendMessageA(tooltipHwnd, WW_SETTEXTW, 0, reinterpret_cast<LPARAM>(GetWideTextBuffer(tooltipText)));
	}

	void CleanupDestroyedWindow(HWND hWnd)
	{
		if (auto pData = FindOwnerDrawData(hWnd))
		{
			if (pData->CacheSurface)
			{
				DeleteSurfaceObject(pData->CacheSurface);
				--OwnerDraw::CachedSurfaceCount;
			}
		}

		OwnerDraw::DialogProcs.erase(hWnd);
		OwnerDraw::Dialogs.erase(hWnd);
		OwnerDraw::SubclassProcs.erase(hWnd);

		const auto pUserData = reinterpret_cast<void*>(::GetWindowLongA(hWnd, GWL_USERDATA));
		if (pUserData)
			YRMemory::Deallocate(pUserData);

		::SetWindowLongA(hWnd, GWL_USERDATA, 0);
		SessionIpb::UnregisterHwnd(hWnd);
	}

	COLORREF ListBoxTextColor()
	{
		return Phobos::UI::ColorTextList;
	}

	COLORREF ListBoxSelectionFillColor()
	{
		return Phobos::UI::ColorSelectionList;
	}

	COLORREF ListBoxDisabledTextColor()
	{
		return Phobos::UI::ColorDisabledList;
	}

	void CharToWideString(wchar_t* pBuffer, int capacity, const char* pText);
	void WideToCharString(char* pBuffer, int capacity, const wchar_t* pText);

	COLORREF ComboBoxTextColor(bool disabled, bool alternatePalette)
	{
		if (alternatePalette)
			return disabled ? OwnerDraw::AltDisabledTextColor : OwnerDraw::AltComboTextColor;

		return disabled ? Phobos::UI::ColorDisabledCombobox : Phobos::UI::ColorTextCombobox;
	}

	void SyncComboDropSelectionColor()
	{
		OwnerDraw::ListSelectionFillColor = Phobos::UI::ColorSelectionCombobox;
	}

	constexpr int ComboBoxArrowWidth = 20;
	constexpr int ComboBoxArrowLeftOffset = 19;
	constexpr int ComboBoxMaxColorItems = 50;
	constexpr int ComboBoxTextEntryInlineBytes = 0;
	constexpr int ComboBoxEditListNotificationCode = 0x300;
	constexpr int ComboBoxParentEditChangeNotificationCode = 5;

	bool IsComboBoxDropDownList(HWND hWnd)
	{
		return (::GetWindowLongA(hWnd, GWL_STYLE) & 3) == CBS_DROPDOWNLIST;
	}

	bool IsComboBoxDropDown(HWND hWnd)
	{
		return (::GetWindowLongA(hWnd, GWL_STYLE) & 3) == CBS_DROPDOWN;
	}

	int BitFontHeight(BitFont* pFont)
	{
		if (!pFont)
			pFont = BitFont::Instance;

		if (!pFont)
			return 10;

		return pFont->field_1C;
	}

	void TrimComboTextToWidth(wchar_t* pText, size_t capacity, BitFont* pFont, int maxWidth)
	{
		if (!pText || !capacity || maxWidth <= 0)
			return;

		pText[capacity - 1] = L'\0';
		size_t length = std::wcslen(pText);
		if (!length)
			return;

		int textWidth = 0;
		int textHeight = 0;
		if (!pFont)
			pFont = BitFont::Instance;

		while (length > 0 && pFont)
		{
			pFont->GetTextDimension(pText, &textWidth, &textHeight, 0);
			if (textWidth <= maxWidth)
				break;

			--length;
			pText[length] = L'\0';
			if (length + 3 < capacity)
				std::wcscat(pText, L"...");
		}
	}

	WWUIComboBoxItem* AllocateComboBoxItem(OwnerDrawDialogElement& data, const wchar_t* pText, bool isWide)
	{
		if (!pText)
			pText = L"";

		const size_t length = std::wcslen(pText);
		const size_t bytes = sizeof(WWUIComboBoxItem) + (length + 1) * sizeof(wchar_t) + ComboBoxTextEntryInlineBytes;
		auto pEntry = static_cast<WWUIComboBoxItem*>(YRMemory::Allocate(bytes));
		if (!pEntry)
			return nullptr;

		pEntry->Next = data.ComboBoxTextEntries();
		pEntry->ItemData = 0;
		pEntry->Text = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(pEntry) + sizeof(WWUIComboBoxItem));
		pEntry->IsWideText = isWide ? 1 : 0;
		std::wcscpy(pEntry->Text, pText);
		data.ComboBoxTextEntries() = pEntry;
		return pEntry;
	}

	void RemoveComboBoxItem(OwnerDrawDialogElement& data, WWUIComboBoxItem* pEntry)
	{
		if (!pEntry)
			return;

		WWUIComboBoxItem* pPrevious = nullptr;
		for (auto pCurrent = data.ComboBoxTextEntries(); pCurrent; pCurrent = pCurrent->Next)
		{
			if (pCurrent != pEntry)
			{
				pPrevious = pCurrent;
				continue;
			}

			if (pPrevious)
				pPrevious->Next = pCurrent->Next;
			else
				data.ComboBoxTextEntries() = pCurrent->Next;

			YRMemory::Deallocate(pCurrent);
			return;
		}
	}

	WWUIComboBoxItem* GetComboBoxItem(WNDPROC pOriginalWndProc, HWND hWnd, int index)
	{
		const auto result = CallSelectedHandler(pOriginalWndProc, hWnd, CB_GETITEMDATA, index, 0);
		if (result == CB_ERR || !result)
			return nullptr;

		return reinterpret_cast<WWUIComboBoxItem*>(result);
	}

	LRESULT ForwardComboTextMessageToEditList(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (IsComboBoxDropDownList(hWnd))
			return CallSelectedHandler(FindWindowProc(OwnerDraw::DialogProcs, hWnd), hWnd, message, wParam, lParam);

		LRESULT result = 0;
		for (HWND child = ::GetWindow(hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
		{
			char className[16] {};
			::GetClassNameA(child, className, sizeof(className));
			if (_strcmpi(className, "listbox"))
				continue;

			if (message == WM_SETFOCUS)
			{
				::SetFocus(child);
				result = 0;
			}
			else
			{
				const UINT forwardedMessage = message == CB_LIMITTEXT ? EM_LIMITTEXT : message;
				result = ::SendMessageA(child, forwardedMessage, wParam, lParam);
			}
		}

		return result;
	}

	int ResolveComboBorderColor(COLORREF color, bool disabledColor)
	{
		if (color == static_cast<COLORREF>(-1))
			return disabledColor ? 0 : -1;

		return ConvertRGBToSurfaceColor(color);
	}

	void DrawComboDropButton(const RectangleStruct& rect, bool dropped, bool alternatePalette)
	{
		DrawScrollArrow(DSurface::Alternate, rect, dropped, dropped, alternatePalette);
	}

	void PaintComboBox(HWND hWnd, OwnerDrawDialogElement& data, const RECT& clientRect, const RECT& ownerRect, WNDPROC pOriginalWndProc)
	{
		if (!DSurface::Alternate)
			return;

		auto pFont = data.ComboBoxFont() ? data.ComboBoxFont() : BitFont::Instance;
		const bool dropped = ::SendMessageA(hWnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
		const int width = ownerRect.right - ownerRect.left;
		const int height = ownerRect.bottom - ownerRect.top;

		RectangleStruct comboRect
		{
			ownerRect.left,
			ownerRect.top,
			width,
			24
		};

		RectangleStruct localRect { 0, 0, width, height };
		RectangleStruct parentSourceRect = localRect;
		OwnerDrawDialogElement* pParentData = nullptr;
		if (const HWND parentHwnd = ::GetParent(hWnd))
		{
			pParentData = FindOwnerDrawData(parentHwnd);
			if (pParentData)
			{
				RECT parentRect {};
				OwnerDraw::GetRectangle(parentHwnd, &parentRect);

				if (pParentData->CacheSurface)
				{
					parentSourceRect.X = ownerRect.left - parentRect.left;
					parentSourceRect.Y = ownerRect.top - parentRect.top;
				}
			}
		}
		EnsureScrollBarCache(data, pParentData, localRect, parentSourceRect);

		OwnerDraw::CopyDimmedBackground(&comboRect, hWnd, static_cast<unsigned char>(data.Alpha));
		BlendFillRect(comboRect, DSurface::Alternate, 0, static_cast<unsigned char>(data.Alpha));

		const LONG style = ::GetWindowLongA(hWnd, GWL_STYLE);
		const bool disabled = (style & WS_DISABLED) != 0;
		const bool alternatePalette = data.ComboBoxUseAlternatePalette();
		const COLORREF borderColor = alternatePalette
			? (disabled ? OwnerDraw::AltDisabledBorderColor : OwnerDraw::AltBorderColor)
			: (disabled ? OwnerDraw::DisabledBorderColor : OwnerDraw::DefaultBorderColor);

		DrawBeveledBorder(DSurface::Alternate, comboRect, 2, ResolveComboBorderColor(borderColor, disabled));

		RectangleStruct textAreaRect { comboRect.X, comboRect.Y, comboRect.Width - ComboBoxArrowWidth, comboRect.Height };
		RectangleStruct buttonRect { ownerRect.right - ComboBoxArrowLeftOffset, comboRect.Y + 1, comboRect.Width, comboRect.Height };
		DrawComboDropButton(buttonRect, dropped, alternatePalette);

		if (disabled)
			BlendFillRect(comboRect, DSurface::Alternate, 0, static_cast<unsigned char>(data.Alpha));

		if ((style & 3) != CBS_DROPDOWNLIST)
		{
			::ValidateRect(hWnd, nullptr);
			return;
		}

		const int selectedIndex = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, CB_GETCURSEL, 0, 0));
		wchar_t textBuffer[0x100] {};
		if (auto pItem = GetComboBoxItem(pOriginalWndProc, hWnd, selectedIndex))
		{
			std::wcsncpy(textBuffer, pItem->Text ? pItem->Text : L"", std::size(textBuffer) - 1);
		}

		COLORREF textColor = ComboBoxTextColor(disabled, alternatePalette);
		if (data.ComboBoxUseItemColorOverrides()
			&& selectedIndex >= 0
			&& selectedIndex < ComboBoxMaxColorItems
			&& data.ComboBoxItemColorOverrides()[selectedIndex] >= 0)
		{
			textColor = static_cast<COLORREF>(data.ComboBoxItemColorOverrides()[selectedIndex]);
			auto fillRect = textAreaRect;
			InsetSurfaceRect(fillRect, 2, 2);
			DSurface::Alternate->FillRect(&fillRect, ConvertRGBToSurfaceColor(textColor));
		}

		TrimComboTextToWidth(textBuffer, std::size(textBuffer), pFont, clientRect.right - ComboBoxArrowWidth);

		RECT textRect
		{
			ownerRect.left + 2,
			ownerRect.top,
			ownerRect.right,
			ownerRect.bottom
		};

		OwnerDraw::DrawWideText(DSurface::Alternate, textBuffer, &textRect, pFont, textColor, 4, 12, 0, 0, 0);
		::ValidateRect(hWnd, nullptr);
	}

	LRESULT AddOrInsertComboString(
		OwnerDrawDialogElement& data,
		WNDPROC pOriginalWndProc,
		HWND hWnd,
		UINT message,
		WPARAM wParam,
		LPARAM lParam,
		bool wideText)
	{
		char narrowText[5120] {};
		wchar_t wideBuffer[5120] {};

		const LPARAM nativeTextParam = [&]() -> LPARAM
		{
			if (wideText)
			{
				const auto pWideText = reinterpret_cast<const wchar_t*>(lParam);
				std::wcsncpy(wideBuffer, pWideText ? pWideText : L"", std::size(wideBuffer) - 1);
				WideToCharString(narrowText, std::size(narrowText), wideBuffer);
				return reinterpret_cast<LPARAM>(narrowText);
			}

			const auto pText = reinterpret_cast<const char*>(lParam);
			std::strncpy(narrowText, pText ? pText : "", std::size(narrowText) - 1);
			CharToWideString(wideBuffer, std::size(wideBuffer), narrowText);
			return reinterpret_cast<LPARAM>(narrowText);
		}();

		const bool add = message == WW_CB_ADDSTRINGA || message == WW_CB_ADDSTRINGW;
		const UINT nativeMessage = add ? CB_ADDSTRING : CB_INSERTSTRING;
		const WPARAM nativeIndex = add ? 0 : wParam;
		const auto nativeResult = CallSelectedHandler(pOriginalWndProc, hWnd, nativeMessage, nativeIndex, nativeTextParam);
		if (nativeResult == CB_ERR || nativeResult == CB_ERRSPACE)
			return nativeResult;

		const int itemIndex = static_cast<int>(nativeResult);
		auto pEntry = AllocateComboBoxItem(data, wideBuffer, wideText);
		if (!pEntry)
		{
			CallSelectedHandler(pOriginalWndProc, hWnd, CB_DELETESTRING, itemIndex, 0);
			return CB_ERRSPACE;
		}

		const auto setDataResult = CallSelectedHandler(
			pOriginalWndProc,
			hWnd,
			CB_SETITEMDATA,
			itemIndex,
			reinterpret_cast<LPARAM>(pEntry));

		if (setDataResult == CB_ERR || setDataResult == CB_ERRSPACE)
		{
			CallSelectedHandler(pOriginalWndProc, hWnd, CB_DELETESTRING, itemIndex, 0);
			RemoveComboBoxItem(data, pEntry);
			return setDataResult;
		}

		return itemIndex;
	}

	LRESULT FindComboString(OwnerDrawDialogElement& data, WNDPROC pOriginalWndProc, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool wideText, bool exact, bool select)
	{
		(void)data;
		(void)message;

		wchar_t needle[5120] {};
		if (wideText)
		{
			const auto pText = reinterpret_cast<const wchar_t*>(lParam);
			std::wcsncpy(needle, pText ? pText : L"", std::size(needle) - 1);
		}
		else
		{
			CharToWideString(needle, std::size(needle), reinterpret_cast<const char*>(lParam));
		}

		const int count = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, CB_GETCOUNT, 0, 0));
		if (count == CB_ERR)
			return 0;

		int index = static_cast<int>(wParam);
		if (index < 0)
			index = 0;

		if (index >= count)
			return CB_ERR;

		const size_t needleLength = std::wcslen(needle);
		for (; index < count; ++index)
		{
			const auto pEntry = GetComboBoxItem(pOriginalWndProc, hWnd, index);
			const wchar_t* pText = pEntry && pEntry->Text ? pEntry->Text : L"";
			const bool match = exact
				? _wcsicmp(needle, pText) == 0
				: _wcsnicmp(needle, pText, needleLength) == 0;

			if (!match)
				continue;

			if (select)
				return ::SendMessageA(hWnd, CB_SETCURSEL, index, 0);

			return index;
		}

		return CB_ERR;
	}

	LRESULT GetComboText(WNDPROC pOriginalWndProc, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, bool wideOutput)
	{
		if (lParam && message != WW_CB_GETITEMTEXTFORMAT && message != CB_GETLBTEXTLEN)
		{
			if (wideOutput)
				*reinterpret_cast<wchar_t*>(lParam) = L'\0';
			else
				*reinterpret_cast<char*>(lParam) = '\0';
		}

		if (static_cast<int>(wParam) == -1)
			return 0;

		auto pEntry = GetComboBoxItem(pOriginalWndProc, hWnd, static_cast<int>(wParam));
		if (!pEntry)
			return 0;

		if (message == WW_CB_GETITEMTEXTFORMAT)
			return pEntry->IsWideText;

		const wchar_t* pText = pEntry->Text ? pEntry->Text : L"";
		const auto length = static_cast<LRESULT>(std::wcslen(pText));

		if (message == WW_CB_GETLBTEXTA || message == WW_CB_GETLBTEXTW)
		{
			if (lParam)
			{
				if (wideOutput)
					std::wcscpy(reinterpret_cast<wchar_t*>(lParam), pText);
				else
					WideToCharString(reinterpret_cast<char*>(lParam), static_cast<int>(length + 1), pText);
			}
		}

		return length < 0 ? 0 : length;
	}

	LRESULT SetComboSelection(OwnerDrawDialogElement& data, WNDPROC pOriginalWndProc, HWND hWnd, WPARAM wParam)
	{
		const int selection = static_cast<int>(wParam);
		data.ComboBoxCurrentSelection() = selection;

		if (selection == -1)
		{
			::SendMessageA(hWnd, WW_SETTEXTA, 0, reinterpret_cast<LPARAM>(""));
		}
		else if (auto pEntry = GetComboBoxItem(pOriginalWndProc, hWnd, selection))
		{
			if (pEntry->IsWideText)
			{
				::SendMessageA(hWnd, WW_SETTEXTW, 0, reinterpret_cast<LPARAM>(pEntry->Text ? pEntry->Text : L""));
			}
			else
			{
				char buffer[5120] {};
				WideToCharString(buffer, std::size(buffer), pEntry->Text ? pEntry->Text : L"");
				::SendMessageA(hWnd, WW_SETTEXTA, 0, reinterpret_cast<LPARAM>(buffer));
			}
		}

		if (IsComboBoxDropDown(hWnd))
			return 0;

		::InvalidateRect(hWnd, nullptr, FALSE);
		return CallSelectedHandler(pOriginalWndProc, hWnd, CB_SETCURSEL, wParam, 0);
	}

	void CloseComboDropDown(OwnerDrawDialogElement& data, HWND hWnd)
	{
		const HWND dropHwnd = data.ComboBoxDropDownHwnd();
		if (!dropHwnd)
			return;

		::ReleaseCapture();
		if (const HWND parentHwnd = ::GetParent(hWnd))
			::SendMessageA(parentHwnd, WW_BRINGTOTOP, reinterpret_cast<WPARAM>(dropHwnd), 0);

		::DestroyWindow(dropHwnd);
		CleanupDestroyedWindow(dropHwnd);
		data.ComboBoxDropDownHwnd() = nullptr;
	}

	LRESULT OpenComboDropDown(OwnerDrawDialogElement& data, HWND hWnd, const RECT& clientRect, const RECT& ownerRect)
	{
		if (data.ComboBoxDropDownHwnd())
			return 1;

		SyncComboDropSelectionColor();

		const HWND parentHwnd = ::GetParent(hWnd);
		if (!parentHwnd)
			return 1;

		RECT parentRect {};
		OwnerDraw::GetRectangle(parentHwnd, &parentRect);

		RECT dropRect {};
		::SendMessageA(hWnd, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropRect));
		if (dropRect.bottom > parentRect.bottom)
			dropRect.bottom = parentRect.bottom;

		int itemHeight = static_cast<int>(::SendMessageA(hWnd, CB_GETITEMHEIGHT, 0, 0));
		if (itemHeight <= 0)
			itemHeight = 1;

		int dropHeight = dropRect.bottom - dropRect.top;
		int visibleByHeight = (dropHeight - 1) / itemHeight;
		int itemCount = static_cast<int>(::SendMessageA(hWnd, CB_GETCOUNT, 0, 0));
		if (itemCount < 1)
			itemCount = 1;

		const int maxVisibleItems = data.ComboBoxMaxVisibleDropItems();
		if (maxVisibleItems > 0 && itemCount >= maxVisibleItems)
		{
			dropHeight = maxVisibleItems * itemHeight + 1;
		}
		else if (visibleByHeight > itemCount)
		{
			dropHeight = itemCount * itemHeight + 1;
		}

		if (ownerRect.bottom + dropHeight > parentRect.bottom)
			dropHeight = parentRect.bottom - ownerRect.bottom;

		dropHeight -= dropHeight % itemHeight;
		if (dropHeight <= 0)
			dropHeight = itemHeight;

		const int x = ownerRect.left - parentRect.left;
		const int y = ownerRect.top - parentRect.top + clientRect.bottom + 1;
		const int width = clientRect.right - clientRect.left;

		const HWND dropHwnd = ::CreateWindowExA(
			0,
			"ComboDropWin",
			nullptr,
			WS_CHILD,
			x,
			y,
			width,
			dropHeight,
			parentHwnd,
			nullptr,
			Game::hInstance,
			hWnd);

		if (!dropHwnd)
			return 1;

		if (!FindOwnerDrawData(dropHwnd))
		{
			OwnerDrawDialogElement dropData;
			dropData.ComboBoxFont() = BitFont::Instance;
			dropData.ControlType = WWControlType::Default;

			OwnerDraw::Dialogs[dropHwnd] = dropData;
		}

		SessionIpb::RegisterHwnd(dropHwnd);
		SessionIpb::RegisterHwnd(dropHwnd);

		::SendMessageA(dropHwnd, WW_DROPDOWN_INITIALIZE, 0, 0);
		::SendMessageA(parentHwnd, WW_BRINGTOTOP, reinterpret_cast<WPARAM>(dropHwnd), 1);
		::SetCapture(dropHwnd);
		::ShowWindow(dropHwnd, SW_SHOWNORMAL);
		data.ComboBoxDropDownHwnd() = dropHwnd;
		return 1;
	}

	constexpr int SliderValueLabelWidth = 50;
	constexpr int SliderTrackRightPadding = 13;
	constexpr int SliderGripCenterOffset = 6;
	constexpr int SliderGripHitWidth = 12;
	constexpr int SliderMouseHitBottomInset = 18;

	int SliderTrackTravel(const RECT& clientRect, int valueLabelWidth)
	{
		const int travel = clientRect.right - clientRect.left - valueLabelWidth - SliderTrackRightPadding;
		return travel > 1 ? travel : 1;
	}

	int SliderThumbOffsetFromPosition(int positionOffset, int trackTravel, int rangeSpan)
	{
		if (!rangeSpan)
			return 0;

		return positionOffset * trackTravel / rangeSpan;
	}

	int SliderClampedGripX(int x, const RECT& clientRect, int valueLabelWidth)
	{
		int gripX = x - SliderGripCenterOffset;
		if (gripX < 1)
			gripX = 1;

		const int maxGripX = clientRect.right - valueLabelWidth - SliderGripHitWidth;
		if (maxGripX < gripX)
			gripX = maxGripX;

		return gripX;
	}

	void SliderUpdateFromGripX(
		int gripX,
		int rangeSpan,
		int rangeMin,
		int stepValue,
		int trackTravel,
		int& positionOffset,
		int& thumbOffsetPixels)
	{
		if (!trackTravel)
			trackTravel = 1;

		int offset = (rangeSpan + 1) * (gripX - 1) / trackTravel;
		if (offset >= rangeSpan)
			offset = rangeSpan;

		if (!stepValue)
			stepValue = 1;

		positionOffset = stepValue * ((rangeMin + offset) / stepValue) - rangeMin;
		thumbOffsetPixels = SliderThumbOffsetFromPosition(positionOffset, trackTravel, rangeSpan);
	}

	void EnsureSliderCache(HWND hWnd, OwnerDrawDialogElement& data, const RECT& clientRect, const RECT& ownerRect)
	{
		if (data.CacheSurface || !DSurface::Alternate)
			return;

		const int width = clientRect.right + 1;
		const int height = clientRect.bottom + 1;
		if (width <= 0 || height <= 0)
			return;

		data.CacheSurface = GameCreate<BSurface>(width, height);
		++OwnerDraw::CachedSurfaceCount;

		RectangleStruct destRect { 0, 0, width, height };
		RectangleStruct sourceRect { ownerRect.left, ownerRect.top, width, height };
		CopySurfacePart(data.CacheSurface, destRect, DSurface::Alternate, sourceRect);
		BlendFillRect(destRect, data.CacheSurface, 0, static_cast<unsigned char>(data.Alpha));
	}

	int SliderBorderColor(bool disabled)
	{
		const COLORREF color = disabled ? OwnerDraw::DisabledBorderColor : OwnerDraw::DefaultBorderColor;
		if (color == static_cast<COLORREF>(-1))
			return -1;

		return ConvertRGBToSurfaceColor(color);
	}

	void PaintSlider(
		HWND hWnd,
		OwnerDrawDialogElement& data,
		const RECT& clientRect,
		const RECT& ownerRect,
		int thumbHitLeftX,
		int thumbHitRightX,
		int rangeMin,
		int positionOffset,
		int stepValue,
		int valueLabelWidth,
		bool showValueLabel)
	{
		if (!DSurface::Alternate)
			return;

		const int controlLeft = ownerRect.left;
		const int controlTop = ownerRect.top;
		const int controlWidth = ownerRect.right - ownerRect.left;
		const int controlHeight = ownerRect.bottom - ownerRect.top;

		const LONG style = ::GetWindowLongA(hWnd, GWL_STYLE);
		const bool disabled = (style & WS_DISABLED) != 0;

		EnsureSliderCache(hWnd, data, clientRect, ownerRect);

		if (data.CacheSurface)
		{
			RectangleStruct destRect { ownerRect.left, ownerRect.top, clientRect.right + 1, clientRect.bottom + 1 };
			RectangleStruct sourceRect { 0, 0, clientRect.right + 1, clientRect.bottom + 1 };
			CopySurfacePart(DSurface::Alternate, destRect, data.CacheSurface, sourceRect);
		}

		if (showValueLabel)
		{
			const int labelPanelX = controlWidth - valueLabelWidth + controlLeft + 1;
			const int labelPanelTop = controlTop - 1;

			if (auto pMiddle = GetPCXSurface("trofm.pcx"))
			{
				RectangleStruct middleRect { labelPanelX, labelPanelTop, valueLabelWidth, pMiddle->GetHeight() };
				BlitTiledPCX(middleRect, DSurface::Alternate, pMiddle, 0, 0);
			}

			if (auto pLeft = GetPCXSurface("trofl.pcx"))
			{
				RectangleStruct leftRect { labelPanelX, labelPanelTop, pLeft->GetWidth(), pLeft->GetHeight() };
				DrawPCXCopy(DSurface::Alternate, leftRect, pLeft);
			}

			if (auto pRight = GetPCXSurface("trofr.pcx"))
			{
				RectangleStruct rightRect
				{
					controlLeft + controlWidth - pRight->GetWidth() + 1,
					labelPanelTop,
					pRight->GetWidth(),
					pRight->GetHeight()
				};
				DrawPCXCopy(DSurface::Alternate, rightRect, pRight);
			}
		}

		RectangleStruct gripRect
		{
			controlLeft + thumbHitLeftX,
			controlTop,
			thumbHitRightX - thumbHitLeftX,
			controlHeight
		};

		if (auto pGrip = GetPCXSurface("trakgrip.pcx"))
			DrawPCXCopy(DSurface::Alternate, gripRect, pGrip);

		if (disabled)
			BlendFillRect(gripRect, DSurface::Alternate, 0, OwnerDraw::DisabledOverlayAlpha);

		const int borderColor = SliderBorderColor(disabled);
		RectangleStruct trackBorderRect
		{
			controlLeft,
			controlTop,
			controlWidth - valueLabelWidth,
			controlHeight
		};
		DrawBeveledBorder(DSurface::Alternate, trackBorderRect, 2, borderColor);

		const int labelInset = (showValueLabel ? 1 : 0) + 1;
		RectangleStruct labelBorderRect
		{
			controlLeft + controlWidth + labelInset - valueLabelWidth,
			controlTop,
			valueLabelWidth - labelInset,
			controlHeight
		};
		DrawBeveledBorder(DSurface::Alternate, labelBorderRect, 2, borderColor);

		if (showValueLabel)
		{
			if (!stepValue)
				stepValue = 1;

			wchar_t text[0x100] {};
			std::swprintf(text, std::size(text), L"%d", stepValue * ((rangeMin + positionOffset) / stepValue));

			RECT textRect
			{
				ownerRect.right - 49,
				ownerRect.top,
				ownerRect.right,
				ownerRect.bottom
			};

			const COLORREF textColor = disabled ? Phobos::UI::ColorDisabledSlider : Phobos::UI::ColorTextSlider;
			OwnerDraw::DrawWideText(DSurface::Alternate, text, &textRect, data.SliderFont(), textColor, 5, 12, 0, 0, 0);
		}
	}

	void EnsureProgressCache(OwnerDrawDialogElement& data, const RectangleStruct& cacheRect, const RectangleStruct& screenRect)
	{
		if (data.CacheSurface || !DSurface::Alternate || cacheRect.Width <= 0 || cacheRect.Height <= 0)
			return;

		data.CacheSurface = GameCreate<BSurface>(cacheRect.Width, cacheRect.Height);
		if (!data.CacheSurface)
			return;

		++OwnerDraw::CachedSurfaceCount;
		CopySurfacePart(data.CacheSurface, cacheRect, DSurface::Alternate, screenRect);
	}

	void PaintProgress(OwnerDrawDialogElement& data, const RECT& ownerRect)
	{
		if (!DSurface::Alternate)
			return;

		const int width = ownerRect.right - ownerRect.left + 1;
		const int height = ownerRect.bottom - ownerRect.top + 1;
		if (width <= 0 || height <= 0)
			return;

		RectangleStruct cacheRect { 0, 0, width, height };
		RectangleStruct screenRect { ownerRect.left, ownerRect.top, width, height };

		EnsureProgressCache(data, cacheRect, screenRect);

		if (data.CacheSurface)
			CopySurfacePart(DSurface::Alternate, screenRect, data.CacheSurface, cacheRect);

		const int rangeSpan = data.ProgressMaxValue() - data.ProgressMinValue();
		const int widthScale = rangeSpan
			? static_cast<int>((static_cast<long long>(data.ProgressPosition()) << 16) / rangeSpan)
			: 0;

		const WORD color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(255, 0, 0)));
		BlendGradientRect(screenRect, DSurface::Alternate, color, widthScale);
	}

	constexpr int ListBoxScrollBarExtraWidth = 18;
	constexpr int ListBoxTextEntryInlineBytes = 2;

	int SignedLowWord(LPARAM value)
	{
		return static_cast<short>(LOWORD(value));
	}

	int SignedHighWord(LPARAM value)
	{
		return static_cast<short>(HIWORD(value));
	}

	WPARAM ListBoxCommand(HWND hWnd, int notificationCode)
	{
		return static_cast<WPARAM>(
			(::GetWindowLongA(hWnd, GWL_ID) & 0xFFFF)
			| ((notificationCode & 0xFFFF) << 16));
	}

	void NotifyListBoxSelectionChanged(HWND hWnd)
	{
		if (const HWND parentHwnd = ::GetParent(hWnd))
			::SendMessageA(parentHwnd, WM_COMMAND, ListBoxCommand(hWnd, LBN_SELCHANGE), reinterpret_cast<LPARAM>(hWnd));
	}

	void PostListBoxDoubleClick(HWND hWnd)
	{
		if (const HWND parentHwnd = ::GetParent(hWnd))
			::PostMessageA(parentHwnd, WM_COMMAND, ListBoxCommand(hWnd, LBN_DBLCLK), reinterpret_cast<LPARAM>(hWnd));
	}

	void CharToWideString(wchar_t* pBuffer, int capacity, const char* pText)
	{
		if (!pBuffer || capacity <= 0)
			return;

		pBuffer[0] = L'\0';
		if (!pText)
			return;

		::MultiByteToWideChar(CP_ACP, 0, pText, -1, pBuffer, capacity);
		pBuffer[capacity - 1] = L'\0';
	}

	void WideToCharString(char* pBuffer, int capacity, const wchar_t* pText)
	{
		if (!pBuffer || capacity <= 0)
			return;

		pBuffer[0] = '\0';
		if (!pText)
			return;

		::WideCharToMultiByte(CP_ACP, 0, pText, -1, pBuffer, capacity, nullptr, nullptr);
		pBuffer[capacity - 1] = '\0';
	}

	UINT GetCurrentKeyboardCodePage()
	{
		char buffer[7] {};
		const WORD language = LOWORD(::GetKeyboardLayout(0));
		const LCID locale = MAKELCID(language, SORT_DEFAULT);

		if (!::GetLocaleInfoA(locale, LOCALE_IDEFAULTANSICODEPAGE, buffer, static_cast<int>(std::size(buffer))))
			return CP_ACP;

		const int codePage = std::atoi(buffer);
		return codePage > 0 ? static_cast<UINT>(codePage) : CP_ACP;
	}

	wchar_t LocalizeCharacter(char character)
	{
		wchar_t result {};
		::MultiByteToWideChar(GetCurrentKeyboardCodePage(), MB_USEGLYPHCHARS, &character, 1, &result, 1);
		return result;
	}

	WideWstring* EnsureNewEditText(OwnerDrawDialogElement& data)
	{
		if (!data.NewEditText())
		{
			auto pMemory = YRMemory::Allocate(sizeof(WideWstring));
			if (!pMemory)
				return nullptr;

			data.NewEditText() = new (pMemory) WideWstring();
		}

		return data.NewEditText();
	}

	const wchar_t* NewEditTextBuffer(OwnerDrawDialogElement& data)
	{
		const auto pText = EnsureNewEditText(data);
		return pText && pText->Buffer ? pText->Buffer : L"";
	}

	int NewEditTextLength(OwnerDrawDialogElement& data)
	{
		const auto pText = EnsureNewEditText(data);
		return pText ? static_cast<int>(pText->GetLength()) : 0;
	}

	void SetNewEditText(OwnerDrawDialogElement& data, const wchar_t* pText)
	{
		if (auto pTarget = EnsureNewEditText(data))
			*pTarget = pText ? pText : L"";
	}

	void TrimNewEditTextToLimit(OwnerDrawDialogElement& data)
	{
		const int limit = data.NewEditTextLimit();
		if (limit <= 0)
			return;

		if (NewEditTextLength(data) <= limit)
			return;

		std::wstring value(NewEditTextBuffer(data), limit);
		SetNewEditText(data, value.c_str());

		if (data.NewEditCaretIndex() > limit)
			data.NewEditCaretIndex() = limit;
	}

	bool RemoveNewEditTextRange(OwnerDrawDialogElement& data, int index, int length)
	{
		std::wstring value(NewEditTextBuffer(data));
		if (index < 0 || length <= 0 || index >= static_cast<int>(value.size()))
			return false;

		length = std::min(length, static_cast<int>(value.size()) - index);
		value.erase(static_cast<size_t>(index), static_cast<size_t>(length));
		SetNewEditText(data, value.c_str());
		data.NewEditCaretIndex() = std::clamp(data.NewEditCaretIndex(), 0, static_cast<int>(value.size()));
		return true;
	}

	bool InsertNewEditCharacter(OwnerDrawDialogElement& data, wchar_t character)
	{
		if (!character || character <= 0x1F)
			return false;

		if (data.NewEditAsciiOnly() && character >= 0x100)
			return false;

		if (data.NewEditRejectChars() && std::wcschr(data.NewEditRejectChars(), character))
			return false;

		std::wstring value(NewEditTextBuffer(data));
		if (data.NewEditTextLimit() > 0 && static_cast<int>(value.size()) >= data.NewEditTextLimit())
			return false;

		int caretIndex = std::clamp(data.NewEditCaretIndex(), 0, static_cast<int>(value.size()));
		value.insert(value.begin() + caretIndex, character);
		SetNewEditText(data, value.c_str());
		data.NewEditCaretIndex() = caretIndex + 1;
		return true;
	}

	void NotifyNewEditTextChanged(HWND hWnd, HWND parentHwnd)
	{
		if (!parentHwnd)
			return;

		const int controlId = ::GetWindowLongA(hWnd, GWL_ID) & 0xFFFF;
		::SendMessageA(parentHwnd, WM_COMMAND, controlId | 0x03000000, reinterpret_cast<LPARAM>(hWnd));
		::SendMessageA(parentHwnd, WM_COMMAND, controlId | 0x04000000, reinterpret_cast<LPARAM>(hWnd));
	}

	void NotifyNewEditEnterPressed(HWND hWnd, HWND parentHwnd)
	{
		if (parentHwnd)
			::SendMessageA(parentHwnd, WW_EDIT_ENTERPRESSED, 0, reinterpret_cast<LPARAM>(hWnd));
	}

	void NotifyNewEditMultilineEnter(HWND hWnd, HWND parentHwnd)
	{
		if (!parentHwnd)
			return;

		const int controlId = ::GetWindowLongA(hWnd, GWL_ID) & 0xFFFF;
		::SendMessageA(parentHwnd, WM_COMMAND, controlId | 0x05010000, reinterpret_cast<LPARAM>(hWnd));
	}

	bool IsComboBoxParent(HWND parentHwnd)
	{
		if (!parentHwnd)
			return false;

		char className[32] {};
		::GetClassNameA(parentHwnd, className, static_cast<int>(std::size(className)));
		return std::strcmp(className, "ComboBox") == 0;
	}

	void InvalidateNewEdit(HWND hWnd, HWND parentHwnd)
	{
		if (IsComboBoxParent(parentHwnd))
			::InvalidateRect(parentHwnd, nullptr, FALSE);

		::InvalidateRect(hWnd, nullptr, FALSE);
	}

	int NewEditTextWidth(BitFont* pFont, const wchar_t* pText)
	{
		if (!pText || !pText[0])
			return 0;

		if (!pFont)
			pFont = BitFont::Instance;

		if (!pFont)
			return static_cast<int>(std::wcslen(pText)) * 8;

		int textWidth = 0;
		int textHeight = 0;
		pFont->GetTextDimension(pText, &textWidth, &textHeight, 0);
		return textWidth;
	}

	int NewEditFitCharacterCount(BitFont* pFont, const wchar_t* pText, int maxWidth)
	{
		if (!pText || maxWidth <= 0)
			return 0;

		const int length = static_cast<int>(std::wcslen(pText));
		int fitCount = 0;

		for (int count = 1; count <= length; ++count)
		{
			std::wstring candidate(pText, pText + count);
			if (NewEditTextWidth(pFont, candidate.c_str()) > maxWidth)
				break;

			fitCount = count;
		}

		return fitCount;
	}

	int PrintNewEditTextSegment(
		DSurface* pSurface,
		RectangleStruct& rect,
		BitFont* pFont,
		const std::wstring& text,
		int start,
		int end,
		COLORREF color,
		int animationPos)
	{
		if (end <= start || rect.Width <= 0)
			return 0;

		const std::wstring segment(text.begin() + start, text.begin() + end);
		const int width = NewEditTextWidth(pFont, segment.c_str());

		OwnerDraw::PrintTextFixedLength(
			color,
			pFont,
			&rect,
			segment.c_str(),
			static_cast<int>(segment.size()),
			0,
			0,
			pSurface,
			animationPos);

		rect.X += width;
		rect.Width = std::max(rect.Width - width, 0);
		return width;
	}

	void AnimatedNewEditTextPrint(
		DSurface* pSurface,
		RectangleStruct textRect,
		const wchar_t* pText,
		int caretIndex,
		BitFont* pFont,
		COLORREF textColor,
		int& scrollStart,
		bool hasFocus,
		bool maskText,
		bool fillBackground,
		int animationPos,
		int caretBlinkState)
	{
		if (!pSurface || textRect.Width <= 0 || textRect.Height <= 0)
			return;

		if (!pFont)
			pFont = BitFont::Instance;

		const wchar_t* pSource = pText ? pText : L"";
		const int sourceLength = static_cast<int>(std::wcslen(pSource));
		caretIndex = std::clamp(caretIndex, 0, sourceLength);

		int compositionLength = 0;
		int compositionCursor = 0;
		bool composing = false;
		if (hasFocus)
		{
			OwnerDraw::UpdateIMECompositionString();
			compositionLength = std::clamp(OwnerDraw::IMECompositionStringLength, 0, 0x100);
			compositionCursor = std::clamp(OwnerDraw::IMECompositionCursorPos, 0, compositionLength);
			composing = OwnerDraw::IMEComposing != 0;
		}

		constexpr size_t DisplayBufferCapacity = 0x800;
		std::wstring displayText(pSource);
		if (displayText.size() >= DisplayBufferCapacity)
			displayText.resize(DisplayBufferCapacity - 1);

		const int compositionStart = std::min(caretIndex, static_cast<int>(displayText.size()));
		if (compositionLength > 0)
		{
			displayText.insert(
				displayText.begin() + compositionStart,
				OwnerDraw::IMECompositionString,
				OwnerDraw::IMECompositionString + compositionLength);

			if (displayText.size() >= DisplayBufferCapacity)
				displayText.resize(DisplayBufferCapacity - 1);
		}

		if (maskText)
			std::fill(displayText.begin(), displayText.end(), L'*');

		const int displayLength = static_cast<int>(displayText.size());
		const int compositionEnd = std::min(compositionStart + compositionLength, displayLength);
		int displayCaret = composing || compositionLength
			? compositionStart + compositionCursor
			: std::min(caretIndex, displayLength);
		displayCaret = std::clamp(displayCaret, 0, displayLength);

		scrollStart = std::clamp(scrollStart, 0, displayLength);
		if (displayCaret < scrollStart + 5)
			scrollStart = std::max(displayCaret - 5, 0);

		while (scrollStart < displayLength)
		{
			const int visibleCount = NewEditFitCharacterCount(pFont, displayText.c_str() + scrollStart, textRect.Width - 5);
			if (visibleCount >= displayCaret - scrollStart)
				break;

			++scrollStart;
		}

		if (fillBackground)
		{
			RectangleStruct fillRect
			{
				textRect.X - 1,
				textRect.Y - 1,
				NewEditTextWidth(pFont, displayText.c_str() + scrollStart) + 5,
				textRect.Height + 2
			};
			pSurface->FillRect(&fillRect, 0);
		}

		RectangleStruct drawRect = textRect;
		int caretX = -1;

		auto drawRange = [&](int rangeStart, int rangeEnd, COLORREF color)
		{
			int visibleStart = std::max(rangeStart, scrollStart);
			int visibleEnd = std::min(rangeEnd, displayLength);
			if (visibleEnd <= visibleStart)
				return;

			if (caretX < 0 && displayCaret >= visibleStart && displayCaret <= visibleEnd)
			{
				PrintNewEditTextSegment(pSurface, drawRect, pFont, displayText, visibleStart, displayCaret, color, animationPos);
				caretX = drawRect.X;
				PrintNewEditTextSegment(pSurface, drawRect, pFont, displayText, displayCaret, visibleEnd, color, animationPos);
			}
			else
			{
				PrintNewEditTextSegment(pSurface, drawRect, pFont, displayText, visibleStart, visibleEnd, color, animationPos);
			}
		};

		drawRange(0, compositionStart, textColor);
		drawRange(compositionStart, compositionEnd, OwnerDraw::ImeCompositionTextColor);
		drawRange(compositionEnd, displayLength, textColor);

		if (caretX < 0)
		{
			if (displayCaret <= scrollStart)
				caretX = textRect.X;
			else if (displayCaret >= displayLength)
				caretX = drawRect.X;
		}

		if (hasFocus && caretX >= 0 && !caretBlinkState)
		{
			const WORD caretColor = static_cast<WORD>(ConvertRGBToSurfaceColor(Phobos::UI::ColorCaret));
			Point2D start { caretX, textRect.Y };
			Point2D end { caretX, textRect.Y + textRect.Height - 2 };
			DrawAlphaLine(pSurface, start, end, caretColor, 0xFF);

			++start.X;
			++end.X;
			DrawAlphaLine(pSurface, start, end, caretColor, 0xFF);
		}
	}

	void PaintNewEdit(HWND hWnd, OwnerDrawDialogElement& data, HWND parentHwnd)
	{
		if (!DSurface::Alternate)
			return;

		RECT ownerRect {};
		OwnerDraw::GetRectangle(hWnd, &ownerRect);

		const int width = ownerRect.right - ownerRect.left + 1;
		const int height = ownerRect.bottom - ownerRect.top + 1;
		if (width <= 0 || height <= 0)
			return;

		RectangleStruct drawRect { ownerRect.left, ownerRect.top, width, height };
		OwnerDraw::CopyDimmedBackground(&drawRect, hWnd, static_cast<unsigned int>(data.Alpha));

		if (!IsComboBoxParent(parentHwnd))
			DrawBeveledBorder(DSurface::Alternate, drawRect, 2, -1);

		RectangleStruct textRect
		{
			ownerRect.left + 2,
			ownerRect.top,
			ownerRect.right - ownerRect.left + 1,
			ownerRect.bottom - ownerRect.top + 1
		};

		AnimatedNewEditTextPrint(
			DSurface::Alternate,
			textRect,
			NewEditTextBuffer(data),
			data.NewEditCaretIndex(),
			data.NewEditFont(),
			Phobos::UI::ColorTextEdit,
			data.NewEditScrollStart(),
			data.HasFocus != 0,
			((data.NewEditStyleFlags() >> 5) & 1) != 0,
			false,
			0,
			data.NewEditCaretBlinkState());

		::ValidateRect(hWnd, nullptr);
	}

	size_t GetEditWideText(HWND hWnd, wchar_t* pWideText, int capacity, int* pSelectionEndChars)
	{
		if (pSelectionEndChars)
			*pSelectionEndChars = 0;

		if (!pWideText || capacity <= 0)
			return 0;

		const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);

		char ansiText[0x400] {};
		CallSelectedHandler(
			pOriginalWndProc,
			hWnd,
			WM_GETTEXT,
			static_cast<WPARAM>(std::size(ansiText)),
			reinterpret_cast<LPARAM>(ansiText));

		if (pSelectionEndChars)
		{
			DWORD selectionStart = 0;
			DWORD selectionEnd = 0;
			const auto selection = static_cast<DWORD>(CallSelectedHandler(
				pOriginalWndProc,
				hWnd,
				EM_GETSEL,
				reinterpret_cast<WPARAM>(&selectionStart),
				reinterpret_cast<LPARAM>(&selectionEnd)));
			const auto selectionEndBytes = static_cast<size_t>(HIWORD(selection));
			*pSelectionEndChars = static_cast<int>(_mbsnccnt(
				reinterpret_cast<const unsigned char*>(ansiText),
				selectionEndBytes));
		}

		pWideText[0] = L'\0';
		::MultiByteToWideChar(CP_ACP, 0, ansiText, -1, pWideText, capacity);
		pWideText[capacity - 1] = L'\0';

		const size_t wideLength = std::wcslen(pWideText);
		std::wstring normalized;
		normalized.reserve(wideLength + 2);

		bool removedNewLine = false;
		for (size_t i = 0; i < wideLength; ++i)
		{
			if (pWideText[i] == L'\r' || pWideText[i] == L'\n')
			{
				removedNewLine = true;
				continue;
			}

			normalized.push_back(pWideText[i]);
		}

		if (removedNewLine)
			normalized += L"\r\n";

		std::wcsncpy(pWideText, normalized.c_str(), static_cast<size_t>(capacity - 1));
		pWideText[capacity - 1] = L'\0';
		return std::wcslen(pWideText);
	}

	LRESULT ForwardEditSetText(OwnerDrawDialogElement& data, HWND hWnd, WNDPROC pOriginalWndProc)
	{
		char ansiText[0x800] {};
		if (data.TextBuffer)
			WideToCharString(ansiText, static_cast<int>(std::size(ansiText)), data.TextBuffer);

		return CallSelectedHandler(
			pOriginalWndProc,
			hWnd,
			WM_SETTEXT,
			0,
			reinterpret_cast<LPARAM>(ansiText));
	}

	LRESULT CopyEditTextW(HWND hWnd, WPARAM capacityParam, LPARAM lParam)
	{
		auto pBuffer = reinterpret_cast<wchar_t*>(lParam);
		const int capacity = static_cast<int>(capacityParam);
		if (!pBuffer || capacity <= 0)
			return 0;

		std::vector<wchar_t> text(0x800);
		GetEditWideText(hWnd, text.data(), static_cast<int>(text.size()), nullptr);

		std::wcsncpy(pBuffer, text.data(), static_cast<size_t>(capacity - 1));
		pBuffer[capacity - 1] = L'\0';
		return static_cast<LRESULT>(std::wcslen(pBuffer));
	}

	LRESULT CopyEditTextA(HWND hWnd, WPARAM capacityParam, LPARAM lParam)
	{
		auto pBuffer = reinterpret_cast<char*>(lParam);
		const int capacity = static_cast<int>(capacityParam);
		if (!pBuffer || capacity <= 0)
			return 0;

		std::vector<wchar_t> text(0x800);
		GetEditWideText(hWnd, text.data(), static_cast<int>(text.size()), nullptr);
		WideToCharString(pBuffer, capacity, text.data());
		return static_cast<LRESULT>(std::strlen(pBuffer));
	}

	LRESULT AppendEditNewLine(HWND hWnd, WNDPROC pOriginalWndProc)
	{
		const int textLength = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, WM_GETTEXTLENGTH, 0, 0));
		std::vector<char> text(static_cast<size_t>(std::max(textLength + 3, 3)), '\0');

		CallSelectedHandler(
			pOriginalWndProc,
			hWnd,
			WM_GETTEXT,
			static_cast<WPARAM>(text.size()),
			reinterpret_cast<LPARAM>(text.data()));

		const size_t copiedLength = std::strlen(text.data());
		if (copiedLength + 2 < text.size())
			std::memcpy(text.data() + copiedLength, "\r\n", 3);

		CallSelectedHandler(pOriginalWndProc, hWnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.data()));

		if (const HWND parentHwnd = ::GetParent(hWnd))
		{
			const int controlId = ::GetWindowLongA(hWnd, GWL_ID) & 0xFFFF;
			::SendMessageA(parentHwnd, WM_COMMAND, controlId | 0x05010000, reinterpret_cast<LPARAM>(hWnd));
		}

		return 0;
	}

	void PaintEdit(HWND hWnd, OwnerDrawDialogElement& data, HWND parentHwnd, UINT message)
	{
		if (!DSurface::Alternate)
			return;

		RECT ownerRect {};
		OwnerDraw::GetRectangle(hWnd, &ownerRect);

		if (message == WM_PAINT)
		{
			RECT updateRect {};
			if (::GetUpdateRect(hWnd, &updateRect, FALSE))
			{
				updateRect.left += ownerRect.left;
				updateRect.top += ownerRect.top;
				updateRect.right += ownerRect.left;
				updateRect.bottom += ownerRect.top;
			}
		}

		const int width = ownerRect.right - ownerRect.left + 1;
		const int height = ownerRect.bottom - ownerRect.top + 1;
		if (width <= 0 || height <= 0)
			return;

		RectangleStruct drawRect { ownerRect.left, ownerRect.top, width, height };
		OwnerDraw::CopyDimmedBackground(&drawRect, hWnd, static_cast<unsigned int>(data.Alpha));

		if (!IsComboBoxParent(parentHwnd))
			DrawBeveledBorder(DSurface::Alternate, drawRect, 2, -1);

		std::vector<wchar_t> text(0x1400);
		int caretIndex = 0;
		GetEditWideText(hWnd, text.data(), static_cast<int>(text.size()), &caretIndex);

		RectangleStruct textRect { ownerRect.left, ownerRect.top, width, height };
		const bool maskText = ((::GetWindowLongA(hWnd, GWL_STYLE) >> 5) & 1) != 0;

		AnimatedNewEditTextPrint(
			DSurface::Alternate,
			textRect,
			text.data(),
			caretIndex,
			data.EditTextFont(),
			Phobos::UI::ColorText,
			data.EditTextScrollStart(),
			data.HasFocus != 0,
			maskText,
			false,
			0,
			0);

		::ValidateRect(hWnd, nullptr);
	}

	WWUIIntArray* CreateIntArray()
	{
		auto pArray = static_cast<WWUIIntArray*>(YRMemory::Allocate(sizeof(WWUIIntArray)));
		if (pArray)
			*pArray = {};

		return pArray;
	}

	void DeleteIntArray(WWUIIntArray*& pArray)
	{
		if (!pArray)
			return;

		YRMemory::Deallocate(pArray->Items);
		YRMemory::Deallocate(pArray);
		pArray = nullptr;
	}

	void ResizeIntArrayStorage(WWUIIntArray& array, int capacity)
	{
		if (capacity < 10)
			capacity = 10;

		auto pItems = static_cast<int*>(YRMemory::Allocate(sizeof(int) * capacity));
		std::memset(pItems, 0, sizeof(int) * capacity);

		if (array.Items && array.Count > 0)
			std::memcpy(pItems, array.Items, sizeof(int) * array.Count);

		YRMemory::Deallocate(array.Items);
		array.Items = pItems;
		array.Capacity = capacity;
	}

	void EnsureIntArraySize(WWUIIntArray& array, int count, int fillValue)
	{
		if (count <= array.Count)
			return;

		if (count > array.Capacity)
		{
			int capacity = array.Capacity;
			do
			{
				capacity = std::max(capacity * 2, 10);
			}
			while (capacity < count);

			ResizeIntArrayStorage(array, capacity);
		}

		for (int i = array.Count; i < count; ++i)
			array.Items[i] = fillValue;

		array.Count = count;
	}

	void MaybeShrinkIntArray(WWUIIntArray& array)
	{
		if (array.Capacity <= 10 || array.Count * 3 > array.Capacity)
			return;

		ResizeIntArrayStorage(array, std::max(array.Capacity / 2, 10));
	}

	void RemoveIntArrayItem(WWUIIntArray* pArray, int index)
	{
		if (!pArray || index < 0 || index >= pArray->Count)
			return;

		if (index < pArray->Count - 1)
		{
			std::memmove(
				&pArray->Items[index],
				&pArray->Items[index + 1],
				sizeof(int) * (pArray->Count - index - 1));
		}

		--pArray->Count;
		MaybeShrinkIntArray(*pArray);
	}

	void InsertIntArrayItem(WWUIIntArray* pArray, int index, int value)
	{
		if (!pArray)
			return;

		index = std::clamp(index, 0, pArray->Count);
		EnsureIntArraySize(*pArray, pArray->Count + 1, value);

		if (index < pArray->Count - 1)
		{
			std::memmove(
				&pArray->Items[index + 1],
				&pArray->Items[index],
				sizeof(int) * (pArray->Count - index - 1));
		}

		pArray->Items[index] = value;
	}

	void SetIntArrayValue(WWUIIntArray*& pArray, int index, int value, int fillValue)
	{
		if (index < 0)
			return;

		if (!pArray)
			pArray = CreateIntArray();

		if (!pArray)
			return;

		EnsureIntArraySize(*pArray, index + 1, fillValue);
		pArray->Items[index] = value;
	}

	int GetIntArrayValue(const WWUIIntArray* pArray, int index, int defaultValue)
	{
		if (!pArray || index < 0 || index >= pArray->Count)
			return defaultValue;

		return pArray->Items[index];
	}

	void ConstructListBoxCell(WWUIListBoxCell& cell)
	{
		new (&cell) WWUIListBoxCell();
		cell.Format = WWUIListBoxCellFormat::Empty;
		cell.TextColor = static_cast<COLORREF>(-1);
		cell.Image = nullptr;
		cell.Value = -1;
	}

	void ResetListBoxCell(WWUIListBoxCell& cell)
	{
		cell.~WWUIListBoxCell();
		ConstructListBoxCell(cell);
	}

	WWUIListBoxCell* AllocateListBoxCells(int capacity)
	{
		auto pItems = static_cast<WWUIListBoxCell*>(YRMemory::Allocate(sizeof(WWUIListBoxCell) * capacity));
		for (int i = 0; i < capacity; ++i)
			ConstructListBoxCell(pItems[i]);

		return pItems;
	}

	void DeleteListBoxCells(WWUIListBoxCell*& pItems, int capacity)
	{
		if (!pItems)
			return;

		for (int i = 0; i < capacity; ++i)
			pItems[i].~WWUIListBoxCell();

		YRMemory::Deallocate(pItems);
		pItems = nullptr;
	}

	void ResizeListBoxCellStorage(WWUIListBoxColumn& column, int capacity)
	{
		if (capacity < 10)
			capacity = 10;

		auto pItems = AllocateListBoxCells(capacity);
		for (int i = 0; i < column.CellCount; ++i)
			pItems[i] = column.Cells[i];

		DeleteListBoxCells(column.Cells, column.CellCapacity);
		column.Cells = pItems;
		column.CellCapacity = capacity;
	}

	void EnsureListBoxCellCount(WWUIListBoxColumn& column, int count, WWUIListBoxCellFormat defaultFormat)
	{
		if (count <= column.CellCount)
			return;

		if (count > column.CellCapacity)
		{
			int capacity = column.CellCapacity;
			do
			{
				capacity = std::max(capacity * 2, 10);
			}
			while (capacity < count);

			ResizeListBoxCellStorage(column, capacity);
		}

		for (int i = column.CellCount; i < count; ++i)
		{
			auto& cell = column.Cells[i];
			ResetListBoxCell(cell);
			cell.Format = defaultFormat;
		}

		column.CellCount = count;
	}

	void ClearListBoxColumnCells(WWUIListBoxColumn& column, bool releaseStorage)
	{
		if (!column.Cells)
		{
			column.CellCount = 0;
			column.CellCapacity = 0;
			return;
		}

		for (int i = 0; i < column.CellCapacity; ++i)
			ResetListBoxCell(column.Cells[i]);

		column.CellCount = 0;

		if (releaseStorage)
		{
			DeleteListBoxCells(column.Cells, column.CellCapacity);
			column.CellCapacity = 0;
		}
	}

	void DeleteListBoxColumns(WWUIListBoxColumnArray*& pColumns)
	{
		if (!pColumns)
			return;

		for (int i = 0; i < pColumns->Count; ++i)
			ClearListBoxColumnCells(pColumns->Items[i], true);

		YRMemory::Deallocate(pColumns->Items);
		YRMemory::Deallocate(pColumns);
		pColumns = nullptr;
	}

	WWUIListBoxColumnArray* CreateListBoxColumnArray()
	{
		auto pArray = static_cast<WWUIListBoxColumnArray*>(YRMemory::Allocate(sizeof(WWUIListBoxColumnArray)));
		if (pArray)
			*pArray = {};

		return pArray;
	}

	void ResizeListBoxColumnStorage(WWUIListBoxColumnArray& columns, int capacity)
	{
		if (capacity < 10)
			capacity = 10;

		auto pItems = static_cast<WWUIListBoxColumn*>(YRMemory::Allocate(sizeof(WWUIListBoxColumn) * capacity));
		std::memset(pItems, 0, sizeof(WWUIListBoxColumn) * capacity);

		if (columns.Items && columns.Count > 0)
			std::memcpy(pItems, columns.Items, sizeof(WWUIListBoxColumn) * columns.Count);

		YRMemory::Deallocate(columns.Items);
		columns.Items = pItems;
		columns.Capacity = capacity;
	}

	WWUIListBoxColumn* FindListBoxColumn(WWUIListBoxColumnArray* pColumns, int x)
	{
		if (!pColumns)
			return nullptr;

		for (int i = 0; i < pColumns->Count; ++i)
		{
			if (pColumns->Items[i].X == x)
				return &pColumns->Items[i];
		}

		return nullptr;
	}

	WWUIListBoxColumn* FindListBoxColumnAtX(WWUIListBoxColumnArray* pColumns, int x)
	{
		if (!pColumns)
			return nullptr;

		WWUIListBoxColumn* pBest = nullptr;
		for (int i = 0; i < pColumns->Count; ++i)
		{
			auto& column = pColumns->Items[i];
			if (column.X <= x && (!pBest || column.X > pBest->X))
				pBest = &column;
		}

		return pBest;
	}

	WWUIListBoxTextEntry* AllocateListBoxTextEntry(OwnerDrawDialogElement& data, const wchar_t* pText, bool isWide)
	{
		if (!pText)
			pText = L"";

		const size_t length = std::wcslen(pText);
		const size_t bytes = sizeof(WWUIListBoxTextEntry) + (length + 1) * sizeof(wchar_t) + ListBoxTextEntryInlineBytes;
		auto pEntry = static_cast<WWUIListBoxTextEntry*>(YRMemory::Allocate(bytes));
		if (!pEntry)
			return nullptr;

		pEntry->Next = data.ListBoxTextEntries();
		pEntry->ItemData = 0;
		pEntry->Text = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(pEntry) + sizeof(WWUIListBoxTextEntry));
		pEntry->IsWide = isWide ? 1 : 0;
		std::wcscpy(pEntry->Text, pText);
		data.ListBoxTextEntries() = pEntry;
		return pEntry;
	}

	void RemoveListBoxTextEntry(OwnerDrawDialogElement& data, WWUIListBoxTextEntry* pEntry)
	{
		if (!pEntry)
			return;

		WWUIListBoxTextEntry* pPrevious = nullptr;
		for (auto pCurrent = data.ListBoxTextEntries(); pCurrent; pCurrent = pCurrent->Next)
		{
			if (pCurrent != pEntry)
			{
				pPrevious = pCurrent;
				continue;
			}

			if (pPrevious)
				pPrevious->Next = pCurrent->Next;
			else
				data.ListBoxTextEntries() = pCurrent->Next;

			YRMemory::Deallocate(pCurrent);
			return;
		}
	}

	void ClearListBoxTextEntries(OwnerDrawDialogElement& data)
	{
		auto pEntry = data.ListBoxTextEntries();
		while (pEntry)
		{
			auto pNext = pEntry->Next;
			YRMemory::Deallocate(pEntry);
			pEntry = pNext;
		}

		data.ListBoxTextEntries() = nullptr;
	}

	WWUIListBoxTextEntry* GetListBoxTextEntry(WNDPROC pOriginalWndProc, HWND hWnd, int index)
	{
		const auto result = CallSelectedHandler(pOriginalWndProc, hWnd, LB_GETITEMDATA, index, 0);
		if (result == LB_ERR || !result)
			return nullptr;

		return reinterpret_cast<WWUIListBoxTextEntry*>(result);
	}

	void RemoveListBoxRow(OwnerDrawDialogElement& data, int index)
	{
		RemoveIntArrayItem(data.ListBoxItemData(), index);
		RemoveIntArrayItem(data.ListBoxSelectionStates(), index);

		if (auto pColumns = data.ListBoxColumns())
		{
			for (int i = 0; i < pColumns->Count; ++i)
			{
				auto& column = pColumns->Items[i];
				if (index < 0 || index >= column.CellCount)
					continue;

				for (int cellIndex = index; cellIndex < column.CellCount - 1; ++cellIndex)
					column.Cells[cellIndex] = column.Cells[cellIndex + 1];

				ResetListBoxCell(column.Cells[column.CellCount - 1]);
				--column.CellCount;

				if (column.CellCapacity > 10 && column.CellCount * 3 <= column.CellCapacity)
					ResizeListBoxCellStorage(column, std::max(column.CellCapacity / 2, 10));
			}
		}
	}

	void InsertListBoxRow(OwnerDrawDialogElement& data, int index)
	{
		InsertIntArrayItem(data.ListBoxItemData(), index, -1);
		InsertIntArrayItem(data.ListBoxSelectionStates(), index, 0);

		if (auto pColumns = data.ListBoxColumns())
		{
			for (int i = 0; i < pColumns->Count; ++i)
			{
				auto& column = pColumns->Items[i];
				const int insertIndex = std::clamp(index, 0, column.CellCount);
				EnsureListBoxCellCount(
					column,
					column.CellCount + 1,
					i == 0 ? WWUIListBoxCellFormat::ItemText : WWUIListBoxCellFormat::Empty);

				for (int cellIndex = column.CellCount - 1; cellIndex > insertIndex; --cellIndex)
					column.Cells[cellIndex] = column.Cells[cellIndex - 1];

				ResetListBoxCell(column.Cells[insertIndex]);
				column.Cells[insertIndex].Format = i == 0 ? WWUIListBoxCellFormat::ItemText : WWUIListBoxCellFormat::Empty;
			}
		}
	}

	void ClearListBoxRows(OwnerDrawDialogElement& data, bool destroyColumns)
	{
		DeleteIntArray(data.ListBoxItemData());
		DeleteIntArray(data.ListBoxSelectionStates());
		data.ListBoxTopIndex() = 0;
		data.ListBoxCurrentSelection() = -1;

		if (destroyColumns)
		{
			DeleteListBoxColumns(data.ListBoxColumns());
		}
		else if (auto pColumns = data.ListBoxColumns())
		{
			for (int i = 0; i < pColumns->Count; ++i)
				ClearListBoxColumnCells(pColumns->Items[i], false);
		}
	}

	void PaintListBoxCellText(
		HWND hWnd,
		BitFont* pFont,
		RectangleStruct rect,
		const wchar_t* pText,
		COLORREF textColor,
		int maxWidth)
	{
		if (!pText)
			pText = L"";

		wchar_t buffer[512] {};
		std::wcsncpy(buffer, pText, std::size(buffer) - 1);

		int textWidth = 0;
		int textHeight = 0;
		auto pMeasureFont = pFont ? pFont : BitFont::Instance;

		if (pMeasureFont)
		{
			size_t length = std::wcslen(buffer);
			while (length > 0)
			{
				pMeasureFont->GetTextDimension(buffer, &textWidth, &textHeight, maxWidth);
				if (textWidth <= maxWidth)
					break;

				--length;
				buffer[length] = L'\0';
				if (length > 3)
					std::wcscat(buffer, L"...");
			}
		}

		OwnerDraw::PrintTextFixedLength(
			textColor,
			pFont,
			&rect,
			buffer,
			static_cast<int>(std::wcslen(buffer)),
			0,
			0,
			nullptr,
			0);
	}

	void DrawListBoxProgressCell(RectangleStruct rect, int value)
	{
		if (!DSurface::Alternate)
			return;

		WORD color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(0, 0, 192)));
		if (value < 0)
		{
			color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(0, 0, 192)));
			value = 1000;
		}
		else if (value < 300)
		{
			color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(0, 192, 0)));
		}
		else if (value < 500)
		{
			color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(192, 192, 0)));
		}
		else
		{
			color = static_cast<WORD>(ConvertRGBToSurfaceColor(RGB(192, 0, 0)));
		}

		BlendGradientRect(rect, DSurface::Alternate, color, (value << 16) / 1000);
	}

	void PaintListBox(HWND hWnd, OwnerDrawDialogElement& data, const RECT& clientRect, const RECT& ownerRect, WNDPROC pOriginalWndProc)
	{
		if (data.SkipDraw)
		{
			::ValidateRect(hWnd, nullptr);
			return;
		}

		if (!DSurface::Alternate)
			return;

		RectangleStruct drawRect
		{
			ownerRect.left,
			ownerRect.top,
			clientRect.right - clientRect.left,
			clientRect.bottom - clientRect.top
		};

		OwnerDraw::CopyDimmedBackground(&drawRect, hWnd, static_cast<unsigned char>(data.Alpha));
		DrawBeveledBorder(DSurface::Alternate, drawRect, 2, -1);

		RECT updateRect {};
		if (!::GetUpdateRect(hWnd, &updateRect, FALSE))
			return;

		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		int itemIndex = static_cast<int>(::SendMessageA(hWnd, LB_GETTOPINDEX, 0, 0));
		const LONG style = ::GetWindowLongA(hWnd, GWL_STYLE);
		const int selectionColor = ConvertRGBToSurfaceColor(ListBoxSelectionFillColor());
		auto pFont = data.ListBoxFont();

		while (itemIndex >= 0 && itemIndex < itemCount)
		{
			RECT itemRect {};
			if (::SendMessageA(hWnd, LB_GETITEMRECT, itemIndex, reinterpret_cast<LPARAM>(&itemRect)) == LB_ERR)
				break;

			if (clientRect.top + itemRect.bottom > clientRect.bottom)
				break;

			wchar_t itemText[512] {};
			if (auto pEntry = GetListBoxTextEntry(pOriginalWndProc, hWnd, itemIndex))
				std::wcsncpy(itemText, pEntry->Text ? pEntry->Text : L"", std::size(itemText) - 1);

			const bool selected = ::SendMessageA(hWnd, LB_GETSEL, itemIndex, 0) > 0;
			RectangleStruct rowRect
			{
				ownerRect.left + itemRect.left,
				ownerRect.top + itemRect.top,
				itemRect.right - itemRect.left,
				itemRect.bottom - itemRect.top
			};

			if (selected)
				DSurface::Alternate->FillRect(&rowRect, selectionColor);

			if (auto pColumns = data.ListBoxColumns())
			{
				for (int columnIndex = 0; columnIndex < pColumns->Count; ++columnIndex)
				{
					auto& column = pColumns->Items[columnIndex];
					if (itemIndex < 0 || itemIndex >= column.CellCount || !column.Cells)
						continue;

					auto& cell = column.Cells[itemIndex];
					if (cell.Format == WWUIListBoxCellFormat::Empty)
						continue;

					RectangleStruct cellRect
					{
						ownerRect.left + itemRect.left + column.X,
						ownerRect.top + itemRect.top,
						column.Width ? column.Width : rowRect.Width,
						rowRect.Height
					};

					if (cellRect.Width <= 0)
						cellRect.Width = 0xFFFF;

					const int availableWidth = std::min(
						cellRect.Width,
						static_cast<int>(ownerRect.right - cellRect.X));

					switch (cell.Format)
					{
					case WWUIListBoxCellFormat::Text:
					case WWUIListBoxCellFormat::ItemText:
					{
						COLORREF textColor = cell.TextColor == static_cast<COLORREF>(-1)
							? ListBoxTextColor()
							: cell.TextColor;

						if (style & WS_DISABLED)
							textColor = ListBoxDisabledTextColor();

						const wchar_t* pText = cell.Format == WWUIListBoxCellFormat::Text
							? GetWideTextBuffer(cell.PrimaryText)
							: itemText;

						PaintListBoxCellText(hWnd, pFont, cellRect, pText, textColor, availableWidth);
						break;
					}

					case WWUIListBoxCellFormat::Image:
						if (cell.Image)
						{
							RectangleStruct imageRect
							{
								cellRect.X,
								cellRect.Y + (cellRect.Height - cell.Image->GetHeight()) / 2,
								cell.Image->GetWidth(),
								cell.Image->GetHeight()
							};
							PCX::Instance.BlitToSurface(&imageRect, DSurface::Alternate, static_cast<BSurface*>(cell.Image));
						}
						break;

					case WWUIListBoxCellFormat::Progress:
					{
						RectangleStruct progressRect { cellRect.X, cellRect.Y, 32, 12 };
						DrawListBoxProgressCell(progressRect, cell.Value);
						break;
					}

					default:
						break;
					}
				}
			}
			else
			{
				COLORREF textColor = GetIntArrayValue(data.ListBoxItemData(), itemIndex, ListBoxTextColor());
				if (textColor == static_cast<COLORREF>(-1))
					textColor = ListBoxTextColor();

				if (style & WS_DISABLED)
					textColor = ListBoxDisabledTextColor();

				RectangleStruct textRect
				{
					rowRect.X + 2,
					rowRect.Y,
					rowRect.Width,
					rowRect.Height
				};

				OwnerDraw::PrintTextFixedLength(
					textColor,
					pFont,
					&textRect,
					itemText,
					static_cast<int>(std::wcslen(itemText)),
					0,
					0,
					nullptr,
					0);
			}

			++itemIndex;
		}

		::ValidateRect(hWnd, &updateRect);
		if (data.ListBoxScrollBarHwnd())
			::InvalidateRect(data.ListBoxScrollBarHwnd(), nullptr, FALSE);
	}

	void SyncListBoxScrollBar(HWND hWnd, OwnerDrawDialogElement& data, const RECT& clientRect, int itemCount, int itemHeight)
	{
		if (itemHeight <= 0)
			itemHeight = 1;

		const int visibleItems = itemHeight ? (clientRect.bottom - clientRect.top) / itemHeight : 0;
		int maxTopIndex = itemCount - visibleItems;
		if (maxTopIndex < 0)
			maxTopIndex = 0;

		if (data.ListBoxTopIndex() > maxTopIndex)
			data.ListBoxTopIndex() = maxTopIndex;

		if (data.ListBoxScrollBarHwnd() && reinterpret_cast<intptr_t>(data.ListBoxScrollBarHwnd()) > 1)
		{
			SCROLLINFO scrollInfo {};
			scrollInfo.cbSize = sizeof(scrollInfo);
			scrollInfo.fMask = SIF_RANGE | SIF_POS;
			scrollInfo.nMin = 0;
			scrollInfo.nMax = maxTopIndex;
			scrollInfo.nPos = data.ListBoxTopIndex();
			::SendMessageA(data.ListBoxScrollBarHwnd(), SBM_SETSCROLLINFO, 0, reinterpret_cast<LPARAM>(&scrollInfo));
		}
	}

	void UpdateListBoxScrollBar(HWND hWnd, OwnerDrawDialogElement& data, const RECT& clientRect)
	{
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		const bool needsScrollbar = itemCount * itemHeight > clientRect.bottom - clientRect.top;
		const int scrollBarWidth = 2 * OwnerDraw::ControlInsetPx + ListBoxScrollBarExtraWidth;

		SyncListBoxScrollBar(hWnd, data, clientRect, itemCount, itemHeight);

		if (needsScrollbar)
		{
			if (!data.ListBoxScrollBarHwnd())
			{
				data.ListBoxScrollBarHwnd() = reinterpret_cast<HWND>(1);

				const HWND parentHwnd = ::GetParent(hWnd);
				RECT parentRect {};
				RECT listRect {};
				OwnerDraw::GetRectangle(parentHwnd, &parentRect);
				OwnerDraw::GetRectangle(hWnd, &listRect);

				const int x = listRect.left - parentRect.left - scrollBarWidth + clientRect.right + 1;
				const int y = listRect.top - parentRect.top + clientRect.top;
				const int height = listRect.bottom - listRect.top;

				data.ListBoxScrollBarHwnd() = ::CreateWindowExA(
					0,
					"Scrollbar",
					nullptr,
					WS_CHILD | WS_VISIBLE | SBS_VERT | WS_TABSTOP,
					x,
					y,
					scrollBarWidth,
					height,
					parentHwnd,
					nullptr,
					reinterpret_cast<HINSTANCE>(Phobos::hInstance),
					nullptr);

				data.ListBoxScrollBarWidth() = scrollBarWidth;
				OwnerDraw::RegisterChildControlProc(data.ListBoxScrollBarHwnd(), 0);

				if (auto pScrollData = FindOwnerDrawData(data.ListBoxScrollBarHwnd()))
				{
					pScrollData->ScrollBarNotifyHwnd() = hWnd;
					pScrollData->ScrollBarDisabled() = false;
				}

				SyncListBoxScrollBar(hWnd, data, clientRect, itemCount, itemHeight);

				::SetWindowPos(
					hWnd,
					nullptr,
					0,
					0,
					listRect.right - listRect.left - scrollBarWidth,
					listRect.bottom - listRect.top,
					SWP_NOMOVE | SWP_NOZORDER);

				::ShowWindow(data.ListBoxScrollBarHwnd(), SW_SHOW);
				::BringWindowToTop(data.ListBoxScrollBarHwnd());
				::InvalidateRect(data.ListBoxScrollBarHwnd(), nullptr, FALSE);
				::UpdateWindow(data.ListBoxScrollBarHwnd());
			}

			return;
		}

		if (!data.ListBoxScrollBarHwnd() || data.NeedsControlImage)
			return;

		const HWND scrollBarHwnd = data.ListBoxScrollBarHwnd();
		::DestroyWindow(scrollBarHwnd);
		CleanupDestroyedWindow(scrollBarHwnd);
		data.ListBoxScrollBarHwnd() = nullptr;

		RECT listRect {};
		::GetWindowRect(hWnd, &listRect);
		::SetWindowPos(
			hWnd,
			nullptr,
			0,
			0,
			listRect.right - listRect.left + scrollBarWidth,
			listRect.bottom - listRect.top,
			SWP_NOMOVE | SWP_NOZORDER);

		data.ListBoxScrollBarWidth() = 0;
	}

	void FinishDialogInitialization(HWND hWnd)
	{
		if (!SessionClass::Instance.CurrentlyInGame)
			OwnerDraw::LoadNotInGameResources(hWnd);

		OwnerDraw::UpdateTopPanelAnimationFlag(hWnd);
		OwnerDraw::UpdateButtonAnimationFlag(hWnd);
		OwnerDraw::UpdateMainScreenAnimationFlag(hWnd);
		OwnerDraw::UpdateFlagD8FromDialogID(hWnd);

		::EnumChildWindows(hWnd, OwnerDraw::InitCompactDialogControlsProc, 0);

		if (OwnerDraw::TrySetDialogLayoutBand1(hWnd))
		{
			auto baseSize = OwnerDraw::BaseLayoutSize;
			OwnerDraw::UpdateControlPosition(hWnd, &baseSize);
		}
		else
		{
			OwnerDraw::TrySetDialogLayoutBand2(hWnd);
		}

		::EnumChildWindows(hWnd, OwnerDraw::ClassifyLayoutBand, 0);
		::EnumChildWindows(hWnd, OwnerDraw::ResetControlDrawModeAndTimerProc, 0);
		UI::CenterWindow(hWnd);
		::SetFocus(hWnd);
	}

	void RegisterDialogControls(HWND hWnd, int dialogID)
	{
		UI::RegisterComboDropAndNewEditClasses();
		OwnerDraw::CurrentDialogHwnd = hWnd;

		::EnumChildWindows(hWnd, OwnerDraw::SetNeedsControlImage, 1);
		OwnerDraw::SetNeedsControlImage(hWnd, 1);

		::EnumChildWindows(hWnd, OwnerDraw::ReplaceEditWithListboxProc, 1);

		::EnumChildWindows(hWnd, OwnerDraw::RegisterChildControlProc, 0);
		OwnerDraw::RegisterChildControlProc(hWnd, 0);

		::EnumChildWindows(hWnd, OwnerDraw::SetNeedsControlImage, 0);
		OwnerDraw::SetNeedsControlImage(hWnd, 0);

		OwnerDraw::ScaleControls(hWnd);
		OwnerDraw::SetDialogID(hWnd, dialogID);
	}

	LRESULT HandleInitDialog(HWND hWnd, LPARAM lParam)
	{
		++Unsorted::WSDialogCount;

		if (lParam)
		{
			const int dialogID = *reinterpret_cast<const WORD*>(lParam);
			RegisterDialogControls(hWnd, dialogID);
		}
		else
		{
			OwnerDraw::PrepareDialogChildControls(hWnd, 0);
			OwnerDraw::ScaleControls(hWnd);

			if (auto pData = FindOwnerDrawData(hWnd))
				pData->DialogID = 0;
		}

		FinishDialogInitialization(hWnd);
		return 0;
	}

	LRESULT HandlePaint(HWND hWnd)
	{
		auto pData = FindOwnerDrawData(hWnd);
		if (!pData)
			return 0;

		if (pData->SkipDraw)
		{
			::ValidateRect(hWnd, nullptr);
			return 1;
		}

		OwnerDraw::Paint(hWnd);

		pData = FindOwnerDrawData(hWnd);
		if (pData && pData->HasFadeAnimation)
		{
			if (const auto movieHwnd = ::GetDlgItem(hWnd, OwnerDraw::TransitionMovie))
				::SendMessageA(movieHwnd, WW_STATIC_DETACHMOVIE, 0, 0);

			OwnerDraw::DrawCampaignMenuTransition(hWnd, false);
			pData->HasFadeAnimation = false;
		}

		::ValidateRect(hWnd, nullptr);
		return 0;
	}

	LRESULT HandleTooltipRefresh(HWND hWnd, LPARAM lParam)
	{
		const auto tooltipHwnd = ::GetDlgItem(hWnd, OwnerDraw::TooltipText);
		if (!tooltipHwnd)
			return 0;

		const int screenX = LOWORD(lParam);
		const int screenY = HIWORD(lParam);

		RECT dialogRect;
		::GetWindowRect(hWnd, &dialogRect);

		const POINT clientPoint
		{
			screenX - dialogRect.left,
			screenY - dialogRect.top
		};

		WideWstring tooltipText;

		if (const auto controlHwnd = ::ChildWindowFromPointEx(hWnd, clientPoint, CWP_SKIPINVISIBLE))
		{
			const LPARAM pointParam = MAKELPARAM(LOWORD(lParam), HIWORD(lParam));
			const auto hitCode = ::SendMessageA(controlHwnd, WW_QUERYTOOLTIPHIT, 0, pointParam);

			tooltipText = QueryTooltipText(hWnd, controlHwnd, hitCode);

			if (IsEmpty(tooltipText))
			{
				tooltipText = QueryTooltipText(hWnd, controlHwnd, -1);

				if (IsEmpty(tooltipText))
				{
					if (const auto label = OwnerDraw::GetTooltipStringLabel(hWnd, controlHwnd))
					{
						tooltipText = StringTable::LoadString(
							label,
							nullptr,
							"D:\\ra2mdpost\\ownrdraw.cpp",
							1957);
					}
					else
					{
						tooltipText = L"";
					}
				}
			}
		}

		::SendMessageA(tooltipHwnd, WW_SETTEXTW, 0, reinterpret_cast<LPARAM>(GetWideTextBuffer(tooltipText)));
		return 0;
	}
}

LRESULT __fastcall WWUI::OwnerDrawStandardWndProc(HWND hWnd, UINT message, WPARAM, LPARAM lParam)
{
	if (message <= WM_NCHITTEST)
	{
		switch (message)
		{
		case WM_DESTROY:
			UI::RemoveModelessDialog(hWnd);
			--Unsorted::WSDialogCount;
			::SetFocus(Game::hWnd);
			return 0;

		case WM_PAINT:
			return HandlePaint(hWnd);

		case WM_ERASEBKGND:
			return 1;

		case WM_DRAWITEM:
			OwnerDraw::DrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
			return 1;

		case WM_NCHITTEST:
			return HandleTooltipRefresh(hWnd, lParam);

		default:
			return 0;
		}
	}

	if (message == WM_INITDIALOG)
		return HandleInitDialog(hWnd, lParam);

	if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC)
		return reinterpret_cast<LRESULT>(::GetStockObject(BLACK_BRUSH));

	if (message == WW_INITDIALOG)
	{
		::SendMessageA(hWnd, WW_BRINGTOTOP, reinterpret_cast<WPARAM>(hWnd), 1);
		return 0;
	}

	if (message == WW_TRANSITION_COMPLETE)
		::EnumChildWindows(hWnd, OwnerDraw::SendTransitionCompleteToCustomTextChildProc, 0);

	return 0;
}

LRESULT CALLBACK WWUI::OwnerDrawWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_SETCURSOR)
		return 1;

	const bool isPaintMessage = message == WM_PAINT;
	const auto updatePrimaryAfterPaint = [isPaintMessage]()
	{
		if (isPaintMessage)
			RenderDX::UpdateScreen(DSurface::Primary);
	};

	if (OwnerDraw::ServiceIMEMessage(hWnd, message, wParam, lParam))
	{
		const auto imeResult = OwnerDraw::GetIMEResult();
		updatePrimaryAfterPaint();
		return imeResult;
	}

	const auto pSelectedWndProc = FindWindowProc(OwnerDraw::SubclassProcs, hWnd);

	if (message == WM_SYSKEYUP && wParam == VK_TAB)
		::SendMessageA(Game::hWnd, WM_SYSKEYUP, VK_TAB, lParam);

	// RenderDX's original 0x610E77 hook made the final copyback use client-surface coordinates.
	constexpr int windowOffsetX = 0;
	constexpr int windowOffsetY = 0;

	if (IsActiveWindowMessageBlocked(hWnd, message))
	{
		updatePrimaryAfterPaint();
		return 0;
	}

	WindowMessageGuardScope guard(hWnd, message);
	if (!guard.Enter())
	{
		updatePrimaryAfterPaint();
		return 0;
	}

	RECT clientRect {};
	::GetClientRect(hWnd, &clientRect);

	RECT ownerDrawClientRect {};
	OwnerDraw::GetRectangle(hWnd, &ownerDrawClientRect);

	if (isPaintMessage && !Unsorted::GameInFocus)
	{
		::ValidateRect(hWnd, nullptr);
		guard.Release();
		updatePrimaryAfterPaint();
		return 0;
	}

	if (isPaintMessage)
	{
		++OwnerDraw::PaintDepth;

		RECT updateRect {};
		::GetUpdateRect(hWnd, &updateRect, FALSE);
		updateRect.left += ownerDrawClientRect.left;
		updateRect.right += ownerDrawClientRect.left;
		updateRect.top += ownerDrawClientRect.top;
		updateRect.bottom += ownerDrawClientRect.top;
	}

	auto pData = FindOwnerDrawData(hWnd);
	int paintCopyMode = 0;
	bool redrawTooltip = false;
	LRESULT result = 0;

	auto complete = [&](LRESULT result) -> LRESULT
	{
		guard.Release();

		if (isPaintMessage)
			FinishPaint(hWnd, pData, paintCopyMode, windowOffsetX, windowOffsetY);

		if (redrawTooltip)
			OwnerDraw::DrawTooltip(true);

		updatePrimaryAfterPaint();

		return message == WM_INITDIALOG ? 0 : result;
	};

	switch (message)
	{
	case WW_GETHWND:
		return complete(pData && pData->Hwnd_00C ? 1 : 0);

	case WW_GETGDIPROPS:
		if (pData)
		{
			const auto hdc = reinterpret_cast<HDC>(lParam);
			const auto oldFont = ::SelectObject(hdc, ::GetStockObject(SYSTEM_FONT));
			pData->Extra[SavedFontExtraIndex] = reinterpret_cast<int>(oldFont);
			::SelectObject(hdc, oldFont);
			pData->Extra[SavedBkModeExtraIndex] = ::GetBkMode(hdc);
			pData->Extra[SavedBkColorExtraIndex] = ::GetBkColor(hdc);
			pData->Extra[SavedTextColorExtraIndex] = ::GetTextColor(hdc);
			return complete(1);
		}
		break;

	case WW_SETGDIPROPS:
		if (pData)
		{
			const auto hdc = reinterpret_cast<HDC>(lParam);
			::SelectObject(hdc, reinterpret_cast<HGDIOBJ>(pData->Extra[SavedFontExtraIndex]));
			::SetBkMode(hdc, pData->Extra[SavedBkModeExtraIndex]);
			::SetBkColor(hdc, pData->Extra[SavedBkColorExtraIndex]);
			::SetTextColor(hdc, pData->Extra[SavedTextColorExtraIndex]);
			return complete(1);
		}
		break;

	case WW_SETHASIMAGE:
		if (pData)
		{
			const auto previous = pData->NeedsControlImage;
			const HWND linkedHwnd = pData->Hwnd_00C;
			pData->NeedsControlImage = lParam;

			if (linkedHwnd)
			{
				if (auto pLinkedData = FindOwnerDrawData(linkedHwnd))
					pLinkedData->NeedsControlImage = lParam;
			}

			result = previous;
		}
		break;

	case WW_BRINGTOTOP:
		return complete(BringOwnerDrawWindowToTop(hWnd, wParam, lParam));

	default:
		break;
	}

	if (OwnerDraw::ActiveWindowStackCount)
	{
		if (message == WM_WINDOWPOSCHANGING)
		{
			LRESULT windowPosResult = 0;
			if (HandleWindowPosChanging(hWnd, lParam, windowPosResult))
				return complete(windowPosResult);
		}
		else if (message == WM_DESTROY)
		{
			RemoveActiveWindow(hWnd);
		}
	}

	RestoreAndClearTooltipIfNeeded(hWnd, message);

	bool callSelectedHandler = true;

	switch (message)
	{
	case WM_ERASEBKGND:
		return complete(1);

	case WM_SETFOCUS:
		if (pSelectedWndProc == OwnerDraw::OwnerDrawButtonHandler || pSelectedWndProc == OwnerDraw::ListBoxHandler)
			::SetFocus(reinterpret_cast<HWND>(wParam));

		if (pData && !pData->HasFocus)
		{
			OwnerDraw::CancelIMEComposition();
			pData->HasFocus = 1;
		}
		break;

	case WM_KILLFOCUS:
		if (pData)
			pData->HasFocus = 0;
		break;

	case WM_SHOWWINDOW:
		if (!wParam && pData)
			pData->Extra[PaintStateExtraIndex] = 0;
		break;

	default:
		break;
	}

	if (pData)
	{
		switch (message)
		{
		case WW_SETIMAGE:
			result = reinterpret_cast<LRESULT>(pData->ControlImage);
			pData->ControlImage = reinterpret_cast<Surface*>(lParam);
			return complete(result);

		case WW_SETACTIVEIMAGE:
			result = reinterpret_cast<LRESULT>(pData->StateImageSurface);
			pData->StateImageSurface = reinterpret_cast<Surface*>(lParam);
			return complete(result);

		case WW_SETUNKNOWNPROP24:
			result = pData->Erase1;
			pData->Erase1 = lParam;
			return complete(result);

		default:
			break;
		}

		if (pData->Erase1 && (message == WM_TIMER || (message >= WM_MOUSEFIRST && message <= WM_MBUTTONDBLCLK)))
		{
			RECT rect {};
			::GetWindowRect(hWnd, &rect);
			::WindowFromPoint(POINT { rect.left, rect.top });
		}
	}

	if (IsNativeTextMessage(message, result))
		callSelectedHandler = false;

	if (pData)
		HandleElementTextMessage(*pData, hWnd, message, wParam, lParam, result, callSelectedHandler);

	if (message == WM_MOUSEMOVE)
		UpdateTooltipTextOnMouseMove(hWnd, lParam);

	if (isPaintMessage && pData)
	{
		result = DispatchPaintMessage(
			hWnd,
			message,
			wParam,
			lParam,
			pSelectedWndProc,
			ownerDrawClientRect,
			paintCopyMode,
			redrawTooltip);
	}
	else if (callSelectedHandler && pSelectedWndProc)
	{
		result = CallSelectedHandler(pSelectedWndProc, hWnd, message, wParam, lParam);
	}

	if (message == WM_NCDESTROY)
		CleanupDestroyedWindow(hWnd);

	return complete(result);
}

LRESULT CALLBACK WWUI::ListBoxCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto pData = FindOwnerDrawData(hWnd);
	if (!pData)
		return 0;

	auto& data = *pData;
	const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);

	RECT clientRect {};
	::GetClientRect(hWnd, &clientRect);

	RECT ownerRect {};
	OwnerDraw::GetRectangle(hWnd, &ownerRect);

	const int inset = OwnerDraw::ControlInsetPx;
	clientRect.right -= 2 * inset;
	clientRect.bottom -= 2 * inset;
	ownerRect.left += inset;
	ownerRect.top += inset;
	ownerRect.right -= inset;
	ownerRect.bottom -= inset;

	const bool updateScrollBar = message != LB_GETCOUNT
		&& message != LB_GETITEMHEIGHT
		&& message != WM_VSCROLL;

	if (updateScrollBar)
	{
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		SyncListBoxScrollBar(hWnd, data, clientRect, itemCount, itemHeight);
	}

	auto finish = [&](LRESULT result) -> LRESULT
	{
		if (updateScrollBar)
			UpdateListBoxScrollBar(hWnd, data, clientRect);

		return result;
	};

	auto forwardOriginal = [&]() -> LRESULT
	{
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);
	};

	auto setSelection = [&](int index, int selected)
	{
		SetIntArrayValue(data.ListBoxSelectionStates(), index, selected ? 1 : 0, 0);
	};

	auto playClick = []()
	{
		if (RulesClass::Instance)
			VocClass::PlayGlobal(RulesClass::Instance->GenericClick, 0x2000, 1.0f);
	};

	auto addOrInsertString = [&](bool wideText, bool insert) -> LRESULT
	{
		char narrowText[5120] {};
		wchar_t wideBuffer[5120] {};

		const LPARAM nativeTextParam = [&]() -> LPARAM
		{
			if (wideText)
			{
				const auto pWideText = reinterpret_cast<const wchar_t*>(lParam);
				std::wcsncpy(wideBuffer, pWideText ? pWideText : L"", std::size(wideBuffer) - 1);
				WideToCharString(narrowText, std::size(narrowText), wideBuffer);
				return reinterpret_cast<LPARAM>(narrowText);
			}

			const auto pText = reinterpret_cast<const char*>(lParam);
			std::strncpy(narrowText, pText ? pText : "", std::size(narrowText) - 1);
			CharToWideString(wideBuffer, std::size(wideBuffer), narrowText);
			return reinterpret_cast<LPARAM>(narrowText);
		}();

		const UINT nativeMessage = insert ? LB_INSERTSTRING : LB_ADDSTRING;
		const WPARAM nativeIndex = insert ? wParam : 0;
		const auto nativeResult = CallSelectedHandler(pOriginalWndProc, hWnd, nativeMessage, nativeIndex, nativeTextParam);
		if (nativeResult == LB_ERR || nativeResult == LB_ERRSPACE)
			return nativeResult;

		const int itemIndex = static_cast<int>(nativeResult);
		auto pEntry = AllocateListBoxTextEntry(data, wideBuffer, wideText);
		if (!pEntry)
		{
			CallSelectedHandler(pOriginalWndProc, hWnd, LB_DELETESTRING, itemIndex, 0);
			return LB_ERRSPACE;
		}

		const auto setDataResult = CallSelectedHandler(
			pOriginalWndProc,
			hWnd,
			LB_SETITEMDATA,
			itemIndex,
			reinterpret_cast<LPARAM>(pEntry));

		if (setDataResult == LB_ERR)
		{
			RemoveListBoxTextEntry(data, pEntry);
			CallSelectedHandler(pOriginalWndProc, hWnd, LB_DELETESTRING, itemIndex, 0);
			return LB_ERR;
		}

		InsertListBoxRow(data, itemIndex);
		return itemIndex;
	};

	auto findString = [&](bool wideText, bool exact, bool select) -> LRESULT
	{
		wchar_t needle[5120] {};
		if (wideText)
		{
			const auto pText = reinterpret_cast<const wchar_t*>(lParam);
			std::wcsncpy(needle, pText ? pText : L"", std::size(needle) - 1);
		}
		else
		{
			CharToWideString(needle, std::size(needle), reinterpret_cast<const char*>(lParam));
		}

		const int count = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		int index = static_cast<int>(wParam);
		if (index < 0)
			index = 0;

		if (index >= count)
			return LB_ERR;

		const size_t needleLength = std::wcslen(needle);
		for (; index < count; ++index)
		{
			const auto pEntry = GetListBoxTextEntry(pOriginalWndProc, hWnd, index);
			const wchar_t* pText = pEntry && pEntry->Text ? pEntry->Text : L"";
			const bool match = exact
				? _wcsicmp(needle, pText) == 0
				: _wcsnicmp(needle, pText, needleLength) == 0;

			if (!match)
				continue;

			if (select)
				::SendMessageA(hWnd, LB_SETCURSEL, index, 0);

			return index;
		}

		return LB_ERR;
	};

	switch (message)
	{
	case WM_SIZE:
			if (data.ListBoxScrollBarHwnd() && reinterpret_cast<intptr_t>(data.ListBoxScrollBarHwnd()) > 1)
		{
			const HWND parentHwnd = ::GetParent(hWnd);
			RECT parentRect {};
			RECT listRect {};
			OwnerDraw::GetRectangle(parentHwnd, &parentRect);
			OwnerDraw::GetRectangle(hWnd, &listRect);
			::MoveWindow(
				data.ListBoxScrollBarHwnd(),
				listRect.right - parentRect.left,
				listRect.top - parentRect.top,
				2 * inset + ListBoxScrollBarExtraWidth,
				listRect.bottom - listRect.top,
				TRUE);
		}

		if (data.CacheSurface
			&& (LOWORD(lParam) != data.CacheSurface->GetWidth() || HIWORD(lParam) != data.CacheSurface->GetHeight()))
		{
			DeleteSurfaceObject(data.CacheSurface);
			--OwnerDraw::CachedSurfaceCount;
		}

		return finish(forwardOriginal());

	case WM_PAINT:
		PaintListBox(hWnd, data, clientRect, ownerRect, pOriginalWndProc);
		return finish(0);

	case WM_ERASEBKGND:
		return 0;

	case WM_DELETEITEM:
		if (lParam)
		{
			const auto pDeleteItem = reinterpret_cast<DELETEITEMSTRUCT*>(lParam);
			RemoveListBoxTextEntry(data, reinterpret_cast<WWUIListBoxTextEntry*>(pDeleteItem->itemData));
		}
		return 1;

	case WM_SETFONT:
	{
		TEXTMETRICA metrics {};
		if (const HDC hdc = ::GetDC(hWnd))
		{
			::GetTextMetricsA(hdc, &metrics);
			::ReleaseDC(hWnd, hdc);
		}

		::SendMessageA(hWnd, LB_SETITEMHEIGHT, static_cast<WPARAM>(-1), LOWORD(metrics.tmHeight + 2));
		data.Extra[SavedFontExtraIndex] = static_cast<int>(wParam);
		return 0;
	}

	case WM_VSCROLL:
		if (data.ListBoxScrollBarHwnd())
		{
			const auto position = ::SendMessageA(data.ListBoxScrollBarHwnd(), SBM_GETPOS, 0, 0);
			if (position != ::SendMessageA(hWnd, LB_GETTOPINDEX, 0, 0))
				::SendMessageA(hWnd, LB_SETTOPINDEX, position, 0);
		}
		return 0;

	case WM_RBUTTONDOWN:
		if (::GetWindowLongA(hWnd, GWL_STYLE) & LBS_MULTIPLESEL)
		{
			if (auto pSelections = data.ListBoxSelectionStates())
			{
				for (int i = 0; i < pSelections->Count; ++i)
					pSelections->Items[i] = 0;
			}

			data.ListBoxCurrentSelection() = -1;
			NotifyListBoxSelectionChanged(hWnd);
			::InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		break;

	case WM_LBUTTONDBLCLK:
		PostListBoxDoubleClick(hWnd);
		return 0;

	case WM_LBUTTONDOWN:
	{
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		const int itemIndex = data.ListBoxTopIndex() + SignedHighWord(lParam) / itemHeight;
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		if (itemIndex < 0 || itemIndex >= itemCount)
			return 0;

		const LONG style = ::GetWindowLongA(hWnd, GWL_STYLE);
		::SetFocus(hWnd);
		const auto previousImageState = ::SendMessageA(hWnd, WW_SETHASIMAGE, 0, 1);

		if (style & LBS_MULTIPLESEL)
		{
			const bool selected = ::SendMessageA(hWnd, LB_GETSEL, itemIndex, 0) == 0;
			playClick();
			::SendMessageA(hWnd, LB_SETSEL, selected, itemIndex);
		}
		else if (!(style & LBS_NOSEL))
		{
			playClick();
			::SendMessageA(hWnd, LB_SETCURSEL, itemIndex, 0);
		}

		::SendMessageA(hWnd, WW_SETHASIMAGE, 0, previousImageState);
		::InvalidateRect(hWnd, nullptr, FALSE);
		NotifyListBoxSelectionChanged(hWnd);
		return 0;
	}

	case LB_SETSEL:
	{
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		int index = static_cast<int>(lParam);
		if (index < -1)
			return LB_ERR;

		if (index >= itemCount)
			index = itemCount - 1;

		if (!data.ListBoxSelectionStates())
			data.ListBoxSelectionStates() = CreateIntArray();

		if (index == -1)
		{
			if (data.ListBoxSelectionStates())
			{
				EnsureIntArraySize(*data.ListBoxSelectionStates(), itemCount, 0);
				for (int i = 0; i < data.ListBoxSelectionStates()->Count; ++i)
					data.ListBoxSelectionStates()->Items[i] = wParam ? 1 : 0;
			}
		}
		else
		{
			setSelection(index, wParam ? 1 : 0);
		}

		NotifyListBoxSelectionChanged(hWnd);
		::InvalidateRect(hWnd, nullptr, FALSE);
		return finish(0);
	}

	case LB_GETSEL:
		return GetIntArrayValue(data.ListBoxSelectionStates(), static_cast<int>(wParam), 0);

	case LB_GETSELCOUNT:
	{
		int count = 0;
		if (auto pSelections = data.ListBoxSelectionStates())
		{
			for (int i = 0; i < pSelections->Count; ++i)
			{
				if (pSelections->Items[i])
					++count;
			}
		}
		return count;
	}

	case LB_GETSELITEMS:
	{
		int written = 0;
		auto pOut = reinterpret_cast<int*>(lParam);
		if (pOut)
		{
			if (auto pSelections = data.ListBoxSelectionStates())
			{
				for (int i = 0; i < pSelections->Count && written < static_cast<int>(wParam); ++i)
				{
					if (pSelections->Items[i])
						pOut[written++] = i;
				}
			}
		}
		return written;
	}

	case LB_SELITEMRANGE:
	{
		int first = SignedLowWord(lParam);
		int last = SignedHighWord(lParam);
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		if (first < 0 || last < first)
			return LB_ERR;

		if (last >= itemCount)
			last = itemCount - 1;

		if (!data.ListBoxSelectionStates())
			data.ListBoxSelectionStates() = CreateIntArray();

		if (data.ListBoxSelectionStates())
		{
			EnsureIntArraySize(*data.ListBoxSelectionStates(), last + 1, 0);
			for (int i = first; i <= last; ++i)
				data.ListBoxSelectionStates()->Items[i] = wParam ? 1 : 0;
		}

		NotifyListBoxSelectionChanged(hWnd);
		return finish(0);
	}

	case LB_SETCURSEL:
	{
		int selection = static_cast<int>(wParam);
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		if (selection >= -1 && selection < itemCount)
		{
			if (data.ListBoxCurrentSelection() != -1)
				setSelection(data.ListBoxCurrentSelection(), 0);

			data.ListBoxCurrentSelection() = selection;
			if (selection != -1)
				setSelection(selection, 1);
		}

		NotifyListBoxSelectionChanged(hWnd);
		::InvalidateRect(hWnd, nullptr, FALSE);
		return finish(0);
	}

	case LB_GETCURSEL:
		return data.ListBoxCurrentSelection();

	case LB_GETTOPINDEX:
		return data.ListBoxTopIndex();

	case LB_SETTOPINDEX:
	{
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		const int visibleItems = (clientRect.bottom - clientRect.top) / itemHeight;
		int topIndex = static_cast<int>(wParam);
		if (topIndex < 0)
			topIndex = 0;

		if (itemCount - visibleItems <= 0)
			topIndex = 0;
		else if (topIndex > itemCount - visibleItems)
			topIndex = itemCount - visibleItems;

		if (topIndex != data.ListBoxTopIndex())
		{
			data.ListBoxTopIndex() = topIndex;
			::InvalidateRect(hWnd, nullptr, FALSE);
		}

		return finish(0);
	}

	case LB_GETITEMRECT:
	{
		const int index = static_cast<int>(wParam);
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		const int visibleIndex = index - data.ListBoxTopIndex();

		if (index < data.ListBoxTopIndex() || index >= itemCount || visibleIndex > (clientRect.bottom - clientRect.top) / itemHeight)
			return LB_ERR;

		if (auto pRect = reinterpret_cast<RECT*>(lParam))
		{
			pRect->left = clientRect.left;
			pRect->top = visibleIndex * itemHeight;
			pRect->right = clientRect.right - clientRect.left;
			pRect->bottom = pRect->top + itemHeight;
		}
		return 0;
	}

	case LB_GETITEMDATA:
	case LB_SETITEMDATA:
	case CB_GETITEMDATA:
	case CB_SETITEMDATA:
	{
		const auto pEntry = GetListBoxTextEntry(pOriginalWndProc, hWnd, static_cast<int>(wParam));
		if (!pEntry)
			return LB_ERR;

		if (message == LB_GETITEMDATA || message == CB_GETITEMDATA)
			return pEntry->ItemData;

		pEntry->ItemData = static_cast<int>(lParam);
		return reinterpret_cast<LRESULT>(pEntry);
	}

	case LB_DELETESTRING:
	{
		const int index = static_cast<int>(wParam);
		const auto pEntry = GetListBoxTextEntry(pOriginalWndProc, hWnd, index);
		RemoveListBoxRow(data, index);
		const auto result = CallSelectedHandler(pOriginalWndProc, hWnd, LB_DELETESTRING, wParam, lParam);
		RemoveListBoxTextEntry(data, pEntry);
		return finish(result);
	}

	case LB_RESETCONTENT:
		ClearListBoxRows(data, false);
		ClearListBoxTextEntries(data);
		NotifyListBoxSelectionChanged(hWnd);
		return finish(CallSelectedHandler(pOriginalWndProc, hWnd, LB_RESETCONTENT, wParam, lParam));

	case WM_NCDESTROY:
		ClearListBoxRows(data, true);
		ClearListBoxTextEntries(data);
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);

	case LB_GETTEXTLEN:
	case WW_GETTEXTW:
	case WW_GETTEXTA:
	case WW_LB_GETTEXTW:
	case WW_LB_GETTEXTA:
	case WW_LB_GETITEMTEXTFORMAT:
	{
		const auto pEntry = GetListBoxTextEntry(pOriginalWndProc, hWnd, static_cast<int>(wParam));
		if (!pEntry)
			return LB_ERR;

		if (message == WW_LB_GETITEMTEXTFORMAT)
			return pEntry->IsWide;

		const wchar_t* pText = pEntry->Text ? pEntry->Text : L"";
		const auto length = static_cast<LRESULT>(std::wcslen(pText));
		if (message == LB_GETTEXTLEN)
			return length;

		if (lParam)
		{
			if (message == WW_GETTEXTA || message == WW_LB_GETTEXTA)
				WideToCharString(reinterpret_cast<char*>(lParam), static_cast<int>(length + 1), pText);
			else
				std::wcscpy(reinterpret_cast<wchar_t*>(lParam), pText);
		}
		return length;
	}

	case WW_LB_FINDSTRINGA:
		return findString(false, false, false);

	case WW_LB_FINDSTRINGEXACTA:
		return findString(false, true, false);

	case WW_LB_SELECTSTRINGA:
		return findString(false, false, true);

	case WW_LB_FINDSTRINGW:
		return findString(true, false, false);

	case WW_LB_FINDSTRINGEXACTW:
		return findString(true, true, false);

	case WW_LB_SELECTSTRINGW:
		return findString(true, false, true);

	case WW_LB_INSERTSTRINGA:
		return finish(addOrInsertString(false, true));

	case WW_LB_ADDSTRINGA:
		return finish(addOrInsertString(false, false));

	case WW_LB_INSERTSTRINGW:
		return finish(addOrInsertString(true, true));

	case WW_LB_ADDSTRINGW:
		return finish(addOrInsertString(true, false));

	case WW_QUERYTOOLTIPHIT:
	{
		const int x = SignedLowWord(lParam);
		const int y = SignedHighWord(lParam);
		if (x < clientRect.right && y < clientRect.bottom)
		{
			const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
			const int itemIndex = data.ListBoxTopIndex() + y / itemHeight;
			const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
			if (itemIndex >= 0 && itemIndex < itemCount)
				return itemIndex;
		}
		return -1;
	}

	case WW_LB_GETSCROLLBARHWND:
		return reinterpret_cast<LRESULT>(data.ListBoxScrollBarHwnd());

	case WW_LB_ADDCOLUMN:
	{
		if (!data.ListBoxColumns())
			data.ListBoxColumns() = CreateListBoxColumnArray();

		auto pColumns = data.ListBoxColumns();
		if (!pColumns)
			return -1;

		const int x = static_cast<int>(lParam);
		if (FindListBoxColumn(pColumns, x))
			return x;

		if (pColumns->Count >= pColumns->Capacity)
			ResizeListBoxColumnStorage(*pColumns, std::max(pColumns->Capacity * 2, 10));

		auto& column = pColumns->Items[pColumns->Count++];
		column = {};
		column.X = x;
		column.Width = static_cast<int>(wParam);
		return x;
	}

	case WW_LB_REMOVECOLUMN:
	{
		auto pColumns = data.ListBoxColumns();
		if (!pColumns)
			return -1;

		const int x = static_cast<int>(lParam);
		for (int i = 0; i < pColumns->Count; ++i)
		{
			if (pColumns->Items[i].X != x)
				continue;

			ClearListBoxColumnCells(pColumns->Items[i], true);
			if (i < pColumns->Count - 1)
			{
				std::memmove(
					&pColumns->Items[i],
					&pColumns->Items[i + 1],
					sizeof(WWUIListBoxColumn) * (pColumns->Count - i - 1));
			}
			--pColumns->Count;
			std::memset(&pColumns->Items[pColumns->Count], 0, sizeof(WWUIListBoxColumn));
			return x;
		}
		return -1;
	}

	case WW_LB_SETCELLTEXT:
	{
		auto pColumns = data.ListBoxColumns();
		const int columnX = LOWORD(wParam);
		const int rowIndex = HIWORD(wParam);
		auto pColumn = FindListBoxColumn(pColumns, columnX);

		if (!pColumn || rowIndex >= ::SendMessageA(hWnd, LB_GETCOUNT, 0, 0))
			return -1;

		const auto defaultFormat = pColumn == &pColumns->Items[0]
			? WWUIListBoxCellFormat::ItemText
			: WWUIListBoxCellFormat::Empty;

		EnsureListBoxCellCount(*pColumn, rowIndex + 1, defaultFormat);
		auto& target = pColumn->Cells[rowIndex];
		ResetListBoxCell(target);

		if (const auto pSource = reinterpret_cast<const WWUIListBoxCell*>(lParam))
			target = *pSource;

		return columnX;
	}

	case WW_LB_GETCELLTEXT:
	{
		const int itemCount = static_cast<int>(::SendMessageA(hWnd, LB_GETCOUNT, 0, 0));
		const int itemHeight = std::max(static_cast<int>(::SendMessageA(hWnd, LB_GETITEMHEIGHT, 0, 0)), 1);
		const int rowIndex = data.ListBoxTopIndex() + SignedHighWord(wParam) / itemHeight;
		const int x = SignedLowWord(wParam);
		auto pColumn = FindListBoxColumnAtX(data.ListBoxColumns(), x);
		if (!pColumn || rowIndex < 0 || rowIndex >= itemCount || rowIndex >= pColumn->CellCount)
			return 0;

		const auto& text = pColumn->Cells[rowIndex].SecondaryText;
		if (lParam)
			std::wcscpy(reinterpret_cast<wchar_t*>(lParam), GetWideTextBuffer(text));

		return IsEmpty(text) ? 1 : 0;
	}

	case WW_INITDIALOG:
	{
		data.ListBoxCurrentSelection() = -1;

		int fontHeight = 10;
		if (const auto pFont = data.ListBoxFont() ? data.ListBoxFont() : BitFont::Instance)
		{
			if (pFont->InternalPTR)
				fontHeight = pFont->InternalPTR->FontHeight;
		}

		::SendMessageA(hWnd, LB_SETITEMHEIGHT, static_cast<WPARAM>(-1), LOWORD(fontHeight + 2));
		return finish(0);
	}

	case WW_SETCOLOR:
		SetIntArrayValue(data.ListBoxItemData(), static_cast<int>(wParam), static_cast<int>(lParam), -1);
		::InvalidateRect(hWnd, nullptr, FALSE);
		return finish(0);

	default:
		break;
	}

	return finish(forwardOriginal());
}

LRESULT CALLBACK WWUI::EditCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto pData = FindOwnerDrawData(hWnd);
	const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);
	auto forwardOriginal = [&]() -> LRESULT
	{
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);
	};

	if (!pData)
		return forwardOriginal();

	auto& data = *pData;
	if (::GetFocus() == hWnd && !data.EditFocusRestoreReadyFlag())
	{
		data.EditFocusRestorePendingFlag() = 1;
		::SetFocus(Game::hWnd);
	}

	const LONG windowStyle = ::GetWindowLongA(hWnd, GWL_STYLE);
	const HWND parentHwnd = ::GetParent(hWnd);

	if ((message == WM_KEYDOWN || message == WM_KEYUP) && wParam == VK_TAB)
		return 0;

	switch (message)
	{
	case WW_INITDIALOG:
	{
		if (!parentHwnd)
			return 0;

		RECT windowRect {};
		RECT clientRect {};
		RECT parentRect {};
		::GetWindowRect(hWnd, &windowRect);
		::GetClientRect(hWnd, &clientRect);
		::GetWindowRect(parentHwnd, &parentRect);

		::MoveWindow(
			hWnd,
			windowRect.left - parentRect.left + 1,
			windowRect.top - parentRect.top + 1,
			clientRect.right - 2,
			clientRect.bottom - 2,
			FALSE);

		if (::GetFocus() == hWnd)
		{
			data.EditFocusRestorePendingFlag() = 1;
			::SetFocus(Game::hWnd);
		}

		if (windowStyle & WS_TABSTOP)
		{
			data.EditRestoreTabStopFlag() = 1;
			::SetWindowLongA(hWnd, GWL_STYLE, windowStyle & ~static_cast<LONG>(WS_TABSTOP));
		}

		return 0;
	}

	case WM_SETFOCUS:
		::SendMessageA(hWnd, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
		if (!data.EditFocusRestoreReadyFlag())
			::PostMessageA(hWnd, WW_EDIT_DEFERFOCUSRESTORE, 0, 0);

		InvalidateNewEdit(hWnd, parentHwnd);
		return forwardOriginal();

	case WW_GETTEXTW:
		return CopyEditTextW(hWnd, wParam, lParam);

	case WW_GETTEXTA:
		return CopyEditTextA(hWnd, wParam, lParam);

	case WM_GETTEXTLENGTH:
	{
		std::vector<wchar_t> text(0x800);
		return static_cast<LRESULT>(GetEditWideText(hWnd, text.data(), static_cast<int>(text.size()), nullptr));
	}

	case WW_SETTEXTW:
	case WW_SETTEXTA:
		return ForwardEditSetText(data, hWnd, pOriginalWndProc);

	case WM_CHAR:
		if (wParam == VK_RETURN)
		{
			if (windowStyle & ES_MULTILINE)
				return AppendEditNewLine(hWnd, pOriginalWndProc);

			return forwardOriginal();
		}

		if (wParam == VK_TAB)
		{
			if (const HWND nextHwnd = ::GetNextDlgTabItem(parentHwnd, hWnd, FALSE))
				::SetFocus(nextHwnd);
			else
				::SetFocus(hWnd);

			return 0;
		}

		return forwardOriginal();

	case WW_EDIT_RESTOREFOCUS:
		data.EditFocusRestoreReadyFlag() = 1;
		if (data.EditFocusRestorePendingFlag())
		{
			::SetFocus(hWnd);
			data.EditFocusRestorePendingFlag() = 0;
		}

		if (data.EditRestoreTabStopFlag())
			::SetWindowLongA(hWnd, GWL_STYLE, windowStyle | WS_TABSTOP);

		return 0;

	case WM_PAINT:
	case WM_ERASEBKGND:
		PaintEdit(hWnd, data, parentHwnd, message);
		break;

	case WM_CONTEXTMENU:
		return 1;

	case WM_MOUSEMOVE:
		return 1;

	default:
		break;
	}

	switch (message)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_KILLFOCUS:
	case WM_LBUTTONDOWN:
		InvalidateNewEdit(hWnd, parentHwnd);
		break;

	default:
		break;
	}

	return forwardOriginal();
}

LRESULT CALLBACK WWUI::NewEditCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_GETDLGCODE)
		return DLGC_WANTALLKEYS;

	auto pData = FindOwnerDrawData(hWnd);
	const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);
	auto forwardOriginal = [&]() -> LRESULT
	{
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);
	};

	if (!pData)
		return forwardOriginal();

	auto& data = *pData;
	EnsureNewEditText(data);

	const HWND parentHwnd = ::GetParent(hWnd);

	if ((message == WM_KEYDOWN || message == WM_KEYUP) && wParam == VK_TAB)
	{
		if (message == WM_KEYDOWN && parentHwnd)
		{
			const WPARAM shiftPressed = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
			::SendMessageA(parentHwnd, WW_EDIT_TABNAV, shiftPressed, reinterpret_cast<LPARAM>(hWnd));
		}

		return 0;
	}

	auto copyWideText = [&]() -> LRESULT
	{
		const int capacity = static_cast<int>(wParam);
		auto pBuffer = reinterpret_cast<wchar_t*>(lParam);
		if (!pBuffer || capacity <= 0)
			return 0;

		const auto pText = NewEditTextBuffer(data);
		std::wcsncpy(pBuffer, pText, capacity - 1);
		pBuffer[capacity - 1] = L'\0';
		return static_cast<LRESULT>(std::wcslen(pBuffer));
	};

	auto copyAnsiText = [&]() -> LRESULT
	{
		const int capacity = static_cast<int>(wParam);
		auto pBuffer = reinterpret_cast<char*>(lParam);
		if (!pBuffer || capacity <= 0)
			return 0;

		WideToCharString(pBuffer, capacity, NewEditTextBuffer(data));
		return static_cast<LRESULT>(std::strlen(pBuffer));
	};

	auto handleInputCharacter = [&](wchar_t character, bool consumedInput) -> LRESULT
	{
		if (!character)
			return consumedInput ? 0 : forwardOriginal();

		if (InsertNewEditCharacter(data, character))
			NotifyNewEditTextChanged(hWnd, parentHwnd);

		return 0;
	};

	switch (message)
	{
	case WW_INITDIALOG:
	{
		if (!parentHwnd)
			return 0;

		RECT windowRect {};
		RECT clientRect {};
		RECT parentRect {};
		::GetWindowRect(hWnd, &windowRect);
		::GetClientRect(hWnd, &clientRect);
		::GetWindowRect(parentHwnd, &parentRect);

		::SetWindowPos(
			hWnd,
			nullptr,
			windowRect.left - parentRect.left + 1,
			windowRect.top - parentRect.top + 1,
			clientRect.right - 2,
			clientRect.bottom - 2,
			SWP_SHOWWINDOW);
		return 0;
	}

	case EM_LIMITTEXT:
		data.NewEditTextLimit() = static_cast<int>(wParam);
		TrimNewEditTextToLimit(data);
		return forwardOriginal();

	case WW_GETTEXTW:
		return copyWideText();

	case WW_GETTEXTA:
		return copyAnsiText();

	case WM_GETTEXTLENGTH:
		return NewEditTextLength(data);

	case WW_SETTEXTW:
	case WW_SETTEXTA:
		SetNewEditText(data, data.TextBuffer ? data.TextBuffer : L"");
		data.NewEditCaretIndex() = 0;
		data.NewEditScrollStart() = 0;
		TrimNewEditTextToLimit(data);
		data.NewEditCaretIndex() = NewEditTextLength(data);
		break;

	case WM_KEYDOWN:
		if (wParam == VK_RETURN)
		{
			NotifyNewEditEnterPressed(hWnd, parentHwnd);
			if (data.NewEditStyleFlags() & 4)
			{
				if (auto pText = EnsureNewEditText(data))
					*pText += L"\r\n";

				NotifyNewEditMultilineEnter(hWnd, parentHwnd);
			}
			return 0;
		}
		break;

	case WM_SETFOCUS:
		data.NewEditCaretBlinkState() = 0;
		::SetTimer(hWnd, 0, 1000, nullptr);
		InvalidateNewEdit(hWnd, parentHwnd);
		return forwardOriginal();

	case WM_KILLFOCUS:
		::KillTimer(hWnd, 0);
		InvalidateNewEdit(hWnd, parentHwnd);
		return forwardOriginal();

	case WM_TIMER:
		data.NewEditCaretBlinkState() ^= 1;
		::InvalidateRect(hWnd, nullptr, FALSE);
		return forwardOriginal();

	case WM_PAINT:
	case WM_ERASEBKGND:
		PaintNewEdit(hWnd, data, parentHwnd);
		break;

	case WM_CONTEXTMENU:
		return 1;

	case WM_MOUSEMOVE:
		return 1;

	default:
		break;
	}

	switch (message)
	{
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_LBUTTONDOWN:
		InvalidateNewEdit(hWnd, parentHwnd);
		break;

	default:
		break;
	}

	if (message == WM_CHAR)
	{
		if (wParam <= 0x1F)
			return forwardOriginal();

		return handleInputCharacter(LocalizeCharacter(static_cast<char>(wParam)), true);
	}

	if (message == WM_KEYDOWN)
	{
		bool textChanged = false;
		switch (wParam)
		{
		case VK_BACK:
			if (data.NewEditCaretIndex() > 0)
			{
				--data.NewEditCaretIndex();
				textChanged = RemoveNewEditTextRange(data, data.NewEditCaretIndex(), 1);
			}
			break;

		case VK_DELETE:
			if (data.NewEditCaretIndex() < NewEditTextLength(data))
				textChanged = RemoveNewEditTextRange(data, data.NewEditCaretIndex(), 1);
			break;

		case VK_END:
			data.NewEditCaretIndex() = NewEditTextLength(data);
			return 0;

		case VK_HOME:
			data.NewEditCaretIndex() = 0;
			return 0;

		case VK_LEFT:
			if (data.NewEditCaretIndex() > 0)
				--data.NewEditCaretIndex();
			return 0;

		case VK_RIGHT:
			if (data.NewEditCaretIndex() < NewEditTextLength(data))
				++data.NewEditCaretIndex();
			return 0;

		default:
			return forwardOriginal();
		}

		if (textChanged)
			NotifyNewEditTextChanged(hWnd, parentHwnd);

		return 0;
	}

	if (message == WM_IME_CHAR)
		return handleInputCharacter(OwnerDraw::ConvertIMECharToWide(static_cast<UINT>(wParam), lParam), true);

	if (message == WW_EDIT_INPUTCHARW)
		return handleInputCharacter(static_cast<wchar_t>(wParam), true);

	return forwardOriginal();
}

LRESULT CALLBACK WWUI::ComboBoxCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto pData = FindOwnerDrawData(hWnd);
	const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);
	if (!pData)
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);

	auto& data = *pData;

	RECT clientRect {};
	::GetClientRect(hWnd, &clientRect);

	RECT ownerRect {};
	OwnerDraw::GetRectangle(hWnd, &ownerRect);

	auto forwardOriginal = [&]() -> LRESULT
	{
		return CallSelectedHandler(pOriginalWndProc, hWnd, message, wParam, lParam);
	};

	auto handleItemData = [&]() -> LRESULT
	{
		auto pEntry = GetComboBoxItem(pOriginalWndProc, hWnd, static_cast<int>(wParam));
		if (!pEntry)
			return CB_ERR;

		if (message == CB_GETITEMDATA || message == WW_GETITEMDATA)
			return pEntry->ItemData;

		pEntry->ItemData = static_cast<int>(lParam);
		return reinterpret_cast<LRESULT>(pEntry);
	};

	switch (message)
	{
	case WM_DESTROY:
		::SendMessageA(hWnd, CB_SHOWDROPDOWN, 0, 0);
		return forwardOriginal();

	case WM_PAINT:
		PaintComboBox(hWnd, data, clientRect, ownerRect, pOriginalWndProc);
		return 0;

	case WM_ERASEBKGND:
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if (RulesClass::Instance)
			VocClass::PlayGlobal(RulesClass::Instance->GUIComboOpenSound, 0x2000, 1.0f);

		if (LOWORD(lParam) > static_cast<WORD>(clientRect.right - ComboBoxArrowWidth))
		{
			const bool dropped = ::SendMessageA(hWnd, CB_GETDROPPEDSTATE, 0, 0) == 1;
			::PostMessageA(hWnd, CB_SHOWDROPDOWN, dropped ? 0 : 1, 0);
		}
		return 0;

	case WM_SETFOCUS:
	case WM_SETTEXT:
	case WM_GETTEXT:
	case WM_GETTEXTLENGTH:
	case CB_LIMITTEXT:
		return ForwardComboTextMessageToEditList(hWnd, message, wParam, lParam);

	case WM_DELETEITEM:
		if (const auto pDeleteItem = reinterpret_cast<DELETEITEMSTRUCT*>(lParam))
		{
			RemoveComboBoxItem(data, reinterpret_cast<WWUIComboBoxItem*>(pDeleteItem->itemData));
			if (data.ComboBoxCurrentSelection() == static_cast<int>(pDeleteItem->itemID))
				data.ComboBoxCurrentSelection() = -1;
		}
		return forwardOriginal();

	case WM_COMMAND:
		if (HIWORD(wParam) == ComboBoxEditListNotificationCode && !IsComboBoxDropDownList(hWnd))
		{
			if (const HWND parentHwnd = ::GetParent(hWnd))
			{
				const WPARAM command = static_cast<WPARAM>(
					(::GetDlgCtrlID(hWnd) & 0xFFFF)
					| (ComboBoxParentEditChangeNotificationCode << 16));
				::SendMessageA(parentHwnd, WM_COMMAND, command, reinterpret_cast<LPARAM>(hWnd));
			}
			return 0;
		}
		break;

	case CB_GETCURSEL:
		return data.ComboBoxCurrentSelection();

	case CB_GETLBTEXTLEN:
		return GetComboText(pOriginalWndProc, hWnd, message, wParam, lParam, true);

	case CB_SETCURSEL:
		return SetComboSelection(data, pOriginalWndProc, hWnd, wParam);

	case CB_SHOWDROPDOWN:
		if (wParam)
			return OpenComboDropDown(data, hWnd, clientRect, ownerRect);

		CloseComboDropDown(data, hWnd);
		return 1;

	case CB_GETITEMDATA:
	case CB_SETITEMDATA:
	case WW_GETITEMDATA:
	case WW_SETITEMDATA:
		return handleItemData();

	case WW_INITDIALOG:
	{
		const int fontHeight = BitFontHeight(data.ComboBoxFont());
		if (!data.ComboHeightInitialized
			|| ::SendMessageA(hWnd, CB_GETITEMHEIGHT, 0, 0) != fontHeight + 6)
		{
			::SendMessageA(hWnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), fontHeight + 2);
			::SendMessageA(hWnd, CB_SETITEMHEIGHT, 0, fontHeight + 6);
			data.ComboHeightInitialized = 1;
		}

		data.ComboBoxCurrentSelection() = -1;
		std::memset(data.ComboBoxItemColorOverrides(), 0xFF, sizeof(int) * ComboBoxMaxColorItems);
		return 0;
	}

	case WW_SETCOLOR:
		if (wParam <= ComboBoxMaxColorItems)
			data.ComboBoxItemColorOverrides()[wParam] = static_cast<int>(lParam);
		return forwardOriginal();

	case WW_SETTEXTW:
	case WW_GETTEXTW:
	case WW_SETTEXTA:
	case WW_GETTEXTA:
		return ForwardComboTextMessageToEditList(hWnd, message, wParam, lParam);

	case WW_CB_FINDSTRINGA:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, false, false, false);

	case WW_CB_FINDSTRINGEXACTA:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, false, true, false);

	case WW_CB_SELECTSTRINGA:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, false, false, true);

	case WW_CB_FINDSTRINGW:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, true, false, false);

	case WW_CB_FINDSTRINGEXACTW:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, true, true, false);

	case WW_CB_SELECTSTRINGW:
		return FindComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, true, false, true);

	case WW_CB_INSERTSTRINGA:
	case WW_CB_ADDSTRINGA:
		return AddOrInsertComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, false);

	case WW_CB_INSERTSTRINGW:
	case WW_CB_ADDSTRINGW:
		return AddOrInsertComboString(data, pOriginalWndProc, hWnd, message, wParam, lParam, true);

	case WW_CB_GETLBTEXTA:
		return GetComboText(pOriginalWndProc, hWnd, message, wParam, lParam, false);

	case WW_CB_GETLBTEXTW:
		return GetComboText(pOriginalWndProc, hWnd, message, wParam, lParam, true);

	case WW_CB_GETITEMTEXTFORMAT:
		return GetComboText(pOriginalWndProc, hWnd, message, wParam, lParam, true);

	case WW_EDIT_ENTERPRESSED:
	case WW_EDIT_TABNAV:
		if (const HWND parentHwnd = ::GetParent(hWnd))
			::SendMessageA(parentHwnd, message, wParam, lParam);
		return forwardOriginal();

	case WW_CB_ENABLEITEMCOLORS:
		data.ComboBoxUseItemColorOverrides() = lParam == 1;
		return forwardOriginal();

	case WW_CB_SETMAXVISIBLEDROPITEMS:
		data.ComboBoxMaxVisibleDropItems() = static_cast<int>(lParam);
		return forwardOriginal();

	case WW_QUERYTOOLTIPHIT:
		if (const HWND dropHwnd = data.ComboBoxDropDownHwnd())
		{
			RECT comboWindowRect {};
			RECT dropWindowRect {};
			::GetWindowRect(hWnd, &comboWindowRect);
			::GetWindowRect(dropHwnd, &dropWindowRect);

			const int x = LOWORD(lParam) + comboWindowRect.left - dropWindowRect.left;
			const int y = HIWORD(lParam) + comboWindowRect.top - dropWindowRect.top;
			return ::SendMessageA(dropHwnd, WW_QUERYTOOLTIPHIT, 0, MAKELPARAM(x, y));
		}
		return -1;

	case WW_CB_SETALTERNATEPALETTE:
		data.ComboBoxUseAlternatePalette() = lParam == 1;
		return forwardOriginal();

	default:
		break;
	}

	return forwardOriginal();
}

LRESULT CALLBACK WWUI::SliderCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT clientRect {};
	::GetClientRect(hWnd, &clientRect);

	RECT ownerRect {};
	OwnerDraw::GetRectangle(hWnd, &ownerRect);

	auto pData = FindOwnerDrawData(hWnd);
	if (!pData)
		return 0;

	auto& data = *pData;

	int playClickSound = 1;
	int isMouseTracking = data.SliderIsMouseTracking();
	int isThumbDragging = data.SliderIsThumbDragging();
	int rangeSpan = data.SliderRangeSpan();
	int positionOffset = data.SliderPositionOffset();
	int rangeMin = data.SliderRangeMin();
	int thumbOffsetPixels = data.SliderThumbOffsetPixels();
	int stepValue = data.SliderStepValue();
	int showValueLabel = data.SliderShowValueLabel();

	int valueLabelWidth = showValueLabel ? SliderValueLabelWidth : 0;
	const int trackTravel = SliderTrackTravel(clientRect, valueLabelWidth);

	if (!rangeSpan)
	{
		const auto pOriginalWndProc = FindWindowProc(OwnerDraw::DialogProcs, hWnd);
		const int rangeMax = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, WW_SLIDER_GETRANGEMAX, 0, 0));
		rangeMin = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, WW_SLIDER_GETRANGEMIN, 0, 0));
		rangeSpan = rangeMax - rangeMin;
		if (!rangeSpan)
			rangeSpan = 100;

		positionOffset = static_cast<int>(CallSelectedHandler(pOriginalWndProc, hWnd, WW_SLIDER_GETPOS, 0, 0)) - rangeMin;
		thumbOffsetPixels = SliderThumbOffsetFromPosition(positionOffset, trackTravel, rangeSpan);

		data.SliderThumbOffsetPixels() = thumbOffsetPixels;
		data.SliderRangeSpan() = rangeSpan;
		data.SliderPositionOffset() = positionOffset;
		data.SliderRangeMin() = rangeMin;
		data.SliderStepValue() = stepValue;
		data.SliderShowValueLabel() = showValueLabel;
	}

	if (!stepValue)
	{
		valueLabelWidth = SliderValueLabelWidth;
		stepValue = 1;
		showValueLabel = 1;
	}

	int thumbHitLeftX = clientRect.left + thumbOffsetPixels + 1;
	int thumbHitRightX = thumbHitLeftX + SliderGripHitWidth;

	if (isThumbDragging)
	{
		POINT point {};
		::GetCursorPos(&point);
		::ScreenToClient(hWnd, &point);

		SliderUpdateFromGripX(
			SliderClampedGripX(point.x, clientRect, valueLabelWidth),
			rangeSpan,
			rangeMin,
			stepValue,
			trackTravel,
			positionOffset,
			thumbOffsetPixels);

		thumbHitLeftX = thumbOffsetPixels + 1;
		thumbHitRightX = thumbHitLeftX + SliderGripHitWidth;
	}

	auto forwardOriginal = [&]() -> LRESULT
	{
		return CallSelectedHandler(FindWindowProc(OwnerDraw::DialogProcs, hWnd), hWnd, message, wParam, lParam);
	};

	auto writeBack = [&]() -> LRESULT
	{
		const bool valueChanged =
			positionOffset != data.SliderPositionOffset()
			|| rangeSpan != data.SliderRangeSpan()
			|| rangeMin != data.SliderRangeMin();

		data.SliderIsMouseTracking() = isMouseTracking;
		data.SliderIsThumbDragging() = isThumbDragging;
		data.SliderThumbOffsetPixels() = thumbOffsetPixels;
		data.SliderRangeSpan() = rangeSpan;
		data.SliderPositionOffset() = positionOffset;
		data.SliderRangeMin() = rangeMin;
		data.SliderStepValue() = stepValue;
		data.SliderShowValueLabel() = showValueLabel;

		if (valueChanged)
		{
			::InvalidateRect(hWnd, nullptr, FALSE);

			const WPARAM scrollParam = static_cast<WPARAM>(
				((positionOffset + rangeMin) & 0xFFFF) << 16
				| SB_THUMBPOSITION);
			::SendMessageA(::GetParent(hWnd), WM_HSCROLL, scrollParam, reinterpret_cast<LPARAM>(hWnd));

			if (playClickSound == 1 && !data.SliderSuppressClickSound() && RulesClass::Instance)
				VocClass::PlayGlobal(RulesClass::Instance->GenericClick, 0x2000, 1.0f);
		}

		return 0;
	};

	switch (message)
	{
	case WM_ERASEBKGND:
		return 0;

	case WM_NCHITTEST:
	case WM_GETDLGCODE:
		return forwardOriginal();

	case WM_ENABLE:
		::InvalidateRect(hWnd, nullptr, FALSE);
		return writeBack();

	case WM_PAINT:
		PaintSlider(
			hWnd,
			data,
			clientRect,
			ownerRect,
			thumbHitLeftX,
			thumbHitRightX,
			rangeMin,
			positionOffset,
			stepValue,
			valueLabelWidth,
			showValueLabel != 0);
		::ValidateRect(hWnd, nullptr);
		return writeBack();

	case WM_MOUSEMOVE:
		if (isThumbDragging)
			::InvalidateRect(hWnd, &clientRect, FALSE);

		if (wParam & MK_LBUTTON)
			return writeBack();

		[[fallthrough]];

	case WM_LBUTTONUP:
		isMouseTracking = 0;
		isThumbDragging = 0;
		::ReleaseCapture();
		return writeBack();

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if (message == WM_LBUTTONDOWN)
		{
			isMouseTracking = 1;
			::SetCapture(hWnd);
		}
		else
		{
			isMouseTracking = 0;
			::ReleaseCapture();
		}

		if (HIWORD(lParam) > clientRect.bottom - SliderMouseHitBottomInset)
		{
			const int clickX = LOWORD(lParam);
			if (clickX < thumbHitLeftX || clickX >= thumbHitRightX)
			{
				SliderUpdateFromGripX(
					SliderClampedGripX(clickX, clientRect, valueLabelWidth),
					rangeSpan,
					rangeMin,
					stepValue,
					trackTravel,
					positionOffset,
					thumbOffsetPixels);
			}
			else if (message == WM_LBUTTONDOWN)
			{
				isThumbDragging = 1;
			}
		}
		return writeBack();

	case WW_SLIDER_GETPOS:
		return stepValue * ((rangeMin + positionOffset) / stepValue);

	case WW_SLIDER_SETPOS:
	{
		const int requestedOffset = static_cast<int>(lParam) - rangeMin;
		if (requestedOffset <= rangeSpan && requestedOffset >= 0)
			positionOffset = requestedOffset;

		playClickSound = 0;
		thumbOffsetPixels = SliderThumbOffsetFromPosition(positionOffset, trackTravel, rangeSpan);
		return writeBack();
	}

	case WW_SLIDER_SETRANGE:
		rangeMin = LOWORD(lParam);
		rangeSpan = HIWORD(lParam) - LOWORD(lParam);
		if (positionOffset > rangeSpan)
			positionOffset = rangeSpan;

		if (positionOffset < rangeMin)
			positionOffset = rangeMin;

		playClickSound = 0;
		thumbOffsetPixels = SliderThumbOffsetFromPosition(positionOffset, trackTravel, rangeSpan);
		return writeBack();

	case WW_SLIDER_SETSTEP:
		stepValue = static_cast<int>(lParam);
		return writeBack();

	case WW_SLIDER_SHOWVALUE:
		showValueLabel = static_cast<int>(lParam);
		return writeBack();

	case WW_SLIDER_SUPPRESSCLICK:
		data.SliderSuppressClickSound() = wParam == 0;
		return writeBack();

	default:
		return writeBack();
	}
}

LRESULT CALLBACK WWUI::ProgressCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT ownerRect {};
	OwnerDraw::GetRectangle(hWnd, &ownerRect);

	auto pData = FindOwnerDrawData(hWnd);
	if (!pData)
		return 0;

	auto& data = *pData;

	switch (message)
	{
	case WW_PROGRESS_SETRANGE:
		data.ProgressMinValue() = LOWORD(lParam);
		data.ProgressMaxValue() = HIWORD(lParam);
		return 0;

	case WW_PROGRESS_SETPOS:
	{
		int position = static_cast<int>(wParam);
		if (position < data.ProgressMinValue())
			position = data.ProgressMinValue();

		if (position > data.ProgressMaxValue())
			position = data.ProgressMaxValue();

		data.ProgressPosition() = position;
		::InvalidateRect(hWnd, nullptr, FALSE);
		return 0;
	}

	case WM_PAINT:
		PaintProgress(data, ownerRect);
		::ValidateRect(hWnd, nullptr);
		return 0;

	case WW_INITDIALOG:
		data.ProgressMaxValue() = 100;
		return 0;

	default:
		return 0;
	}
}

LRESULT CALLBACK WWUI::ScrollBarCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT clientRect {};
	::GetClientRect(hWnd, &clientRect);

	RECT scrollBarRect {};
	OwnerDraw::GetRectangle(hWnd, &scrollBarRect);

	const int inset = OwnerDraw::ControlInsetPx;
	const int clientWidth = clientRect.right - 2 * inset;
	clientRect.bottom -= 2 * inset;
	clientRect.right = clientWidth;
	scrollBarRect.left += inset;
	scrollBarRect.top += inset;
	scrollBarRect.right -= inset;
	scrollBarRect.bottom -= inset;

	auto pData = FindOwnerDrawData(hWnd);
	if (!pData)
		return 0;

	auto& data = *pData;

	const bool restoreCaptureToNotify = data.ScrollBarRestoreCaptureToNotifyHwnd() != 0;
	bool isMouseTracking = data.ScrollBarIsMouseTracking() != 0;
	bool isThumbDragging = data.ScrollBarIsThumbDragging() != 0;
	int rangeMax = data.ScrollBarRangeMax();
	int position = data.ScrollBarPosition();
	bool upButtonPressed = data.ScrollBarUpButtonPressed() != 0;
	bool downButtonPressed = data.ScrollBarDownButtonPressed() != 0;

	if (!rangeMax)
		rangeMax = 100;

	const int scrollBarWidth = clientRect.right - clientRect.left;
	const int scrollBarLeft = clientRect.right - scrollBarWidth;
	const int trackHeight = clientRect.bottom - clientRect.top - 2 * ScrollBarButtonHeight;

	int thumbHeight = static_cast<int>(
		static_cast<double>(trackHeight)
		- std::log(static_cast<double>(rangeMax + 1)) * static_cast<double>(trackHeight) * 0.2);

	if (thumbHeight <= ScrollBarMinimumThumbHeight)
		thumbHeight = ScrollBarMinimumThumbHeight;

	int thumbTravel = trackHeight - thumbHeight;
	int thumbTravelDivisor = thumbTravel;
	if (thumbTravelDivisor <= 1)
	{
		thumbTravel = 1;
		thumbTravelDivisor = 1;
	}

	int thumbTop = 0;
	int thumbBottom = 0;
	int notifyCode = SB_LINEUP;

	auto updateThumbFromCursor = [&]()
	{
		POINT point {};
		::GetCursorPos(&point);
		::ScreenToClient(hWnd, &point);

		thumbTop = point.y - thumbHeight / 2;
		if (thumbTop < ScrollBarButtonHeight)
			thumbTop = ScrollBarButtonHeight;

		const int maxThumbTop = clientRect.bottom - thumbHeight - ScrollBarButtonHeight;
		if (maxThumbTop < thumbTop)
			thumbTop = maxThumbTop;

		thumbBottom = thumbTop + thumbHeight;
		notifyCode = SB_THUMBTRACK;
		position = rangeMax * (thumbTop - ScrollBarButtonHeight) / thumbTravelDivisor;
	};

	if (message < WM_USER || message == WW_SCROLLBAR_UPDATETHUMB)
	{
		if (isThumbDragging)
		{
			updateThumbFromCursor();
		}
		else
		{
			thumbTop = position * thumbTravel / rangeMax + clientRect.top + ScrollBarButtonHeight;
			thumbBottom = thumbTop + thumbHeight;
		}
	}

	auto restoreCapture = [&]()
	{
		::KillTimer(hWnd, 0);
		::ReleaseCapture();

		if (restoreCaptureToNotify && data.ScrollBarNotifyHwnd())
			::SetCapture(data.ScrollBarNotifyHwnd());
	};

	auto writeBack = [&]() -> LRESULT
	{
		const bool shouldNotify =
			(position != data.ScrollBarPosition() || rangeMax != data.ScrollBarRangeMax())
			&& data.ScrollBarNotifyHwnd();

		data.ScrollBarIsMouseTracking() = isMouseTracking;
		data.ScrollBarIsThumbDragging() = isThumbDragging;
		data.ScrollBarRangeMax() = rangeMax;
		data.ScrollBarPosition() = position;
		data.ScrollBarUpButtonPressed() = upButtonPressed;
		data.ScrollBarDownButtonPressed() = downButtonPressed;

		if (shouldNotify)
		{
			const WPARAM scrollParam = (static_cast<WPARAM>(position & 0xFFFF) << 16)
				| static_cast<WPARAM>(notifyCode & 0xFFFF);

			::SendMessageA(data.ScrollBarNotifyHwnd(), WM_VSCROLL, scrollParam, reinterpret_cast<LPARAM>(hWnd));
			::InvalidateRect(hWnd, nullptr, FALSE);
		}

		return 0;
	};

	switch (message)
	{
	case SBM_GETPOS:
		return position;

	case WM_ERASEBKGND:
		return 0;

	case WM_NCHITTEST:
	case WM_GETDLGCODE:
		return CallSelectedHandler(FindWindowProc(OwnerDraw::DialogProcs, hWnd), hWnd, message, wParam, lParam);

	case WM_PAINT:
		if (data.SkipDraw)
		{
			::ValidateRect(hWnd, nullptr);
			return writeBack();
		}

		if (data.NeedsControlImage)
			return 0;

		PaintScrollBar(
			hWnd,
			data,
			clientRect,
			scrollBarRect,
			thumbTop,
			thumbBottom,
			upButtonPressed,
			downButtonPressed);

		::ValidateRect(hWnd, nullptr);
		return writeBack();

	case SBM_SETPOS:
		if (static_cast<int>(wParam) <= rangeMax && static_cast<int>(wParam) > 0)
			position = static_cast<int>(wParam);

		return writeBack();

	case SBM_SETRANGE:
		rangeMax = static_cast<int>(lParam);
		if (position > rangeMax)
			position = rangeMax;

		return writeBack();

	case SBM_SETSCROLLINFO:
	{
		const auto pScrollInfo = reinterpret_cast<const SCROLLINFO*>(lParam);
		rangeMax = pScrollInfo->nMax;
		position = pScrollInfo->nPos;
		return writeBack();
	}

	case WM_TIMER:
	{
		POINT point {};
		::GetCursorPos(&point);
		::ScreenToClient(hWnd, &point);

		upButtonPressed = false;
		downButtonPressed = false;

		if (isMouseTracking && point.x > scrollBarLeft)
		{
			if (point.y < ScrollBarButtonHeight)
			{
				upButtonPressed = !isThumbDragging;
				if (position)
				{
					notifyCode = SB_LINEUP;
					--position;
					::SetTimer(hWnd, 0, ScrollBarRepeatMs, nullptr);
					return writeBack();
				}
			}
			else if (point.y > clientRect.bottom - ScrollBarButtonHeight)
			{
				downButtonPressed = !isThumbDragging;
				if (position + 1 <= rangeMax)
				{
					notifyCode = SB_LINEDOWN;
					++position;
				}
			}
		}

		::SetTimer(hWnd, 0, ScrollBarRepeatMs, nullptr);
		return writeBack();
	}

	case WM_MOUSEMOVE:
		if (isThumbDragging)
		{
			RECT invalidateRect
			{
				scrollBarLeft,
				clientRect.top,
				clientRect.right,
				clientRect.bottom
			};
			::InvalidateRect(hWnd, &invalidateRect, FALSE);
		}

		if (wParam & MK_LBUTTON)
			return writeBack();

		[[fallthrough]];

	case WM_LBUTTONUP:
		isMouseTracking = false;
		isThumbDragging = false;

		if (upButtonPressed || downButtonPressed)
			::InvalidateRect(hWnd, nullptr, FALSE);

		upButtonPressed = false;
		downButtonPressed = false;
		restoreCapture();
		notifyCode = SB_ENDSCROLL;
		return writeBack();

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if (message == WM_LBUTTONDOWN)
		{
			isMouseTracking = true;
			::SetCapture(hWnd);
			::SetTimer(hWnd, 0, ScrollBarInitialRepeatMs, nullptr);
		}
		else
		{
			isMouseTracking = false;
			isThumbDragging = false;
			restoreCapture();
		}

		{
			const int clickX = LOWORD(lParam);
			const int clickY = HIWORD(lParam);
			const int repeatCount = message == WM_LBUTTONDBLCLK ? 2 : 1;

			upButtonPressed = false;
			downButtonPressed = false;

			for (int i = 0; i < repeatCount; ++i)
			{
				if (clickX <= scrollBarLeft)
					continue;

				if (clickY < ScrollBarButtonHeight && position)
				{
					upButtonPressed = true;
					notifyCode = SB_LINEUP;
					--position;
				}
				else if (clickY <= clientRect.bottom - ScrollBarButtonHeight || position + 1 > rangeMax)
				{
					if (clickY < thumbTop || clickY >= thumbBottom)
					{
						thumbTop = clickY - thumbHeight / 2;
						if (thumbTop < ScrollBarButtonHeight)
							thumbTop = ScrollBarButtonHeight;

						const int maxThumbTop = clientRect.bottom - thumbHeight - ScrollBarButtonHeight;
						if (maxThumbTop < thumbTop)
							thumbTop = maxThumbTop;

						thumbBottom = thumbTop + thumbHeight;
						notifyCode = SB_THUMBTRACK;
						position = rangeMax * (thumbTop - ScrollBarButtonHeight) / thumbTravelDivisor;
					}
					else if (message == WM_LBUTTONDOWN)
					{
						isThumbDragging = true;
					}
				}
				else
				{
					++position;
					downButtonPressed = true;
					notifyCode = SB_LINEDOWN;
				}
			}
		}

		return writeBack();

	default:
		break;
	}

	if (message == WW_DROPDOWN_SETACTIVE)
	{
		data.ScrollBarRestoreCaptureToNotifyHwnd() = lParam != 0;
		return writeBack();
	}

	if (message == WW_CB_SETALTERNATEPALETTE)
	{
		data.ScrollBarDisabled() = lParam == 1;
		return writeBack();
	}

	return writeBack();
}
