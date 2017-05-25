#ifndef	__zealink_winlinux_define__
#define	__zealink_winlinux_define__
/////////////////////////////////////////////////////////////////////////////
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<math.h>
#include	<stdarg.h>
#include	<ctype.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<sys/stat.h>
#include	<sys/types.h>

#if (defined WIN32) || (defined WIN64)
//#ifndef	LINUX | _WIN64
	#include	<share.h>
	#include	<time.h>
	#include	<io.h>
	#include	<sys/utime.h>
	#include	<winsock2.h>
	#include	<windows.h>
	#include	<tchar.h>
#else
	//#include <char.h>
	#include  <wchar.h>
	#include	<unistd.h>
	#include	<pthread.h>
	#include	<dirent.h>
	#include	<fnmatch.h>
	#include	<unistd.h>
	#include	<zlib.h>
	#include	<netdb.h>
	#include	<signal.h>
	#include	<getopt.h>
	#include	<termios.h>
	#include	<sysexits.h>
	#include	<utime.h>

	#include	<netinet/in.h>
	#include	<arpa/inet.h>
	// #include	<linux/keyboard.h>
	#include	<linux/fb.h>
	#include	<net/if.h>
	#include	<net/if_arp.h>

	#include	<sys/wait.h>
	#include	<sys/uio.h>
	#include	<sys/un.h>
	#include	<sys/socket.h>
	#include	<sys/ioctl.h>
	#include	<sys/mman.h>
	#include	<sys/io.h>
	#include  <sys/fcntl.h>
	#include	<sys/kd.h>
	#include	<sys/time.h>
	#include	<sys/param.h>
	#include	<sys/utsname.h>
#endif
#pragma pack(push,1)
/////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
	typedef	unsigned char		XBYTE;
	typedef	unsigned short		XWORD;
	typedef	long				XINT32;
	typedef	unsigned long		XDWORD;
	typedef	__int64				XINT64;
	typedef	unsigned __int64	XBIGINT;
#else
	typedef	unsigned char		XBYTE;
	typedef	unsigned short		XWORD;
	typedef	int					XINT32;
	typedef	unsigned int		XDWORD;
	typedef	long long			XINT64;
	typedef	unsigned long long	XBIGINT;
#endif

#if (defined WIN32) || (defined WIN64)
	typedef	HANDLE	HTHREAD;
	typedef	HANDLE	HPIPE;

	typedef	int	socklen_t;
	#define	NOVTABLE __declspec(novtable)
	#ifdef TEST_SOLUTION
		#define PRIVATE   public
	#else
		#define PRIVATE   private
	#endif

	#ifdef TEST_SOLUTION
		#define PROTECTED   public
	#else
		#define PROTECTED   protected
	#endif
#else	//Global Define(like win)
	typedef	pthread_t	HTHREAD;
	typedef	int		HPIPE[2];
	typedef XDWORD			WPARAM;
	typedef XINT32			LPARAM;
	typedef const void		*LPCVOID;
	typedef const char		*LPCSTR;
	typedef char			*LPSTR;
	typedef const wchar_t	*LPCWSTR;
	typedef wchar_t			*LPWSTR;
	typedef	__U32_TYPE		__time32_t;
	typedef unsigned char	*LPBYTE;
	typedef XDWORD			COLORREF;
	typedef void			*LPVOID;
	typedef void			*HANDLE;
	typedef void			*HINSTANCE;
	typedef XINT32			BOOL;
	typedef XINT32			LRESULT;
	typedef int				SOCKET;
	typedef void*			HWND;
	typedef short			SHORT;
	typedef char			TCHAR;
	typedef const char* LPCTSTR;
	typedef char* LPTSTR;
	const XINT32 MAX_PATH=260;

	#define	FILE_BEGIN		0
	#define FILE_CURRENT	1
	#define FILE_END		2

	#define INVALID_SOCKET  (SOCKET)-1
	#define	WINAPI
	#define	NOVTABLE

	#define MAKEWORD(a,b)	((XWORD)(((XBYTE)(a))|((XWORD)((XBYTE)(b)))<<8))
	#define MAKELONG(a,b)	((XINT32)(((XWORD)(a))|((XDWORD)((XWORD)(b)))<<16))
	#define LOWORD(l)		((XWORD)(l))
	#define HIWORD(l)		((XWORD)(((XDWORD)(l)>>16)&0xFFFF))
	#define LOBYTE(w)		((XBYTE)(w))
	#define HIBYTE(w)		((XBYTE)(((XWORD)(w)>>8)&0xFF))
	#define RGB(r,g,b)		((COLORREF)(((XBYTE)(r)|((XWORD)((XBYTE)(g))<<8))|(((XDWORD)(XBYTE)(b))<<16)))

