#pragma once
#include <vector>
#include "Path.h"

class CFileTask;
//自动升级类
class Updater
{
public:
    Updater(CFileTask* pFileTask);
	~Updater();

	BOOL IsNeedUpdate();

public:
    CFileTask*			    m_pFileTask;
	std::vector<CString>	m_aryUpdateFileList;
};
