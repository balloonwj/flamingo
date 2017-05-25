/** 
* 线程类基类, Thread.h
* zhangyl 2017.03.13
**/
#pragma once
#include <thread>
#include <memory>

class CThread
{
public:
    CThread();
    virtual ~CThread();

    CThread(const CThread& rhs) = delete;
    CThread& operator=(const CThread& rhs) = delete;

    void Start();
    virtual void Stop() = 0;

    void Join();

protected:
    virtual void Run() = 0;

private:
    void ThreadProc();

protected:
    bool                            m_bStop{ false };

private:
    std::shared_ptr<std::thread>    m_spThread;
};