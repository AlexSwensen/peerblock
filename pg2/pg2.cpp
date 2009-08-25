/*
	Original code copyright (C) 2004-2005 Cory Nelson
	Modifications copyright (C) 2009 Mark Bulas

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
		claim that you wrote the original software. If you use this software
		in a product, an acknowledgment in the product documentation would be
		appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
		misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
	
	CVS Info :
		$Author: phrostbyte $
		$Date: 2005/07/07 19:23:11 $
		$Revision: 1.27 $
*/

#include "stdafx.h"
#include "resource.h"
#include "updatelists.h"
#include "exceptionfilter.h"
using namespace std;

#if defined(_WIN32_WINNT) && _WIN32_WINNT>=0x0501
static const LPCTSTR g_mutex_name=_T("Global\\PeerGuardian2");
#else
static const LPCTSTR g_mutex_name=_T("PeerGuardian2");
#define SM_SERVERR2             89
#endif

#include "TraceLog.h"
TraceLog g_tlog;

// blocks ips without updating for vista
//void BlockWithoutUpdating(HWND hwnd);


//================================================================================================
//
//  CheckOS()
//
//    - Called by _tWinMain at app start
//
/// <summary>
///   Simply gets/logs the OS Version, doesn't actually do anything permanent with it.
/// </summary>
//
static bool CheckOS() {
	OSVERSIONINFOEX osv = {0};
	osv.dwOSVersionInfoSize = sizeof(osv);
	if(!GetVersionEx((OSVERSIONINFO *)&osv)) return false;

	SYSTEM_INFO si;
	GetSystemInfo(&si);


	//TODO: save version info

	tstring strOsName;
	bool bKnownOs = true;

	if ( osv.dwMajorVersion == 6 && osv.dwMinorVersion == 1 && osv.wProductType == VER_NT_WORKSTATION )
		strOsName = _T("Windows 7");
	else if ( osv.dwMajorVersion == 6 && osv.dwMinorVersion == 1 && osv.wProductType != VER_NT_WORKSTATION )
		strOsName = _T("Windows Server 2008 R2");
	else if ( osv.dwMajorVersion == 6 && osv.dwMinorVersion == 0 && osv.wProductType == VER_NT_WORKSTATION )
		strOsName = _T("Windows Vista");
	else if ( osv.dwMajorVersion == 6 && osv.dwMinorVersion == 0 && osv.wProductType != VER_NT_WORKSTATION )
		strOsName = _T("Windows Server 2008");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 2 && GetSystemMetrics(SM_SERVERR2) != 0 )
		strOsName = _T("Windows Server 2003 R2");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 2 && osv.wSuiteMask == VER_SUITE_WH_SERVER )
		strOsName = _T("Windows Home Server");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 2 && osv.wProductType == VER_NT_WORKSTATION
				&& si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64 )
		strOsName = _T("Windows XP Professional 64-bit Edition");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 2 && GetSystemMetrics(SM_SERVERR2) == 0 )
		strOsName = _T("Windows Server 2003");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 1 )
		strOsName = _T("Windows XP");
	else if ( osv.dwMajorVersion == 5 && osv.dwMinorVersion == 0 )
		strOsName = _T("Windows 2000");
	else
		strOsName = boost::str(tformat(_T("UNKNOWN OS %1%.%2%")) % osv.dwMajorVersion % osv.dwMinorVersion );

	// TODO: Check for Media Center / Starter / Tablet PC (as per OSVERSIONINFOEX docs)

	tstring strOsBitness;

	if ( si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64 )
		strOsBitness = _T("64-bit");
	else if (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_INTEL )
		strOsBitness = _T("32-bit");
	else
		strOsBitness = _T("??-bit");


	tstring strOsString = boost::str(tformat(_T("%1% %5% - Build:[%2%], SP:[%3%.%4%]")) 
		% strOsName % osv.dwBuildNumber % osv.wServicePackMajor % osv.wServicePackMinor % strOsBitness.c_str());

	TCHAR chBuf[256];
	_stprintf_s(chBuf, sizeof(chBuf)/2, _T("Running on OS: %s"), strOsString.c_str());
	g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_SUCCESS);

	return true;

} // End of CheckOS()



