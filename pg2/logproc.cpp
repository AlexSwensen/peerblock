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
	
	CVS Info :
		$Author: phrostbyte $
		$Date: 2005/07/09 03:47:26 $
		$Revision: 1.68 $
*/

#include "stdafx.h"
#include "resource.h"
using namespace std;
using namespace sqlite3x;

#include "tracelog.h"
extern TraceLog g_tlog;


static const UINT TIMER_TEMPALLOW=2;
static const UINT TIMER_COMMITLOG=3;
static const UINT ID_LIST_ALLOWFOR15MIN=201;
static const UINT ID_LIST_ALLOWFORONEHOUR=202;
static const UINT ID_LIST_ALLOWPERM=203;
static const UINT ID_LIST_BLOCKFOR15MIN=204;
static const UINT ID_LIST_BLOCKFORONEHOUR=205;
static const UINT ID_LIST_BLOCKPERM=206;
static const UINT ID_LIST_COPYIP=207;

allowlist_t g_allowlist, g_blocklist;
threaded_sqlite3_connection g_con;

class LogFilterAction {
private:
	struct Action {
		time_t Time;
		std::string Name;
		unsigned int SourceIp, DestIp;
		unsigned short SourcePort, DestPort;
		int Protocol;
		bool Blocked;
	};

	tstring allowed, blocked;

	HWND hwnd,log;
	mutex mutex;
	bool usedb;
	
	static tstring format_ipport(const sockaddr &addr) {
		TRACEV("[LogFilterAction] [format_ipport]  > Entering routine.");
		TCHAR buf[256];
		DWORD buflen = 256;

		WSAAddressToString((sockaddr*)&addr, (addr.sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), 0, buf, &buflen);

		return buf;
	}

	static tstring current_time() {
		TRACEV("[LogFilterAction] [current_time]  > Entering routine.");
		time_t t=time(NULL);
		TCHAR buf[9];

		_tcsftime(buf, 9, _T("%H:%M:%S"), localtime(&t));

		return buf;
	}

	static tstring current_datetime() {
		TRACEV("[LogFilterAction] [current_datetime]  > Entering routine.");
		time_t t=time(NULL);
		TCHAR buf[21];

		_tcsftime(buf, 21, _T("%m/%d/%Y %H:%M:%S"), localtime(&t));

		return buf;
	}

	std::queue<Action> dbqueue;

public:
	LogFilterAction(HWND hwnd, HWND l, bool db=true) : hwnd(hwnd),log(l),usedb(db) 
	{
		TRACEV("[LogFilterAction] [LogFilterAction]  > Entering routine.");

		TCHAR chBuf[256];
		_stprintf_s(chBuf, sizeof(chBuf)/2, _T("[LogFilterAction] [LogFilterAction]    hwnd:[%p] log:[%p] usedb:[%d]"), hwnd, log, usedb);
		g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_VERBOSE);

		allowed=LoadString(IDS_ALLOWED);
		blocked=LoadString(IDS_BLOCKED);

