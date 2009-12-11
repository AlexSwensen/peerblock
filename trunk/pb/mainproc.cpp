/*
	Copyright (C) 2004-2005 Cory Nelson
	PeerBlock modifications copyright (C) 2009 PeerBlock, LLC

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

*/

#include "stdafx.h"
#include "resource.h"
#include "tracelog.h"
using namespace std;
using namespace sqlite3x;

static UINT WM_TRAY_CREATED;
static UINT WM_PB_VISIBLE;
static UINT WM_PB_LOADLISTS;
static const UINT WM_PB_TRAY=WM_APP+2;
static const UINT TRAY_ID=1;
static const UINT TIMER_BLINKTRAY=1;
static const UINT TIMER_PROCESSDB=2;

bool g_trayactive;
NOTIFYICONDATA g_nid={0};
static bool g_trayblink=false;
DWORD g_blinkstart=0;

extern TraceLog g_tlog;

HWND g_main;
boost::shared_ptr<pbfilter> g_filter;

TabData g_tabs[]={
	{ IDS_LOG, MAKEINTRESOURCE(IDD_LOG), Log_DlgProc },
	{ IDS_SETTINGS, MAKEINTRESOURCE(IDD_SETTINGS), Settings_DlgProc }
};
static const size_t g_tabcount=sizeof(g_tabs)/sizeof(TabData);



void SetBlock(bool block) 
{
	TRACEV("[mainproc] [SetBlock]  > Entering routine.");

	tstring strBuf = boost::str(tformat(_T("[mainproc] [SetBlock]   setting PeerBlock blocking from [%1%] to [%2%]")) 
		% g_config.Block % block);
	TRACEBUFI(strBuf);
	g_config.Block=block;
	g_filter->setblock(block);
	
	g_nid.hIcon=(HICON)LoadImage(GetModuleHandle(NULL), block?g_config.BlockHttp?MAKEINTRESOURCE(IDI_MAIN):MAKEINTRESOURCE(IDI_HTTPDISABLED):MAKEINTRESOURCE(IDI_DISABLED), IMAGE_ICON, 0, 0, LR_SHARED|LR_DEFAULTSIZE);
	if(g_trayactive) Shell_NotifyIcon(NIM_MODIFY, &g_nid);

	SendMessage(g_main, WM_SETICON, ICON_BIG, (LPARAM)g_nid.hIcon);
	SendMessage(g_main, WM_SETICON, ICON_SMALL, (LPARAM)g_nid.hIcon);

	SendMessage(g_tabs[0].Tab, WM_LOG_HOOK, 0, 0);
	TRACEV("[mainproc] [SetBlock]  < Leaving routine.");
}



void SetBlockHttp(bool block) 
{
	TRACEV("[mainproc] [SetBlockHttp]  > Entering routine.");

	tstring strBuf = boost::str(tformat(_T("[mainproc] [SetBlockHttp]   setting PeerBlock HTTP blocking from [%1%] to [%2%]")) 
		% g_config.BlockHttp % block);
	TRACEBUFI(strBuf);
	g_config.BlockHttp=block;
	g_filter->setblockhttp(block);

	if (g_config.Block)
	{
		g_nid.hIcon=(HICON)LoadImage(GetModuleHandle(NULL), block?MAKEINTRESOURCE(IDI_MAIN):MAKEINTRESOURCE(IDI_HTTPDISABLED), IMAGE_ICON, 0, 0, LR_SHARED|LR_DEFAULTSIZE);
		if(g_trayactive) Shell_NotifyIcon(NIM_MODIFY, &g_nid);

		SendMessage(g_main, WM_SETICON, ICON_BIG, (LPARAM)g_nid.hIcon);
		SendMessage(g_main, WM_SETICON, ICON_SMALL, (LPARAM)g_nid.hIcon);
	}

	SendMessage(g_tabs[0].Tab, WM_LOG_HOOK, 0, 0);
	TRACEV("[mainproc] [SetBlock]  < Leaving routine.");
}



//================================================================================================
//
//  Shutdown()
//
//    - Called by ???
//
/// <summary>
///   Performs final cleanup operations prior to letting the program terminate.  Saves config,
///   resets driver(s), closes WinSock, etc.
/// </summary>
//
void Shutdown()
{
	TRACES("[mainproc] [Shutdown]    Shutting down PeerBlock.");

	if(g_filter) 
	{
		TRACES("[mainproc] [Shutdown]    Resetting filter driver on exit");
		g_filter.reset();
	}

	TRACES("[mainproc] [Shutdown]    Shutting down winsock");
	WSACleanup();

	TRACES("[mainproc] [Shutdown]    Finished shutting down PeerBlock.");

} // End of Shutdown()