//critical section macro:
	typedef XINT32 HRESULT;
	typedef	pthread_mutex_t CRITICAL_SECTION;
	#define	InitializeCriticalSection(hMutex)	pthread_mutex_init(hMutex,NULL)
	#define	DeleteCriticalSection(hMutex)		pthread_mutex_destroy(hMutex)
	#define	EnterCriticalSection(hMutex)		pthread_mutex_lock(hMutex)
	#define	LeaveCriticalSection(hMutex)		pthread_mutex_unlock(hMutex)
//map win func
	#define TlsAlloc() pthread_key_create()
	#define _vstprintf vsprintf
	#define _snprintf snprintf
	#define _sntprintf snprintf
	#define	lstrlenW(str) wcslen(str)
	#define	lstrlen(str) strlen(str)
	#define	lstrlenA(str) strlen(str)
	#define wsprintf	sprintf
	#define wsprintfW   swprintf
	#define wsprintfA	sprintf
	#define _istalnum   isalnum
	#define _istalpha	isalpha
	#define _istdigit	isdigit
	#define _atoi64 atoll
	#define _wtof(str)  wcstof(str, NULL)
	#define _wtoi64(str)    wcstoll (str, NULL, 10)
	#define _tfopen  fopen
	#define _wtol(str)  wcstol(str, NULL, 10)
	#define _wtoi(str)  wcstol(str, NULL, 10)
	#define	lstrcpy(str1,str2) strcpy(str1,str2)
	#define	lstrcpyA(str1,str2) strcpy(str1,str2)
	#define	lstrcpyn(str1,str2,size) strncpy(str1,str2,size)
	#define	lstrcpynW(str1,str2,size) wcscpy(str1,str2)
	#define	lstrcpynA(str1,str2,size) strncpy(str1,str2,size)
	#define strcpy_s(str1, size, str2) strncpy(str1, str2,size)
	//#define strcpy_s(str1, str2) strcpy(str1, str2)
	#define	lstrcat(str1,str2) strcat(str1,str2)
	#define	lstrcatA(str1,str2) strcat(str1,str2)
	#define	lstrcmp(str1,str2) strcmp(str1,str2)
	#define	lstrcmpA(str1,str2) strcmp(str1,str2)
	#define	lstrcmpi(str1,str2) strcasecmp(str1,str2)
	#define	lstrcmpiA(str1,str2) strcasecmp(str1,str2)
	#define	stricmp(str1,str2) strcasecmp(str1,str2)
	#define	strnicmp(str1,str2,n) strncasecmp(str1,str2,n)
	#define	closesocket(h)  ::close(h)
	#define	Sleep(lSeconds)	usleep(lSeconds*1000L)
	#define		ARRAYSIZE(a)  (sizeof(a)/sizeof(a[0]))
	#define  _T(x)	x
	#define TEXT(x) 	x
	typedef struct _LARGE_INTEGER
	{
		struct
		{
			XDWORD	LowPart;
			XINT32	HighPart;
		};
		XINT64	QuadPart;
	}LARGE_INTEGER;

#ifndef FILETIME
	typedef struct _FILETIME
	{
		XDWORD	dwLowDateTime;
		XDWORD	dwHighDateTime;
	}FILETIME,*PFILETIME,*LPFILETIME;
#endif

	typedef struct _SYSTEMTIME
	{
		XWORD	wYear;
		XWORD	wMonth;
		XWORD	wDayOfWeek;
		XWORD	wDay;
		XWORD	wHour;
		XWORD	wMinute;
		XWORD	wSecond;
		XWORD	wMilliseconds;
	}SYSTEMTIME,*PSYSTEMTIME,*LPSYSTEMTIME;
	typedef struct tagDBTIMESTAMP
    {
		short	year;
		XWORD	month;
		XWORD	day;
		XWORD	hour;
		XWORD	minute;
		XWORD	second;
		XDWORD	fraction;
    }DBTIMESTAMP;
