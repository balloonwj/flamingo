#pragma once
#include <stdint.h>

//可以在作用域消失以后自动释放的迷你内存类
class CMiniBuffer
{
public:
	CMiniBuffer(int64_t nSize, BOOL bAutoRelease = TRUE);
	~CMiniBuffer();

	void Release();

	int64_t GetSize();
	char* GetBuffer();
	
	//TODO: 加一个接口，使CMiniBuffer对象可以直接被当作字符串指针使用
	//PSTR operator PSTR(); 
	
	void EnableAutoRelease(BOOL bAutoRelease);
	BOOL IsAutoRelease();

private:
	BOOL	m_bAutoRelease;
    int64_t	m_nSize;
	char*	m_pData;
};