//================================================================================================
//
//  _tWinMain()
//
//    - Called by Windows when the app is first started
//
/// <summary>
///   Initial starting point of the app.  Performs some quick sanity-checking and then starts up
///	  a different thread (serviced by the Main_DlgProc() routine in mainproc.cpp) to handle all 
///	  the "real" work.
/// </summary>
//
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int nCmdShow) 
{
	path pathLog = path::base_dir()/L"peerblock.log";	// TODO: This should be a config-string!
	g_tlog.SetLogfile(pathLog.c_str());

	TRACEC("PeerBlock Starting");

	TCHAR buf[64];
	swprintf_s(buf, sizeof(buf)/2, L"%S", PB_BLDSTR);
	TRACEBUFC(buf);

	g_tlog.ProcessMessages();
	TRACES("Flushed tracelog");

	if(!CheckOS()) {
		TRACEE("ERROR:  Failed checking for OS rev!");
		return -1;
	}

#ifdef _WIN32_WINNT
	// PeerBlock requires Admin Mode in order to load the pbfilter.sys driver
	if(!IsUserAnAdmin()) {
		TRACEE("ERROR:  User not running as Admin!");
		MessageBox(NULL, IDS_NEEDADMINTEXT, IDS_NEEDADMIN, MB_ICONERROR|MB_OK);
		return -1;
	}
	TRACES("User running as Admin");
#endif

	// If PeerGuardian2 is already running, bring it to the forefront and exit this new instance.
	HANDLE mutex=OpenMutex(MUTEX_ALL_ACCESS, FALSE, g_mutex_name);
	if(mutex) {
		TRACEW("PG/PeerBlock already running");
		UINT msg=RegisterWindowMessage(_T("PeerGuardian2SetVisible"));
		if(msg) SendMessage(HWND_BROADCAST, msg, 0, TRUE);
		return 0;
	}
	else mutex=CreateMutex(NULL, FALSE, g_mutex_name);
	TRACES("Created program mutex");

	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	{
		INITCOMMONCONTROLSEX icx={0};
		icx.dwSize=sizeof(icx);
		icx.dwICC=ICC_DATE_CLASSES|ICC_INTERNET_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS|ICC_TAB_CLASSES|ICC_UPDOWN_CLASS|ICC_USEREX_CLASSES|ICC_WIN95_CLASSES;

		InitCommonControlsEx(&icx);
	}
	TRACES("Initialized common controls");

	RegisterColorPicker(hInstance);
	TRACES("Registered color picker");

	{
		WSADATA data;
		WSAStartup(WINSOCK_VERSION, &data);
	}
	TRACES("Initialized winsock");

	SetUnhandledExceptionFilter( PeerblockExceptionFilter );
	BOOL bRet = PreventSetUnhandledExceptionFilter();
	if (bRet)
	{
		TRACES("Successfully PreventSetUnhandledExceptionFilter()");
	}
	else
	{
		TRACEW("Could NOT PreventSetUnhandledExceptionFilter()");
	}

	try {
		// Spawn a new thread to handle the UI Dialog; this thread becomes the main workhorse of the program
		HWND hwnd=CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MAIN), NULL, Main_DlgProc);
		TRACES("Created main UI window");

		// Set main window caption to version-string from versioninfo.h
		TCHAR * chBuf;
		chBuf = (TCHAR *)malloc(256 * sizeof(chBuf));
		swprintf_s(chBuf, 256, L"%S", PB_BLDSTR);
		SetWindowText(hwnd, chBuf);
		free(chBuf);

		TRACES("Starting message-loop");
		MSG msg;
		while(GetMessage(&msg, NULL, 0, 0)>0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		TRACES("Message loop ended, shutting down PeerBlock");
	}
	catch(exception &ex) {
		UncaughtExceptionBox(NULL, ex, __FILE__, __LINE__);
	}
	catch(...) {
		UncaughtExceptionBox(NULL, __FILE__, __LINE__);
	}

	if(g_filter) 
	{
		TRACES("Resetting filter driver on exit");
		g_filter.reset();
	}

	TRACES("Shutting down winsock");
	WSACleanup();

	TRACES("PeerBlock has finished shutting down.  Have a nice day!");
	return 0;

} // End of _tWinMain()



//================================================================================================
//
//  BlockWithoutUpdating()
//
//    - Called by ???
//
/// <summary>
///   Commented-out routine, looks like it might have once been a starting point at a workaround
///	  for some of the PG2 load problems on Vista?
/// </summary>
//
//void BlockWithoutUpdating(HWND hwnd)
//{
	//pgfilter *f = g_filter.get();
	//f->m_filter.load(L"pgfilter");
	//f->setblock(g_config.Block);
	//f->setblockhttp(g_config.BlockHttp);

	//UINT msg=RegisterWindowMessage(_T("PeerGuardian2LoadLists"));
	//SendMessage(HWND_BROADCAST, WM_PG2_LOADLISTS, 0, 0);

//} // End of BlockWithoutUpdating()