#endif

#ifndef	TRUE
	#define	TRUE	1
#endif

#ifndef	FALSE
	#define	FALSE	0
#endif

#ifndef max
	#define	max(a,b)	(((a)>(b))?(a):(b))
#endif

#ifndef min
	#define min(a,b)	(((a)<(b))?(a):(b))
#endif

#ifndef	DELETE_PTR
	#define	DELETE_PTR(p) if(p){delete p;p=NULL;}
#endif

#ifndef	DELETE_PTRA
	#define	DELETE_PTRA(p) if(p){delete[] p;p=NULL;}
#endif

#ifndef	COPY_STRING
	#define	COPY_STRING(p1,p2)\
	lstrcpyn(p1,p2,sizeof(p1))
#endif

#ifndef	COPY_STRINGEX
	#define	COPY_STRINGEX(p1,p2)\
	memset(p1,0,sizeof(p1));\
	lstrcpyn(p1,p2,sizeof(p1));
#endif

#ifndef	WLSTAT_DEFINE
	#define	WLSTAT_DEFINE
	#ifdef	WIN32
		typedef	struct _stati64 STAT_STRU;
		#define	STAT_PROC _stati64
		#define	FSTAT_PROC _fstati64
	#else
		typedef	struct stat64 STAT_STRU;
		#define	STAT_PROC stat64
		#define	FSTAT_PROC fstat64
	#endif
#endif

#if (defined WIN32) || (defined WIN64)
	#define	WSA_LAST_ERROR	WSAGetLastError()
	#define	LAST_ERROR	GetLastError()

	#ifndef	CLOSE_HANDLE
		#define	CLOSE_HANDLE(h) if(h){CloseHandle(h);h=NULL;}
	#endif

	#ifndef	CLOSE_FILE
		#define	CLOSE_FILE(h) if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}
	#endif

	#ifndef	CLOSE_SOCKET
		#define	CLOSE_SOCKET(h)	if(h!=INVALID_SOCKET){closesocket(h);h=INVALID_SOCKET;}
	#endif

	#ifndef	THREAD_CREATE
		#define	THREAD_CREATE(AfxProc,Class,Method)\
		DWORD WINAPI AfxProc(LPVOID lpParam)\
		{\
			CoInitialize(NULL);\
			try\
			{\
				((Class*)lpParam)->Method();\
			}\
			catch(...)\
			{\
			}\
			CoUninitialize();\
			return(0);\
		}
	#endif

	#ifndef	THREAD_SAFE_CLOSE
		#define	THREAD_SAFE_CLOSE(h,tm)\
		if(h)\
		{\
			if(WaitForSingleObject(h,tm)!=WAIT_OBJECT_0)\
			{\
				TerminateThread(h,0);\
			}\
			CloseHandle(h);\
			h = NULL;\
		}
	#endif

	#define	DECLARE_FILEOPEN(fp,filename,openmode)	FILE*fp=_fsopen(filename,openmode,SH_DENYNO)
#else	//Linux
	#define	WM_USER		0x0400

	#define	WSA_LAST_ERROR	errno
	#define	LAST_ERROR	errno

	#ifndef	CLOSE_FILE
		#define	CLOSE_FILE(h) if(h!=NULL){fclose(h);h=NULL;}
	#endif

	#ifndef	CLOSE_SOCKET
		#define	CLOSE_SOCKET(h)	if(h>0){closesocket(h);h=-1;}
	#endif

	#ifndef	THREAD_CREATE
		#define	THREAD_CREATE(AfxProc,Class,Method)\
		void*AfxProc(void*lpParam)\
		{\
			try\
			{\
				((Class*)lpParam)->Method();\
			}\
			catch(...)\
			{\
			}\
			return(NULL);\
		}
	#endif

	#ifndef	THREAD_SAFE_CLOSE
		#define	THREAD_SAFE_CLOSE(h,tm)\
		if(h)\
		{\
			pthread_join(h,NULL);\
			h = 0;\
		}
	#endif

	#define	DECLARE_FILEOPEN(fp,filename,openmode)	FILE*fp=fopen(filename,openmode)
#endif
/////////////////////////////////////////////////////////////////////////////
#ifndef	ZERO_STRING
	#define	ZERO_STRING(s) memset(s,0,sizeof(s))
#endif

