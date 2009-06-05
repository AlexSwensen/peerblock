/*
	Copyright (C) 2004-2005 Cory Nelson

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
		$Date: 2005/07/16 00:43:00 $
		$Revision: 1.94 $
*/

#include "stdafx.h"
#include "resource.h"
using namespace std;

#define DO_STRINGIFY(x) #x
#define STRINGIFY(x) DO_STRINGIFY(x)

#define BUILDDATE 70602

#ifdef _WIN32_WINNT
#ifdef _WIN64
// nt x64 build
#define BUILDTYPE 30
#else
// nt build
#define BUILDTYPE 20
#endif
#else
// 9x build
#define BUILDTYPE 10
#endif

#define BUILDNUM (BUILDTYPE*100000+BUILDDATE)
#define BUILDSTR STRINGIFY(BUILDTYPE) STRINGIFY(BUILDDATE)

static const char *g_agent="PeerGuardian/2.0";
static const LPCTSTR g_homepage=_T("http://peerguardian.sourceforge.net");
static const LPCTSTR g_updateserver=_T("http://peerguardian.sourceforge.net");

const unsigned int g_build=BUILDNUM;
static const char *g_updateurl="http://peerguardian.sourceforge.net/update.php?build="BUILDSTR;

static const UINT TIMER_COUNTDOWN=1;
static unsigned short g_countdown;
static HWND g_updater=NULL;

class UpdateThread {
private:
	struct HandleData {
		char errbuf[CURL_ERROR_SIZE];
		UpdateThread *ut;
		string url;
		path tempfile;
		FILE *fp;
		DynamicList *list;
		int index;
		bool finished;
		double progress;

		HandleData(UpdateThread *ut) : ut(ut),fp(NULL),list(NULL),finished(false),progress(0.0) {
			errbuf[0]='\0';
		}
	};

	set<unsigned int> allowed;

	vector<HandleData*> hdata;
	double progressmod, maxprogress;

	HWND hwnd, list, progress;
	bool autoupdate;

	static void preconnect_func(void *clientp, sockaddr *addr, int addrlen) {
		if(addrlen >= sizeof(sockaddr_in) && addr->sa_family == AF_INET) {
			unsigned int ip = htonl(((sockaddr_in*)addr)->sin_addr.s_addr);

			set<unsigned int> &allowed = *(set<unsigned int>*)clientp;

			set<unsigned int>::iterator iter = allowed.lower_bound(ip);
			if(iter == allowed.end() || *iter != ip) {
				allowed.insert(iter, ip);

				if(g_filter) {
					p2p::list allow;

					for(iter = allowed.begin(); iter != allowed.end(); ++iter) {
						allow.insert(p2p::range(L"Auto-allow for updating", *iter, *iter));
					}

					allow.optimize(true);

					if(g_filter) {
						g_filter->setranges(allow, false);
					}
				}
			}
		}
	}

	static size_t append_func(void *data, size_t size, size_t count, void *arg) {
		size_t ret=size*count;

		((string*)arg)->append((const char*)data, ret);

		return ret;
	}

	static int progress_func(void *arg, double total, double pos, double, double) {
		HandleData *data=(HandleData*)arg;

		if(total>0.0) {
			data->progress=min(pos/total*100.0, 100.0);
			if(data->ut->progress) data->ut->UpdateProgress();
		}

		if(data->ut->list) {
			tstring str;

			if(total>0.0) {
				int pcnt=(int)data->progress;
				str=boost::str(tformat(LoadString(IDS_DOWNLOADINGPCNT))%pcnt);
			}
			else str=LoadString(IDS_DOWNLOADING);

			LVITEM lvi={0};
			lvi.mask=LVIF_TEXT;
			lvi.iItem=data->index;
			lvi.iSubItem=2;
			lvi.pszText=(LPTSTR)str.c_str();
			ListView_SetItem(data->ut->list, &lvi);
		}

		return 0;
	}

	void UpdateProgress() {
		double total=this->progressmod;

		for(vector<HandleData*>::size_type i=0; i<hdata.size(); i++)
			total+=hdata[i]->progress;
		
		total=min(total, maxprogress);

		SendMessage(progress, PBM_SETPOS, (WPARAM)(int)total, 0);
	}

