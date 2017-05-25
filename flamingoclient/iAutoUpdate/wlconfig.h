#ifndef	__WINLINUX_CONFIG__
#define	__WINLINUX_CONFIG__
/////////////////////////////////////////////////////////////////////////////
#include "zwldef.h"
#pragma pack(push,1)
/////////////////////////////////////////////////////////////////////////////
class NOVTABLE wl_config
{
public:
	typedef union tagVersion
	{
		struct 
		{
			BYTE	main;	//主版本号
			BYTE	sub;	//次版本号
			WORD	build;	//build号
		}part;
		DWORD	version;
	}version,*pversion;

	static LPSTR GetApart(LPSTR lpString,DWORD*lOffset,char chMask)
	{
		DWORD l(*lOffset),t(l);
		for(;lpString[t]&&lpString[t]!=chMask;++t);
		if(lpString[t]) lpString[t++] = 0;
		*lOffset = t;
		return(lpString+l);
	}
	static DWORD read_version(LPSTR buffer)
	{
		DWORD l(0),verData={0};
		pversion version=(pversion)&verData;
		version->part.main = atoi(GetApart(buffer,&l,'.'));
		version->part.sub = atoi(GetApart(buffer,&l,'.'));
		version->part.build = atoi(GetApart(buffer,&l,'.'));
		return verData;
	}
	BOOL read(LPCSTR keyStart,LPSTR buffer,long maxRead)
	{
		memset(buffer,0,maxRead);
		if(!__text__) return(FALSE);
		char*lpStart=strstr(__text__,keyStart);
		if(!lpStart) return(FALSE);
		lpStart += lstrlenA(keyStart);
		char*lpEnd=strstr(lpStart,"</");
		if(lpEnd)
		{
			long ln(lpEnd-lpStart+1);
			maxRead=min(ln,maxRead);
		}
		lstrcpynA(buffer,lpStart,maxRead);
		return(TRUE);
	}
	BOOL loadconfig(LPCTSTR lpFormat,...)
	{
		wl_config::release();
		TCHAR filename[MAX_PATH]={0};
		va_list arg_ptr;
		va_start(arg_ptr,lpFormat);
#ifdef	WIN32
	#ifdef	_UNICODE
			vswprintf(filename,lpFormat,arg_ptr);
	#else
			vsprintf(filename,lpFormat,arg_ptr);
	#endif
#else
		vsprintf(filename,lpFormat,arg_ptr);
#endif
		va_end(arg_ptr);
#ifdef	WIN32
		HANDLE hFile=INVALID_HANDLE_VALUE;
#else
		FILE*hFile=NULL;
#endif
		do
		{
#ifdef	WIN32
			SECURITY_ATTRIBUTES sa={0};
			sa.nLength = sizeof(sa);
			hFile=CreateFile(filename,
				GENERIC_READ|GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE,
				&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
			if(hFile==INVALID_HANDLE_VALUE) return(FALSE);
			__size__=GetFileSize(hFile,NULL);
#else
			if(!(hFile=fopen(filename,"rb"))) return(FALSE);
			__size__=filelength(fileno(hFile));
#endif
			if(!(__text__=new char[__size__+2])) break;
			memset(__text__,0,__size__+2);
			if(__size__)
			{
#ifdef	WIN32
				DWORD dwBytes(0);
				if(!ReadFile(hFile,__text__,__size__,&dwBytes,NULL)) break;
#else
				if(fread(__text__,__size__,1,hFile)!=1) break;
#endif
			}
#ifdef	WIN32
			CloseHandle(hFile);
#else
			fclose(hFile);
#endif
			return(TRUE);
		}while(0);
#ifdef	WIN32
		CloseHandle(hFile);
#else
		fclose(hFile);
#endif
		wl_config::release();
		return(FALSE);
	}
	void release(void)
	{
		if(__text__)
		{
			delete[] __text__;
			__text__ = NULL;
		}
		__size__ = 0;
	}
	wl_config(void):__size__(0),__text__(NULL)
	{
	}
	~wl_config(void)
	{
		wl_config::release();
	}
	long __size__;
	char*__text__;
};
/////////////////////////////////////////////////////////////////////////////
#pragma pack(pop)
#endif