#ifndef	ROUNDUP_SIZE
	#define ROUNDUP_SIZE(size,amount)	(((ULONG)(size)+((amount)-1))&~((amount)-1))
#endif

#ifndef	CHECK_STRING_ENDNULL
	#define	CHECK_STRING_ENDNULL(s) s[sizeof(s)-1] = 0;
#endif

#ifdef	WIN32
	#ifdef	_UNICODE
		#define	__string_lwr__(s) _wcslwr(s)
		#define	__string_upr__(s) _wcsupr(s)
		#define	__bigint_to_string__(i,s,b) _i64tow(i,s,b)
		#define	__string_to_bigint__(s) _wtoi64(s)
		#define	__long_to_string__(i,s,b) _itow(i,s,b)
		#define	__string_to_long__(s) _wtol(s)
		#define	__string_to_double__(s) _wtof(s)
		#define	__string_search__(s,sub) wcsstr(s,sub)
		#define	__string_char__(s,c) wcschr(s,c)
		#define	__csprintf__ swprintf
	#else
		#define	__string_lwr__(s) _strlwr(s)
		#define	__string_upr__(s) _strupr(s)
		#define	__bigint_to_string__(i,s,b) _i64toa(i,s,b)
		#define	__string_to_bigint__(s) _atoi64(s)
		#define	__long_to_string__(i,s,b) _itoa(i,s,b)
		#define	__string_to_long__(s) atol(s)
		#define	__string_to_double__(s) atof(s)
		#define	__string_search__(s,sub) strstr(s,sub)
		#define	__string_char__(s,c) strchr(s,c)
		#define	__csprintf__ sprintf
	#endif
	#define	__stringA_to_bigint__(s) _atoi64(s)
#else
	#define	__string_lwr__(s)  _strlwr(s)
	#define	__string_upr__(s)  _strupr(s)
	#define	__bigint_to_string__(i,s,b)    CONVERT_BIGINT(s,i)
	#define	__string_to_bigint__(s)  atoll(s)
	#define	__long_to_string__(i,s,b)  sprintf(s, "%d", i) 
	#define	__string_to_long__(s) atol(s)
	#define	__string_to_double__(s) atof(s)
	#define	__string_search__(s,sub) strstr(s,sub)
	#define	__string_char__(s,c) strchr(s,c)
	#define	__csprintf__ sprintf
	#define	__stringA_to_bigint__(s) atoll(s)
#endif

//通用宏定义
#ifndef	__SAFE_DELETE_GDIOBJ__	//删除GDI句柄
#define	__SAFE_DELETE_GDIOBJ__(hObj)\
	if(hObj)\
	{	DeleteObject(hObj);\
		hObj = NULL;\
	}
#endif

#ifndef	__DELETE_OBJECTS__	//删除GDI句柄数组
#define __DELETE_OBJECTS__(objs)\
	for(int i=0;i<ARRAYSIZE(objs);++i)\
	{\
		__SAFE_DELETE_GDIOBJ__(objs[i])\
	}
#endif

#ifndef	COLORREF2RGB
	#define COLORREF2RGB(Color) (Color&0XFF00)|((Color>>16)&0XFF)|((Color<<16)&0XFF0000)
#endif

#ifndef	__CHECK_STRING__
	#define	__CHECK_STRING__(s) s[sizeof(s)/sizeof(s[0])-1] = 0
#endif

#ifndef	__define_protocol_header__
#define	__define_protocol_header__
typedef struct tagProtocolHead
{
	__time32_t	time_check;		//时间校验
	XDWORD	req_ans_command;	//req/ans command id
	XDWORD	source_size;		//source size(no compress)
	XDWORD	package_size;		//package size,if no compress,this must be 0
	XDWORD	self_param;			//HWND(x86)or other
}protocol_head,*pprotocol_head;
#endif

