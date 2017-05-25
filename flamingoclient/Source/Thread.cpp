/**
* 线程类基类, Thread.cpp
* zhangyl 2017.03.13
**/
#include "stdafx.h"
#include "Thread.h"

CThread::CThread()
{

}

CThread::~CThread()
{

}

void CThread::Start()
{
    m_spThread.reset(new std::thread(std::bind(&CThread::ThreadProc, this)));
}

void CThread::ThreadProc()
{
    Run();
}

void CThread::Join()
{
    m_spThread->join();
}