	static DWORD GetFileSize(LPCTSTR file) {
		HANDLE fp=CreateFile(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if(fp==INVALID_HANDLE_VALUE) throw win32_error("CreateFile");

		DWORD ret=::GetFileSize(fp, NULL);
		if(ret==INVALID_FILE_SIZE) throw win32_error("GetFileSize");

		CloseHandle(fp);

		return ret;
	}

public:
	int changes;
	bool aborted;

	UpdateThread() {}
	UpdateThread(HWND hwnd, HWND list, HWND progress, bool autoupdate)
		: hwnd(hwnd), list(list), progress(progress), autoupdate(autoupdate),changes(0),aborted(false),progressmod(0.0) {}

	~UpdateThread() {
		if(allowed.size() > 0 && g_filter) {
			g_filter->setranges(p2p::list(), false);
		}
	}

	int _Process() {
		unsigned short total=0;
		bool updatepg=false;
		bool updatelists=false;

		if(g_config.UpdatePeerGuardian) total+=1;
		if(g_config.UpdateLists) {
			for(vector<DynamicList>::size_type i=0; i<g_config.DynamicLists.size(); i++) {
				if(g_config.DynamicLists[i].Enabled) {
					total++;
					updatelists=true;
				}
			}
		}

		if(total>0) {
			const string proxy=TSTRING_UTF8(g_config.UpdateProxy);

			string build;
			vector<CURL*> handles;

			LVITEM lvi={0};
			lvi.mask=LVIF_TEXT;

			if(progress) {
				SendMessage(progress, PBM_SETPOS, 0, 0);
				SendMessage(progress, PBM_SETRANGE, 0, MAKELPARAM(0, total*100));
				maxprogress=total*100.0;
			}

			curl_global_init(CURL_GLOBAL_WIN32);

			CURLM *multi=curl_multi_init();

			///////////////////////////////////////////////
			/// Update PeerGuardian

			if(g_config.UpdatePeerGuardian) {
				HandleData *data=new HandleData(this);

				CURL *site=curl_easy_init();
				if(site) {
					curl_easy_setopt(site, CURLOPT_PRECONNECT, preconnect_func);
					curl_easy_setopt(site, CURLOPT_PRECONNECTDATA, &allowed);
					curl_easy_setopt(site, CURLOPT_FOLLOWLOCATION, 1);
					curl_easy_setopt(site, CURLOPT_PROGRESSFUNCTION, progress_func);
					curl_easy_setopt(site, CURLOPT_PROGRESSDATA, data);
					curl_easy_setopt(site, CURLOPT_NOPROGRESS, 0);
					curl_easy_setopt(site, CURLOPT_USERAGENT, g_agent);
					curl_easy_setopt(site, CURLOPT_URL, g_updateurl);
					curl_easy_setopt(site, CURLOPT_WRITEFUNCTION, append_func);
					curl_easy_setopt(site, CURLOPT_WRITEDATA, &build);
					curl_easy_setopt(site, CURLOPT_FAILONERROR, 1);
					curl_easy_setopt(site, CURLOPT_PRIVATE, data);
					curl_easy_setopt(site, CURLOPT_ERRORBUFFER, data->errbuf);
					if(proxy.length()>0) {
						curl_easy_setopt(site, CURLOPT_PROXY, proxy.c_str());
						curl_easy_setopt(site, CURLOPT_PROXYTYPE, g_config.UpdateProxyType);
					}

					hdata.push_back(data);
					handles.push_back(site);
					curl_multi_add_handle(multi, site);
				}
				else {
					delete data;
					data=NULL;
				}

				if(list) {
					tstring text=LoadString(IDS_UPDATEPG);

					lvi.pszText=(LPTSTR)text.c_str();
					ListView_InsertItem(list, &lvi);

					lvi.iSubItem=1;
					lvi.pszText=(LPTSTR)g_updateserver;
					ListView_SetItem(list, &lvi);

					lvi.iSubItem=2;
					text=LoadString(data?IDS_CONNECTING:IDS_ERRCURL);
					lvi.pszText=(LPTSTR)text.c_str();
					ListView_SetItem(list, &lvi);

					if(data) data->index=lvi.iItem;

					lvi.iItem++;
				}
			}

			///////////////////////////////////////////////
			/// Update dynamic lists

			if(g_config.UpdateLists && updatelists) {
				time_t curtime=time(NULL);

				for(vector<DynamicList>::size_type i=0; i<g_config.DynamicLists.size(); i++) {
					if(!g_config.DynamicLists[i].Enabled) continue;

					const path file=g_config.DynamicLists[i].File();
					if(curtime-g_config.DynamicLists[i].LastUpdate >= 43200 || !path::exists(file) || GetFileSize(file.c_str())==0) {
						HandleData *data=new HandleData(this);

						data->list=&g_config.DynamicLists[i];
						data->url=TSTRING_UTF8(g_config.DynamicLists[i].Url);
						data->tempfile=g_config.DynamicLists[i].TempFile();

						if(!path::exists(data->tempfile.without_leaf())) path::create_directory(data->tempfile.without_leaf());

						data->fp=_tfopen(data->tempfile.c_str(), _T("wbT"));

						if(data->fp) {
							CURL *site=curl_easy_init();
							if(site) {
								curl_easy_setopt(site, CURLOPT_PRECONNECT, preconnect_func);
								curl_easy_setopt(site, CURLOPT_PRECONNECTDATA, &allowed);
								curl_easy_setopt(site, CURLOPT_FOLLOWLOCATION, 1);
								curl_easy_setopt(site, CURLOPT_PROGRESSFUNCTION, progress_func);
								curl_easy_setopt(site, CURLOPT_PROGRESSDATA, data);
								curl_easy_setopt(site, CURLOPT_NOPROGRESS, 0);
								curl_easy_setopt(site, CURLOPT_USERAGENT, g_agent);
								curl_easy_setopt(site, CURLOPT_URL, data->url.c_str());
								curl_easy_setopt(site, CURLOPT_WRITEFUNCTION, NULL);
								curl_easy_setopt(site, CURLOPT_WRITEDATA, data->fp);
								curl_easy_setopt(site, CURLOPT_PRIVATE, data);
								curl_easy_setopt(site, CURLOPT_ERRORBUFFER, data->errbuf);
								if(proxy.length()>0) {
									curl_easy_setopt(site, CURLOPT_PROXY, proxy.c_str());
									curl_easy_setopt(site, CURLOPT_PROXYTYPE, g_config.UpdateProxyType);
								}

								if(g_config.DynamicLists[i].LastUpdate && path::exists(g_config.DynamicLists[i].File())) {
									curl_easy_setopt(site, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
									curl_easy_setopt(site, CURLOPT_TIMEVALUE, g_config.DynamicLists[i].LastUpdate);
								}
								else curl_easy_setopt(site, CURLOPT_TIMECONDITION, CURL_TIMECOND_NONE);
								
								hdata.push_back(data);
								handles.push_back(site);
								curl_multi_add_handle(multi, site);
							}
							else {
								fclose(data->fp);
								{
									try {
										path::remove(data->tempfile);
									}
									catch(exception &ex) {
										ExceptionBox(hwnd, ex, __FILE__, __LINE__);
									}
								}

								delete data;
								data=NULL;
								g_config.DynamicLists[i].FailedUpdate=true;
							}
						}
						else {
							delete data;
							data=NULL;

							g_config.DynamicLists[i].FailedUpdate=true;
						}

						if(list) {
							lvi.iSubItem=0;
							lvi.pszText=(LPTSTR)g_config.DynamicLists[i].Description.c_str();
							ListView_InsertItem(list, &lvi);

							lvi.iSubItem=1;
							lvi.pszText=(LPTSTR)g_config.DynamicLists[i].Url.c_str();
							ListView_SetItem(list, &lvi);

							const tstring str=LoadString(data?IDS_CONNECTING:IDS_ERRFILEOPEN);
							lvi.iSubItem=2;
							lvi.pszText=(LPTSTR)str.c_str();
							ListView_SetItem(list, &lvi);

							if(data) data->index=lvi.iItem;
							else this->progressmod+=100.0;

							lvi.iItem++;
						}
					}
					else {
						this->progressmod+=100.0;

						if(list) {
							lvi.iSubItem=0;
							lvi.pszText=(LPTSTR)g_config.DynamicLists[i].Description.c_str();
							ListView_InsertItem(list, &lvi);

							lvi.iSubItem=1;
							lvi.pszText=(LPTSTR)g_config.DynamicLists[i].Url.c_str();
							ListView_SetItem(list, &lvi);

							const tstring str=LoadString(IDS_NOUPDATENEEDED);
							lvi.iSubItem=2;
							lvi.pszText=(LPTSTR)str.c_str();
							ListView_SetItem(list, &lvi);

							lvi.iItem++;
						}

						g_config.DynamicLists[i].FailedUpdate=false;
					}
				}
			}

			if(progress) this->UpdateProgress();

			if(handles.size()>0) {
				///////////////////////////////////////////////
				/// Perform Updates

				int max;
				fd_set read, write, error;

				int running;
				while(curl_multi_perform(multi, &running)==CURLM_CALL_MULTI_PERFORM);

				while(!aborted && running) {
					FD_ZERO(&read); FD_ZERO(&write); FD_ZERO(&error);
					curl_multi_fdset(multi, &read, &write, &error, &max);

					timeval tv={2,0};

					if(select(FD_SETSIZE, &read, &write, &error, &tv)!=-1) {
						while(curl_multi_perform(multi, &running)==CURLM_CALL_MULTI_PERFORM);

						int msgs;
						while(CURLMsg *msg=curl_multi_info_read(multi, &msgs)) {
							if(msg->msg==CURLMSG_DONE) {
								HandleData *data;
								curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data);

								data->finished=true;
								data->progress=100.0;
								if(this->progress) this->UpdateProgress();

								if(data->fp) {
									fclose(data->fp);
									data->fp=NULL;
								}

								if(msg->data.result==CURLE_OK) {
									long code;
									curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &code);

									if(code==200) {
										if(data->list) {
											try {
												path::move(data->tempfile, data->list->File(), true);
											}
											catch(exception &ex) {
												ExceptionBox(hwnd, ex, __FILE__, __LINE__);
												data->list->FailedUpdate=true;
											}
											
											data->list->FailedUpdate=false;

											time(&data->list->LastUpdate);
											changes++;

											if(list) {
												const tstring str=LoadString(IDS_FINISHED);

												lvi.iItem=data->index;
												lvi.iSubItem=2;
												lvi.pszText=(LPTSTR)str.c_str();
												ListView_SetItem(list, &lvi);
											}
										}
										else {
											try {
												unsigned int b=boost::lexical_cast<unsigned int>(build);

												if(list) {
													const tstring str=LoadString((b>g_build)?IDS_UPDATEAVAILABLE:IDS_NONEAVAILABLE);

													lvi.iItem=data->index;
													lvi.iSubItem=2;
													lvi.pszText=(LPTSTR)str.c_str();
													ListView_SetItem(list, &lvi);
												}
												else updatepg=(b>g_build);
											}
											catch(...) {
												// keep going...
											}
										}
									}
									else if(code==304) {
										if(data->list) {
											time(&data->list->LastUpdate);
											data->list->FailedUpdate=false;
										}

										if(list) {
											const tstring str=LoadString(IDS_NOUPDATEAVAIL);

											lvi.iItem=data->index;
											lvi.iSubItem=2;
											lvi.pszText=(LPTSTR)str.c_str();
											ListView_SetItem(list, &lvi);
										}
									}
									else if(code>=300) {
										if(data->list) data->list->FailedUpdate=true;

										if(list) {
											const tstring str=LoadString(IDS_ERRORCONTACTING);

											lvi.iItem=data->index;
											lvi.iSubItem=2;
											lvi.pszText=(LPTSTR)str.c_str();
											ListView_SetItem(list, &lvi);
										}
									}
								}
								else if(data->list) {
									data->list->FailedUpdate=true;

									if(list) {
										tstring str;
										
										if(data->errbuf[0])
											str=boost::str(tformat(LoadString(IDS_ERRORCONTACTINGWHY))%data->errbuf);
										else str=LoadString(IDS_ERRORCONTACTING);

										lvi.iItem=data->index;
										lvi.iSubItem=2;
										lvi.pszText=(LPTSTR)str.c_str();
										ListView_SetItem(list, &lvi);
									}
								}

								if(data->list) {
									try {
										if(path::exists(data->tempfile)) path::remove(data->tempfile);
									}
									catch(exception &ex) {
										ExceptionBox(hwnd, ex, __FILE__, __LINE__);
									}
								}
							}
						}
					}
				}

				///////////////////////////////////////////////
				/// Cleanup

				for(vector<CURL*>::size_type i=0; i<handles.size(); i++) {
					curl_multi_remove_handle(multi, handles[i]);

					HandleData *data;
					curl_easy_getinfo(handles[i], CURLINFO_PRIVATE, &data);

					if(list && aborted && !data->finished) {
						const tstring str=LoadString(IDS_ABORTED);
						lvi.iItem=data->index;
						lvi.iSubItem=2;
						lvi.pszText=(LPTSTR)str.c_str();
						ListView_SetItem(list, &lvi);
					}

					if(data->fp) fclose(data->fp);

					if(data->list) {
						try {
							if(path::exists(data->tempfile)) path::remove(data->tempfile);
						}
						catch(exception &ex) {
							ExceptionBox(hwnd, ex, __FILE__, __LINE__);
						}
					}

					delete data;

					curl_easy_cleanup(handles[i]);
				}
			}

			if(progress) SendMessage(progress, PBM_SETPOS, (int)(total*100), 0);

			curl_multi_cleanup(multi);
			curl_global_cleanup();

			if(!aborted && updatepg && MessageBox(hwnd, IDS_PGUPDATETEXT, IDS_PGUPDATE, MB_ICONQUESTION|MB_YESNO)==IDYES)
				ShellExecute(NULL, NULL, g_homepage, NULL, NULL, SW_SHOWNORMAL);
		}

		HWND abort=GetDlgItem(hwnd, IDC_ABORT);
		HWND close=GetDlgItem(hwnd, IDC_CLOSE);

		SendMessage(hwnd, DM_SETDEFID, IDC_CLOSE, 0);
		SetFocus(close);

		EnableWindow(abort, FALSE);
		EnableWindow(close, TRUE);

		if(IsWindowVisible(hwnd)) {
			if(g_config.UpdateCountdown==0) {
				if(g_updater==(HWND)-1) EndDialog(hwnd, changes);
				else DestroyWindow(hwnd);
			}
			if(g_config.UpdateCountdown>0) {
				g_countdown=(unsigned short)g_config.UpdateCountdown;
				tstring s=boost::str(tformat(LoadString(IDS_CLOSEX))%g_countdown);
				SetDlgItemText(hwnd, IDC_CLOSE, s.c_str());

				SetTimer(hwnd, TIMER_COUNTDOWN, 1000, NULL);
			}
		}
		else if(g_updater==(HWND)-1) EndDialog(hwnd, changes);
		else DestroyWindow(hwnd);

		return changes;
	}