//数据库连接结构
#ifndef	__udb_conninfo__
#define	__udb_conninfo__
enum
{
	DBMS_MSSQL=0,	//sql server 2000/2005/2008
	DBMS_MYSQL,		//mysql 4.x/5.x
	DBMS_ORACLE,	//oracle 8i/9i/10g/11g
	DBMS_DB2,		//ibm db2 8.x/9.x
	DBMS_COMMON,    //通用数据库
};
typedef struct tagUDBConnInfo
{
	XBYTE	dbtype;	
	//数据库类别:
	//0:sql server
	//1:mysql
	//2:oracle
	//3:db2

	XDWORD	policy;
	//策略:
	//0x01 : ODBC连接
	//0x02 : windows身份认证
	//0x04 : OCI连接
	//0x08 : ORACLE数据库MS驱动

	//0x0100 : excel

	char	dburl[32];		//地址
	char	dbname[32];		//数据库名称
	char	dbuser[32];		//用户名
	char	dbowner[32];	//所有者
	char	dbpasswd[32];	//密码
	char	utsmask[32];	//时间戳名

	char	spaceTable[32];	//数据表空间
	char	spaceIndex[32];	//索引表空间
	char	spaceText[32];	//大文本表空间

	char	reserved[32];	//保留信息
	char	dbScript[32];	//描述信息
}udbconn_info,*pudbconn_info;
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////
enum
{
	__umymessage_netcommand__=WM_USER+0x9F01,	//网络消息
	__umessage_wndtrace__,				//鼠标移动窗口消息
	__umessage_scrollbar__,				//滚动条消息;横向:wparam=1;纵向:wparam=0

	__umymessage_parentdraw__,			//发送消息给父窗口绘制
	__umymessage_parent_mousemove__,	//发送鼠标移过消息给父窗口
	__umymessage_parent_lbtndown__,		//发送鼠标左键按下消息给父窗口
	__umymessage_parent_rbtndown__,		//发送鼠标右键按下消息给父窗口
	__umymessage_parent_lbtnup__,		//发送鼠标左键释放消息给父窗口
	__umymessage_parent_rbtnup__,		//发送鼠标右键释放消息给父窗口
	__umymessage_parent_lbtndblclk__,	//鼠标左键双击
	__umymessage_parent_keydown__,		//键盘按下
	__umymessage_parent_lbtndblclk1__,	//鼠标左键双击(传递行号和列号)

	__umymessage_setstock__,
	__umymessage_setblock__,
	__umymessage_setindex__,
	__umymessage_setcode__,
	__umymessage_overlay__,				//叠加数据
	__umymessage_uxsnormal__,
	__umymessage_setblock2__,
	__umymessage_setkeyindex__,

	__umymessage_parent_size_zoom_in__,	//放大
	__umymessage_parent_size_zoom_out__,//缩小
	__umymessage_parent_close__,        //发送关闭消息给父窗口
	__umymessage_preparentdraw__,		//发送消息给父窗口绘制(子窗口绘制之前发送)
	__umymessage_afterdraw__,			//窗口画图完成
	__umymessage_char_curve_selected__,	//选中线消息 (chart)
	__umymessage_showwindows__,			//显示隐藏窗口事件

	__umymessage_resetblock__,			//重置板块
	__umymessage_addnewblock__,			//新建板块
	__umymessage_deleteblock__,			//删除板块
	__umymessage_settype__,				//属性设置
	__umymessage_reload__,				//重新加载

	__umymessage_remove_module__,		//删除一个摸版 wParam=m_lIdentify

	__umymessage_setcode_by_navigate_plus__,	//第3方板块项选中 wParam= PUMS_BLOCK_ITEM, lParam = m_pNavigatePlus->_szVDLItemParamName
	__umymessage_setblock_by_navigate_plus__,	//第3方板块项选中 wParam= PUMS_BLOCK_ITEM, lParam = m_pNavigatePlus->_szVDLBlockParamName

	__umymessage_change_module__,	//面板切换成其他类型 wParam=m_lIdentify lParam=新的类型
	__umymessage_hq_to_minutes__,	//行情面板F5, wParam=m_lIdentify, lParam = stock
	__umymessage_hq_to_history__,	//行情面板F5, wParam=m_lIdentify, lParam = stock

	__umymessage_news_content__,

	__umymessage_update_friend_panel__,
	__umymessage_maxmin_panle__,	//最大最小化面板 wParam=(1.最大 0.还原) lParam= UMyModule9*
	__umymessage_size_panel___,		//调整大小
	__umymessage_changemax_panle__,	//改变最大化面板  lParam= UMyModule9* 最大化面板切换面板类型后使新面板最大化

	__umymessage_callback_setcode__=0x1522,