		TRACEV("[LogFilterAction] [LogFilterAction]  < Leaving routine.");
	}
	
	~LogFilterAction() 
	{ 
		TRACEV("[LogFilterAction] [~LogFilterAction]  > Entering routine.");
		this->Commit(true); 
		TRACEV("[LogFilterAction] [~LogFilterAction]  < Leaving routine.");
	}



	void operator()(const pgfilter::action &action) 
	{
		TRACEV("[LogFilterAction] [operator()]  > Entering routine.");
		unsigned int sourceip, destip;
		unsigned int destport;
		
		if(action.src.addr.sa_family == AF_INET) {
			TRACEV("[LogFilterAction] [operator()]    src AF_INET");
			sourceip = htonl(action.src.addr4.sin_addr.s_addr);
		}
		else {
			TRACEV("[LogFilterAction] [operator()]    src NOT AF_INET");
			sourceip = 0;
		}

		if(action.dest.addr.sa_family == AF_INET) {
			TRACEV("[LogFilterAction] [operator()]    dest AF_INET");
			destip = htonl(action.dest.addr4.sin_addr.s_addr);
			destport = htons(action.dest.addr4.sin_port);
		}
		else {
			TRACEV("[LogFilterAction] [operator()]    dest NOT AF_INET");
			destip = 0;
			destport = htons(action.dest.addr6.sin6_port);
		}

		if((action.type == pgfilter::action::blocked || g_config.LogAllowed || g_config.ShowAllowed) && (sourceip!=INADDR_LOOPBACK || destip!=INADDR_LOOPBACK)) 
		{
			TRACEV("[LogFilterAction] [operator()]    allowed, not loopbacks");
			if(action.type == pgfilter::action::blocked && g_config.BlinkOnBlock!=Never && (g_config.BlinkOnBlock==OnBlock || destport==80 || destport==443))
			{
				TRACEV("[LogFilterAction] [operator()]    start blinking");
				g_blinkstart=GetTickCount();
			}

			if(g_config.ShowAllowed || action.type == pgfilter::action::blocked) 
			{
				TCHAR chBuf[256];
				_stprintf_s(chBuf, sizeof(chBuf)/2, _T("[LogFilterAction] [operator()]    updating list for window log:[%p]"), log);
				g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_VERBOSE);

				int count=ListView_GetItemCount(log);

				_stprintf_s(chBuf, sizeof(chBuf)/2, _T("[LogFilterAction] [operator()]    log:[%p], cnt:[%d], lsz:[%d]"), log, count, g_config.LogSize);
				g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_VERBOSE);

				while(count-- >= (int)g_config.LogSize) ListView_DeleteItem(log, g_config.LogSize-1);

				if(g_config.LogSize>0) {
					TRACEV("[LogFilterAction] [operator()]    logsize > 0");
					const tstring name=(action.label!=_T("n/a"))?action.label:_T("");
					const tstring source=format_ipport(action.src.addr);
					const tstring dest=format_ipport(action.dest.addr);
					const tstring time=current_time();
					const tstring &actionstr=(action.type == pgfilter::action::blocked) ? blocked : allowed;

					LVITEM lvi={0};
					lvi.mask=LVIF_TEXT|LVIF_PARAM;

					lvi.iSubItem=0;
					lvi.pszText=(LPTSTR)time.c_str();

					if(action.type == pgfilter::action::blocked) {
						TRACEV("[LogFilterAction] [operator()]    blocked action");
						if(action.protocol==IPPROTO_TCP && (destport==80 || destport==443))
							lvi.lParam=(LPARAM)2;
						else lvi.lParam=(LPARAM)1;
					}
					else 
					{
						TRACEV("[LogFilterAction] [operator()]    not blocked action");
						lvi.lParam=(LPARAM)0;
					}

					ListView_InsertItem(log, &lvi);

					lvi.mask=LVIF_TEXT;
					lvi.iSubItem=1;
					lvi.pszText=(LPTSTR)name.c_str();
					ListView_SetItem(log, &lvi);

					lvi.iSubItem=2;
					lvi.pszText=(LPTSTR)source.c_str();
					ListView_SetItem(log, &lvi);

					lvi.iSubItem=3;
					lvi.pszText=(LPTSTR)dest.c_str();
					ListView_SetItem(log, &lvi);

					lvi.iSubItem=4;
					switch(action.protocol) {
						case IPPROTO_ICMP: lvi.pszText=_T("ICMP"); break;
						case IPPROTO_IGMP: lvi.pszText=_T("IGMP"); break;
						case IPPROTO_GGP: lvi.pszText=_T("Gateway^2"); break;
						case IPPROTO_TCP: lvi.pszText=_T("TCP"); break;
						case IPPROTO_PUP: lvi.pszText=_T("PUP"); break;
						case IPPROTO_UDP: lvi.pszText=_T("UDP"); break;
						case IPPROTO_IDP: lvi.pszText=_T("XNS IDP"); break;
						case IPPROTO_ND: lvi.pszText=_T("NetDisk"); break;
						default: lvi.pszText=_T("Unknown"); break;
					}
					ListView_SetItem(log, &lvi);

					lvi.iSubItem=5;
					lvi.pszText=(LPTSTR)actionstr.c_str();
					ListView_SetItem(log, &lvi);
				}
			}

			if(usedb && action.src.addr.sa_family == AF_INET && action.dest.addr.sa_family == AF_INET) 
			{
				TRACEV("[LogFilterAction] [operator()]    using db, src and dest not af_inet");
				if((action.type != pgfilter::action::blocked && g_config.LogAllowed) || (action.type == pgfilter::action::blocked && g_config.LogBlocked)) 
				{
					TRACEV("[LogFilterAction] [operator()]    logging to dbqueue");
					LogFilterAction::Action a;
					::time(&a.Time);
					a.Name=TSTRING_UTF8(action.label);
					a.SourceIp=htonl(action.src.addr4.sin_addr.s_addr);
					a.SourcePort=htons(action.src.addr4.sin_port);
					a.DestIp=htonl(action.dest.addr4.sin_addr.s_addr);
					a.DestPort=htons(action.dest.addr4.sin_port);
					a.Protocol=action.protocol;
					a.Blocked=action.type == pgfilter::action::blocked;

					mutex::scoped_lock lock(this->mutex);
					this->dbqueue.push(a);
				}
			}
		}
		TRACEV("[LogFilterAction] [operator()]  < Leaving routine.");

	} // End of operator()


