
#pragma once

#include "types.h"
#include <stdio.h>

#define WSYSPLUS_API
#define NOVTABLE

///////////////////////////////////////////////////////////////////////////////////////////////////
//分配内存:lsize为需要的长度,msize不为空时存储实际分配的长度(一般都会多分配一些,避免越界)
//成功返回内存地址,失败返回NULL
//注意:分配成功的内存必须由wsysplus_release释放
char* wsysplus_malloc(XINT32 lsize, XINT32* msize = NULL);
void  wsysplus_count(XINT32 lsize);

//释放内存(lpData为内存地址)
//释放完毕,自动将地址指针置NULL
//lpData为NULL时,立即返回,不会异常
void  wsysplus_release(char*&lpData);

///////////////////////////////////////////////////////////////////////////////////////////////////
class WSYSPLUS_API  NOVTABLE  wsysplus_memory
{
public:
	//申请内存,成功返回地址,失败返回NULL
	//如果已分配控件够用,则复用之;否则,重新申请
	//成功返回时,地址空间已经全部清0
	LPSTR	GetBuf(XINT32 lsize);

	//为空时不会返回NULL,而是0结束的字符串
	LPSTR	SafeBuffer(void);

	//扩展现有内存到新长度,成功返回首地址,失败返回NULL
	//成功申请时,会拷贝原地址数据到新空间
	LPSTR	Extern(XINT32 maxSize);

	//拷贝数据到本类中(会自动判断内存长度),成功返回首地址,失败返回NULL
	LPSTR	Copy(LPCVOID lpData, XINT32 lsize);

	//追加数据到本类中(会自动判断内存长度),成功返回首地址,失败返回NULL
	LPSTR	Append(LPCVOID lpData, XINT32 lsize);

	//绑定另一管理类,复用其数据
	void	Attach(wsysplus_memory*pMemory);

	//释放内存
	void	Release(void);
	//置空,相应的结构已经在外部挪用
	void	Detach(void);

	//操作符重载:返回内存首地址
	inline operator LPCSTR(void) const { return bufData; }

	//Copy函数操作符重载
	const wsysplus_memory& operator=(LPCSTR lpString);
	const wsysplus_memory& operator=(wsysplus_memory&iMemory);

	//Append函数操作符重载
	const wsysplus_memory& operator+=(LPCSTR lpString);

	wsysplus_memory(LPCSTR lpString = NULL);
	wsysplus_memory(wsysplus_memory&iMemory);
	~wsysplus_memory(void);

	// UMYPRIVATE:
	XINT32	sizeData;		//已使用数据长度
	XINT32	sizeMalloc;		//实际分配数据长度
	LPSTR	bufData;		//数据首地址
};

//分配管理器:
//1.独立申请,一次释放
//2.析构函数自动释放,无需人工干预
//3.适用于小范围集中内存管理
class WSYSPLUS_API  NOVTABLE wsysplus_vector
{
	typedef	struct tagItem
	{
		tagItem	*pNext;
		char	szData[1];
	}ITEM, *PITEM;
public:
	//申请
	LPSTR	Malloc(XINT32 dataSize, XINT32*msize = NULL);
	//一次释放所有已分配内存
	void	ReleaseAll(void);
	//转移
	void	Attach(wsysplus_vector*pVector);

	wsysplus_vector(XINT32 nType = 0);
	~wsysplus_vector(void);
private:
	PITEM	_items;
	XINT32	_nType;	//0:内存池分配;1:直接分配
};

//使用普通内存类逐步扩展内存太浪费了,造成内存抖动
//所以做了一个数组类,一次预分配多点空间
class WSYSPLUS_API  NOVTABLE  wsysplus_array
{
public:
	//队尾追加numData个数据
	BOOL	Append(LPCVOID lpData, XDWORD numData = 1);
	XDWORD	GetNext(LPVOID lpData, BOOL blFirst = FALSE, XDWORD maxData = 1);

	void	Release(void);
	XDWORD	GetTotal(void);

	//perSize是数组单位宽度
	wsysplus_array(XDWORD perSize);
	~wsysplus_array(void);
protected:
	typedef struct tagBufArray
	{
		tagBufArray*pNext;
		char	data[4000];
	}BUF_ARRAY, *PBUF_ARRAY;

	PBUF_ARRAY	_ptrData;	//数据首页地址
	PBUF_ARRAY	_ptrEnd;	//数据尾页地址(不检索,直接追加,快!)
	PBUF_ARRAY	_ptrRead;	//当前读取的页面地址
	XDWORD	_perSize;	//数组单位宽度
	XDWORD	_off;		//当前分配的页面偏移
	XDWORD	_rdoff;		//当前读取偏移
	XDWORD	_Total;		//已使用总个数
};