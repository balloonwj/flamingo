/**
 *  CallActionBarModel.h
 *
 *  通话窗底部操作条的<u>纯数据模型</u>：按当前形态（呼叫中 / 来电 / 通话中）、媒体类型、
 *  摄像头与静音开关，算出该显示哪几个按钮、各自是否处于激活态、哪个是危险操作；
 *  以及视频态下操作条何时该淡入淡出。
 *
 *  从对话框类里抽出来单独成模块，使这部分判断可被 FlamingoUnit 单元测试直接调用 ——
 *  不碰窗口、不碰网络、不碰绘制。真正的按钮创建与淡入淡出动画仍留在 CBalloonCallDlg。
 *
 *  全部为 header-only 的 inline 纯函数，与 BalloonMain/AvatarSpec.h 同做法：这类只做
 *  枚举 / 布尔映射的小模块无需单独编译单元，也免去在 Flamingo 与 FlamingoUnit 两个
 *  工程里各挂一次源文件。
 *
 *  头像的字形与底色<u>不在本文件</u>：项目已有 BalloonMain/AvatarSpec.h +
 *  AvatarPalette.h 提供（按 userid 稳定取色、无真图时回退字形），通话窗直接复用即可。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <vector>

namespace call {

/**
 *  操作条上一个按钮的身份。用于把"该显示哪些按钮"与"按钮长什么样 / 点了做什么"解耦：
 *  本模型只产出 id 序列，图标与点击分发由 CBalloonCallDlg 按 id 决定。
 */
enum CallActionId
{
    kActionMute        = 1,   // 静音：开关本端麦克风采集（纯本地，不影响对端）
    kActionCamera      = 2,   // 摄像头开关：只关自己的画面，不改会话媒体类型（仅视频态出现）
    kActionSwitchMedia = 3,   // 切换媒体类型：语音态"切到视频" / 视频态"切回语音"
    kActionHangup      = 4,   // 挂断：结束通话（通话中态）
    kActionAccept      = 5,   // 接听：来电态
    kActionReject      = 6,   // 拒绝：来电态
    kActionCancel      = 7,   // 取消：呼叫中态，被叫应答前撤回
    kActionVolume      = 8,   // 音量：点击弹出扬声器音量滑块（调本端听到的声音大小，纯本地）
};

/**
 *  通话窗的三种形态。与 CBalloonCallDlg::Mode 同值，单列一份是为了让本模型不依赖
 *  对话框头文件（否则测试工程要连带引入 ATL/WTL 与整个窗口类）。
 */
enum CallBarMode
{
    kBarCalling   = 0,   // 呼叫中：已发出邀请，等被叫应答
    kBarIncoming  = 1,   // 来电：收到邀请，等本端接听 / 拒绝
    kBarConnected = 2,   // 通话中：已接通
};

/**
 *  操作条上的一个按钮。
 */
struct CallActionItem
{
    int  m_id;       // 按钮身份，取值见 CallActionId
    bool m_active;   // 是否处于"已激活"态：静音已开、摄像头已关时为 true，绘制成实心 + 斜杠图标
    bool m_danger;   // 是否为危险操作（挂断 / 拒绝 / 取消）：绘制成红底白图标，直径更大

    CallActionItem()
        : m_id(0)
        , m_active(false)
        , m_danger(false)
    {
    }

    CallActionItem(int id, bool active, bool danger)
        : m_id(id)
        , m_active(active)
        , m_danger(danger)
    {
    }
};

/**
 *  操作条淡出前的静止时长（毫秒）。与 balloonui DuiScrollBar 的 auto-hide 口径一致
 *  （见开发约定文档「UI 约定」第 1 条：滚动淡入、约 800ms idle 后淡出），区别仅在于
 *  这里的触发源是鼠标移动而非滚动。
 */
const unsigned long long kActionBarIdleHideMs = 800;

/**
 *  算出当前该显示哪些按钮，按从左到右的顺序返回。
 *
 *  @param mode      通话窗形态，取值见 CallBarMode
 *  @param video     当前是否为视频通话（仅 kBarConnected 时有意义）
 *  @param cameraOn  本端摄像头是否开启（仅视频态有意义）；关闭时摄像头按钮呈激活态
 *  @param muted     本端是否已静音；已静音时静音按钮呈激活态
 *
 *  注：音量按钮恒不处于激活态 —— 它是"点开弹滑块"的入口，没有开 / 关两态可言。
 *  @return 按钮序列（值语义）。非通话中态不含静音 / 摄像头 / 切换 —— 通话还没接通，
 *          这些操作无从谈起，服务端也会拒绝（见 CallManager::switchMedia 的状态校验）。
 */
inline std::vector<CallActionItem> BuildActionBar(int mode, bool video, bool cameraOn, bool muted)
{
    std::vector<CallActionItem> items;

    if (mode == kBarIncoming)
    {
        //来电：拒绝在左、接听在右，与主流 IM 一致（危险操作在左，避免误点）
        items.push_back(CallActionItem(kActionReject, false, true));
        items.push_back(CallActionItem(kActionAccept, false, false));
        return items;
    }

    if (mode == kBarCalling)
    {
        //呼叫中：只有取消
        items.push_back(CallActionItem(kActionCancel, false, true));
        return items;
    }

    //通话中：静音恒有；摄像头仅视频态有；切换恒有；挂断在最右且为危险操作。
    items.push_back(CallActionItem(kActionMute, muted, false));
    //音量：调本端扬声器输出大小，与静音是两回事（静音掐的是本端麦克风采集，
    //音量调的是本端听到的声音），故两个按钮并存、互不替代。
    items.push_back(CallActionItem(kActionVolume, false, false));
    if (video)
    {
        //摄像头"已关闭"是需要高亮提示的状态，故 cameraOn 为 false 时置激活态
        items.push_back(CallActionItem(kActionCamera, !cameraOn, false));
    }
    items.push_back(CallActionItem(kActionSwitchMedia, false, false));
    items.push_back(CallActionItem(kActionHangup, false, true));
    return items;
}

/**
 *  判断视频态下浮在画面上的操作条此刻是否应该显示。
 *
 *  @param lastActivityMs 上次鼠标活动时刻（GetTickCount64 口径，毫秒）
 *  @param nowMs          当前时刻（同上）
 *  @param hovering       鼠标是否正悬停在操作条上
 *  @param video          当前是否为视频通话
 *  @return 应显示返回 true
 *
 *  规则：
 *    · 语音态恒显示 —— 画面区只有头像与昵称，操作条不挡任何东西，藏起来只会让用户找不到按钮。
 *    · 悬停其上时恒显示 —— 否则鼠标刚移上去它就淡出，根本点不着。
 *    · 静止时长<u>未超过</u>阈值时显示；边界（正好等于阈值）按显示处理。
 *    · 时钟回拨导致 now 早于 lastActivity 时差为负，按显示处理，不误判成超时。
 */
inline bool ShouldShowActionBar(unsigned long long lastActivityMs,
                                unsigned long long nowMs,
                                bool hovering,
                                bool video)
{
    if (!video)
    {
        return true;
    }
    if (hovering)
    {
        return true;
    }
    //无符号相减会绕回成极大值，故先判回拨
    if (nowMs <= lastActivityMs)
    {
        return true;
    }
    return (nowMs - lastActivityMs) <= kActionBarIdleHideMs;
}

} // namespace call