//================================================================================================
//
//  PerformPrevRelUpdates()
//
//    - Called by Main_OnInitDialog()
//
/// <summary>
///   Checks the version of the last time we ran, and if that number falls within certain ranges
///	  we'll do some release-specific cleanup.
/// </summary>
//
void PerformPrevRelUpdates()
{
	int prevRelease = g_config.LastVersionRun;

	if (prevRelease == PB_VER_BUILDNUM)
		return;

	if (prevRelease > PB_VER_BUILDNUM)
	{
		TRACEW("[mainproc] [PerformPrevRelUpdates]    WARNING:  Downgrade detected!");
		return;
	}


	//--------------------------------------------------
	// Update PG hosted lists to iblocklist

	if (prevRelease < 134)
	{
		TRACEW("[mainproc] [PerformPrevRelUpdates]    Checking for old peerguardian-hosted lists, and updating any found to iblocklist.com-hosted ones");

		bool bOldUrlFound = false;
		vector<DynamicList> tempList;

		// check each list in configured lists
		for(vector<DynamicList>::size_type i = 0; i < g_config.DynamicLists.size(); ++i)
		{
			// if it's a peerguardian list
			DynamicList *list = &(g_config.DynamicLists[i]);	
			if (list->Url.find(_T("http://peerguardian.sourceforge.net/lists/")) != string::npos)
			{
				// swap it out
				tstring strBuf = boost::str(tformat(_T("[mainproc] [PerformPrevRelUpdates]    found old URL: [%1%]")) % list->Url );
				TRACEBUFW(strBuf);
				bOldUrlFound = true;

				if (list->Url.find(_T("ads.php")) != string::npos)
				{
					// http://list.iblocklist.com/?list=bt_ads
					TRACEW("[mainproc] [PerformPrevRelUpdates]    - replacing ads.php list with bt_ads");
					//list->Url = _T("http://list.iblocklist.com/?list=bt_ads");
					DynamicList newList = *list;
					newList.Url = _T("http://list.iblocklist.com/lists/bluetack/ads-trackers-and-bad-pr0n");
					tempList.push_back(newList);
				}
				else if (list->Url.find(_T("edu.php")) != string::npos)
				{
					// http://list.iblocklist.com/?list=bt_edu
					TRACEW("[mainproc] [PerformPrevRelUpdates]    - replacing edu.php list with bt_edu");
					//list->Url = _T("http://list.iblocklist.com/?list=bt_edu");
					DynamicList newList = *list;
					newList.Url = _T("http://list.iblocklist.com/lists/bluetack/edu");
					tempList.push_back(newList);
				}
				else if (list->Url.find(_T("p2p.php")) != string::npos)
				{
					// http://list.iblocklist.com/?list=bt_level1
					TRACEW("[mainproc] [PerformPrevRelUpdates]    - replacing p2p.php list with bt_level1");
					//list->Url = _T("http://list.iblocklist.com/?list=bt_level1");
					DynamicList newList = *list;
					newList.Url = _T("http://list.iblocklist.com/lists/bluetack/level-1");
					tempList.push_back(newList);
				}
				else if (list->Url.find(_T("spy.php")) != string::npos)
				{
					// http://list.iblocklist.com/?list=bt_spyware
					TRACEW("[mainproc] [PerformPrevRelUpdates]    - replacing spy.php list with bt_spyware");
					//list->Url = _T("http://list.iblocklist.com/?list=bt_spyware");
					DynamicList newList = *list;
					newList.Url = _T("http://list.iblocklist.com/lists/bluetack/spyware");
					tempList.push_back(newList);
				}
				else if (list->Url.find(_T("gov.php")) != string::npos)
				{
					// remove list
					TRACEW("[mainproc] [PerformPrevRelUpdates]    - removing gov list");
				}
				else
				{
					TRACEE("[mainproc] [PerformPrevRelUpdates]    ERROR:  Unknown PG2 list!!");
				}
			}
			else
			{
				TRACED("[mainproc] [PerformPrevRelUpdates]    found non-PG2 URL");
				DynamicList newList = *list;
				tempList.push_back(newList);
			}
		}

		// Rebuild list if we need to remove Gov list.  Also, check for duplicates.
		g_config.DynamicLists.clear();
		for(vector<DynamicList>::size_type i = 0; i < tempList.size(); ++i)
		{
			if (std::find(g_config.DynamicLists.begin(), g_config.DynamicLists.end(), tempList[i]) == g_config.DynamicLists.end())
			{
				g_config.DynamicLists.push_back(tempList[i]);
			}
		}
	}

}; // End of PerformPrevRelUpdates()



static void Main_OnVisible(HWND hwnd, BOOL visible);
static void Main_OnClose(HWND hwnd) {
	if(g_config.HideOnClose) Main_OnVisible(hwnd, FALSE);
	else DestroyWindow(hwnd);
}

