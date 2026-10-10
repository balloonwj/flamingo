/**
 *  文件共享分享者一端「删除」用到的回收站判定与执行，ShareRecycle.h
 *
 *  做法依据 docs/文件共享方案.md 8.5 的实证（第 2 期第 1 步）：超过回收站容量的文件、经网络路径访问的文件，
 *  Windows 的文件操作接口在「放入回收站、不确认、不弹界面」时不提示就直接彻底删除。因此：
 *    1. 执行之前先判定：目标所在的卷是本机固定盘、该卷的回收站没有设为直接删除、本次要删的总字节数不超过
 *       该卷回收站的容量；任一条不满足回 810（error_code_share_recycle_unavailable），不执行；
 *    2. 执行时用 IFileOperation 放进回收站（不确认、不弹界面、出错即停），并挂进度回调：PreDeleteItem 收到的
 *       标志不含 TSF_DELETE_RECYCLE_IF_POSSIBLE 时中止，回 810；
 *    3. 不用 FOF_WANTNUKEWARNING：它会在分享者的桌面上弹确认框。
 *
 *  判定规则是纯函数（ShareRecyclePolicy），可脱离系统单测；查卷信息与执行删除在 CShareSystemRecycler 里，经接口
 *  IShareRecycler 交给写操作模块（ShareWriteOps），单元测试用假实现，不往真实的回收站里放东西。
 *  单元测试见 FlamingoFileShare/Tests/ShareRecycleTests.cpp（标签 [sharerecycle]）。
 *
 *  典型用法（分享者处理 sf_delete）：
 *      CShareSystemRecycler recycler;
 *      int code = recycler.CheckRecyclable(absPathOfAnyItem, totalBytes);
 *      if (code == error_code_ok)
 *          code = recycler.Recycle(absPath);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>

namespace fileshare {

//系统默认回收站容量的分段点：卷容量的前 40 GiB 按 10% 计，其余部分按 5% 计（本机 C: 与 E: 两个卷的注册表实际值都与此吻合）
const int64_t kShareRecycleFirstTierBytes = 40LL * 1024 * 1024 * 1024;

//默认容量算法里前一段与后一段的比例分母：前 40 GiB 取十分之一，其余取二十分之一
const int64_t kShareRecycleFirstTierDivisor = 10;
const int64_t kShareRecycleRestDivisor = 20;

//注册表里回收站容量 MaxCapacity 的单位：MB
const int64_t kShareRecycleCapacityUnitBytes = 1024LL * 1024;

//一个卷的回收站情况，由 CShareSystemRecycler::QueryVolume 填写，交给 ShareRecyclePolicy::Check 判定
struct ShareVolumeRecycleInfo
{
    UINT    m_driveType;      //GetDriveTypeW 的结果；只有 DRIVE_FIXED 能放进回收站
    bool    m_nukeOnDelete;   //该卷的回收站是否设为「不移到回收站，直接删除」
    int64_t m_capacityBytes;  //该卷回收站的容量，单位：字节；取不到时为 0（按不能放进回收站处理）

    ShareVolumeRecycleInfo()
        : m_driveType(DRIVE_UNKNOWN)
        , m_nukeOnDelete(false)
        , m_capacityBytes(0)
    {
    }
};

/**
 *  ShareRecyclePolicy —— 能否放进回收站的判定规则；全部为静态纯函数，不可实例化。
 */
class ShareRecyclePolicy
{
public:
    /**
     *  注册表里没有该卷的回收站容量时，按系统默认值估算：卷容量的前 40 GiB 取 10%，其余部分取 5%。
     *  @param volumeBytes 卷的总容量，单位：字节；小于等于 0 时返回 0
     *  @return 估算的回收站容量，单位：字节
     */
    static int64_t DefaultCapacityBytes(int64_t volumeBytes);

    /**
     *  判定一批条目能否放进回收站。按总字节数而不是按单个条目判定，是保守的做法：几个各自放得下的条目合起来超过
     *  容量时也会被拒，宁可拒绝也不彻底删除。
     *  @param info       目标所在卷的回收站情况
     *  @param totalBytes 本次要删除的全部条目的总字节数（目录按其下全部文件之和），须 >= 0
     *  @return error_code_ok；不能放进回收站时为 error_code_share_recycle_unavailable（810）
     */
    static int Check(const ShareVolumeRecycleInfo& info, int64_t totalBytes);

private:
    ShareRecyclePolicy() = delete;
};

/**
 *  回收站的执行接口。真实实现是 CShareSystemRecycler；单元测试用假实现记录调用、按需返回失败。
 *  方法可在任意线程调用，但同一个对象不应被多条线程同时使用。
 */
class IShareRecycler
{
public:
    virtual ~IShareRecycler()
    {
    }

    /**
     *  判定一批条目能否放进回收站（查卷信息后按 ShareRecyclePolicy::Check 判定）。
     *  @param anyPath    任一条目的本机绝对路径（同一个共享根下的条目都在同一个卷上）
     *  @param totalBytes 全部条目的总字节数
     *  @return error_code_ok；否则为 810
     */
    virtual int CheckRecyclable(const std::wstring& anyPath, int64_t totalBytes) = 0;

    /**
     *  把一个文件或目录放进回收站。
     *  @param absPath 本机绝对路径（不带 \\?\ 前缀）
     *  @return error_code_ok；系统判定不能放进回收站时为 810；条目不存在为 809；其余失败（被占用、无权限、
     *          路径过长导致外壳无法解析等）为 811
     */
    virtual int Recycle(const std::wstring& absPath) = 0;
};

/**
 *  CShareSystemRecycler —— 用系统接口实现的回收站：查卷信息（驱动器类型、注册表里的回收站设置、卷容量），
 *  经 IFileOperation 放进回收站。不打日志以外的副作用：只删除调用方指定的条目。
 */
class CShareSystemRecycler : public IShareRecycler
{
public:
    CShareSystemRecycler()
    {
    }

    /**
     *  查一个路径所在卷的回收站情况。
     *  @param absPath 本机绝对路径；UNC 路径与映射的网络盘得到非 DRIVE_FIXED 的驱动器类型
     *  @param out     [出参] 卷的回收站情况；取不到卷时保持默认值（按不能放进回收站处理）
     *  @return 取到卷的根目录返回 true
     */
    static bool QueryVolume(const std::wstring& absPath, ShareVolumeRecycleInfo& out);

    virtual int CheckRecyclable(const std::wstring& anyPath, int64_t totalBytes);

    virtual int Recycle(const std::wstring& absPath);
};

} // namespace fileshare
