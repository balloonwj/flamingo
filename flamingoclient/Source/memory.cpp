#include "wsysplus.h"
#include "../base/logging.h"

#include <mutex>
#include <atomic>

std::atomic<XINT32>   __memory_count__(0);
/////////////////////////////////////////////////////////////////////////////////////////////////
template<class _Tc>
class template_malloc
{
#pragma pack(push,1)	//强制本结构1字节对齐
	typedef	struct tagItem
	{
		tagItem	*next;
		_Tc	data;
	}ITEM, *PITEM;
#pragma pack(pop)
public:
	char*mlloc(void)
	{
		PITEM pitem = NULL;

		std::unique_lock<std::mutex> lock(__mutex__);
		if ( NULL != (pitem = __item__))
		{
			--__count__;
			__item__ = __item__->next;
			*(XINT32*)pitem = sizeof(_Tc);
			return(pitem->data);
		}
		return NULL;
	}

	void release(char*pdata)
	{
		PITEM pitem = (PITEM)pdata;
		memset(pitem, 0, sizeof(ITEM));
		
		std::unique_lock<std::mutex> lock(__mutex__);

		pitem->next = __item__;
		__item__ = pitem;
		++__count__;
	}
	void clean(void)
	{
		char*lpData = NULL;
		
		std::unique_lock<std::mutex> lock(__mutex__);
		for (PITEM pnext = NULL; __item__; __item__ = pnext)
		{
			pnext = __item__->next;
			lpData = (char*)__item__;
			delete[] lpData;
		}
		__count__ = 0;
	}
	template_malloc(void) : __count__(0), __item__(NULL)
	{
	}
	~template_malloc(void)
	{
		clean();
	}
	XINT32   __count__;
	PITEM	__item__;
	std::mutex __mutex__;
};
/////////////////////////////////////////////////////////////////////////////////////////////////
static template_malloc<char[64]>			_alloc_64;
static template_malloc<char[128]>			_alloc_128;
static template_malloc<char[256]>			_alloc_256;
static template_malloc<char[512]>			_alloc_512;
static template_malloc<char[1024]>			_alloc_1k;
static template_malloc<char[1024 * 4L]>		_alloc_4k;
static template_malloc<char[1024 * 16L]>		_alloc_16k;
static template_malloc<char[1024 * 64L]>		_alloc_64k;
static template_malloc<char[1024 * 256L]>		_alloc_256k;
static template_malloc<char[1024 * 512L]>		_alloc_512k;
static template_malloc<char[1024 * 1024L]>	_alloc_1m;
/////////////////////////////////////////////////////////////////////////////////////////////////
#define	__malloc__(T,P)\
	else if (lsize <= T)\
{\
	lpData = P.mlloc(); \
	_size = T; \
}
WSYSPLUS_API char*wsysplus_malloc(XINT32 lsize, XINT32*msize)
{
#ifdef DEBUG
	__memory_count__ += 1;
#endif
	char*lpData = NULL;
	XINT32 _size(0);
	if (lsize <= 64)
	{
		lpData = _alloc_64.mlloc();
		_size = 64;
	}
	__malloc__(128, _alloc_128)
		__malloc__(256, _alloc_256)
		__malloc__(512, _alloc_512)
		__malloc__(1024, _alloc_1k)
		__malloc__(1024 * 4L, _alloc_4k)
		__malloc__(1024 * 16L, _alloc_16k)
		__malloc__(1024 * 64L, _alloc_64k)
		__malloc__(1024 * 256L, _alloc_256k)
		__malloc__(1024 * 512L, _alloc_512k)
		__malloc__(1024 * 1024L, _alloc_1m)
	else
	{
		_size = lsize;
	}
	if (!lpData)
	{
		char*lpMalloc = new char[_size + sizeof(void*)+32];
		if (!lpMalloc) return(NULL);
		*(XINT32*)lpMalloc = _size;
		memset(lpData = lpMalloc + sizeof(void*), 0, _size + 32);
	}
	if (msize) *msize = _size;
	//	LogDebugA(ID_MEMORY_ALLOC,"malloc index=%d, addir=%x", __memory_count__, lpData);
	return(lpData);
}