static void Main_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) 
{
	switch(id) 
	{
		case ID_TRAY_PEERBLOCK:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'PeerBlock' item");
			Main_OnVisible(hwnd, g_config.WindowHidden?TRUE:FALSE);
			break;

		case ID_TRAY_ENABLED:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Enabled' item");
			if(!g_config.Block) SetBlock(true);
			break;

		case ID_TRAY_DISABLED:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Disabled' item");
			if(g_config.Block) SetBlock(false);
			break;

		case ID_TRAY_BLOCKHTTP:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Block HTTP' item");
			SetBlockHttp(!g_config.BlockHttp);
			break;

		case ID_TRAY_ALWAYSONTOP:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Always on Top' item");
			g_config.AlwaysOnTop=!g_config.AlwaysOnTop;
			SetWindowPos(hwnd, g_config.AlwaysOnTop?HWND_TOPMOST:HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);

			// kinda hackish
			CheckDlgButton(g_pages[1].hwnd, IDC_ONTOP, g_config.AlwaysOnTop?BST_CHECKED:BST_UNCHECKED);
			break;

		case ID_TRAY_HIDETRAYICON:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Hide tray menu' item");
			if(g_trayactive) {
				g_trayactive=false;
				Shell_NotifyIcon(NIM_DELETE, &g_nid);
				
				if(g_config.FirstHide) {
					g_config.FirstHide=false;
					MessageBox(hwnd, IDS_HIDINGTEXT, IDS_HIDING, MB_ICONINFORMATION|MB_OK);
				}
			}
			break;

		case ID_TRAY_HELP:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Help' item");
			ShellExecute(NULL, NULL, _T("http://www.peerblock.com/quick-guide"), NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_TRAY_SUPPORT:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Support' item");
			ShellExecute(NULL, NULL, _T("http://forums.peerblock.com"), NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_TRAY_ABOUT:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'About' item");
			if(!g_about) g_about=CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUT), hwnd, About_DlgProc);
			else SetForegroundWindow(g_about);
			break;

		case ID_TRAY_EXIT:
			TRACEI("[mainproc] [Main_OnCommand]    user clicked tray-icon right-click menu 'Exit' item");
			DestroyWindow(hwnd);
			break;
	}
}

static void Main_OnDestroy(HWND hwnd) 
{
	TRACEI("[mainproc] [Main_OnDestroy]    destroying main window");
	{
		TRACEI("[mainproc] [Main_OnDestroy]    writing config out to disk");
		try {
			g_config.Save();
		}
		catch(exception &ex) {
			ExceptionBox(hwnd, ex, __FILE__, __LINE__);
		}
		TRACEI("[mainproc] [Main_OnDestroy]    finished saving config to peerblock.conf");
	}

	if(g_trayactive) Shell_NotifyIcon(NIM_DELETE, &g_nid);

	{
		TRACEI("[mainproc] [Main_OnDestroy]    deleting tray icons");
		try {
			for(size_t i=0; i<g_tabcount; i++)
				DestroyWindow(g_tabs[i].Tab);
		}
		catch(exception &ex) {
			ExceptionBox(hwnd, ex, __FILE__, __LINE__);
		}
		TRACEI("[mainproc] [Main_OnDestroy]    tray icons destroyed");
	}

	TRACEW("[mainproc] [Main_OnDestroy]    Terminating program...");
	PostQuitMessage(0);
	TRACEW("[mainproc] [Main_OnDestroy]    post quit message");
}

static void Main_OnEndSession(HWND hwnd, BOOL fEnding) 
{
	if(fEnding) 
	{
		TRACEW("[mainproc] [Main_OnEndSession]    System requires immediate termination, due to shutdown/logoff.");
		Main_OnDestroy(hwnd);
		Shutdown();
		TRACES("PeerBlock is now exiting, due to Windows shutdown/logoff.  Have a nice day!");
	}
	else
	{
		TRACEI("[mainproc] [Main_OnEndSession]    not shutting down");
	}
}

static void Main_OnGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo) {
	RECT rc={0};
	rc.right=363;
	rc.bottom=235;

	MapDialogRect(hwnd, &rc);

	lpMinMaxInfo->ptMinTrackSize.x=rc.right;
	lpMinMaxInfo->ptMinTrackSize.y=rc.bottom;
}

static void FitTabChild(HWND tabs, HWND child) {
	RECT rc;
	GetClientRect(tabs, &rc);
	TabCtrl_AdjustRect(tabs, FALSE, &rc);

	MoveWindow(child, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, TRUE);
}

