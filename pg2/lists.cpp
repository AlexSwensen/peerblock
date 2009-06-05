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
		$Revision: 1.40 $
*/

#include "stdafx.h"
#include "resource.h"
using namespace std;

p2p::list g_tempallow, g_tempblock;

enum FileType { File_Unknown, File_Zip, File_Gzip, File_7zip };

static FileType GetType(const path &file) {
	FILE *fp=_tfopen(file.file_str().c_str(), _T("rb"));
	if(!fp) throw runtime_error("file does not exist");

	unsigned char buf[6];
	size_t read=fread(buf, sizeof(unsigned char), 6, fp);
	fclose(fp);

	if(read>=2) {
		if(buf[0]==0x1F && buf[1]==0x8B) return File_Gzip;
		if(read>=4) {
			if(!memcmp(buf, "PK\x03\x04", 4)) return File_Zip;
			if(read==6 && !memcmp(buf, "7z\xBC\xAF\x27\x1C", 6)) return File_7zip;
		}
	}

	return File_Unknown;
}

static pair<boost::shared_array<char>,size_t> UngzipFile(const path &file) {
	int fd=_topen(file.file_str().c_str(), _O_RDONLY|_O_BINARY);
	if(fd==-1) throw zip_error("unable to open file");

	gzFile fp=gzdopen(fd, "rb");
	if(fp==NULL) {
		_close(fd);
		throw zip_error("gzopen");
	}

	vector<char> buf;

	char tmp[4096];
	int read;

	while((read=gzread(fp, tmp, sizeof(tmp)))) {
		if(read<0) {
			int e;
			zip_error err("gzread", gzerror(fp, &e));
			gzclose(fp);

			throw err;
		}
		copy(tmp, tmp+read, back_inserter(buf));
	}

	gzclose(fp);

	boost::shared_array<char> ret(new char[buf.size()]);
	copy(buf.begin(), buf.end(), ret.get());

	return make_pair(ret, buf.size());
}

struct CFileInStream {
	ISzInStream InStream;
	FILE *File;
};

#define kBufferSize 4096
static Byte g_Buffer[kBufferSize];

SZ_RESULT SzFileReadImp(void *object, void **buffer, size_t maxRequiredSize, size_t *processedSize) {
	if(maxRequiredSize>kBufferSize) maxRequiredSize=kBufferSize;

	CFileInStream *s=(CFileInStream*)object;

	size_t processedSizeLoc=fread(g_Buffer, 1, maxRequiredSize, s->File);

	*buffer=g_Buffer;
	if(processedSize!=NULL) *processedSize=processedSizeLoc;

	return SZ_OK;
}

SZ_RESULT SzFileSeekImp(void *object, CFileSize pos) {
	CFileInStream *s=(CFileInStream*)object;

	int res=fseek(s->File, (long)pos, SEEK_SET);

	return (!res)?SZ_OK:SZE_FAIL;
}

bool LoadList(path file, p2p::list &list) {
	if(!file.has_root()) file=path::base_dir()/file;
	if(!path::exists(file)) return false;

	switch(GetType(file)) {
		case File_Zip: {
			ZipFile zip(file);

			if(zip.GoToFirstFile()) {
				do {
					zip.OpenCurrentFile();

					pair<boost::shared_array<char>,size_t> buf=zip.ReadCurrentFile();
					
					zip.CloseCurrentFile();

					list.load(istrstream((const char*)buf.first.get(), (streamsize)buf.second));
				} while(zip.GoToNextFile());
			}
		} break;
		case File_Gzip: {
			pair<boost::shared_array<char>,size_t> buf=UngzipFile(file);

			list.load(istrstream((const char*)buf.first.get(), (streamsize)buf.second));
		} break;
		case File_7zip: {
			CFileInStream is;

			is.File=_tfopen(file.c_str(), _T("rb"));
			if(!is.File) throw zip_error("unable to open file");

			is.InStream.Read = SzFileReadImp;
			is.InStream.Seek = SzFileSeekImp;

			ISzAlloc ai;
			ai.Alloc=SzAlloc;
			ai.Free=SzFree;

			ISzAlloc aitemp;
			aitemp.Alloc=SzAllocTemp;
			aitemp.Free=SzFreeTemp;
			
			CArchiveDatabaseEx db;
			
			InitCrcTable();
			SzArDbExInit(&db);

			SZ_RESULT res=SzArchiveOpen(&is.InStream, &db, &ai, &aitemp);
			if(res!=SZ_OK) {
				fclose(is.File);
				throw zip_error("SzArchiveOpen");
			}

			for(unsigned int i=0; i<db.Database.NumFiles; i++) {
				char *outbuf=NULL;
				size_t bufsize, offset, processed;
				unsigned int index;

				res=SzExtract(&is.InStream, &db, 0, &index, (unsigned char**)&outbuf, &bufsize, &offset, &processed, &ai, &aitemp);
				if(res!=SZ_OK) {
					SzArDbExFree(&db, ai.Free);
					fclose(is.File);
					throw zip_error("SzExtract");
				}

				try {
					list.load(istrstream((const char*)outbuf, (streamsize)processed));
				}
				catch(...) {
					ai.Free(outbuf);
					throw;
				}

				ai.Free(outbuf);
			}

			SzArDbExFree(&db, ai.Free);
			fclose(is.File);
		} break;
		case File_Unknown: {
			HANDLE h=CreateFile(file.file_str().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(h==INVALID_HANDLE_VALUE) throw win32_error("CreateFile");

			DWORD size=GetFileSize(h, NULL);
			if(size>0) {
				HANDLE m=CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);
				if(m==NULL) {
					win32_error ex("CreateFileMapping");

					CloseHandle(h);
					throw ex;
				}

				void *v=MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
				if(v==NULL) {
					win32_error ex("MapViewOfFile");

					CloseHandle(m);
					CloseHandle(h);
					throw ex;
				}

				try {
					list.load(istrstream((const char*)v, (streamsize)size));
				}
				catch(...) {
					UnmapViewOfFile(v);
					CloseHandle(m);
					CloseHandle(h);
					throw;
				}

				UnmapViewOfFile(v);
				CloseHandle(m);
			}
			CloseHandle(h);
		} break;
		default: __assume(0);
	}

	return true;
}