WSYSPLUS_API void wsysplus_count(XINT32 lsize)
{
	switch (lsize)
	{
	case	64:			LOG_INFO << "_alloc_64 count = " << _alloc_64.__count__; break;
	case	128:		LOG_INFO << "_alloc_128 count = " << _alloc_128.__count__; break;
	case	256:		LOG_INFO << "_alloc_256 count = " << _alloc_256.__count__; break;
	case	512:		LOG_INFO << "_alloc_512 count = " << _alloc_512.__count__; break;
	case	1024:		LOG_INFO << "_alloc_1k count = " << _alloc_1k.__count__; break;
	case	1024 * 4L:	LOG_INFO << "_alloc_4k count = " << _alloc_4k.__count__; break;
	case	1024 * 16L:	LOG_INFO << "_alloc_16k count = " << _alloc_16k.__count__; break;
	case	1024 * 64L:	LOG_INFO << "_alloc_64k count = " << _alloc_64k.__count__; break;
	case	1024 * 256L:	LOG_INFO << "_alloc_256k count = " << _alloc_256k.__count__; break;
	case	1024 * 512L:	LOG_INFO << "_alloc_512k count = " << _alloc_512k.__count__; break;
	case	1024 * 1024L:	LOG_INFO << "_alloc_1m count = " << _alloc_1m.__count__; break;
	}
}

WSYSPLUS_API void wsysplus_release(char*&lpData)
{
	if (lpData)
	{
		//		LogDebugA(ID_MEMORY_ALLOC,"release index=%d, addir=%x", __memory_count__, lpData);
#ifdef DEBUG
		__memory_count__--;
#endif
		char*lpMalloc = lpData - sizeof(void*);
		switch (*(XINT32*)lpMalloc)
		{
		case	64:			_alloc_64.release(lpMalloc); break;
		case	128:		_alloc_128.release(lpMalloc); break;
		case	256:		_alloc_256.release(lpMalloc); break;
		case	512:		_alloc_512.release(lpMalloc); break;
		case	1024:		_alloc_1k.release(lpMalloc); break;
		case	1024 * 4L:	_alloc_4k.release(lpMalloc); break;
		case	1024 * 16L:	_alloc_16k.release(lpMalloc); break;
		case	1024 * 64L:	_alloc_64k.release(lpMalloc); break;
		case	1024 * 256L:	_alloc_256k.release(lpMalloc); break;
		case	1024 * 512L:	_alloc_512k.release(lpMalloc); break;
		case	1024 * 1024L:	_alloc_1m.release(lpMalloc); break;
		default: delete[] lpMalloc;
		}
		lpData = NULL;
	}
}

WSYSPLUS_API void wsysplus_memory_clean(void)
{
	_alloc_64.clean();
	_alloc_128.clean();
	_alloc_256.clean();
	_alloc_512.clean();
	_alloc_1k.clean();
	_alloc_4k.clean();
	_alloc_16k.clean();
	_alloc_64k.clean();
	_alloc_256k.clean();
	_alloc_512k.clean();
	_alloc_1m.clean();
}
/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
wsysplus_memory::wsysplus_memory(LPCSTR lpString) :sizeData(0), sizeMalloc(0), bufData(NULL)
{
	if (lpString)
		wsysplus_memory::Copy(lpString, (XINT32)strlen(lpString));
}

wsysplus_memory::wsysplus_memory(wsysplus_memory&iMemory) :
sizeData(0), sizeMalloc(0), bufData(NULL)
{
	if (iMemory.bufData>0)
		wsysplus_memory::Copy(iMemory.bufData, (XINT32)strlen(iMemory.bufData));
}