static void Main_OnSize(HWND hwnd, UINT state, int cx, int cy);
static BOOL Main_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	g_main=hwnd;

	TRACEI("[mainproc] [Main_OnInitDialog]  > Entering routine.");

	TRACEI("[mainproc] [Main_OnInitDialog]    loading config");
	bool firsttime=false;
	try {
		if(!g_config.Load()) {
			TRACEI("[mainproc] [Main_OnInitDialog]    displaying first-time wizard");
			DisplayStartupWizard(hwnd);
			g_config.Save();
			firsttime=true;
		}
		else
		{
			TRACES("[mainproc] [Main_OnInitDialog]    Config loaded.");
			TRACEI("[mainproc] [Main_OnInitDialog]    checking if previous-release updates are required");
			PerformPrevRelUpdates();
		}

		// Setup tracelogging
		if (g_config.TracelogEnabled)
			g_tlog.SetLoglevel((TRACELOG_LEVEL)g_config.TracelogLevel);
		else
		{
			TRACEC("Logging disabled, shutting down logger.");
			g_tlog.SetLoglevel(TRACELOG_LEVEL_NONE);
		}
	}
	catch(exception &ex) {
		TRACEC("[mainproc] [Main_OnInitDialog]    Exception trying to load config!");
		ExceptionBox(hwnd, ex, __FILE__, __LINE__);
		DestroyWindow(hwnd);
		PostQuitMessage(0);
		return FALSE;
	}

	TRACEI("[mainproc] [Main_OnInitDialog]    resetting g_filter");
	try {
		g_filter.reset(new pbfilter());
		TRACES("[mainproc] [Main_OnInitDialog]    g_filter reset.");
	}
	catch(peerblock_error &ex) 
	{
		PeerBlockExceptionBox(NULL, ex);
		DestroyWindow(hwnd);
		PostQuitMessage(0);
		return FALSE;
	}
	catch(win32_error &ex) {
		DWORD code = ex.error();
		if (code == 577)
		{
			const tstring text=boost::str(tformat(LoadString(IDS_UNSIGNEDDRIVERTEXT)) % ex.func() % ex.error() % ex.what());
			MessageBox(hwnd, text, IDS_DRIVERERR, MB_ICONERROR|MB_OK);
		}
		else
		{
			const tstring text=boost::str(tformat(LoadString(IDS_DRIVERERRWIN32TEXT)) % ex.func() % ex.error() % ex.what());
			MessageBox(hwnd, text, IDS_DRIVERERR, MB_ICONERROR|MB_OK);
		}

		DestroyWindow(hwnd);
		PostQuitMessage(0);
		return FALSE;
	}
	catch(exception &ex) {
		const tstring text=boost::str(tformat(LoadString(IDS_DRIVERERRTEXT))%typeid(ex).name()%ex.what());
		MessageBox(hwnd, text, IDS_DRIVERERR, MB_ICONERROR|MB_OK);

		DestroyWindow(hwnd);
		PostQuitMessage(0);
		return FALSE;
	}

	TRACEI("[mainproc] [Main_OnInitDialog]    getting tabs");
	HWND tabs=GetDlgItem(hwnd, IDC_TABS);

	for(size_t i=0; i<g_tabcount; i++) {
		tstring buf=LoadString(g_tabs[i].Title);

		TCITEM tci={0};
		tci.mask=TCIF_TEXT;
		tci.pszText=const_cast<LPTSTR>(buf.c_str());
		TabCtrl_InsertItem(tabs, i, &tci);

		g_tabs[i].Tab=CreateDialogParam(GetModuleHandle(NULL), g_tabs[i].Template, tabs, g_tabs[i].Proc, 0);
		FitTabChild(tabs, g_tabs[i].Tab);
	}

	if(firsttime && !g_config.UpdateAtStartup) {
		TRACEI("[mainproc] [Main_OnInitDialog]    updating lists for firsttime");
		UpdateLists(g_tabs[0].Tab);
		SendMessage(g_tabs[0].Tab, WM_TIMER, TIMER_UPDATE, 0);
	}

	TRACEI("[mainproc] [Main_OnInitDialog]    setting block");
	g_filter->setblock(g_config.Block);

	TRACEI("[mainproc] [Main_OnInitDialog]    setting HTTP block");
	g_filter->setblockhttp(g_config.BlockHttp);

	if (!firsttime)
	{
		TRACEI("[mainproc] [Main_OnInitDialog]    loading lists");
		LoadLists(hwnd);
		TRACES("[mainproc] [Main_OnInitDialog]    Lists loaded.");
	}

	SendMessage(g_tabs[0].Tab, WM_LOG_HOOK, 0, g_config.Block?TRUE:FALSE);

	TRACEI("[mainproc] [Main_OnInitDialog]    doing other stuff");
	g_nid.cbSize=sizeof(NOTIFYICONDATA);
	g_nid.hWnd=hwnd;
	g_nid.uID=TRAY_ID;
	g_nid.uCallbackMessage=WM_PB_TRAY;
	g_nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;	
	g_nid.hIcon=(HICON)LoadImage(GetModuleHandle(NULL), g_config.Block?g_config.BlockHttp?MAKEINTRESOURCE(IDI_MAIN):MAKEINTRESOURCE(IDI_HTTPDISABLED):MAKEINTRESOURCE(IDI_DISABLED), IMAGE_ICON, 0, 0, LR_SHARED|LR_DEFAULTSIZE);
	StringCbCopy(g_nid.szTip, sizeof(g_nid.szTip), _T("PeerBlock"));

	if((g_trayactive=(!g_config.HideTrayIcon && !g_config.StayHidden))) 
	{
		Shell_NotifyIcon(NIM_ADD, &g_nid);
	}

	WM_TRAY_CREATED=RegisterWindowMessage(_T("TaskbarCreated"));

	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_nid.hIcon);
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_nid.hIcon);

	if(!g_config.StartMinimized && !g_config.HideTrayIcon && !g_config.WindowHidden) {
		if(g_config.ShowSplash) DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SPLASH), hwnd, Splash_DlgProc);
		SendMessage(hwnd, WM_MAIN_VISIBLE, 0, TRUE);
	}
	else g_config.WindowHidden=true;

	UINT swpflags=0;
	if(
		g_config.WindowPos.left==0 && g_config.WindowPos.top==0 &&
		g_config.WindowPos.right==0 && g_config.WindowPos.bottom==0
	) swpflags=SWP_NOMOVE|SWP_NOSIZE;
	if(!g_config.AlwaysOnTop) swpflags|=SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER;

	SetWindowPos(hwnd, g_config.AlwaysOnTop?HWND_TOPMOST:NULL,
		g_config.WindowPos.left, g_config.WindowPos.top,
		g_config.WindowPos.right-g_config.WindowPos.left,
		g_config.WindowPos.bottom-g_config.WindowPos.top,
		swpflags);

	if((swpflags&SWP_NOSIZE) == 0) SendMessage(hwnd, DM_REPOSITION, 0, 0);

	RECT rc;
	GetClientRect(hwnd, &rc);
	Main_OnSize(hwnd, 0, rc.right, rc.bottom);

	WM_PB_VISIBLE=RegisterWindowMessage(_T("PeerBlockSetVisible"));
	WM_PB_LOADLISTS=RegisterWindowMessage(_T("PeerBlockLoadLists"));

	SetTimer(hwnd, TIMER_BLINKTRAY, 500, NULL);
	SetTimer(hwnd, TIMER_PROCESSDB, 600000, NULL);

	#ifdef _WIN32_WINNT
	if(g_config.WindowHidden)
		SetProcessWorkingSetSize(GetCurrentProcess(), (size_t)-1, (size_t)-1);
	#endif

	if (g_config.UpdateAtStartup)
	{
		UpdateLists(hwnd);	// needs internet connection
		LoadLists(hwnd);
	}
	else if(g_filter)	// HACK: This should allow us to block at startup, even if we don't update
	{
		p2p::list allow;
		allow.insert(p2p::range(L"Auto-allow hack", 0, 0));
		allow.optimize(true);
		g_filter->setranges(allow, false);
	}

	return FALSE;
}