private:
	void _Commit(bool force) {
		mutex::scoped_lock lock(this->mutex);

		if(!this->dbqueue.empty()) {
			sqlite3_try_lock lock(g_con, true);
			if(force && !lock.is_locked()) lock.enter();

			if(lock.is_locked()) {
				sqlite3_command cmd_getnameid(g_con, "select id from t_names where name=?;");
				sqlite3_command cmd_setnameid(g_con, "insert into t_names(name) values(?);");
				sqlite3_command cmd_history(g_con, "insert into t_history values(julianday(?, 'unixepoch'),?,?,?,?,?,?,?);");

				for(; !this->dbqueue.empty(); this->dbqueue.pop()) {
					const Action &a=this->dbqueue.front();

					try {
						cmd_getnameid.bind(1, a.Name);
						sqlite3_reader reader=cmd_getnameid.executereader();

						long long nameid;

						if(reader.read()) {
							nameid=reader.getint64(0);
							reader.close();
						}
						else {
							reader.close();

							cmd_setnameid.bind(1, a.Name);
							cmd_setnameid.executenonquery();

							nameid=g_con.insertid();
						}

						cmd_history.bind(1, (int)a.Time);
						cmd_history.bind(2, nameid);
						cmd_history.bind(3, (int)a.SourceIp);
						cmd_history.bind(4, (int)a.SourcePort);
						cmd_history.bind(5, (int)a.DestIp);
						cmd_history.bind(6, (int)a.DestPort);
						cmd_history.bind(7, a.Protocol);
						cmd_history.bind(8, a.Blocked?1:0);
						cmd_history.executenonquery();
					}
					catch(exception &ex) {
						ExceptionBox(NULL, ex, __FILE__, __LINE__);
						return;
					}
				}

				lock.commit();
			}
		}
	}

public:
	void Commit(bool force=false) {
		try {
			_Commit(force);
		}
		catch(exception &ex) {
			UncaughtExceptionBox(NULL, ex, __FILE__, __LINE__);
		}
		catch(...) {
			UncaughtExceptionBox(NULL, __FILE__, __LINE__);
		}
	}
};

static boost::shared_ptr<LogFilterAction> g_log;



static void UpdateStatus(HWND hwnd) 
{
	TRACEI("[LogProc] [UpdateStatus]  > Entering routine.");
	tstring enable, http, blocking, httpstatus, update, lastupdate;

	enable=LoadString(g_config.Block?IDS_DISABLE:IDS_ENABLE);
	http=LoadString(g_config.BlockHttp?IDS_ALLOWHTTP:IDS_BLOCKHTTP);

	if(g_config.Block) blocking=boost::str(tformat(LoadString(IDS_PGACTIVE)) % g_filter->blockcount());
	else blocking=LoadString(IDS_PGDISABLED);

	httpstatus=boost::str(tformat(LoadString(IDS_HTTPIS)) % LoadString(g_config.BlockHttp?IDS_BLOCKED:IDS_ALLOWED));

	{
		unsigned int lists=(unsigned int)(g_config.StaticLists.size()+g_config.DynamicLists.size());
		unsigned int uptodate=(unsigned int)g_config.StaticLists.size();
		unsigned int failed=0;
		unsigned int disabled=0;

		TRACEV("[LogProc] [UpdateStatus]    counting num disabled static lists");
		for(vector<StaticList>::size_type i=0; i<g_config.StaticLists.size(); i++)
			if(!g_config.StaticLists[i].Enabled) disabled++;

		TRACEV("[LogProc] [UpdateStatus]    counting dynamic lists");
		for(vector<DynamicList>::size_type i=0; i<g_config.DynamicLists.size(); i++) {
			bool exists=path::exists(g_config.DynamicLists[i].File());

			if(exists && time(NULL)-g_config.DynamicLists[i].LastUpdate < 604800)
				uptodate++;
			if(!exists || !g_config.DynamicLists[i].Enabled) disabled++;
			if(g_config.DynamicLists[i].FailedUpdate) failed++;
		}

		update=boost::str(tformat(LoadString(IDS_UPDATESTATUS)) % lists % uptodate % failed % disabled);
		TRACEV("[LogProc] [UpdateStatus]    done generating list metrics");
	}

	if(g_config.LastUpdate) 
	{
		TRACEV("[LogProc] [UpdateStatus]    g_config.LastUpdate: [true]");
		time_t dur=time(NULL)-g_config.LastUpdate;

		if(dur<604800) 
		{
			TRACEV("[LogProc] [UpdateStatus]    dur < 604800");
			TCHAR buf[64];
			_tcsftime(buf, 64, _T("%#x"), localtime(&g_config.LastUpdate));

			lastupdate=boost::str(tformat(LoadString(IDS_LISTSUPTODATE))%buf);
		}
		else if(dur>=g_config.UpdateInterval*86400 && (g_config.UpdatePeerGuardian || g_config.UpdateLists)) 
		{
			TRACEI("[LogProc] [UpdateStatus]    need to update lists");
			UpdateLists(hwnd);
			TRACEI("[LogProc] [UpdateStatus]    lists updated");

			TCHAR buf[64];
			_tcsftime(buf, 64, _T("%#x"), localtime(&g_config.LastUpdate));

			lastupdate=boost::str(tformat(LoadString(IDS_LISTSUPTODATE))%buf);
		}
		else {
			TRACEV("[LogProc] [UpdateStatus]    didn't update lists");
			lastupdate=boost::str(tformat(LoadString(IDS_LISTSNOTUPTODATE))%(dur/604800));
		}
	}
	else {
		TRACEI("[LogProc] [UpdateStatus]    g_config.LastUpdate: [false]");
		lastupdate=LoadString(IDS_LISTSNOTUPDATED);
	}

	SetWindowText(GetDlgItem(hwnd, IDC_ENABLE), enable.c_str());
	SetWindowText(GetDlgItem(hwnd, IDC_HTTP), http.c_str());
	SetWindowText(GetDlgItem(hwnd, IDC_ENABLED_STATUS), blocking.c_str());
	SetWindowText(GetDlgItem(hwnd, IDC_HTTP_STATUS), httpstatus.c_str());
	SetWindowText(GetDlgItem(hwnd, IDC_UPDATE_STATUS), update.c_str());
	SetWindowText(GetDlgItem(hwnd, IDC_LAST_UPDATE), lastupdate.c_str());

	TRACEV("[LogProc] [UpdateStatus]  < Leaving routine.");

} // End of UpdateStatus()