class GenCacheFuncs {
private:
	HWND hwnd;
	vector<StaticList>::size_type i;
	bool stat, dyn, opt, save, needupdate;
	p2p::list allow;
	p2p::list &block;

	static void ListProblem(HWND hwnd, const path &p, const exception &ex) {
		const tstring text=boost::str(tformat(LoadString(IDS_LISTERRTEXT))%p.c_str()%typeid(ex).name()%ex.what());
		MessageBox(hwnd, text, IDS_LISTERR, MB_ICONERROR|MB_OK);
	}

public:
	GenCacheFuncs(HWND hwnd, p2p::list &work)
		: hwnd(hwnd),block(work),stat(false),dyn(false),opt(true),save(true),needupdate(false),i(0) {}

	int Init() {
		int len=2;

		if(g_config.StaticLists.size()>0) {
			stat=true;
			for(vector<StaticList>::size_type i=0; i<g_config.StaticLists.size(); i++)
				if(g_config.StaticLists[i].Enabled) len++;
		}

		if(g_config.DynamicLists.size()>0) {
			dyn=true;
			for(vector<DynamicList>::size_type i=0; i<g_config.DynamicLists.size(); i++)
				if(g_config.DynamicLists[i].Enabled) len++;
		}

		return len;
	}

	bool Process() {
		if(stat) {
			if(g_config.StaticLists[i].Enabled) {
				p2p::list &l=(g_config.StaticLists[i].Type==List::Allow)?allow:block;

				try {
					if(!LoadList(g_config.StaticLists[i].File, l)) {
						tstring str=boost::str(tformat(LoadString(IDS_FILENOTFOUNDTEXT))%g_config.StaticLists[i].File.file_str());
						MessageBox(hwnd, str, IDS_FILENOTFOUND, MB_ICONWARNING|MB_OK);
					}
				}
				catch(exception &ex) {
					ListProblem(hwnd, g_config.StaticLists[i].File, ex);

					if(!(stat=(++i < g_config.StaticLists.size()))) i=0;
					return true;
				}
			}
			if(!(stat=(++i < g_config.StaticLists.size()))) i=0;
		}
		else if(dyn) {
			if(g_config.DynamicLists[i].Enabled) {
				p2p::list &l=(g_config.DynamicLists[i].Type==List::Allow)?allow:block;

				try {
					if(!LoadList(g_config.DynamicLists[i].File(), l))
						needupdate=true;
				}
				catch(exception &ex) {
					ListProblem(hwnd, g_config.DynamicLists[i].Url, ex);

					if(!(dyn=(++i < g_config.DynamicLists.size()))) i=0;
					return true;
				}
			}
			if(!(dyn=(++i < g_config.DynamicLists.size()))) i=0;
		}
		else if(opt) {
			if(block.size()>0) {
				if(allow.size()>0) block.erase(allow);
				if(block.size()>0) block.optimize();
			}
			opt=false;
		}
		else if(save) {
			block.save(TSTRING_MBS((path::base_dir()/_T("cache.p2b")).c_str()), p2p::list::file_p2b);
			save=false;
		}
		else {	
			if(needupdate && hwnd)
				MessageBox(hwnd, IDS_NEEDUPDATETEXT, IDS_NEEDUPDATE, MB_ICONWARNING|MB_OK);
			return false;
		}

		return true;
	}
};

