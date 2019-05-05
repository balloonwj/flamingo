#pragma once

#include "MysqlTask.h"
#include <stdint.h>

#define MAX_TASK_NUM 1000

class CTaskList
{
public:
    CTaskList();
    ~CTaskList(void);

    bool Push(IMysqlTask* poTask);                  // 逻辑线程修改 
    IMysqlTask* Pop();				                // 数据库线程修改 

private:
	uint16_t            m_uReadIndex;               // 数据库线程修改 
	uint16_t            m_uWriteIndex;              // 逻辑线程修改   
	IMysqlTask*         m_pTaskNode[MAX_TASK_NUM];
};