	int Process() {
		try {
			return this->_Process();
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

	void operator()() {
		this->Process();
	}
};

static UpdateThread g_ut;
static boost::shared_ptr<thread> g_updatethread;

static void UpdateLists_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
	if(id==IDC_ABORT) {
		EnableWindow(GetDlgItem(hwnd, IDC_ABORT), FALSE);
		g_ut.aborted=true;
	}
	else if(id==IDC_CLOSE) {
		if(g_updater==(HWND)-1) EndDialog(hwnd, g_ut.changes);
		else DestroyWindow(hwnd);
	}
}

static void UpdateLists_OnDestroy(HWND hwnd) {
	if(g_ut.changes>0) LoadLists(GetParent(hwnd));

	g_updatethread.reset();

	HWND list=GetDlgItem(hwnd, IDC_LIST);
	SaveListColumns(list, g_config.UpdateColumns);

	g_updater=NULL;
}

static void UpdateLists_OnGetMinMaxInfo(HWND hwnd, LPMINMAXINFO lpMinMaxInfo) {
	RECT rc={0};
	rc.right=268;
	rc.bottom=185;

	MapDialogRect(hwnd, &rc);

	lpMinMaxInfo->ptMinTrackSize.x=rc.right;
	lpMinMaxInfo->ptMinTrackSize.y=rc.bottom;
}

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

static void UpdateLists_OnSize(HWND hwnd, UINT state, int cx, int cy);
static BOOL UpdateLists_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
	HWND list=GetDlgItem(hwnd, IDC_LIST);
	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT|LVS_EX_LABELTIP);

	InsertColumn(list, 0, g_config.UpdateColumns[0], IDS_DESCRIPTION);
	InsertColumn(list, 1, g_config.UpdateColumns[1], IDS_TASK);
	InsertColumn(list, 2, g_config.UpdateColumns[2], IDS_STATUS);

	if(
		g_config.UpdateWindowPos.left!=0 || g_config.UpdateWindowPos.top!=0 ||
		g_config.UpdateWindowPos.right!=0 || g_config.UpdateWindowPos.bottom!=0
	) {
		SetWindowPos(hwnd, NULL,
			g_config.UpdateWindowPos.left,
			g_config.UpdateWindowPos.top,
			g_config.UpdateWindowPos.right-g_config.UpdateWindowPos.left,
			g_config.UpdateWindowPos.bottom-g_config.UpdateWindowPos.top,
			SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER
		);
	}

	RECT rc;
	GetClientRect(hwnd, &rc);
	UpdateLists_OnSize(hwnd, 0, rc.right, rc.bottom);

	g_ut=UpdateThread(hwnd, list, GetDlgItem(hwnd, IDC_PROGRESS), false);
	g_updatethread=boost::shared_ptr<thread>(new thread(boost::ref(g_ut)));
	return TRUE;
}

