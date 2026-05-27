#pragma once

#include <windows.h>

namespace WWUI
{
	LRESULT __fastcall OwnerDrawStandardWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK OwnerDrawWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK ScrollBarCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK ListBoxCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK ComboBoxCtrl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
}
