#include "WWUI.h"

#include <OwnerDraw.h>
#include <SessionClass.h>
#include <StringTable.h>
#include <UI.h>
#include <Unsorted.h>

namespace
{
	WWWinData* FindOwnerDrawData(HWND hWnd)
	{
		if (!OwnerDraw::Dialogs.size())
			return nullptr;

		return OwnerDraw::Dialogs.try_get(hWnd);
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