static void Log_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) 
{
	switch(id) {

		case IDC_LISTS: 
		{
			TRACEV("[LogProc] [Log_OnCommand]  IDC_LISTS");
			INT_PTR ret=DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_LISTS), hwnd, Lists_DlgProc);
			if(ret&LISTS_NEEDUPDATE) {
				UpdateLists(hwnd);
				SendMessage(hwnd, WM_TIMER, TIMER_UPDATE, 0);

				g_config.Save();
			}
			if(g_filter.get() && ret&LISTS_NEEDRELOAD) LoadLists(hwnd);
		} break;

		case IDC_UPDATE: 
		{
			TRACEV("[LogProc] [Log_OnCommand]  IDC_UPDATE");
			int ret=UpdateLists(hwnd);

			g_config.Save();

			if(g_filter.get()) {
				if(ret>0) LoadLists(hwnd);
				UpdateStatus(hwnd);
			}
		} break;

		case IDC_HISTORY:
			TRACEV("[LogProc] [Log_OnCommand]  IDC_HISTORY");
			DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_HISTORY), hwnd, History_DlgProc);
			break;

		case IDC_CLEAR:
			TRACEV("[LogProc] [Log_OnCommand]  IDC_CLEAR");
			ListView_DeleteAllItems(GetDlgItem(hwnd, IDC_LIST));
			break;

		case IDC_ENABLE:
			TRACEV("[LogProc] [Log_OnCommand]  IDC_ENABLE");
			SetBlock(!g_config.Block);
			break;

		case IDC_HTTP:
			TRACEV("[LogProc] [Log_OnCommand]  IDC_HTTP");
			SetBlockHttp(!g_config.BlockHttp);
			break;
	}
}



static void Log_OnDestroy(HWND hwnd) 
{
	TRACEV("[LogProc] [Log_OnDestroy]  > Entering routine.");
	HWND list=GetDlgItem(hwnd, IDC_LIST);

	SaveListColumns(list, g_config.LogColumns);

	TRACEV("[LogProc] [Log_OnDestroy]    setting filter action-function to nothing");
	g_filter->setactionfunc();
	TRACEV("[LogProc] [Log_OnInitDialog]    setting g_log to empty LogFilterAction");
	g_log=boost::shared_ptr<LogFilterAction>();

	TRACEV("[LogProc] [Log_OnDestroy]  < Leaving routine.");

} // End of Log_OnDestroy()



