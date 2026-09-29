//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Windows Unicode interface with the DX12 window-title variant.
//
//=============================================================================//

#include "unicode/unicode.h"

#include <cwchar>
#include <string>

namespace
{
static bool IsDx12CommandLine()
{
	const wchar_t *pCommandLine = ::GetCommandLineW();
	if ( !pCommandLine )
		return false;

	// The launcher passes -dx12 as a standalone switch. Handle quoted executable
	// paths and quoted switches without changing the command line or allocating.
	while ( *pCommandLine )
	{
		while ( *pCommandLine == L' ' || *pCommandLine == L'\t' )
			++pCommandLine;
		if ( !*pCommandLine )
			break;

		bool bQuoted = false;
		if ( *pCommandLine == L'"' )
		{
			bQuoted = true;
			++pCommandLine;
		}

		const wchar_t *pArgument = pCommandLine;
		while ( *pCommandLine && ( bQuoted ? *pCommandLine != L'"' : ( *pCommandLine != L' ' && *pCommandLine != L'\t' ) ) )
			++pCommandLine;

		const bool bSwitchMatch = pCommandLine - pArgument == 5 && wcsncmp( pArgument, L"-dx12", 5 ) == 0;
		const bool bTokenEnded = !bQuoted || ( *pCommandLine == L'"' && ( pCommandLine[1] == L'\0' || pCommandLine[1] == L' ' || pCommandLine[1] == L'\t' ) );
		if ( bSwitchMatch && bTokenEnded )
			return true;

		if ( bQuoted && *pCommandLine == L'"' )
			++pCommandLine;
		while ( *pCommandLine && *pCommandLine != L' ' && *pCommandLine != L'\t' )
			++pCommandLine;
	}

	return false;
}

static bool Dx12WindowTitle( LPCWSTR lpWindowName, std::wstring &title )
{
	if ( !lpWindowName || !IsDx12CommandLine() )
		return false;

	static const wchar_t kDx9Title[] = L"Direct3D 9";
	static const wchar_t kDx12Title[] = L"Direct3D 12";
	const size_t dx9Length = _countof( kDx9Title ) - 1;
	const size_t dx12Length = _countof( kDx12Title ) - 1;

	if ( !wcsstr( lpWindowName, kDx9Title ) )
		return false;

	try
	{
		title.assign( lpWindowName );
		for ( size_t offset = 0; ; )
		{
			offset = title.find( kDx9Title, offset );
			if ( offset == std::wstring::npos )
				break;
			title.replace( offset, dx9Length, kDx12Title, dx12Length );
			offset += dx12Length;
		}
		return true;
	}
	catch ( ... )
	{
		// A title allocation failure must not change the original Unicode wrapper
		// behavior or prevent the engine from creating its window.
		return false;
	}
}

class CUnicodeWindows : public IUnicodeWindows
{
public:
	virtual LRESULT DefWindowProcW(
		HWND hWnd,
		UINT Msg,
		WPARAM wParam,
		LPARAM lParam )
	{
		if ( Msg == WM_SETTEXT )
		{
			std::wstring title;
			LPCWSTR pTitle = reinterpret_cast<LPCWSTR>( lParam );
			if ( Dx12WindowTitle( pTitle, title ) )
				pTitle = title.c_str();
			return ::DefWindowProcW( hWnd, Msg, wParam, reinterpret_cast<LPARAM>( pTitle ) );
		}
		return ::DefWindowProcW( hWnd, Msg, wParam, lParam );
	}

	virtual HWND CreateWindowExW(
		DWORD dwExStyle,
		LPCWSTR lpClassName,
		LPCWSTR lpWindowName,
		DWORD dwStyle,
		int x,
		int y,
		int nWidth,
		int nHeight,
		HWND hWndParent,
		HMENU hMenu,
		HINSTANCE hInstance,
		LPVOID lpParam )
	{
		std::wstring title;
		LPCWSTR pTitle = lpWindowName;
		if ( Dx12WindowTitle( lpWindowName, title ) )
			pTitle = title.c_str();
		return ::CreateWindowExW( dwExStyle, lpClassName, pTitle, dwStyle,
			x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam );
	}


	virtual ATOM RegisterClassW( CONST WNDCLASSW *lpWndClass )
	{
		return ::RegisterClassW( lpWndClass );
	}

	virtual BOOL UnregisterClassW( LPCWSTR lpClassName, HINSTANCE hInstance )
	{
		return ::UnregisterClassW( lpClassName, hInstance );
	}
};
}

EXPOSE_SINGLE_INTERFACE( CUnicodeWindows, IUnicodeWindows, VENGINE_UNICODEINTERFACE_VERSION );