	__umymessage_init_umsdatashow__,
	UMS_SHOWWINDOW,			//窗口显示/隐藏消息

};
/////////////////////////////////////////////////////////////////////////////////////////////////
#define	NUM_PRICE_LEVEL_L2	10	// Level2价位数量(10档)
#define	NUM_PRICE_LEVEL_L1	5	// Level1价位数量(5档)
typedef	struct	tagStockAttrib	//证券属性
{
	XBIGINT	Market:8;	//市场
	XBIGINT	Type:8;		//主属性:类别
	XBIGINT	decNum:3;	//显示的小数位数
	XBIGINT	unitBase:3;	//计量单位位数
//子属性(子属性的含义都是确定的，以后可添加,但不能更改)
	XBIGINT	A:1;	//GP:A股		JJ:封闭式基金	ZQ:国债			HG:抵押式    期货:大连				美股:NYSE
	XBIGINT	B:1;	//GP:B股		JJ:开放式基金	ZQ:企债			HG:买断式	 期货:上海				美股:AMEX
	XBIGINT	C:1;	//GP:新股申购	JJ:LOF			ZQ:转债			HG:国债		 期货:郑州				美股:NASDAQ
	XBIGINT	D:1;	//GP:增发		JJ:ETF			ZQ:央行票据		HG:企债		 期货:是否为连续

	XBIGINT	E:1;	//GP:			JJ:FOF			ZQ:金融债
	XBIGINT	F:1;	//GP:			JJ:基金组合产品
	XBIGINT	G:1;	//GP:			JJ:
	XBIGINT	H:1;	//GP:			JJ:

	XBIGINT	I:1;	//GP:			JJ:
	XBIGINT	J:1;	//GP:			JJ:

	XBIGINT	unitContract:16;	//期货合约单位
//其他属性
	XBIGINT	TS:2;		//1:休市;2:退市
	XBIGINT TRADETIME:5;//开收市时间类型
	XBIGINT	Reserved:9;
}stock_attrib,*pstock_attrib;

typedef struct tagStockIndex	//码表
{
	struct tagStockIndex	*pNext;	//下一个(pagedown翻页用)
	struct tagStockIndex	*pPrev;	//上一个(pageup翻页用)
	stock_attrib	attrib;		//属性

	XBYTE	bcType1;
	XBYTE	bcType2;
	XBYTE	bcType3;

	char	ansiSymbol[10];
	char	ansiName[32];
	char	orgid[16];

	TCHAR	szSymbol[10];
	TCHAR	szName[32];
	TCHAR	szSpell[12];

	XINT32	minuteAccess;
	XINT32	verData;
	XINT32	stkIndex;

	XINT32	preClosepx;
	XINT32	highPx;
	XINT32	lowPx;
	XINT32	lastPrice;

	double	tradable_mkt_val;	//流通股本
	double	total_mkt_val;		//总股本
	double	eps;				//每股收益
	double	BVPS;				//每股净资产
	XINT64	vol_5;				//5日均量
}STOCK_INDEX,*PSTOCK_INDEX;

typedef struct tagMARKETDATA3	//客户端内存中数据(无版本号)
{
	XINT32	dataTimeStamp;					// 数据产生的时间，以163025表示16:30:25

	XINT32	preClosePx;						// 昨收价
	XINT32	openPx;							// 今开
	XINT32	highPx;							// 最高
	XINT32	lowPx;							// 最低

	XINT32	lastPrice;						// 最新

	XINT64	bidSize[NUM_PRICE_LEVEL_L2];	// 申买量
	XINT32	bidPx[NUM_PRICE_LEVEL_L2];		// 申买价
	XINT64	offerSize[NUM_PRICE_LEVEL_L2];	// 申卖量
	XINT32	offerPx[NUM_PRICE_LEVEL_L2];	// 申卖价

	XINT32	numTrades;						// 成交比数
	XINT64	totalVolumeTrade;				// 成交总量
	XINT64	totalValueTrade;				// 成交总金额
	XBIGINT	volInside;						// 内盘总量
	XBIGINT	volOutside;						// 外盘总量

	union
	{
		XBIGINT	vol_5;	//5日均量
		struct
		{
			XWORD NUM_STOCKS;	//成份股个数
			XWORD UP_NUM;		//上涨家数
			XWORD DOWN_NUM;		//下跌家数
			XWORD UN_CHANGE_NUM;	//平盘家数
		}index;	//指数
	};
	XINT64	totalBidQty;					// 委买总量
	XINT32	weightedAvgBidPx;				// 加权平均委买价格
	XINT32	altWeightedAvgBidPx;			// 调整加权平均委买价格
	XINT64	totalOfferQty;					// 委卖总量
	XINT32	weightedAvgOfferPx;				// 加权平均委卖价格
	XINT32	altWeightedAvgOfferPx;			// 调整加权平均委卖价格

	XINT32	IOPV;							// IOPV净值估值
	XINT32	yieldToMaturity;				// 到期收益率
	XINT64	totalWarrantExecQty;			// 权证执行的总数量

	XINT64	warLowerPx;						// 权证跌停价格
	XINT64	warUpperPx;						// 权证涨停价格

	XDWORD	verData;		//数据更新版本号
}market_data3,*pmarket_data3;