static void InsertColumn(HWND hList, INT iSubItem, INT iWidth, UINT idText) {
	LVCOLUMN lvc={0};
	
	lvc.mask=LVCF_FMT|LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM;
	lvc.fmt=LVCFMT_LEFT;
	lvc.cx=iWidth;
	lvc.iSubItem=iSubItem;

	tstring buf=LoadString(idText);
	lvc.pszText=(LPTSTR)buf.c_str();

	ListView_InsertColumn(hList, iSubItem, &lvc);
}



static BOOL Log_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) 
{
	TRACEV("[LogProc] [Log_OnInitDialog]  > Entering routine.");
	HWND list=GetDlgItem(hwnd, IDC_LIST);
	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT|LVS_EX_LABELTIP);

	InsertColumn(list, 0, g_config.LogColumns[0], IDS_TIME);
	InsertColumn(list, 1, g_config.LogColumns[1], IDS_RANGE);
	InsertColumn(list, 2, g_config.LogColumns[2], IDS_SOURCE);
	InsertColumn(list, 3, g_config.LogColumns[3], IDS_DESTINATION);
	InsertColumn(list, 4, g_config.LogColumns[4], IDS_PROTOCOL);
	InsertColumn(list, 5, g_config.LogColumns[5], IDS_ACTION);

	{
		try {
			g_con.open((path::base_dir()/_T("history.db")).c_str());
			g_con.setbusytimeout(5000);
			g_con.executenonquery("pragma page_size=4096;");

			{
				TRACEV("[LogProc] [Log_OnInitDialog]    acquiring sqlite3 lock");
				sqlite3_lock lock(g_con, true);
				TRACEV("[LogProc] [Log_OnInitDialog]    acquired sqlite3 lock");
			
				if(g_con.executeint("select count(*) from sqlite_master where name='t_names';")==0)
					g_con.executenonquery("create table t_names(id integer primary key, name text unique);");

				if(g_con.executeint("select count(*) from sqlite_master where name='t_history';")==0)
					g_con.executenonquery("create table t_history(time real, nameid integer, source integer, sourceport integer, destination integer, destport integer, protocol integer, action integer);");

				if(g_con.executeint("select count(*) from sqlite_master where name='i_time';")==0)
					g_con.executenonquery("create index i_time on t_history(time);");

				if(g_con.executeint("select count(*) from sqlite_master where name='i_actiontime';")==0)
					g_con.executenonquery("create index i_actiontime on t_history(action,time);");

				TRACEV("[LogProc] [Log_OnInitDialog]    committing sqlite3 lock");
				lock.commit();
				TRACEV("[LogProc] [Log_OnInitDialog]    committed sqlite3 lock");
			}
			TRACEV("[LogProc] [Log_OnInitDialog]    setting g_log to new LogFilterAction");
			g_log=boost::shared_ptr<LogFilterAction>(new LogFilterAction(hwnd, list));
			SetTimer(hwnd, TIMER_COMMITLOG, 15000, NULL);
		}
		catch(database_error&) {
			TRACEE("[LogProc] [Log_OnInitDialog]    ERROR:  Caught database_error");
			MessageBox(hwnd, IDS_HISTORYOPEN, IDS_HISTORYERR, MB_ICONERROR|MB_OK);
			EnableWindow(GetDlgItem(hwnd, IDC_HISTORY), FALSE);
			g_log=boost::shared_ptr<LogFilterAction>(new LogFilterAction(hwnd, list, false));
		}
	}

	g_filter->setactionfunc(boost::ref(*g_log.get()));

	TRACEI("[LogProc] [Log_OnInitDialog]    updating status");
	UpdateStatus(hwnd);

	SetTimer(hwnd, TIMER_UPDATE, 30000, NULL);
	SetTimer(hwnd, TIMER_TEMPALLOW, 30000, NULL);

	TRACEV("[LogProc] [Log_OnInitDialog]  < Leaving routine.");
	return TRUE;

} // End of Log_OnInitDialog()



static unsigned int ParseIp(LPCTSTR str) {
	unsigned int ipa, ipb, ipc, ipd;

	if(_stscanf(str, _T("%u.%u.%u.%u"), &ipa, &ipb, &ipc, &ipd)==4) {
		union {
			unsigned int ip;
			unsigned char bytes[4];
		};

		bytes[0]=ipd;
		bytes[1]=ipc;
		bytes[2]=ipb;
		bytes[3]=ipa;

		return ip;
	}

	return 0;
}