static void Main_OnMove(HWND hwnd, int x, int y) {
	GetWindowRect(hwnd, &g_config.WindowPos);
}

static LRESULT Main_OnNotify(HWND hwnd, INT idCtrl, LPNMHDR pnmh) {
	if(pnmh->code==TCN_SELCHANGE && pnmh->idFrom==IDC_TABS) {
		int sel=TabCtrl_GetCurSel(pnmh->hwndFrom);

		if(sel!=-1) {
			SetWindowPos(g_tabs[sel].Tab, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
			for(size_t i=0; i<g_tabcount; i++)
				if(i!=sel) ShowWindow(g_tabs[i].Tab, SW_HIDE);
		}
	}

	return 0;
}

static void Main_OnSize(HWND hwnd, UINT state, int cx, int cy) {
	HWND tabs=GetDlgItem(hwnd, IDC_TABS);

	MoveWindow(tabs, 7, 7, cx-14, cy-14, TRUE);

	HDWP dwp=BeginDeferWindowPos(g_tabcount);

	RECT rc;
	GetClientRect(tabs, &rc);
	TabCtrl_AdjustRect(tabs, FALSE, &rc);

	for(size_t i=0; i<g_tabcount; i++)
		DeferWindowPos(dwp, g_tabs[i].Tab, NULL, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);

	EndDeferWindowPos(dwp);
	
	if(state==SIZE_RESTORED) {
		SaveWindowPosition(hwnd, g_config.WindowPos);
	}
}

static string format_ipport(unsigned int ip, unsigned short port) {
	const unsigned char *bytes=reinterpret_cast<const unsigned char*>(&ip);

	char buf[24];
	StringCbPrintfA(buf, sizeof(buf), port?"%u.%u.%u.%u:%u":"%u.%u.%u.%u", bytes[3], bytes[2], bytes[1], bytes[0], port);

	return buf;
}



static boost::shared_ptr<thread> g_dbthread;
static spinlock g_processlock;



static void Main_ProcessDb() 
{
	TRACEI("[mainproc] [Main_ProcessDb]  > Entering routine.");
	sqlite3_try_lock lock(g_con, true);

	if(lock.is_locked()) 
	{
		// Handle archival function first; deletion will be processed later
		if(g_config.CleanupType==ArchiveDelete) 
		{
			TRACEI("[mainproc] [Main_ProcessDb]    performing 'archive & delete' type cleanup");
			try 
			{
				// First, archive history.db entries
				TRACEI("[mainproc] [Main_ProcessDb]    archiving history.db");
				queue<string> dates;

				{
					ostringstream ss;
					ss << "select distinct date(time, 'localtime') from t_history where time <= julianday('now', '-" << g_config.CleanupInterval << " days');";

					sqlite3_command cmd(g_con, ss.str());
					sqlite3_reader reader=cmd.executereader();
					while(reader.read())
						dates.push(reader.getstring(0));
				}

				for(; !dates.empty(); dates.pop()) 
				{
					path p=g_config.ArchivePath;

					if(!p.has_root()) p=path::base_dir()/p;
					if(!path::exists(p)) path::create_directory(p);

					p/=(UTF8_TSTRING(dates.front())+_T(".log"));

					FILE *fp=_tfopen(p.file_str().c_str(), _T("a"));

					if(fp) {
						boost::shared_ptr<FILE> safefp(fp, fclose);

						sqlite3_command cmd(g_con, "select time(time, 'localtime'),name,source,sourceport,destination,destport,protocol,action from t_history,t_names where time>=julianday('"+dates.front()+"', 'utc') and time<julianday('"+dates.front()+"', '+1 days', 'utc') and t_names.id=nameid;");

						sqlite3_reader reader=cmd.executereader();
						while(reader.read()) {
							const string time=reader.getstring(0);
							string name=reader.getstring(1);

							const char *protocol;
							switch(reader.getint(6)) {
								case IPPROTO_ICMP: protocol="ICMP"; break;
								case IPPROTO_IGMP: protocol="IGMP"; break;
								case IPPROTO_GGP: protocol="Gateway^2"; break;
								case IPPROTO_TCP: protocol="TCP"; break;
								case IPPROTO_PUP: protocol="PUP"; break;
								case IPPROTO_UDP: protocol="UDP"; break;
								case IPPROTO_IDP: protocol="XNS IDP"; break;
								case IPPROTO_ND: protocol="NetDisk"; break;
								default: protocol="Unknown"; break;
							}

							fprintf(fp, "%s; %s; %s; %s; %s; %s\n",
								time.c_str(), name.c_str(),
								format_ipport((unsigned int)reader.getint(2), (unsigned short)reader.getint(3)).c_str(),
								format_ipport((unsigned int)reader.getint(4), (unsigned short)reader.getint(5)).c_str(),
								protocol, reader.getint(7)?"Blocked":"Allowed");
						}
					}
				} // End for(!dates.empty())

				// Now archive peerblock.log
				TRACEI("[mainproc] [Main_ProcessDb]    archiving peerblock.log");
				path p=g_config.ArchivePath;
				if(!p.has_root()) p=path::base_dir()/p;
				if(!path::exists(p)) path::create_directory(p);

				SYSTEMTIME st;
				GetLocalTime(&st);
				TCHAR chToFilename[270];
				swprintf_s(chToFilename, sizeof(chToFilename)/2, L"peerblock.%4d-%02d-%02d.log", st.wYear, st.wMonth, st.wDay);

				path pathLogFrom = path::base_dir()/L"peerblock.log";
				path pathLogTo = p/chToFilename;
				path::copy(pathLogFrom, pathLogTo, true);
			}
			catch(exception &ex) {
				ExceptionBox(NULL, ex, __FILE__, __LINE__);
			}
			TRACEI("[mainproc] [Main_ProcessDb]    done with 'archive & delete' type cleanup");
		}

		if(g_config.CleanupType==Delete || g_config.CleanupType==ArchiveDelete) 
		{
			TRACEI("[mainproc] [Main_ProcessDb]    performing 'delete' type cleanup");
			try 
			{
				ostringstream ss;

				// first delete all data from the t_history table
				ss << "delete from t_history where time <= julianday('now', '-" << g_config.CleanupInterval << " days');";
				g_con.executenonquery(ss.str());
				lock.commit();

				// now delete all free btree page structures, as per 
				// http://web.utk.edu/~jplyon/sqlite/SQLite_optimization_FAQ.html#compact
				g_con.executenonquery(_T("vacuum;"));

				// HACK:  For some reason, vacuum doesn't do anything if we call it prior to commit(),
				//		  and commit() throws an exception if we call it twice, so we're doing it this way.

				// finally delete the peerblock.log file
				path pathLog = path::base_dir()/L"peerblock.log";
				g_tlog.StopLogging();
				path::remove(pathLog);
				g_tlog.StartLogging();
				if (g_config.CleanupType==ArchiveDelete)
					TRACES("[mainproc] [Main_ProcessDb]    Archived & deleted history.db and peerblock.log files")
				else
					TRACES("[mainproc] [Main_ProcessDb]    Deleted history.db and peerblock.log files");
			}
			catch(database_error &ex) 
			{
				TRACEE("[mainproc] [Main_ProcessDb]  * ERROR:  Caught database_error exception while deleting files!!");
				ExceptionBox(NULL, ex, __FILE__, __LINE__);
			}
			catch(exception &ex) {
				TRACEE("[mainproc] [Main_ProcessDb]  * ERROR:  Caught exception while deleting files!!");
				ExceptionBox(NULL, ex, __FILE__, __LINE__);
			}
			TRACEI("[mainproc] [Main_ProcessDb]    done with 'delete' type cleanup");
		}
	}

	g_dbthread=boost::shared_ptr<thread>();
	g_processlock.leave();
	TRACEI("[mainproc] [Main_ProcessDb]  < Leaving routine.");
}

static void Main_OnTimer(HWND hwnd, UINT id) 
{
	if(id==TIMER_BLINKTRAY && g_trayactive) {
		if(g_config.BlinkOnBlock!=Never && (GetTickCount()-6000) < g_blinkstart) {
			if((g_trayblink=!g_trayblink)) {
				HICON icon=g_nid.hIcon;

				g_nid.hIcon=NULL;
				Shell_NotifyIcon(NIM_MODIFY, &g_nid);
				g_nid.hIcon=icon;
			}
			else Shell_NotifyIcon(NIM_MODIFY, &g_nid);
		}
		else if(g_trayblink) {
			Shell_NotifyIcon(NIM_MODIFY, &g_nid);
			g_trayblink=false;
		}
	}
	else if(id==TIMER_PROCESSDB && g_config.CleanupType!=None && g_processlock.tryenter())
	{
		if(time(NULL)-g_config.LastArchived > 86400 /* seconds */ * g_config.CleanupInterval)
		{
			TRACEI("[mainproc] [Main_OnTimer]    Starting archival process");
			g_config.LastArchived = time(NULL);
			g_dbthread=boost::shared_ptr<thread>(new thread(Main_ProcessDb, THREAD_PRIORITY_BELOW_NORMAL));
			TRACEI("[mainproc] [Main_OnTimer]    finished with archival process");
		}
		else
			g_processlock.leave();
	}
}

static void Main_OnTray(HWND hwnd, UINT id, UINT eventMsg) {
	if(eventMsg==WM_LBUTTONUP)
		SendMessage(hwnd, WM_MAIN_VISIBLE, 0, TRUE);
	else if(eventMsg==WM_RBUTTONUP) {
		POINT pt;
		GetCursorPos(&pt);

		HMENU menu=LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_TRAYMENU));
		HMENU submenu=GetSubMenu(menu, 0);

		CheckMenuItem(submenu, ID_TRAY_PEERBLOCK, MF_BYCOMMAND|(g_config.WindowHidden?MF_UNCHECKED:MF_CHECKED));
		CheckMenuItem(submenu, ID_TRAY_ALWAYSONTOP, MF_BYCOMMAND|(g_config.AlwaysOnTop?MF_CHECKED:MF_UNCHECKED));
		CheckMenuItem(submenu, ID_TRAY_ENABLED, MF_BYCOMMAND|(g_config.Block?MF_CHECKED:MF_UNCHECKED));
		CheckMenuItem(submenu, ID_TRAY_DISABLED, MF_BYCOMMAND|(g_config.Block?MF_UNCHECKED:MF_CHECKED));
		CheckMenuItem(submenu, ID_TRAY_BLOCKHTTP, MF_BYCOMMAND|(g_config.BlockHttp?MF_UNCHECKED:MF_CHECKED));

		SetForegroundWindow(hwnd);
		TrackPopupMenuEx(submenu, TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, NULL);

		DestroyMenu(menu);
	}
}