static void UpdateLists_OnSize(HWND hwnd, UINT state, int cx, int cy) {
	HWND list=GetDlgItem(hwnd, IDC_LIST);
	HWND progress=GetDlgItem(hwnd, IDC_PROGRESS);
	HWND close=GetDlgItem(hwnd, IDC_CLOSE);
	HWND abort=GetDlgItem(hwnd, IDC_ABORT);

	RECT rc, prc;
	GetWindowRect(close, &rc);
	GetWindowRect(progress, &prc);

	HDWP dwp=BeginDeferWindowPos(4);

	int mid=cx/2-((rc.right-rc.left)*2+7)/2;

	DeferWindowPos(dwp, list, NULL, 7, 7, cx-14, cy-28-(rc.bottom-rc.top)-(prc.bottom-prc.top), SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	DeferWindowPos(dwp, progress, NULL, 7, cy-14-(rc.bottom-rc.top)-(prc.bottom-prc.top), cx-14, prc.bottom-prc.top, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);
	DeferWindowPos(dwp, close, NULL, mid, cy-7-(rc.bottom-rc.top), 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);
	DeferWindowPos(dwp, abort, NULL, mid+7+(rc.right-rc.left), cy-7-(rc.bottom-rc.top), 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOSIZE);

	EndDeferWindowPos(dwp);

	if(state==SIZE_RESTORED) {
		RECT rc;
		GetWindowRect(hwnd, &rc);

		if(rc.left>=0 && rc.top>=0 && rc.right>=0 && rc.bottom>=0)
			g_config.UpdateWindowPos=rc;
	}
}

static void UpdateLists_OnTimer(HWND hwnd, UINT id) {
	if(id==TIMER_COUNTDOWN) {
		if(!--g_countdown) {
			if(g_updater==(HWND)-1) EndDialog(hwnd, g_ut.changes);
			else DestroyWindow(hwnd);
			g_updater=NULL;
		}
		else {
			tstring s=boost::str(tformat(LoadString(IDS_CLOSEX))%g_countdown);
			SetDlgItemText(hwnd, IDC_CLOSE, s.c_str());
		}
	}
}

static INT_PTR CALLBACK UpdateLists_DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	try {
		switch(msg) {
			HANDLE_MSG(hwnd, WM_COMMAND, UpdateLists_OnCommand);
			HANDLE_MSG(hwnd, WM_DESTROY, UpdateLists_OnDestroy);
			HANDLE_MSG(hwnd, WM_GETMINMAXINFO, UpdateLists_OnGetMinMaxInfo);
			HANDLE_MSG(hwnd, WM_INITDIALOG, UpdateLists_OnInitDialog);
			HANDLE_MSG(hwnd, WM_SIZE, UpdateLists_OnSize);
			HANDLE_MSG(hwnd, WM_TIMER, UpdateLists_OnTimer);
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

int UpdateLists(HWND parent) {
	int ret=0;

	if(g_updater==NULL) {
		if(parent) {
			g_updater=(HWND)-1;
			ret=(int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_UPDATELISTS), parent, UpdateLists_DlgProc);
		}
		else g_updater=CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_UPDATELISTS), parent, UpdateLists_DlgProc);

		time(&g_config.LastUpdate);
	}
	else if(parent) {	
		ShowWindow(g_updater, SW_SHOW);
		ShowWindow(g_updater, SW_RESTORE);
		SetForegroundWindow(g_updater);
	}

	return ret;
}
