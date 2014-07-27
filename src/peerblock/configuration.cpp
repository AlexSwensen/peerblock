/*
	Original code copyright (C) 2004-2005 Cory Nelson
	PeerBlock modifications copyright (C) 2009-2014 PeerBlock, LLC

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
#include "versioninfo.h"
#include "rpc.h"

using namespace std;

#include "tracelog.h"
extern TraceLog g_tlog;

Configuration g_config;

Configuration::Configuration()
	: Block(true)
	, AllowLocal(true)
	, UpdatePeerBlock(true)
	, IgnoreListUpdateLimit(false)
	, UpdateLists(true)
	, UpdateAtStartup(true)
	, ShowSplash(false)
	, WindowHidden(false)
	, UpdateInterval(2)
	, LogSize(12)
	, LastUpdate(0)
	, LastArchived(0)
	, LastStarted(0)
	, CleanupInterval(7)
	, LogAllowed(true)
	, LogBlocked(true)
	, ShowAllowed(false)
	, CacheCrc(0)
	, UpdateCountdown(10)
	, RecentBlockWarntime(60)
	, UpdateProxyType(CURLPROXY_HTTP)
	, UpdateWindowPos(RECT())
	, ListManagerWindowPos(RECT())
	, WindowPos(RECT())
	, StayHidden(false)
	, ListEditorWindowPos(RECT())
	, HistoryWindowPos(RECT())
	, HideOnClose(true)
	, AlwaysOnTop(false)
	, HideTrayIcon(false)
	, FirstBlock(true)
	, FirstHide(true)
	, EnableListSanityChecking(true)
	, EnableWarningIconForHttpAllow(true)
	, BlinkOnBlock(OnHttpBlock)
	, NotifyOnBlock(Never)
	, CleanupType(Delete)
	, TempAllowingHttpShort(false)
	, TempAllowingHttpLong(false)
	, TracelogEnabled(true)
	, TracelogLevel(TRACELOG_LEVEL_DEFAULT)
	, LastVersionRun(0)
	, ArchivePath(_T("archives"))
	, StartMinimized(false)
	, ColorCode(true)
	, MaxHistorySize(100000000)
	, HistoryCheckInterval(10000) {
		HistoryColumns[0]=64;
		HistoryColumns[1]=128;
		HistoryColumns[2]=124;
		HistoryColumns[3]=124;
		HistoryColumns[4]=64;
		HistoryColumns[5]=64;

		LogColumns[0]=64;
		LogColumns[1]=128;
		LogColumns[2]=124;
		LogColumns[3]=124;
		LogColumns[4]=64;
		LogColumns[5]=64;

		ListEditorColumns[0]=256;
		ListEditorColumns[1]=128;
		ListEditorColumns[2]=128;

		ListManagerColumns[0]=192;
		ListManagerColumns[1]=64;
		ListManagerColumns[2]=192;

		UpdateColumns[0]=128;
		UpdateColumns[1]=102;
		UpdateColumns[2]=128;

		AllowedColor.Text=RGB(192, 192, 192);
		AllowedColor.Background=RGB(255, 255, 255);

		BlockedColor.Text=RGB(0, 0, 0);
		BlockedColor.Background=RGB(255, 255, 255);

		HttpColor.Text=RGB(0, 0, 192);
		HttpColor.Background=RGB(255, 255, 255);
}

static bool GetChild(const TiXmlElement *root, const string &node, string &value) {
	const TiXmlElement *e=root->FirstChildElement(node);
	if(!e) return false;

	const TiXmlNode *n=e->FirstChild();
	if(!n) return false;

	const TiXmlText *t=n->ToText();
	if(!t) return false;

	const char *v=t->Value();
	if(!v) return false;

	value=UTF8_MBS(v);
	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, wstring &value) {
	const TiXmlElement *e=root->FirstChildElement(node);
	if(!e) return false;

	const TiXmlNode *n=e->FirstChild();
	if(!n) return false;

	const TiXmlText *t=n->ToText();
	if(!t) return false;

	const char *v=t->Value();
	if(!v) return false;

	value=UTF8_TSTRING(v);
	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, bool &value) {
	string v;
	if(!GetChild(root, node, v)) return false;

	value=(v=="yes");
	return true;
}

template<class InT>
static bool GetChild(const TiXmlElement *root, const string &node, InT &value) {
	string v;
	if(!GetChild(root, node, v)) return false;

	try {
		value=boost::lexical_cast<InT>(v);
	}
	catch(...) {
		return false;
	}

	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, RECT &rc) {
	const TiXmlElement *e=root->FirstChildElement(node);
	if(!e) return false;

	bool ret=true;

	if(!GetChild(e, "Top", rc.top)) ret=false;
	if(!GetChild(e, "Left", rc.left)) ret=false;
	if(!GetChild(e, "Bottom", rc.bottom)) ret=false;
	if(!GetChild(e, "Right", rc.right)) ret=false;

	rc.top=max(rc.top, 0L);
	rc.left=max(rc.left, 0L);
	rc.bottom=max(rc.bottom, 0L);
	rc.right=max(rc.right, 0L);

	return ret;
}

static bool GetChild(const TiXmlElement *root, const string &node, COLORREF &c) {
	string v;
	if(!GetChild(root, node, v)) return false;

	unsigned short r, g, b;
	if(sscanf(v.c_str(), "%2hx%2hx%2hx", &r, &g, &b)!=3) return false;

	c=RGB(r, g, b);
	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, Color &c) {
	const TiXmlElement *e=root->FirstChildElement(node);
	if(!e) return false;

	bool ret=true;

	if(!GetChild(e, "Text", c.Text)) ret=false;
	if(!GetChild(e, "Background", c.Background)) ret=false;

	return ret;
}

static bool GetChild(const TiXmlElement *root, const string &node, NotifyType &nt) {
	string v;
	if(!GetChild(root, node, v)) return false;

	if(v=="Never") nt=Never;
	else if(v=="OnHttpBlock") nt=OnHttpBlock;
	else if(v=="OnBlock") nt=OnBlock;
	else return false;

	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, CleanType &nt) {
	string v;
	if(!GetChild(root, node, v)) return false;

	if(v=="None") nt=None;
	else if(v=="Delete") nt=Delete;
	else if(v=="ArchiveDelete") nt=ArchiveDelete;
	else return false;

	return true;
}

static bool GetChild(const TiXmlElement *root, const string &node, PortType &pt) {
	string v;
	if(!GetChild(root, node, v)) return false;

	if(v=="Destination") pt=Destination;
	else if(v=="Source") pt=Source;
	else if(v=="Both") pt=Both;
	else return false;

	return true;
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node) {
	return root->InsertEndChild(TiXmlElement(node))->ToElement();
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, const string &value) {
	TiXmlElement *e=InsertChild(root, node);
	if(value.length()) e->InsertEndChild(TiXmlText(value));

	return e;
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, const wstring &value) {
	return InsertChild(root, node, TSTRING_UTF8(value));
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, const char *value) {
	return InsertChild(root, node, string(value));
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, bool value) {
	return InsertChild(root, node, value?"yes":"no");
}

template<class InT>
static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, InT value) {
	return InsertChild(root, node, boost::lexical_cast<string>(value));
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, const RECT &rc) {
	TiXmlElement *e=InsertChild(root, node);

	InsertChild(e, "Top", rc.top);
	InsertChild(e, "Left", rc.left);
	InsertChild(e, "Bottom", rc.bottom);
	InsertChild(e, "Right", rc.right);

	return e;
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, COLORREF c) {
	char buf[7];
	StringCbPrintfA(buf, sizeof(buf), "%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
	return InsertChild(root, node, buf);
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, const Color &c) {
	TiXmlElement *e=InsertChild(root, node);

	InsertChild(e, "Text", c.Text);
	InsertChild(e, "Background", c.Background);

	return e;
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, NotifyType nt) {
	const char *v;

	switch(nt) {
		case Never: v="Never"; break;
		case OnHttpBlock: v="OnHttpBlock"; break;
		case OnBlock: v="OnBlock"; break;
		default: __assume(0);
	}

	return InsertChild(root, node, v);
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, CleanType nt) {
	const char *v;

	switch(nt) {
		case None: v="None"; break;
		case Delete: v="Delete"; break;
		case ArchiveDelete: v="ArchiveDelete"; break;
		default: __assume(0);
	}

	return InsertChild(root, node, v);
}

static TiXmlElement *InsertChild(TiXmlNode *root, const string &node, PortType pt) {
	const char *v;

	switch(pt) {
		case Destination: v="Destination"; break;
		case Source: v="Source"; break;
		case Both: v="Both"; break;
		default: __assume(0);
	}

	return InsertChild(root, node, v);
}

static void InsertAttribute(TiXmlElement *root, PortRange pr) {
	root->SetAttribute("Start", pr.Start);

	if (pr.End > pr.Start)
		root->SetAttribute("End", pr.End);
}



//================================================================================================
//
//  LoadFile()
//
//    - Called by Configuration::Load()
//
/// <summary>
///   Attempts to load the specified config-file.  Returns true if successful, false if not
/// </summary>
//
bool Configuration::LoadFile(const TCHAR *file, HANDLE *fp, HANDLE *map, const void **view)
{
		TCHAR chBuf[256];
		_stprintf_s(chBuf, _countof(chBuf), _T("[Configuration] [LoadFile]    loading file:[%s]"), file);
		g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_INFO);

		*fp=CreateFile(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if(*fp==INVALID_HANDLE_VALUE)
		{
			TRACEERR("[Configuration] [LoadFile]", L"Can't open file", GetLastError());
			return false;
		}

		// Attempting to access a 0-byte file causes "The volume for a file has been externally
		// altered so that the opened file is no longer valid." errors, both here and when trying
		// to save the .conf file later on.
		DWORD size=0;
		size=GetFileSize(*fp, NULL);
		if (size == 0)
		{
			TRACEW("[Configuration] [LoadFile]    0 byte conf file");
			CloseHandle(*fp);
			*fp = 0;
			return false;
		}
		else if (size == INVALID_FILE_SIZE)
		{
			TRACEERR("[Configuration] [LoadFile]", L"Can't get file size", GetLastError());
			CloseHandle(*fp);
			*fp = 0;
			return false;
		}
		else
		{
			tstring strBuf = boost::str(tformat(_T("[Configuration] [LoadFile]    about to memory-map file - size: [%1%]")) % size );
			TRACEBUFI(strBuf);
		}

		*map=CreateFileMapping(*fp, NULL, PAGE_READONLY, 0, 0, NULL);
		if(*map==NULL)
		{
			TRACEERR("[Configuration] [LoadFile]", L"Can't create file map", GetLastError());
			return false;
		}

		*view=MapViewOfFile(*map, FILE_MAP_READ, 0, 0, 0);
		if(*view==NULL)
		{
			TRACEERR("[Configuration] [LoadFile]", L"Can't map view of file", GetLastError());
			return false;
		}

		_stprintf_s(chBuf, _countof(chBuf), _T("[Configuration] [LoadFile]    Successfully loaded file:[%s]"), file);
		g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_SUCCESS);

		return true;

}; // End of LoadFile()



//================================================================================================
//
//  Load()
//
//    - Called by Main_OnInitDialog()
//
/// <summary>
///   Attempts to load the config-file.  Returns true if successful, false if not
/// </summary>
/// <remarks>
///   If we're unsuccessful, the caller will assume this is the first time we've run the program
///   and will run through the startup-wizard.
/// </remarks>
//
bool Configuration::Load()
{
	TRACEI("[Configuration] [Load]  > Entering routine.");

	TiXmlDocument doc;

	const path pb=path::base_dir()/_T("peerblock.conf");
	const path oldpb=path::base_dir()/_T("peerblock.conf.bak");
	const path pg2=path::base_dir()/_T("pg2.conf");

	vector<path> files;
	files.push_back(pb);
	files.push_back(oldpb);
	files.push_back(pg2);

	bool loaded = false;
    bool isDirty = false;

	for(vector<path>::iterator itr = files.begin(); itr != files.end() && !loaded; ++itr)
	{
		{
			HANDLE fp = NULL;
			HANDLE map = NULL;
			const void *view = NULL;
			if (!LoadFile((*itr).file_str().c_str(), &fp, &map, &view))	// first try to find a PeerBlock file
			{
				tstring strBuf = boost::str(tformat(_T("[Configuration] [Load]    WARNING:  Unable to load configuration file [%1%]")) %
					(*itr).file_str().c_str() );
				TRACEBUFW(strBuf);
				continue;
			}

			tstring strBuf = boost::str(tformat(_T("[Configuration] [Load]    Loaded configuration file [%1%]")) %
				(*itr).file_str().c_str() );
			TRACEBUFI(strBuf);

			boost::shared_ptr<void> fp_safe(fp, CloseHandle);
			boost::shared_ptr<void> map_safe(map, CloseHandle);
			boost::shared_ptr<const void> view_safe(view, UnmapViewOfFile);

			doc.Parse((const char*)view);

			if(doc.Error())
			{
				TRACEE("[Configuration] [Load]  * ERROR:  Can't parse xml document");
				tstring strBuf = boost::str(tformat(_T("[Configuration] [Load]  * - Error: [%1%] (\"%2%\") at row:[%3%] col:[%4%]")) %
					doc.ErrorId() % doc.ErrorDesc() % doc.ErrorRow() % doc.ErrorCol() );
				TRACEBUFE(strBuf);

				// copy over to .failed
				path failedFile;
				failedFile = itr->file_str();
				failedFile = failedFile.file_str() + L".failed";
				path::copy(*itr, failedFile, true);
				strBuf = boost::str(tformat(_T("[Configuration] [Load]    - Copied failed-parsing config-file to: [%1%]")) % failedFile.file_str() );
				TRACEBUFE(strBuf);
				continue;
			}
		}

		if (*itr == pb)
		{
			TRACEI("[Configuration] [Load]    found peerblock configuration file");
		}
		else if (*itr == oldpb)
		{
			TRACEW("[Configuration] [Load]    using last-known-good configuration file");
			MessageBox(NULL, IDS_CONFERRTEXT, IDS_CONFERR, MB_ICONWARNING|MB_OK);
			g_config.Save(_T("peerblock.conf"));	// save restored config to regular file
		}
		else if (*itr == pg2)
		{
			TRACEI("[Configuration] [Load]    found old-style pg2 configuration file");
		}
		else	// can't find anything, return false so caller can run the startup-wizard
		{
			TRACEW("[Configuration] [Load]    WARNING:  No configuration file found.");
			return false;
		}

		loaded = true;
	}

	if (!loaded)
	{
		// can't find anything, return false so caller can run the startup-wizard
		TRACEW("[Configuration] [Load]    ERROR:  No configuration file loaded!");
		return false;
	}

	TRACEI("[Configuration] [Load]    parsing config root element");

	const TiXmlElement *root=doc.RootElement();
	if(!root || (strcmp(root->Value(), "PeerGuardian2") && strcmp(root->Value(), "PeerBlock")))
	{
		TRACEI("[Configuration] [Load]    ERROR:  Not a valid configuration file!");
		return false;
	}

	TRACEI("[Configuration] [Load]    parsing config settings element");
	if(const TiXmlElement *settings=root->FirstChildElement("Settings")) {
		GetChild(settings, "Block", this->Block);
		if (GetChild(settings, "BlockHttp", this->PortSet.AllowHttp))
			this->PortSet.AllowHttp = !this->PortSet.AllowHttp;
		GetChild(settings, "AllowLocal", this->AllowLocal);
		GetChild(settings, "CacheCrc", this->CacheCrc);
		GetChild(settings, "BlinkOnBlock", this->BlinkOnBlock);
		GetChild(settings, "NotifyOnBlock", this->NotifyOnBlock);
		GetChild(settings, "LastVersionRun", this->LastVersionRun);

		// specially-treat RecentBlockWarntime since otherwise TinyXML will save it to the .conf as a hex value
		int i=(DWORD)RecentBlockWarntime;
		GetChild(settings, "RecentBlockWarntime", i);
		this->RecentBlockWarntime=(DWORD)i;

		GetChild(settings, "ListSanityChecking", this->EnableListSanityChecking);
		GetChild(settings, "WarningIconForHttpAllow", this->EnableWarningIconForHttpAllow);
	}

	TRACEI("[Configuration] [Load]    parsing config logging element");
	if(const TiXmlElement *logging=root->FirstChildElement("Logging")) {
		GetChild(logging, "LogSize", this->LogSize);
		GetChild(logging, "LogAllowed", this->LogAllowed);
		GetChild(logging, "LogBlocked", this->LogBlocked);
		GetChild(logging, "ShowAllowed", this->ShowAllowed);

		GetChild(logging, "Cleanup", this->CleanupType);
		if(const TiXmlElement *c=logging->FirstChildElement("Cleanup")) {
			int i=0;
			if(c->Attribute("Interval", &i))
				this->CleanupInterval=(unsigned short)i;
		}

		tstring p;
		if(GetChild(logging, "ArchivePath", p))
			this->ArchivePath=p;

		GetChild(logging, "MaxHistorySize", this->MaxHistorySize);

		GetChild(logging, "HistoryCheckInterval", this->HistoryCheckInterval);
	}

	TRACEI("[Configuration] [Load]    parsing config tracelogging element");
	if(const TiXmlElement *logging=root->FirstChildElement("TraceLog")) {
		GetChild(logging, "Enabled", this->TracelogEnabled);
		GetChild(logging, "Level", this->TracelogLevel);
	}

	TRACEI("[Configuration] [Load]    parsing config colors element");
	if(const TiXmlElement *colors=root->FirstChildElement("Colors")) {
		GetChild(colors, "ColorCode", this->ColorCode);
		GetChild(colors, "Blocked", this->BlockedColor);
		GetChild(colors, "Allowed", this->AllowedColor);
		GetChild(colors, "Http", this->HttpColor);
	}

	TRACEI("[Configuration] [Load]    parsing config windowing element");
	if(const TiXmlElement *windowing=root->FirstChildElement("Windowing")) {
		GetChild(windowing, "Main", this->WindowPos);
		GetChild(windowing, "Update", this->UpdateWindowPos);
		GetChild(windowing, "ListManager", this->ListManagerWindowPos);
		GetChild(windowing, "ListEditor", this->ListEditorWindowPos);
		GetChild(windowing, "History", this->HistoryWindowPos);

		GetChild(windowing, "StartMinimized", this->StartMinimized);
		GetChild(windowing, "ShowSplash", this->ShowSplash);
		GetChild(windowing, "StayHidden", this->StayHidden);
		GetChild(windowing, "HideOnClose", this->HideOnClose);

		GetChild(windowing, "HideMain", this->WindowHidden);
		GetChild(windowing, "AlwaysOnTop", this->AlwaysOnTop);
		GetChild(windowing, "HideTrayIcon", this->HideTrayIcon);

		if(const TiXmlElement *hcol=windowing->FirstChildElement("HistoryColumns")) {
			int cols[6]={0};

			GetChild(hcol, "Time", cols[0]);
			GetChild(hcol, "Range", cols[1]);
			GetChild(hcol, "Source", cols[2]);
			GetChild(hcol, "Destination", cols[3]);
			GetChild(hcol, "Protocol", cols[4]);
			GetChild(hcol, "Action", cols[5]);

			SaveListColumns(cols, this->HistoryColumns);
		}

		if(const TiXmlElement *lcol=windowing->FirstChildElement("LogColumns")) {
			int cols[6]={0};

			GetChild(lcol, "Time", cols[0]);
			GetChild(lcol, "Range", cols[1]);
			GetChild(lcol, "Source", cols[2]);
			GetChild(lcol, "Destination", cols[3]);
			GetChild(lcol, "Protocol", cols[4]);
			GetChild(lcol, "Action", cols[5]);

			SaveListColumns(cols, this->LogColumns);
		}

		if(const TiXmlElement *lcol=windowing->FirstChildElement("ListEditorColumns")) {
			int cols[3]={0};

			GetChild(lcol, "Range", cols[0]);
			GetChild(lcol, "StartingIp", cols[1]);
			GetChild(lcol, "EndingIp", cols[2]);

			SaveListColumns(cols, this->ListEditorColumns);
		}

		if(const TiXmlElement *lcol=windowing->FirstChildElement("ListManagerColumns")) {
			int cols[3]={0};

			GetChild(lcol, "File", cols[0]);
			GetChild(lcol, "Type", cols[1]);
			GetChild(lcol, "Description", cols[2]);

			SaveListColumns(cols, this->ListManagerColumns);
		}

		if(const TiXmlElement *ucol=windowing->FirstChildElement("UpdateColumns")) {
			int cols[3]={0};

			GetChild(ucol, "Description", cols[0]);
			GetChild(ucol, "Task", cols[1]);
			GetChild(ucol, "Status", cols[2]);

			SaveListColumns(cols, this->UpdateColumns);
		}
	}

	TRACEI("[Configuration] [Load]    parsing config updates element");
	if(const TiXmlElement *updates=root->FirstChildElement("Updates")) {
		string lastupdate;

		GetChild(updates, "UpdatePeerBlock", this->UpdatePeerBlock);
		GetChild(updates, "UpdateLists", this->UpdateLists);
		GetChild(updates, "UpdateAtStartup", this->UpdateAtStartup);
		GetChild(updates, "UpdateInterval", this->UpdateInterval);
		GetChild(updates, "UpdateCountdown", this->UpdateCountdown);
		GetChild(updates, "IgnoreListUpdateLimit", this->IgnoreListUpdateLimit);

		GetChild(updates, "UpdateProxy", this->UpdateProxy);
		if(const TiXmlElement *proxy=updates->FirstChildElement("UpdateProxy")) {
			const char *t=proxy->Attribute("Type");

			this->UpdateProxyType=(t!=NULL && !strcmp(t, "http"))?CURLPROXY_HTTP:CURLPROXY_SOCKS5;
		}

		GetChild(updates, "LastUpdate", lastupdate);
		if(lastupdate.length()>0) {
			try {
				this->LastUpdate=boost::lexical_cast<int>(lastupdate);
			}
			catch(...) {
				// keep going
			}
		}

		GetChild(updates, "LastArchived", lastupdate);
		if(lastupdate.length()>0) {
			try {
				this->LastArchived=boost::lexical_cast<int>(lastupdate);
			}
			catch(...) {
				// keep going
			}
		}

		GetChild(updates, "LastStarted", lastupdate);
		if(lastupdate.length()>0) {
			try {
				this->LastStarted=boost::lexical_cast<int>(lastupdate);
			}
			catch(...) {
				// keep going
			}
		}

        GetChild(updates, "UniqueId", UniqueId);
        if (EnsureUniqueId()) {
            // we generated a new unique ID for this, so make sure we save it later on
            isDirty = true;
        }
	}

	TRACEI("[Configuration] [Load]    parsing config messages element");
	if(const TiXmlElement *messages=root->FirstChildElement("Messages")) {
		GetChild(messages, "FirstBlock", this->FirstBlock);
		GetChild(messages, "FirstHide", this->FirstHide);
	}

	TRACEI("[Configuration] [Load]    parsing config ports element");
	if (const TiXmlElement *portset = root->FirstChildElement("PortSet")) {
		GetChild(portset, "AllowHttp", this->PortSet.AllowHttp);
		GetChild(portset, "AllowFtp", this->PortSet.AllowFtp);
		GetChild(portset, "AllowSmtp", this->PortSet.AllowSmtp);
		GetChild(portset, "AllowPop3", this->PortSet.AllowPop3);

		if (const TiXmlElement *profiles = portset->FirstChildElement("Profiles")) {
			for (const TiXmlElement *profile = profiles->FirstChildElement("Profile"); profile != NULL; profile = profile->NextSiblingElement("Profile")) {
				PortProfile pf;
				GetChild(profile, "Name", pf.Name);
				GetChild(profile, "Enabled", pf.Enabled);
				GetChild(profile, "Type", pf.Type);

				if (const TiXmlElement *ports = profile->FirstChildElement("Ports")) {
					for (const TiXmlElement *range = ports->FirstChildElement("Range"); range != NULL; range = range->NextSiblingElement("Range")) {
						const char *start = range->Attribute("Start");
						const char *end = range->Attribute("End");

						if (start) {
							try {
								USHORT s = boost::lexical_cast<USHORT>(start);
								USHORT e = s;

								if (end)
									e = boost::lexical_cast<USHORT>(end);

								PortRange pr;
								pr.Start = s;
								pr.End = e;

								pf.Ports.push_back(pr);
							}
							catch (boost::bad_lexical_cast &) {
								// not valid number
							}
						}
					}
				}

				this->PortSet.Profiles.push_back(pf);
			}
		}
	}

	TRACEI("[Configuration] [Load]    parsing config lists element");
	if(const TiXmlElement *lists=root->FirstChildElement("Lists")) {
		for(const TiXmlElement *list=lists->FirstChildElement("List"); list!=NULL; list=list->NextSiblingElement("List")) {
			string file, url, type, description, lastupdate, lastdownload;
			bool enabled, failedupdate=false;

			GetChild(list, "File", file);
			GetChild(list, "Url", url);
			GetChild(list, "Type", type);
			GetChild(list, "Description", description);
			GetChild(list, "LastUpdate", lastupdate);
			GetChild(list, "LastDownload", lastdownload);
			GetChild(list, "FailedUpdate", failedupdate);
			if(!GetChild(list, "Enabled", enabled))
				enabled=true;

			if(file.length()>0) {
				StaticList l;

				l.Type=(type=="allow")?List::Allow:List::Block;
				l.File=UTF8_TSTRING(file);
				l.Description=UTF8_TSTRING(description);
				l.Enabled=enabled;

				this->StaticLists.push_back(l);
			}
			else {
				DynamicList l;

				l.Type=(type=="allow")?List::Allow:List::Block;
				l.Url=UTF8_TSTRING(url);
				l.Description=UTF8_TSTRING(description);
				l.Enabled=enabled;
				l.FailedUpdate=failedupdate;

				if(lastupdate.length()>0) {
					try {
						l.LastUpdate=boost::lexical_cast<int>(lastupdate);
					}
					catch(...) {
						// keep going
					}
				}

				if(lastdownload.length()>0) {
					try {
						l.LastDownload=boost::lexical_cast<int>(lastdownload);
					}
					catch(...) {
						// keep going
					}
				}

				this->DynamicLists.push_back(l);
			}
		}
	}

	TRACEI("[Configuration] [Load]    parsing I-Blocklist Subscription element");
	if(const TiXmlElement *ibl=root->FirstChildElement("I-Blocklist")) {
		GetChild(ibl, "Username", this->IblUsername);
		GetChild(ibl, "PIN", this->IblPin);
	}

    if (isDirty) {
    	TRACEI("[Configuration] [Load]    config loaded dirty, saving");
        Save();
    }

	TRACEI("[Configuration] [Load]  < Leaving routine.");
	return true;

} // End of Load()



static bool RectValid(const RECT &rc) {
	return (rc.top!=0 || rc.left!=0 || rc.bottom!=0 || rc.right!=0);
}



//================================================================================================
//
//  Save()
//
//    - Called by ???
//
/// <summary>
///   Writes out currently-set config to peerblock.conf file.
/// </summary>
//
void Configuration::Save(const TCHAR * _filename)
{
	TRACEI("[Configuration] [Save]  > Entering routine.");

	if (TempAllowingHttpShort || TempAllowingHttpLong)
		SetBlockHttp(this->PortSet.AllowHttp);	// make sure we don't accidentally startup allowed

	TiXmlDocument doc;
	doc.InsertEndChild(TiXmlDeclaration("1.0", "UTF-8", "yes"));

	TiXmlElement *root=InsertChild(&doc, "PeerBlock");

	{
		TiXmlElement *settings=InsertChild(root, "Settings");

		InsertChild(settings, "AllowLocal", this->AllowLocal);
		InsertChild(settings, "CacheCrc", this->CacheCrc);
		InsertChild(settings, "BlinkOnBlock", this->BlinkOnBlock);
		InsertChild(settings, "NotifyOnBlock", this->NotifyOnBlock);
		InsertChild(settings, "LastVersionRun", PB_VER_BUILDNUM);
		InsertChild(settings, "RecentBlockWarntime", (int)this->RecentBlockWarntime);
		InsertChild(settings, "ListSanityChecking", this->EnableListSanityChecking);
		InsertChild(settings, "WarningIconForHttpAllow", this->EnableWarningIconForHttpAllow);
	}

	{
		TiXmlElement *logging=InsertChild(root, "Logging");

		InsertChild(logging, "LogSize", this->LogSize);
		InsertChild(logging, "LogAllowed", this->LogAllowed);
		InsertChild(logging, "LogBlocked", this->LogBlocked);
		InsertChild(logging, "ShowAllowed", this->ShowAllowed);

		{
			TiXmlElement *e=InsertChild(logging, "Cleanup", this->CleanupType);
			e->SetAttribute("Interval", (int)this->CleanupInterval);
		}

		InsertChild(logging, "ArchivePath", this->ArchivePath.directory_str());

		InsertChild(logging, "MaxHistorySize", this->MaxHistorySize);

		InsertChild(logging, "HistoryCheckInterval", this->HistoryCheckInterval);
	}

	{
		TiXmlElement *logging=InsertChild(root, "TraceLog");

		InsertChild(logging, "Enabled", this->TracelogEnabled);
		InsertChild(logging, "Level", this->TracelogLevel);
	}

	{
		TiXmlElement *colors=InsertChild(root, "Colors");

		InsertChild(colors, "ColorCode", this->ColorCode);
		InsertChild(colors, "Blocked", this->BlockedColor);
		InsertChild(colors, "Allowed", this->AllowedColor);
		InsertChild(colors, "Http", this->HttpColor);
	}

	{
		TiXmlElement *windowing=InsertChild(root, "Windowing");

		if(RectValid(this->WindowPos))
			InsertChild(windowing, "Main", this->WindowPos);

		if(RectValid(this->UpdateWindowPos))
			InsertChild(windowing, "Update", this->UpdateWindowPos);

		if(RectValid(this->ListManagerWindowPos))
			InsertChild(windowing, "ListManager", this->ListManagerWindowPos);

		if(RectValid(this->ListEditorWindowPos))
			InsertChild(windowing, "ListEditor", this->ListEditorWindowPos);

		if(RectValid(this->HistoryWindowPos))
			InsertChild(windowing, "History", this->HistoryWindowPos);

		InsertChild(windowing, "StartMinimized", this->StartMinimized);
		InsertChild(windowing, "ShowSplash", this->ShowSplash);
		InsertChild(windowing, "StayHidden", this->StayHidden);
		InsertChild(windowing, "HideOnClose", this->HideOnClose);

		InsertChild(windowing, "HideMain", this->WindowHidden);
		InsertChild(windowing, "AlwaysOnTop", this->AlwaysOnTop);
		InsertChild(windowing, "HideTrayIcon", this->HideTrayIcon);

		{
			TiXmlElement *hcol=InsertChild(windowing, "HistoryColumns");

			InsertChild(hcol, "Time", this->HistoryColumns[0]);
			InsertChild(hcol, "Range", this->HistoryColumns[1]);
			InsertChild(hcol, "Source", this->HistoryColumns[2]);
			InsertChild(hcol, "Destination", this->HistoryColumns[3]);
			InsertChild(hcol, "Protocol", this->HistoryColumns[4]);
			InsertChild(hcol, "Action", this->HistoryColumns[5]);
		}

		{
			TiXmlElement *lcol=InsertChild(windowing, "LogColumns");

			InsertChild(lcol, "Time", this->LogColumns[0]);
			InsertChild(lcol, "Range", this->LogColumns[1]);
			InsertChild(lcol, "Source", this->LogColumns[2]);
			InsertChild(lcol, "Destination", this->LogColumns[3]);
			InsertChild(lcol, "Protocol", this->LogColumns[4]);
			InsertChild(lcol, "Action", this->LogColumns[5]);
		}

		{
			TiXmlElement *lcol=InsertChild(windowing, "ListEditorColumns");

			InsertChild(lcol, "Range", this->ListEditorColumns[0]);
			InsertChild(lcol, "StartingIp", this->ListEditorColumns[1]);
			InsertChild(lcol, "EndingIp", this->ListEditorColumns[2]);
		}

		{
			TiXmlElement *lcol=InsertChild(windowing, "ListManagerColumns");

			InsertChild(lcol, "File", this->ListManagerColumns[0]);
			InsertChild(lcol, "Type", this->ListManagerColumns[1]);
			InsertChild(lcol, "Description", this->ListManagerColumns[2]);
		}

		{
			TiXmlElement *ucol=InsertChild(windowing, "UpdateColumns");

			InsertChild(ucol, "Description", this->UpdateColumns[0]);
			InsertChild(ucol, "Task", this->UpdateColumns[1]);
			InsertChild(ucol, "Status", this->UpdateColumns[2]);
		}
	}

	{
		TiXmlElement *updates=InsertChild(root, "Updates");

		InsertChild(updates, "UpdatePeerBlock", this->UpdatePeerBlock);
		InsertChild(updates, "UpdateLists", this->UpdateLists);
		InsertChild(updates, "UpdateAtStartup", this->UpdateAtStartup);
		InsertChild(updates, "UpdateInterval", this->UpdateInterval);
		InsertChild(updates, "UpdateCountdown", this->UpdateCountdown);
		InsertChild(updates, "IgnoreListUpdateLimit", this->IgnoreListUpdateLimit);
		InsertChild(updates, "UniqueId", this->UniqueId);

		{
			TiXmlElement *proxy=InsertChild(updates, "UpdateProxy", this->UpdateProxy);
			proxy->SetAttribute("Type", (this->UpdateProxyType==CURLPROXY_HTTP)?"http":"socks5");
		}

		TiXmlElement *lastupdate=InsertChild(updates, "LastUpdate");
		if(this->LastUpdate) {
			string t=boost::lexical_cast<string>((int)this->LastUpdate);
			lastupdate->InsertEndChild(TiXmlText(t));
		}

		TiXmlElement *lastarchived=InsertChild(updates, "LastArchived");
		if(this->LastArchived) {
			string t=boost::lexical_cast<string>((int)this->LastArchived);
			lastarchived->InsertEndChild(TiXmlText(t));
		}

		TiXmlElement *laststarted=InsertChild(updates, "LastStarted");
		if(this->LastStarted) {
			string t=boost::lexical_cast<string>((int)this->LastStarted);
			laststarted->InsertEndChild(TiXmlText(t));
		}
	}

	{
		TiXmlElement *messages=InsertChild(root, "Messages");

		InsertChild(messages, "FirstBlock", this->FirstBlock);
		InsertChild(messages, "FirstHide", this->FirstHide);
	}

	{
		TiXmlElement *portset = InsertChild(root, "PortSet");

		InsertChild(portset, "AllowHttp", this->PortSet.AllowHttp);
		InsertChild(portset, "AllowFtp", this->PortSet.AllowFtp);
		InsertChild(portset, "AllowSmtp", this->PortSet.AllowSmtp);
		InsertChild(portset, "AllowPop3", this->PortSet.AllowPop3);

		TiXmlElement *profiles = InsertChild(portset, "Profiles");
		for (vector<PortProfile>::const_iterator pfit = this->PortSet.Profiles.begin(); pfit != this->PortSet.Profiles.end(); ++pfit) {
			TiXmlElement *profile = InsertChild(profiles, "Profile");

			PortProfile pf = (PortProfile) *pfit;

			InsertChild(profile, "Name", pf.Name);
			InsertChild(profile, "Enabled", pf.Enabled);
			InsertChild(profile, "Type", pf.Type);

			TiXmlElement *ports = InsertChild(profile, "Ports");
			for (vector<PortRange>::const_iterator pit = pf.Ports.begin(); pit != pf.Ports.end(); ++pit) {
				TiXmlElement *range = InsertChild(ports, "Range");
				InsertAttribute(range, (PortRange) *pit);
			}
		}
	}

	{
		TiXmlElement *lists=InsertChild(root, "Lists");

		for(vector<StaticList>::size_type i=0; i<this->StaticLists.size(); i++) {
			TiXmlElement *list=InsertChild(lists, "List");

			InsertChild(list, "File", TSTRING_UTF8(this->StaticLists[i].File.file_str()));
			InsertChild(list, "Type", (this->StaticLists[i].Type==List::Allow)?"allow":"block");
			InsertChild(list, "Description", this->StaticLists[i].Description);
			InsertChild(list, "Enabled", this->StaticLists[i].Enabled);
		}

		for(vector<DynamicList>::size_type i=0; i<this->DynamicLists.size(); i++) {
			TiXmlElement *list=InsertChild(lists, "List");

			InsertChild(list, "Url", this->DynamicLists[i].Url);
			InsertChild(list, "Type", (this->DynamicLists[i].Type==List::Allow)?"allow":"block");
			InsertChild(list, "Description", this->DynamicLists[i].Description);
			InsertChild(list, "Enabled", this->DynamicLists[i].Enabled);
			InsertChild(list, "FailedUpdate", this->DynamicLists[i].FailedUpdate);

			TiXmlElement *lastupdate=InsertChild(list, "LastUpdate");
			if(this->DynamicLists[i].LastUpdate) {
				string s=boost::lexical_cast<string>((int)this->DynamicLists[i].LastUpdate);
				lastupdate->InsertEndChild(TiXmlText(s));
			}

			TiXmlElement *lastdownload=InsertChild(list, "LastDownload");
			if(this->DynamicLists[i].LastDownload) {
				string s=boost::lexical_cast<string>((int)this->DynamicLists[i].LastDownload);
				lastdownload->InsertEndChild(TiXmlText(s));
			}
		}
	}

	{
		TiXmlElement *ibl=InsertChild(root, "I-Blocklist");

		InsertChild(ibl, "Username", this->IblUsername);
		InsertChild(ibl, "PIN", this->IblPin);
	}

	// First save to a temp-file
	std::wstring tempfile = _filename;
	tempfile += L".tmp";
	FILE *fp=_tfopen((path::base_dir()/tempfile.c_str()).file_str().c_str(), _T("w"));
	if(!fp)
	{
		TRACEERR("[Configuration] [Save]", L"Can't open file", GetLastError());
		throw runtime_error("unable to save configuration");
	}
	{
		boost::shared_ptr<void> ptr(fp, fclose);
		doc.Print(fp);
	}

	// Now, only after that's done should we overwrite the original
	path::move((path::base_dir()/tempfile.c_str()), (path::base_dir()/_filename), true);

	TRACEI("[Configuration] [Save]  < Leaving routine.");

} // End of Save()



//================================================================================================
//
//  EnsureUniqueId()
//
//    - Called by Configuration::Load(), and when updating lists
//
/// <summary>
///   Generates an anonymous unique id identifying this specific machine.  A server can key off of this to help
///   prevent a user from getting screwed out of his free weekly update, just because someone else behind his
///   same public-facing IP already updated this week.  
/// </summary>
/// <returns>
///   True if we generated a new ID, false if not.
/// </returns>
//
bool Configuration::EnsureUniqueId()
{
    if (!UniqueId.empty()) {
        return false;
    } else {
        // generate an anonymous id for this machine
        UUID uuid;
        TCHAR* buf;
        UuidCreate(&uuid);
        UuidToString(&uuid, (RPC_WSTR*)&buf);
        tstring strBuf = boost::str(tformat(_T("[Configuration] [EnsureUniqueId]    - created new anonymous id: [%1%]")) % buf );
        TRACEBUFI(strBuf);
        UniqueId = buf;
        return true;
    }

} // End of EnsureUniqueId()