static void Main_OnVisible(HWND hwnd, BOOL visible) 
{
	TRACEI("[Main_OnVisible]  > Entering routine.");
	int index=TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_TABS));
	TRACEV("[Main_OnVisible]    tabctrl_getcursel");

	if(visible) {
		TRACEI("[Main_OnVisible]    visible:[TRUE]");
		if(index!=-1) 
		{
			TRACEI("[Main_OnVisible]    index != [-1], showing window based on tab");
			ShowWindow(g_tabs[index].Tab, SW_SHOW);
		}

		TRACEI("[Main_OnVisible]    showing window (SH_SHOW)");
		ShowWindow(hwnd, SW_SHOW);
		TRACEV("[Main_OnVisible]    showing window (SW_RESTORE)");
		ShowWindow(hwnd, SW_RESTORE);
		TRACEV("[Main_OnVisible]    set foreground window");
		SetForegroundWindow(hwnd);
		TRACEV("[Main_OnVisible]    setting g_config.WindowHidden to false");
		g_config.WindowHidden=false;
		TRACEI("[Main_OnVisible]    checking for g_trayactive and g_config.StayHidden");

		if(!g_trayactive && !g_config.StayHidden) {
			TRACEV("[Main_OnVisible]    NOT g_trayactive AND NOT g_config.StayHidden");
			g_trayactive=true;
			g_config.HideTrayIcon=false;
			TRACEI("[Main_OnVisible]    showing notify-icon (NIM_ADD)");
			Shell_NotifyIcon(NIM_ADD, &g_nid);
			TRACEV("[Main_OnVisible]    done showing notify-icon");
		}
	}
	else {
		TRACEI("[Main_OnVisible]    visible:[FALSE]");
		if(index!=-1) 
		{
			TRACEI("[Main_OnVisible]    index != [-1], hiding window based on tab");
			ShowWindow(g_tabs[index].Tab, SW_HIDE);
		}

		TRACEI("[Main_OnVisible]    about to hide window");
		ShowWindow(hwnd, SW_HIDE);
		TRACEV("[Main_OnVisible]    done hiding window");
		g_config.WindowHidden=true;
		TRACEV("[Main_OnVisible]    set g_config.WindowHidden to [TRUE]");

	#ifdef _WIN32_WINNT
		TRACEI("[Main_OnVisible]    about to SetProcessWorkingSetSize");
		SetProcessWorkingSetSize(GetCurrentProcess(), (size_t)-1, (size_t)-1);
		TRACEV("[Main_OnVisible]    finished with SetProcessWorkingSetSize");
	#endif
	}
	TRACEI("[Main_OnVisible]  < Leaving routine.");
}

