/**
 *  TrayFlashController.h
 *
 *  托盘"新消息闪头像"的<u>纯逻辑状态机</u>。把"当前有哪些会话未读、谁是最近一条、
 *  单击托盘该打开谁、托盘该闪哪张头像"这套状态从 CMainDlg 里抽出来，做成一个
 *  不依赖 HWND、不依赖 Shell_NotifyIcon、不依赖 Flamingo 业务对象的纯类，便于
 *  被 FlamingoUnit 测试工程直接断言。真正的托盘图标切换 / 弹窗 / 开聊天窗等带
 *  窗口副作用的部分仍留在 CMainDlg 与 CBalloonMainPanel。
 *
 *  与 v2 消息流的衔接：CBalloonMainPanel::OnRecvChatMessage 收到一条"应在托盘
 *  闪烁"的消息（非窗口抖动、对端发来、且对应聊天窗当前未打开）时，把会话标识与
 *  画头像所需的首字 / 底色算好，经自定义窗口消息回投给 CMainDlg，由 CMainDlg
 *  调本类 PushUnread；用户打开 / 读完某会话时调 ClearConversation。头像首字与
 *  底色由调用方（主面板，持有联系人 / 群资料）算好传入，本类只存不算 —— 故本类
 *  不依赖 monogram 渲染、调色板等任何 GDI 设施。
 *
 *  排序约定：内部集合<u>最近在前</u>（下标 0 = 最近）。同一会话重复到达时把它
 *  移到最前并累加未读，与 QQ"最新活动浮到顶部"一致；单击托盘打开的就是下标 0。
 *
 *  本文件遵循 balloonui 头文件约定中"纯模型只依赖标准头"的做法（参照
 *  MainPanelTrayMenu.h）：只依赖 <windows.h>（COLORREF）、<string>、<vector>、
 *  <cstdint>，字符串用 std::wstring 而非 CString，避免 MFC/ATL 在共享头里冲突。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#pragma once

#include <windows.h>      // COLORREF
#include <cstdint>        // int64_t
#include <string>
#include <vector>

namespace balloonmain {

/**
 *  TrayFlashEntry —— 托盘未读会话集合里的一条。承载"单击开窗"与"悬停弹窗 / 闪头像"
 *  两个消费方各自所需的最小数据。会话用 (m_ctype, m_cid) 唯一标识。
 */
struct TrayFlashEntry
{
    int          m_ctype;        // 会话类型：1=单聊（m_cid 为对端 userid）、2=群聊（m_cid 为群 id）
    int64_t      m_cid;          // 会话 ID：单聊为对端 userid、群聊为群 id
    int          m_unreadCount;  // 本会话累计未读条数（≥1），悬停弹窗以红色角标显示
    std::wstring m_displayName;  // 会话显示名（好友昵称 / 群名），悬停弹窗用；可为空
    std::wstring m_preview;      // 最新一条消息的预览文字（与会话列表 digest 同口径，如"[图片]"/正文）；悬停弹窗副行显示，过长由控件自动省略号截断
    std::wstring m_avatarText;   // monogram 头像首字（取显示名前 1-2 个字符）；可为空
    COLORREF     m_avatarColor;  // monogram 头像底色（由调用方按 id / 文本稳定取色）

    TrayFlashEntry()
        : m_ctype(1)
        , m_cid(0)
        , m_unreadCount(0)
        , m_avatarColor(RGB(0, 0, 0))
    {
    }
};

/**
 *  CTrayFlashController
 *
 *  托盘新消息闪烁的状态机。持有一组"待处理未读会话"，对外回答三件事：是否在闪
 *  （IsFlashing）、单击该打开哪个会话（GetTarget）、悬停弹窗 / 闪头像该用哪些数据
 *  （GetEntries / GetTarget）。本类不创建窗口、不发系统调用，全部为内存操作。
 */
class CTrayFlashController
{
public:
    CTrayFlashController();

    /**
     *  收到一条"应在托盘闪烁"的新消息时调用（语义判定由调用方完成：非窗口抖动、
     *  对端发来、且对应聊天窗当前未打开）。
     *    若 (ctype,cid) 已在集合中：未读 +1，并把它移到最前（成为最近一条），同时
     *      用本次传入的 displayName / avatarText / avatarColor 覆盖刷新（晚到的资料更准）。
     *    若为新会话：以未读 1 追加为最近一条。
     *
     *    ctype        会话类型（1 单聊 2 群聊）。
     *    cid          会话 ID（单聊=对端 userid，群聊=群 id）；应 >0，否则本次调用被忽略。
     *    displayName  会话显示名（昵称 / 群名）；可空。
     *    preview      最新一条消息的预览文字（覆盖刷新：每来一条都更新成最新这条）；可空。
     *    avatarText   头像首字（1-2 字符）；可空。
     *    avatarColor  头像底色（调用方按 id / 文本取色）。
     */
    void PushUnread(int ctype, int64_t cid, const std::wstring& displayName,
                    const std::wstring& preview, const std::wstring& avatarText,
                    COLORREF avatarColor);

    /**
     *  某会话被打开 / 读完：未读清零并移出闪烁集合。集合里没有该会话时无操作。
     *    ctype  会话类型（1 单聊 2 群聊）。
     *    cid    会话 ID。
     *  @return 处理后是否<u>仍</u>处于闪烁中（集合里还有别的未读会话）。
     */
    bool ClearConversation(int ctype, int64_t cid);

    /**
     *  清空全部未读会话（如注销 / 退出登录 / "取消闪烁"）。清空后 IsFlashing 为 false。
     */
    void Clear();

    /**
     *  是否正处于闪烁中（集合非空）。
     */
    bool IsFlashing() const;

    /**
     *  当前未读<u>会话</u>数（= 集合大小，不是未读消息总条数）。
     */
    int GetConversationCount() const;

    /**
     *  取"最近一条"未读会话 —— 单击托盘要打开、要闪头像的就是它。
     *    out  [出参] 命中时写入最近一条；未命中不修改。
     *  @return true 且通过 out 带回最近一条；集合为空时返回 false。
     */
    bool GetTarget(TrayFlashEntry& out) const;

    /**
     *  取全部未读会话，按"最近在前"排序 —— 悬停弹窗据此逐行渲染。返回的引用指向
     *  内部集合，调用方不得跨"下一次 PushUnread / ClearConversation / Clear"持有。
     */
    const std::vector<TrayFlashEntry>& GetEntries() const;

private:
    /**
     *  在集合里按 (ctype,cid) 找下标；找不到返回 -1。
     */
    int FindEntry(int ctype, int64_t cid) const;

private:
    // 待处理未读会话集合。约定<u>下标越小越"近"</u>（下标 0 = 最近一条，单击打开它）。
    // 同一会话重复到达时移到下标 0 并累加未读。生命周期与本控制器一致。
    std::vector<TrayFlashEntry> m_entries;
};

} // namespace balloonmain
