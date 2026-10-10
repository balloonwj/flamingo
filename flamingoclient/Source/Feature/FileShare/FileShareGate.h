/**
 *  文件共享模块该不该处于「已登录」状态的判定（纯函数），FileShareGate.h
 *
 *  docs/文件共享方案.md 5.8、7.3（第 3 期第 1 步）：模块只在「已登录」且「本人所属企业的文件共享开关打开」时
 *  进入已登录状态（OnLoggedIn：拉取我的共享、上报可提供状态）。运行中开关由开变关（1013）时让模块按退出登录收尾
 *  （OnLoggingOut：关闭全部窗口、结束会话，DLL 不卸载）；由关变开时再进入已登录状态，已有共享随分享者的 2210
 *  逐步恢复「可用」。CFileShareHost 按本函数的结果动作，判定本身不碰 DLL、可单测。
 *  单元测试见 Source/Tests/FileShareGateTests.cpp（标签 [fileshareswitch]）。
 *
 *  典型用法（CFileShareHost::OnFeaturesChanged）：
 *      switch (FileShareGateAction(m_loggedIn, m_featureEnabled, m_moduleActive)) { ... }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

namespace balloonfileshare {

//宿主对模块要做的动作，FileShareGateAction 的返回值
enum file_share_gate_action
{
    file_share_gate_none       = 0,   //保持现状
    file_share_gate_activate   = 1,   //加载（尚未加载时）并让模块进入已登录状态（OnLoggedIn）
    file_share_gate_deactivate = 2,   //让模块按退出登录收尾（OnLoggingOut）：关闭窗口、结束会话，不卸载 DLL
};

/**
 *  判定宿主对模块要做的动作。
 *  @param loggedIn       是否处于已登录状态
 *  @param featureEnabled 本人所属企业的文件共享开关是否打开（登录应答的 features 与 1013 给出）
 *  @param moduleActive   模块当前是否处于已登录状态
 *  @return file_share_gate_action
 */
inline int FileShareGateAction(bool loggedIn, bool featureEnabled, bool moduleActive)
{
    if (loggedIn && featureEnabled)
        return moduleActive ? file_share_gate_none : file_share_gate_activate;
    return moduleActive ? file_share_gate_deactivate : file_share_gate_none;
}

} // namespace balloonfileshare
