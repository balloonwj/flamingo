/**
 *  临界区的作用域锁，FlamingoLang.dll 内部使用。
 *
 *  构造时进入临界区，析构时离开；持锁期间抛出异常（如内存不足）也能保证释放，避免之后的调用一直等锁。
 *
 *  典型用法：
 *      CScopedLock guard(&m_lock);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

namespace flamingolang {

/**
 *  临界区的作用域锁。不可复制。
 */
class CScopedLock
{
public:
    /**
     *  进入临界区。
     *  @param lock 已初始化的临界区，归调用方所有，生命周期须长于本对象
     */
    explicit CScopedLock(CRITICAL_SECTION* lock) : m_lock(lock)
    {
        ::EnterCriticalSection(m_lock);
    }

    //离开临界区。
    ~CScopedLock()
    {
        ::LeaveCriticalSection(m_lock);
    }

    CScopedLock(const CScopedLock&) = delete;
    CScopedLock& operator=(const CScopedLock&) = delete;

private:
    CRITICAL_SECTION* m_lock;   //所保护的临界区，由调用方持有
};

} // namespace flamingolang
