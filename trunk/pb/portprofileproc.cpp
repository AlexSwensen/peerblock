/*
	Copyright (C) 2009 PeerBlock, LLC

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

//================================================================================================
//  portprofile.cpp
//
//  This file contains all the routines used to handle the editing of port sets: the window that
//  pops up after you click the "Ports" button on the Settings screen.
//================================================================================================


#include "stdafx.h"
#include "resource.h"
using namespace std;



static tstring GetDlgItemText(HWND hDlg, int nIDDlgItem) 
{
	TRACEI("[portprofileproc] [GetDlgItemText]  > Entering routine.");
	HWND ctrl=GetDlgItem(hDlg, nIDDlgItem);

	int len=GetWindowTextLength(ctrl)+1;
	boost::scoped_array<TCHAR> buf(new TCHAR[len]);

	TRACEI("[portprofileproc] [GetDlgItemText]  < Leaving routine.");
	return tstring(buf.get(), GetWindowText(ctrl, buf.get(), len));

} // End of GetDlgItemText()



static void PortProfile_OnClose(HWND hwnd) 
{
	TRACEI("[portprofileproc] [PortProfile_OnClose]  > Entering routine.");
	EndDialog(hwnd, IDCANCEL);
	TRACEI("[portprofileproc] [PortProfile_OnClose]  < Leavingroutine.");

} // End of PortProfile_OnClose()



static BOOL PortProfile_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) 
{
	TRACEI("[portprofileproc] [PortProfile_OnInitDialog]  > Entering routine.");
	PortProfile *profile = (PortProfile*) lParam;

#pragma warning(disable:4244)
	SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR) profile);
#pragma warning(default:4244)

	HWND hname = GetDlgItem(hwnd, IDC_PORTNAME);
	Edit_SetText(hname, profile->Name.c_str());

	tstringstream ports;
	for (set<ULONG>::const_iterator iter = profile->Ports.begin(); iter != profile->Ports.end(); iter++) 
	{
		ports << *iter;
		ports << _T("\r\n");
	}

	HWND hports = GetDlgItem(hwnd, IDC_PORTPORTS);
	Edit_SetText(hports, ports.str().c_str());

	TRACEI("[portprofileproc] [PortProfile_OnInitDialog]  < Leaving routine.");
	return TRUE;

} // End of PortProfile_OnInitDialog()



static void PortProfile_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) 
{
	switch (id)
	{
		case IDOK: 
		{
			TRACEI("[portprofileproc] [PortProfile_OnCommand]  > IDOK");
			PortProfile *pp = (PortProfile*)(LONG_PTR)GetWindowLongPtr(hwnd, DWLP_USER);

			tstring name = GetDlgItemText(hwnd, IDC_PORTNAME);
			pp->Name = name;
			pp->Enabled = true;
			pp->Ports.clear();

			std::vector<tstring> ports;
			tstring txtports = GetDlgItemText(hwnd, IDC_PORTPORTS);

			// allow for a whole range of separators
			boost::split(ports, txtports, boost::is_any_of("\t \n\r,|"));

			for (std::vector<tstring>::size_type i = 0; i < ports.size(); i++) 
			{
				try 
				{
					int p = boost::lexical_cast<int>(ports[i]);

					if (p > 0 && p <= 65535)
						pp->Ports.insert((ULONG) p);
				}
				catch (...) 
				{
					TRACEW("[portprofileproc] [PortProfile_OnCommand]  * EXCEPTION caught (and ignored) while processing IDOK");
				}
			}

			TRACEI("[portprofileproc] [PortProfile_OnCommand]  < IDOK");
			EndDialog(hwnd, IDOK);

		} break;	// End of IDOK

		case IDCANCEL:
		{
			TRACEI("[portprofileproc] [PortProfile_OnCommand]  > IDCANCEL");
			EndDialog(hwnd, IDCANCEL);
			TRACEI("[portprofileproc] [PortProfile_OnCommand]  < IDCANCEL");
		}
	}

} // End of PortProfile_OnCommand()



INT_PTR CALLBACK PortProfile_DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
	try 
	{
		switch(msg) 
		{
			HANDLE_MSG(hwnd, WM_CLOSE, PortProfile_OnClose);
			HANDLE_MSG(hwnd, WM_COMMAND, PortProfile_OnCommand);
			HANDLE_MSG(hwnd, WM_INITDIALOG, PortProfile_OnInitDialog);
			default: return 0;
		}
		return 0;
	}
	catch(exception &ex) 
	{
		UncaughtExceptionBox(hwnd, ex, __FILE__, __LINE__);
		return 0;
	}
	catch(...) 
	{
		UncaughtExceptionBox(hwnd, __FILE__, __LINE__);
		return 0;
	}

} // End of PortProfile_DlgProc()