static bool IsCacheValid() {
	boost::crc_32_type crc;

	for(vector<StaticList>::size_type i=0; i<g_config.StaticLists.size(); i++) {
		if(g_config.StaticLists[i].Enabled) {
			path file=g_config.StaticLists[i].File;
			if(!file.has_root()) file=path::base_dir()/file;

			HANDLE fp=CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(fp!=INVALID_HANDLE_VALUE) {
				tstring str=file.c_str();
				crc.process_bytes(str.data(), str.size()*sizeof(TCHAR));
				crc.process_bytes(&g_config.StaticLists[i].Type, sizeof(List::ListType));

				FILETIME ft;
				if(GetFileTime(fp, NULL, NULL, &ft))
					crc.process_bytes(&ft, sizeof(ft));

				CloseHandle(fp);
			}
		}
	}

	for(vector<DynamicList>::size_type i=0; i<g_config.DynamicLists.size(); i++) {
		if(g_config.DynamicLists[i].Enabled) {
			HANDLE fp=CreateFile(g_config.DynamicLists[i].File().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(fp!=INVALID_HANDLE_VALUE) {
				crc.process_bytes(g_config.DynamicLists[i].Url.c_str(), g_config.DynamicLists[i].Url.size()*sizeof(TCHAR));
				crc.process_bytes(&g_config.DynamicLists[i].Type, sizeof(List::ListType));

				FILETIME ft;
				if(GetFileTime(fp, NULL, NULL, &ft))
					crc.process_bytes(&ft, sizeof(ft));

				CloseHandle(fp);
			}
		}
	}

	unsigned int res=(unsigned int)crc.checksum();

	if(res!=g_config.CacheCrc) {
		g_config.CacheCrc=res;
		return false;
	}

	return path::exists(path::base_dir()/_T("cache.p2b"));
}

static bool GenCache(HWND hwnd, p2p::list &work) {
	if(!IsCacheValid()) {
		GenCacheFuncs funcs(hwnd, work);

		if(hwnd) {
			tstring title=LoadString(IDS_GENCACHE);

			LoadingData data;
			data.Title=title.c_str();
			data.InitFunc=boost::bind(&GenCacheFuncs::Init, &funcs);
			data.ProcessFunc=boost::bind(&GenCacheFuncs::Process, &funcs);

			DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_LOADING), hwnd, Loading_DlgProc, (LPARAM)&data);
		}
		else {
			funcs.Init();
			while(funcs.Process());
		}

		return true;
	}
	else return false;
}

void LoadLists(HWND parent) {
	p2p::list block;

	//night_stalker_z: Load lists always
	if(!GenCache(parent, block))
		LoadList(_T("cache.p2b"), block);

	if(block.size()>0) {
		p2p::list allow;

		if(g_config.AllowLocal) {
			set<unsigned int> locals;
			GetLocalIps(locals, LOCALIP_ADAPTER|LOCALIP_GATEWAY|LOCALIP_DHCP|LOCALIP_DNS);

			for(set<unsigned int>::const_iterator iter=locals.begin(); iter!=locals.end(); iter++)
				allow.insert(p2p::range(L"", *iter, *iter));
		}

		if(g_tempallow.size()>0) allow.insert(g_tempallow);

		if(allow.size()>0) block.erase(allow);
		if(block.size()>0) block.optimize();
	}

	g_filter->setranges(block, true);

	SendMessage(g_tabs[0].Tab, WM_LOG_RANGES, 0, (UINT)g_filter->blockcount());
}

path DynamicList::File() const {
	boost::crc_32_type crc;
	crc.process_bytes(this->Url.c_str(), this->Url.length()*sizeof(TCHAR));

	TCHAR buf[32];
	return path::base_dir()/_T("lists")/(_ultot((unsigned long)crc.checksum(), buf, 10)+tstring(_T(".list")));
}

path DynamicList::TempFile() const {
	boost::crc_32_type crc;
	crc.process_bytes(this->Url.c_str(), this->Url.length()*sizeof(TCHAR));

	TCHAR buf[32];
	return path::base_dir()/_T("lists")/(_ultot((unsigned long)crc.checksum(), buf, 10)+tstring(_T(".list.tmp")));
}
