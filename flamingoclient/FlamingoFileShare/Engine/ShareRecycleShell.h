/**
 *  经 IFileOperation 把一个条目放进回收站的底层实现，ShareRecycleShell.h（只供 ShareRecycle.cpp 使用）
 *
 *  单独成文件的原因：IFileOperation、IFileOperationProgressSink、SHCreateItemFromParsingName 在 SDK 头文件里要求
 *  _WIN32_IE >= 0x0700，而 FlamingoFileShare 与 FlamingoUnit 的 stdafx.h 把 _WIN32_IE 定为 0x0601。实现文件因此不包含
 *  stdafx.h，自己定义版本宏（Windows 7 满足 0x0700 的要求，与 DLL 的最低系统一致）。本文件只用基本类型，不依赖那些宏。
 *  结果只描述发生了什么、不打日志，由 ShareRecycle.cpp 归为错误码并记日志。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

namespace fileshare {

//执行到哪一步失败，ShareShellRecycleResult::m_stage 的取值
enum share_shell_recycle_stage
{
    share_shell_stage_done        = 0,   //操作已执行（结果看其余字段）
    share_shell_stage_parse_path  = 1,   //SHCreateItemFromParsingName 失败（路径不存在、外壳无法解析超长路径等）
    share_shell_stage_create_op   = 2,   //创建 IFileOperation 失败
};

//一次「放进回收站」的结果
struct ShareShellRecycleResult
{
    int  m_stage;           //失败所在的步骤，share_shell_recycle_stage
    long m_hr;              //该步骤或 PerformOperations 的 HRESULT
    bool m_advised;         //进度回调是否挂上了
    bool m_vetoed;          //进度回调是否因「不能放进回收站」而中止了操作
    bool m_aborted;         //GetAnyOperationsAborted 的结果
    bool m_deleteReported;  //是否收到过删除成功的回报
    bool m_recycledItem;    //删除成功时是否在回收站里得到了新条目；为假说明被彻底删除了

    ShareShellRecycleResult()
        : m_stage(share_shell_stage_done)
        , m_hr(0)
        , m_advised(false)
        , m_vetoed(false)
        , m_aborted(false)
        , m_deleteReported(false)
        , m_recycledItem(false)
    {
    }
};

/**
 *  在已初始化 COM 的线程上把一个条目放进回收站：不确认、不弹任何界面、出错即停，不带 FOF_WANTNUKEWARNING；
 *  进度回调在 PreDeleteItem 收到的标志不含 TSF_DELETE_RECYCLE_IF_POSSIBLE 时中止操作。
 *  @param absPath 本机绝对路径（不带 \\?\ 前缀），以 NUL 结尾
 *  @param out     [出参] 结果
 */
void ShareShellRecycleItem(const wchar_t* absPath, ShareShellRecycleResult& out);

} // namespace fileshare