typedef struct tagUXSTimeSeed
{
	XDWORD		year:6;		//[year%60]
	XDWORD		month:4;
	XDWORD		day:5;
	XDWORD		hour:5;
	XDWORD		minute:6;
	XDWORD		second:6;
	XDWORD		seed;
}uxs_timeseed,*puxs_timeseed;

typedef struct tagUXSTreeData
{
	XBYTE	isGroup;			//是否分组
	XINT32	iconIndex;			//图标ID
	XINT64	parentCode;			//父ID
	XINT64	thisCode;			//本级ID
	union
	{
		XWORD	wName[32];		//显示名称:老版本兼容
		char	szName[64];		//显示名称:老版本兼容
	};
	union
	{
		XWORD	wInfo[64];		//备注信息
		char	szInfo[128];	//备注信息
	};
	XINT32	extLength;			//扩展数据长度
	XINT32	extLength2;			//扩展数据2长度
	XINT32	extLength3;			//扩展数据3长度
	XINT32	extLength4;			//扩展数据4长度
	XINT32	extLength5;			//扩展数据3长度
	XINT32	extLength6;			//扩展数据4长度
	char	reserved[40];		//保留信息
	char	dataBuffer[1];		//具体存放数据
}uxs_treedata,*puxs_treedata;
/////////////////////////////////////////////////////////////////////////////////////////////////
const XDWORD	UERROR_NO=0;
const XDWORD	UERROR_UNKNOW=9999;
/////////////////////////////////////////////////////////////////////////////
//system error:
const XDWORD UERROR_MALLOC=101;		//malloc/new data error
const XDWORD UERROR_NETWORK=102;		//net work error
const XDWORD UERROR_NOTSUPPORT=103;	//not support
const XDWORD UERROR_SERIAL=104;		//error serial id
const XDWORD UERROR_SRVNOTRUN=105;	//service not run
const XDWORD UERROR_BUSY=106;		//system busy
const XDWORD UERROR_NODATA=107;		//数据一致，无需下载
/////////////////////////////////////////////////////////////////////////////
//public error:
const XDWORD UERROR_ACCOUNTEXIST=201;	//帐号已存在
const XDWORD UERROR_ACCOUNTNOTEXIST=202;	//帐号不存在
const XDWORD UERROR_PASSWD=203;			//passwd error
const XDWORD UERROR_NETWORKCARD=204;		//network card denied
const XDWORD UERROR_DISABLED=205;		//disabled error
const XDWORD UERROR_EXPIRED=206;			//expired error
const XDWORD UERROR_IPADDRESS=207;		//ipaddress error
const XDWORD UERROR_OVERMAX=208;			//over max
const XDWORD UERROR_DENIED=209;			//denied

const XDWORD UERROR_FILEOPEN=301;		//open file error
const XDWORD UERROR_FILESEEK=302;		//seek file error
const XDWORD UERROR_FILEREAD=303;		//read file error
const XDWORD UERROR_FILEWRITE=304;		//write file error
const XDWORD UERROR_QUOTA=305;			//dir quota

const XDWORD UERROR_DBCONNSRV=401;		//connect to database server
const XDWORD UERROR_DBREAD=402;			//read recordset error
const XDWORD UERROR_DBWRITE=403;			//write recordset error
const XDWORD UERROR_TABLENOTEXIST=404;	//表不存在
const XDWORD UERROR_UTSMASK=405;			//时间戳掩码错误
const XDWORD UERROR_CREATETABLE=406;		//创建表失败
///////////////////////////////////////////////////////////////////////////////////////////////////
#pragma pack(pop)
#endif
