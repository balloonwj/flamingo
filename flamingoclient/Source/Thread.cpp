/**
* 线程类基类, Thread.cpp
* zhangyl 2017.03.13
**/
#include "stdafx.h"

#include <functional>

#include "Thread.h"

CThread::CThread()
{

}

CThread::~CThread()
{

}

void CThread::Start()
{
    if (!m_spThread)
        m_spThread.reset(new std::thread(std::bind(&CThread::ThreadProc, this)));
}

void CThread::ThreadProc()
{
    Run();
}

void CThread::Join()
{
    if (m_spThread && m_spThread->joinable())
        m_spThread->join();
}