static INT_PTR Log_OnNotify(HWND hwnd, int idCtrl, NMHDR *nmh) 
{
	TRACED("[LogProc] [Log_OnNotify]  > Entering routine.");
	if(nmh->code==NM_CUSTOMDRAW && nmh->idFrom==IDC_LIST && g_config.ColorCode) 
	{
		TRACED("[LogProc] [Log_OnNotify]    custom draw list colorcode");
		NMLVCUSTOMDRAW *cd=(NMLVCUSTOMDRAW*)nmh;
		switch(cd->nmcd.dwDrawStage) {
			case CDDS_PREPAINT:
				SetWindowLongPtr(hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
				break;
			case CDDS_ITEMPREPAINT: {
				Color c;
				
				switch((int)cd->nmcd.lItemlParam) {
					case 0:
						c=g_config.AllowedColor;
						break;
					case 1:
						c=g_config.BlockedColor;
						break;
					case 2:
						c=g_config.HttpColor;
						break;
					default: __assume(0);
				}

				cd->clrText=c.Text;
				cd->clrTextBk=c.Background;
				SetWindowLongPtr(hwnd, DWLP_MSGRESULT, CDRF_NEWFONT);
			} break;
			default:
				SetWindowLongPtr(hwnd, DWLP_MSGRESULT, CDRF_DODEFAULT);
		}
		TRACED("[LogProc] [Log_OnNotify]  < Leaving routine (TRUE).");
		return TRUE;
	}
	else if(nmh->code==NM_RCLICK && nmh->idFrom==IDC_LIST) 
	{
		TRACED("[LogProc] [Log_OnNotify]    right-click on list");
		NMITEMACTIVATE *nmia=(NMITEMACTIVATE*)nmh;

		if(nmia->iItem!=-1) {
			set<unsigned int> ips;
			GetLocalIps(ips, LOCALIP_ADAPTER);

			LVITEM lvi={0};
			lvi.mask=LVIF_PARAM;
			lvi.iItem=nmia->iItem;
			ListView_GetItem(nmh->hwndFrom, &lvi);

			TCHAR text[32], name[256];
			ListView_GetItemText(nmh->hwndFrom, nmia->iItem, 2, text, 32);

			TCHAR *end=_tcschr(text, _T(':'));
			if(end) *end=_T('\0');

			unsigned int allowip=ParseIp(text);
			if(ips.find(allowip)!=ips.end()) {
				ListView_GetItemText(nmh->hwndFrom, nmia->iItem, 3, text, 32);

				end=_tcschr(text, _T(':'));
				if(end) *end=_T('\0');

				allowip=ParseIp(text);
			}

			ListView_GetItemText(nmh->hwndFrom, nmia->iItem, 1, name, 256);

			HMENU menu=CreatePopupMenu();

			tstring str;
			if(lvi.lParam) {
				str=boost::str(tformat(LoadString(IDS_ALLOWXFOR15MIN))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_ALLOWFOR15MIN, str.c_str());

				str=boost::str(tformat(LoadString(IDS_ALLOWXFORONEHOUR))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_ALLOWFORONEHOUR, str.c_str());

				str=boost::str(tformat(LoadString(IDS_ALLOWXPERM))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_ALLOWPERM, str.c_str());
			}
			else {
				str=boost::str(tformat(LoadString(IDS_BLOCKXFOR15MIN))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_BLOCKFOR15MIN, str.c_str());

				str=boost::str(tformat(LoadString(IDS_BLOCKXFORONEHOUR))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_BLOCKFORONEHOUR, str.c_str());

				str=boost::str(tformat(LoadString(IDS_BLOCKXPERM))%text);
				AppendMenu(menu, MF_STRING, ID_LIST_BLOCKPERM, str.c_str());
			}

			AppendMenu(menu, MF_SEPARATOR, 0, NULL);

			str=boost::str(tformat(LoadString(IDS_COPYXTOCLIPBOARD))%text);
			AppendMenu(menu, MF_STRING, ID_LIST_COPYIP, str.c_str());

			RECT rect;
			GetWindowRect(nmh->hwndFrom, &rect);

			SetForegroundWindow(hwnd);
			UINT ret=TrackPopupMenuEx(menu, TPM_TOPALIGN|TPM_RETURNCMD, rect.left+nmia->ptAction.x, rect.top+nmia->ptAction.y, hwnd, NULL);

			DestroyMenu(menu);

			wstring name_wchar=TSTRING_WCHAR(name);
			p2p::range r(name_wchar, allowip, allowip);

			switch(ret) {
				case ID_LIST_ALLOWFOR15MIN:
					g_tempallow.insert(r);
					g_allowlist.push_back(make_pair(time(NULL)+900, r));
					LoadLists(hwnd);
					break;
				case ID_LIST_ALLOWFORONEHOUR:
					g_tempallow.insert(r);
					g_allowlist.push_back(make_pair(time(NULL)+3600, r));
					LoadLists(hwnd);
					break;
				case ID_LIST_ALLOWPERM: {
					path dir=path::base_dir()/_T("lists");

					if(!path::exists(dir))
						path::create_directory(dir);

					path file=dir/_T("permallow.p2b");

					p2p::list list;
					LoadList(file, list);

					list.insert(p2p::range(name_wchar, allowip, allowip));
					list.optimize();

					list.save(TSTRING_MBS(file.file_str()), p2p::list::file_p2b);

					bool found=false;

					for(vector<StaticList>::size_type i=0; i<g_config.StaticLists.size(); i++) {
						if(g_config.StaticLists[i].File==_T("lists\\permallow.p2b")) {
							found=true;
							break;
						}
					}

					if(!found) {
						StaticList list;
						list.Type=List::Allow;
						list.Description=LoadString(IDS_PERMALLOWS);
						list.File=_T("lists\\permallow.p2b");

						g_config.StaticLists.push_back(list);
					}

					LoadLists(hwnd);
				} break;
				case ID_LIST_BLOCKFOR15MIN:
					g_tempblock.insert(r);
					g_blocklist.push_back(make_pair(time(NULL)+900, r));
					LoadLists(hwnd);
					break;
				case ID_LIST_BLOCKFORONEHOUR:
					g_tempblock.insert(r);
					g_blocklist.push_back(make_pair(time(NULL)+3600, r));
					LoadLists(hwnd);
					break;
				case ID_LIST_BLOCKPERM: {
					path dir=path::base_dir()/_T("lists");

					if(!path::exists(dir))
						path::create_directory(dir);

					path file=dir/_T("permblock.p2b");

					p2p::list list;
					LoadList(file, list);

					list.insert(p2p::range(name_wchar, allowip, allowip));
					list.optimize();

					list.save(TSTRING_MBS(file.file_str()), p2p::list::file_p2b);

					bool found=false;

					for(vector<StaticList>::size_type i=0; i<g_config.StaticLists.size(); i++) {
						if(g_config.StaticLists[i].File==_T("lists\\permblock.p2b")) {
							found=true;
							break;
						}
					}

					if(!found) {
						StaticList list;
						list.Type=List::Block;
						list.Description=LoadString(IDS_PERMBLOCKS);
						list.File=_T("lists\\permblock.p2b");

						g_config.StaticLists.push_back(list);
					}

					LoadLists(hwnd);
				} break;
				case ID_LIST_COPYIP:
					if(OpenClipboard(hwnd)) {
						EmptyClipboard();

						size_t len=(_tcslen(text)+1)*sizeof(TCHAR);

						HGLOBAL buf=GlobalAlloc(GMEM_MOVEABLE, len);

						CopyMemory(GlobalLock(buf), text, len);
						GlobalUnlock(buf);

#ifdef _UNICODE
						SetClipboardData(CF_UNICODETEXT, buf);
#else
						SetClipboardData(CF_TEXT, buf);
#endif
						CloseClipboard();
					}
					break;
			}
		}
	}
	else
	{
		TRACED("[LogProc] [Log_OnNotify]    doing nothing");
	}

	TRACED("[LogProc] [Log_OnNotify]  < Leaving routine (0).");
	return 0;
}



static void Log_OnSize(HWND hwnd, UINT state, int cx, int cy) 
{
	TRACEV("[LogProc] [Log_OnSize]    Entering routine.");
	HWND enable=GetDlgItem(hwnd, IDC_ENABLE);
	HWND lists=GetDlgItem(hwnd, IDC_LISTS);
	HWND update=GetDlgItem(hwnd, IDC_UPDATE);
	HWND http=GetDlgItem(hwnd, IDC_HTTP);
	HWND history=GetDlgItem(hwnd, IDC_HISTORY);
	HWND clear=GetDlgItem(hwnd, IDC_CLEAR);
	HWND enabledstatus=GetDlgItem(hwnd, IDC_ENABLED_STATUS);
	HWND httpstatus=GetDlgItem(hwnd, IDC_HTTP_STATUS);
	HWND updatestatus=GetDlgItem(hwnd, IDC_UPDATE_STATUS);
	HWND lastupdate=GetDlgItem(hwnd, IDC_LAST_UPDATE);
	HWND log=GetDlgItem(hwnd, IDC_LIST);

	RECT rc;
	GetWindowRect(enable, &rc);

	int midwidth=cx-20-(rc.right-rc.left)*2;
	int halfwidth=(midwidth-5)/2;
	
	HDWP dwp=BeginDeferWindowPos(11);

	DeferWindowPos(dwp, enable, NULL, 5, 5, 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	DeferWindowPos(dwp, lists, NULL, 5, 10+(rc.bottom-rc.top), 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	DeferWindowPos(dwp, update, NULL, 5, 15+(rc.bottom-rc.top)*2, 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	
	DeferWindowPos(dwp, http, NULL, cx-5-(rc.right-rc.left), 5, 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	DeferWindowPos(dwp, history, NULL, cx-5-(rc.right-rc.left), 10+(rc.bottom-rc.top), 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	DeferWindowPos(dwp, clear, NULL, cx-5-(rc.right-rc.left), 15+(rc.bottom-rc.top)*2, 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);

	DeferWindowPos(dwp, enabledstatus, NULL, 10+(rc.right-rc.left), 5, halfwidth, (rc.bottom-rc.top), SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	DeferWindowPos(dwp, httpstatus, NULL, 15+(rc.right-rc.left)+halfwidth, 5, halfwidth, (rc.bottom-rc.top), SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	DeferWindowPos(dwp, updatestatus, NULL, 10+(rc.right-rc.left), 10+(rc.bottom-rc.top), midwidth, (rc.bottom-rc.top), SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	DeferWindowPos(dwp, lastupdate, NULL, 10+(rc.right-rc.left), 15+(rc.bottom-rc.top)*2, midwidth, (rc.bottom-rc.top), SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);

	DeferWindowPos(dwp, log, NULL, 5, 20+(rc.bottom-rc.top)*3, cx-10, cy-25-(rc.bottom-rc.top)*3, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);

	EndDeferWindowPos(dwp);
}

class TempAllowRemovePred {
private:
	p2p::list &l;
	time_t current;
	bool changed;

public:
	TempAllowRemovePred(p2p::list &list) : l(list),current(time(NULL)),changed(false) {}

	~TempAllowRemovePred() {
		if(changed) LoadLists(NULL);
	}

	bool operator()(const allowlist_t::value_type &v) {
		if((v.first-current)<0) {
			changed=true;
			l.erase(v.second);
			return true;
		}
		else return false;
	}
};

static void Log_OnTimer(HWND hwnd, UINT id) 
{
	TRACEV("[LogProc] [Log_OnTimer]  > Entering routine.");
	switch(id) {
		case TIMER_UPDATE:
			TRACEV("[LogProc] [Log_OnTimer]    TIMER_UPDATE");
			if(g_config.UpdateInterval>0 && (time(NULL)-g_config.LastUpdate >= ((time_t)g_config.UpdateInterval)*86400))
			{
				TRACEV("[LogProc] [Log_OnTimer]    updating lists");
				UpdateLists(NULL);
			}
			UpdateStatus(hwnd);
			break;
		case TIMER_TEMPALLOW:
			TRACEV("[LogProc] [Log_OnTimer]    TIMER_TEMPALLOW");
			g_allowlist.remove_if(TempAllowRemovePred(g_tempallow));
			g_blocklist.remove_if(TempAllowRemovePred(g_tempblock));
			break;
		case TIMER_COMMITLOG:
			TRACEV("[LogProc] [Log_OnTimer]    TIMER_COMMITLOG");
			g_log->Commit();
			break;
	}
	TRACEV("[LogProc] [Log_OnTimer]  < Leaving routine.");
}

INT_PTR CALLBACK Log_DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	try {
		TCHAR chBuf[256];
		_stprintf_s(chBuf, sizeof(chBuf)/2, _T("[LogProc] [Log_DlgProc]    processing hwnd:[%p] msg:[%d]"), hwnd, msg);
		g_tlog.LogMessage(chBuf, TRACELOG_LEVEL_DEBUG);

		switch(msg) {
			HANDLE_MSG(hwnd, WM_COMMAND, Log_OnCommand);
			HANDLE_MSG(hwnd, WM_DESTROY, Log_OnDestroy);
			HANDLE_MSG(hwnd, WM_INITDIALOG, Log_OnInitDialog);
			HANDLE_MSG(hwnd, WM_NOTIFY, Log_OnNotify);
			HANDLE_MSG(hwnd, WM_SIZE, Log_OnSize);
			HANDLE_MSG(hwnd, WM_TIMER, Log_OnTimer);
			case WM_LOG_HOOK:
			case WM_LOG_RANGES:
				TRACEV("[LogProc] [Log_DlgProc]    WM_LOG_HOOK or WM_LOG_RANGES");
				UpdateStatus(hwnd);
				return 1;
			default: return 0;
		}
	}
	catch(exception &ex) {
		UncaughtExceptionBox(hwnd, ex, __FILE__, __LINE__);
		return 0;
	}
	catch(...) {
		UncaughtExceptionBox(hwnd, __FILE__, __LINE__);
		return 0;
	}
}