INT_PTR CALLBACK Main_DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	//TCHAR chBuf[128];
	//if (msg != 289)
	//{
	//	_stprintf_s(chBuf, sizeof(chBuf)/2, _T("[Main_DlgProc]  Processing Window MSG: [%d]"), msg);
	//	g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_CRITICAL);
	//}

	try {

		switch(msg) {
			HANDLE_MSG(hwnd, WM_CLOSE, Main_OnClose);
			HANDLE_MSG(hwnd, WM_COMMAND, Main_OnCommand);
			HANDLE_MSG(hwnd, WM_DESTROY, Main_OnDestroy);
			HANDLE_MSG(hwnd, WM_ENDSESSION, Main_OnEndSession);
			HANDLE_MSG(hwnd, WM_GETMINMAXINFO, Main_OnGetMinMaxInfo);
			HANDLE_MSG(hwnd, WM_INITDIALOG, Main_OnInitDialog);
			HANDLE_MSG(hwnd, WM_MOVE, Main_OnMove);
			HANDLE_MSG(hwnd, WM_NOTIFY, Main_OnNotify);
			HANDLE_MSG(hwnd, WM_SIZE, Main_OnSize);
			HANDLE_MSG(hwnd, WM_TIMER, Main_OnTimer);
			case WM_PB_TRAY:
				Main_OnTray(hwnd, (UINT)wParam, (UINT)lParam);
				return 1;
			case WM_MAIN_VISIBLE:
				Main_OnVisible(hwnd, (BOOL)lParam);
				return 1;
			default:
				if(msg==WM_TRAY_CREATED && g_trayactive) Shell_NotifyIcon(NIM_ADD, &g_nid);
				else if(msg==WM_PB_VISIBLE) 
				{
					TRACEI("[Main_DlgProc]    received WM_PB_VISIBLE message; setting visible");
					Main_OnVisible(hwnd, (BOOL)lParam);
					TRACEI("[Main_DlgProc]    finished setting visible");
					return 1;
				}
				else if(msg==WM_PB_LOADLISTS) {
					LoadLists(hwnd);
					return 1;
				}
				return 0;
		}
	}
	catch(exception &ex) {
		UncaughtExceptionBox(hwnd, ex, __FILE__, __LINE__);
		PostQuitMessage(0);
		return 0;
	}
	catch(...) {
		UncaughtExceptionBox(hwnd, __FILE__, __LINE__);
		PostQuitMessage(0);
		return 0;
	}
}
