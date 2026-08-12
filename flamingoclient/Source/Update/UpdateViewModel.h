/**
 *  升级窗的<u>纯数据 / 纯逻辑</u>部分，UpdateViewModel.h
 *
 *  把"这次检查更新的结果该让升级窗显示成什么样"这件事，从窗口类
 *  CBalloonUpdateWindow 里抽出来，做成不依赖 HWND / balloonui / 业务对象的纯函数，
 *  便于被 FlamingoUnit 测试工程直接调用断言。窗口只负责把状态摆出来，不自己做判断。
 *  做法与既有的 BalloonMain/AboutDlgModel.h 一致。
 *
 *  典型用法：
 *      update::UpdateTaskResult r;
 *      if (update::UpdateManager::GetInstance().TakeLastResult(r))
 *      {
 *          update::UpdateViewState st = update::BuildUpdateViewState(r);
 *          m_labTitle->SetText(st.title.c_str());
 *          m_btnLater->SetVisible(st.showLaterButton);
 *          ...
 *      }
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>

#include "UpdateManager.h"   // UpdateTaskResult

namespace update {

//升级窗的四种形态。窗口按形态决定摆哪些控件，形态本身由 BuildUpdateViewState 推出。
enum UpdateViewKind
{
    //有可用新版本，且不强制。用户可以点「立即更新」，也可以点「稍后」关掉。
    update_view_has_update = 0,
    //有可用新版本且强制升级。此时用户本来就登不上（服务端会以
    //error_code_version_too_old 拒绝登录），故不给"稍后再说"的出口。
    update_view_forced     = 1,
    //已是最新版本。关于页点「检查更新」的常见结果。
    update_view_latest     = 2,
    //本次检查未能完成（超时 / 未连接 / 服务端返回了业务错误码）。
    update_view_failed     = 3,
};

//升级窗要呈现的完整状态。窗口按它摆控件，不再自己判断。
struct UpdateViewState
{
    UpdateViewKind kind;             //形态
    bool           showLaterButton;  //是否显示「稍后」按钮
    bool           closable;         //窗口是否允许关闭（含标题栏关闭按钮与 ESC）
    bool           canDownload;      //是否给「立即更新」入口
    std::wstring   title;            //标题文案
    std::wstring   versionLine;      //版本行文案；无更新 / 失败时为空串

    UpdateViewState()
        : kind(update_view_failed), showLaterButton(true)
        , closable(true), canDownload(false)
    {
    }
};

/**
 *  由一次检查更新的结果推出升级窗该显示什么。
 *
 *    r： 一次检查（可选下载）的结果，通常取自 UpdateManager::TakeLastResult
 *
 *  @return 升级窗要呈现的完整状态。纯函数、无副作用。
 *
 *  两条容易写错、故意用测试钉住的规则：
 *    · r.m_hasUpdate 为 true 但 r.m_packageUsable 为 false（服务端报告有更新，却未下发完整的
 *      令牌 / MD5 / 大小）时一律按<u>无更新</u>处理，不给下载入口 —— 下了也校验不过，
 *      让用户点一个必然失败的按钮不如不给。
 *    · 只有强制升级才把 closable 置 false。其余形态一律允许关窗。
 *
 *    currentVersion： 本机当前版本号（宽字符），用于拼版本行「当前版本 X → 最新版本 Y」。
 *                     传空串则版本行只写最新版本。
 */
UpdateViewState BuildUpdateViewState(const UpdateTaskResult& r,
                                     const std::wstring& currentVersion);

/**
 *  算下载进度的百分比。
 *
 *    received： 已接收并落盘的字节数
 *    total：    文件总字节数
 *
 *  @return 0~100 的整数。
 *          total <= 0 时返回 0（服务端还没告知总大小，不能除零）；
 *          received 大于 total 这种异常输入钳到 100，不会返回大于 100 的值。
 *
 *  中间量按 64 位算：3 GiB 级别的包 received * 100 在 32 位下会溢出成负数，
 *  进度条会出现回退。
 */
int ComputeDownloadPercent(long long received, long long total);

/**
 *  这次检查结果是否应当拦住登录流程。
 *
 *    r： 一次检查的结果
 *
 *  @return 只有"确认为强制升级且包可用"时返回 true，其余一律 false。
 *
 *  特别地，检查失败 / 超时一律<u>放行</u> —— 检查更新这条链路自己挂了却把用户挡在
 *  门外，是拿自己的故障惩罚用户。真有版本过低的问题，服务端在 cmd_login 那一步还会
 *  再拒一次（error_code_version_too_old），拦不住的漏不掉。
 */
bool ShouldBlockLogin(const UpdateTaskResult& r);

} // namespace update