wsysplus_memory::~wsysplus_memory(void)
{
	if (bufData)
	{
		wsysplus_release(bufData);
		bufData = NULL;
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////
LPSTR wsysplus_memory::GetBuf(XINT32 lsize)
{
	if ((sizeData = lsize) + 8>sizeMalloc)
	{
		if (bufData)
			wsysplus_release(bufData);
		bufData = wsysplus_malloc(lsize + 16, &sizeMalloc);
	}
	if (bufData&&sizeMalloc)
		memset(bufData, 0, sizeMalloc);
	return(bufData);
}

LPSTR wsysplus_memory::SafeBuffer(void)
{
	return(bufData ? bufData : NULL);
}

void wsysplus_memory::Release(void)
{
	if (bufData)
		wsysplus_release(bufData);
	sizeData = sizeMalloc = 0;
}

//置空,相应的结构已经在外部挪用
void wsysplus_memory::Detach(void)
{
	bufData = NULL;
	sizeData = sizeMalloc = 0;
}

LPSTR wsysplus_memory::Copy(LPCVOID lpData, XINT32 lsize)
{
	if (!wsysplus_memory::GetBuf(lsize))	return(NULL);
	if (sizeData == lsize)
		memcpy(bufData, lpData, lsize);
	return(bufData);
}

LPSTR wsysplus_memory::Append(LPCVOID lpData, XINT32 lsize)
{
	if (lsize)
	{
		if (!Extern(sizeData + lsize)) return(NULL);
		memcpy(bufData + sizeData, lpData, lsize);
		sizeData += lsize;
	}
	return(bufData);
}

LPSTR wsysplus_memory::Extern(XINT32 maxSize)
{
	if (maxSize + 8>sizeMalloc)
	{
		XINT32 sizeNew(0);
		LPSTR bufNew = wsysplus_malloc(maxSize + 16, &sizeNew);
		if (!bufNew) return(NULL);
		if (bufData)
		{
			if (sizeData)
				memcpy(bufNew, bufData, sizeData);
			wsysplus_release(bufData);
		}
		bufData = bufNew;
		sizeMalloc = sizeNew;
	}
	return(bufData);
}

const wsysplus_memory&wsysplus_memory::operator=(LPCSTR lpString)
{
	if (lpString)
		wsysplus_memory::Copy(lpString, (XINT32)strlen(lpString));
	else
		wsysplus_memory::Copy(NULL, 0);
	return(*this);
}

const wsysplus_memory&wsysplus_memory::operator=(wsysplus_memory&iMemory)
{
	if (this == &iMemory)
	{
		return *this;
	}

	if (iMemory.bufData)
		wsysplus_memory::Copy(iMemory.bufData, (XINT32)strlen(iMemory.bufData));
	else
		wsysplus_memory::Copy(NULL, 0);
	return(*this);
}

const wsysplus_memory&wsysplus_memory::operator+=(LPCSTR lpString)
{
	if (lpString)
		wsysplus_memory::Append(lpString, (XINT32)strlen(lpString));
	return(*this);
}

void wsysplus_memory::Attach(wsysplus_memory*pMemory)
{
	if (this == pMemory)
	{
		return ;
	}

	if (bufData)
		wsysplus_release(bufData);

	sizeData = pMemory->sizeData;
	sizeMalloc = pMemory->sizeMalloc;
	bufData = pMemory->bufData;

	pMemory->sizeData = pMemory->sizeMalloc = 0;
	pMemory->bufData = NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
wsysplus_vector::wsysplus_vector(XINT32 nType) :_items(NULL), _nType(nType)
{
}

wsysplus_vector::~wsysplus_vector(void)
{
	wsysplus_vector::ReleaseAll();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
LPSTR wsysplus_vector::Malloc(XINT32 dataSize, XINT32*msize)
{
	XINT32 nMalloc(0);
	LPSTR lpBuffer = NULL;
	if (_nType)
	{
		lpBuffer = new char[dataSize + sizeof(ITEM)];
		if (lpBuffer)
			memset(lpBuffer, 0, nMalloc = dataSize + sizeof(ITEM));
	}
	else
		lpBuffer = wsysplus_malloc(dataSize + sizeof(ITEM), &nMalloc);
	if (lpBuffer)
	{
		PITEM pItem = (PITEM)lpBuffer;
		pItem->pNext = _items;
		_items = pItem;
		if (msize) *msize = nMalloc - sizeof(ITEM);
		return(pItem->szData);
	}
	return(NULL);
}

void wsysplus_vector::ReleaseAll(void)
{
	LPSTR lpBuffer(NULL);
	for (PITEM pNext = NULL; _items; _items = pNext)
	{
		pNext = _items->pNext;
		lpBuffer = (LPSTR)_items;
		if (_nType) delete[] lpBuffer;
		else wsysplus_release(lpBuffer);
	}
}

void wsysplus_vector::Attach(wsysplus_vector*pVector)
{
	if (this == pVector)
	{
		return;
	}
	wsysplus_vector::ReleaseAll();
	_items = pVector->_items;
	pVector->_items = NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
wsysplus_array::wsysplus_array(XDWORD perSize) :_ptrData(NULL), _ptrEnd(NULL), _ptrRead(NULL), _perSize(max((XDWORD)1, perSize))
, _off(0), _rdoff(0), _Total(0)
{
}

wsysplus_array::~wsysplus_array(void)
{
	wsysplus_array::Release();
}

void wsysplus_array::Release(void)
{
	for (PBUF_ARRAY pNext = NULL; _ptrData; _ptrData = pNext)
	{
		pNext = _ptrData->pNext;
		wsysplus_release((LPSTR&)_ptrData);
	}
	_ptrData = _ptrEnd = _ptrRead = NULL;
	_off = _rdoff = _Total = 0;
}

XDWORD wsysplus_array::GetTotal(void)
{
	return(_Total);
}
/////////////////////////////////////////////////////////////////////////////
BOOL wsysplus_array::Append(LPCVOID lpData, XDWORD numData)
{
	if (numData<1) return(TRUE);
	if (!_ptrEnd)
	{
		if (!(_ptrEnd = (PBUF_ARRAY)wsysplus_malloc(sizeof(BUF_ARRAY)))) return(FALSE);
		_ptrRead = _ptrData = _ptrEnd;
		_off = 0;
	}
	if (_off*_perSize >= 4000)
	{
		PBUF_ARRAY pArray = (PBUF_ARRAY)wsysplus_malloc(sizeof(BUF_ARRAY));
		if (!pArray) return(FALSE);
		_ptrEnd->pNext = pArray;
		_ptrEnd = pArray;
		_off = 0;
	}
	XDWORD i(0);
	LPCSTR bufData = (LPCSTR)lpData;
	for (; i<numData && (_off*_perSize)<4000; ++i, ++_off, ++_Total)
	{
		memcpy(_ptrEnd->data + _off*_perSize, bufData + i*_perSize, _perSize);
	}
	return(i<numData ? Append(bufData + i*_perSize, numData - i) : TRUE);
}

XDWORD wsysplus_array::GetNext(LPVOID lpData, BOOL blFirst, XDWORD maxData)
{
	if (blFirst)
	{
		_rdoff = 0;
		_ptrRead = _ptrData;
	}
	if (!_ptrRead) return(0);
	XDWORD n(0), i(0);
	LPSTR bufData = (LPSTR)lpData;
	for (; _ptrRead; _ptrRead = _ptrRead->pNext, _rdoff = 0)
	{
		n = (_ptrRead == _ptrEnd) ? _off : (4000 / _perSize);
		for (; _rdoff<n&&i<maxData; ++_rdoff, ++i)
		{
			memcpy(bufData + i*_perSize, _ptrRead->data + _rdoff*_perSize, _perSize);
		}
		if (i >= maxData) break;
	}
	return(i);
}
////////////////////////////////////////////////////////////////////
#ifdef TEST_SOLUTION
WSYSPLUS_API XINT32 wsysplus_getcount(XINT32 lsize)
{
	switch (lsize)
	{
	case	256:		return _alloc_256.__count__;
	case	512:		return _alloc_512.__count__;
	case	1024:		return _alloc_1k.__count__;
	case	1024 * 4L:	return _alloc_4k.__count__;
	case	1024 * 16L:	return _alloc_16k.__count__;
	case	1024 * 64L:	return _alloc_64k.__count__;
	case	1024 * 256L:	return _alloc_256k.__count__;
	case	1024 * 512L:	return _alloc_512k.__count__;
	case	1024 * 1024L:	return _alloc_1m.__count__;
	default: return 0;
	}
}
#endif
