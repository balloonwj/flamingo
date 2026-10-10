/**
 *  共享电脑声音的进程内互斥（2026-10-06，docs/音视频通话方案.md 第 10 节），SystemAudioExclusive.h
 *
 *  电脑声音可以从两处发给对方：一对一通话里的「共享电脑声音」按钮，以及屏幕共享控制条上的「共享电脑声音」。两处同时
 *  打开时对方会听到两遍，所以同一时刻只允许一处在共享：后打开的一方在本类登记时，先前登记的一方收到 OnSystemAudioPreempted
 *  并停下自己的电脑声音。两个控制器（CCallController、CShareController）各自实现 SystemAudioHolder。
 *
 *  线程：只在 UI 线程使用（两个控制器的开关都在 UI 线程上），不加锁。
 *  生命周期：登记的一方在自己析构之前须调用 Release，避免本类留着悬空指针。
 *
 *  典型用法（控制器里）：
 *      //打开之前：让另一处停下
 *      call::SystemAudioExclusive::Instance().Acquire(this);
 *      ...
 *      //关闭之后（含通话结束、共享结束）：
 *      call::SystemAudioExclusive::Instance().Release(this);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

namespace call {

//正在（或即将）共享电脑声音的一方
class SystemAudioHolder
{
public:
    /**
     *  另一处开始共享电脑声音，本方须停下自己的电脑声音（在 UI 线程上、于对方 Acquire 的调用之内同步回调）。
     *  本方此时可能已经不在共享（例如漏了 Release），实现须能重复调用。
     */
    virtual void OnSystemAudioPreempted() = 0;

protected:
    ~SystemAudioHolder()
    {
    }
};

//共享电脑声音的进程内互斥：同一时刻最多一方登记
class SystemAudioExclusive
{
public:
    //进程内唯一的实例（函数内静态对象，首次调用时创建）
    static SystemAudioExclusive& Instance();

    SystemAudioExclusive();

    SystemAudioExclusive(const SystemAudioExclusive&) = delete;
    SystemAudioExclusive& operator=(const SystemAudioExclusive&) = delete;

    /**
     *  holder 开始共享电脑声音：当前登记的是另一方时，先回调它的 OnSystemAudioPreempted，再改登记为 holder。
     *  @param holder 开始共享的一方，不拥有；为 NULL 时什么也不做
     *  @return 被要求停下的那一方；没有（当前无人登记，或登记的就是 holder 自己）时返回 NULL
     */
    SystemAudioHolder* Acquire(SystemAudioHolder* holder);

    /**
     *  holder 停止共享电脑声音：当前登记的是它时清除登记；不是它时什么也不做（它已被别人抢占）。
     *  @param holder 停止共享的一方
     */
    void Release(SystemAudioHolder* holder);

    //当前登记的一方；没有时为 NULL
    SystemAudioHolder* Current() const;

private:
    SystemAudioHolder* m_current;   //当前登记、正在共享电脑声音的一方（不拥有）；没有时为 NULL
};

} // namespace call
