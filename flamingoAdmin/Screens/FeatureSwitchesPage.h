/**
 *  flamingoAdmin —— 企业详情屏「功能开关」子 tab 的内容页 FeatureSwitchesPage。
 *
 *  2026-10-08 起，原先分散在两处的功能开关集中到这里：平台超管在企业设置弹窗里代企业设置的那一块、
 *  企业管理员在平台设置页「功能开关」卡里设置本企业的那一张卡都已删除。两种角色看到的内容完全一样：
 *  13 个开关（各一个完整样式的 FeatureSwitchPanel）加一个「云端录像空间」面板（CloudRecordQuotaPanel），
 *  按类别分成四张卡片：
 *    · 账号与会话：允许用户自助注册、邀请入群需被邀请人同意、单聊密聊、帖子群；
 *    · 文件与传输：文件共享、客户端点对点直连；
 *    · 音视频：群语音 / 群视频通话、语音字幕、云端录制，以及紧跟在云端录制之下的云端录像空间；
 *    · 机器人：机器人总开关、允许群主添加群机器人、允许员工配置个人机器人、允许群主给群机器人配置外部服务
 *      （docs/机器人方案.md §11 Q18：四个机器人开关排在云端录像空间之后，本页保持这一顺序）。
 *  宽度够时排成两栏（左栏前两张卡、右栏后两张卡），否则排成一栏；内容高过本页时可用滚轮或右缘的悬浮滚动条
 *  （OverlayScrollbar）上下滚动，绘制裁剪到本页矩形，滚出本页的部分既看不见也点不到。
 *
 *  本页不直接接触网络与对话框：各面板的请求出口与界面请求经 SetRequestSink / SetUiRequestHandler 交给宿主
 *  （见 FileShareUi.h）；开关的 DUIN_VALUECHANGED、「保存」按钮的 DUIN_CLICK 与接口应答由宿主（AdminFrame）
 *  转交 OnSwitchToggled / OnQuotaSaveClicked / OnApiResult。单元测试见 Tests/FeatureSwitchesPageTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      FeatureSwitchesPage* page = m_enterpriseDetail->GetFeaturesPage();
 *      page->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
 *      page->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, this);
 *      //切到「功能开关」tab 时：平台超管传企业 ID，企业管理员传 0
 *      page->Load(enterpriseId);
 *      //收到通知或应答时
 *      page->OnSwitchToggled(n->ctrlId, n->extra != 0);
 *      page->OnApiResult(tag, http);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "DuiControl.h"
#include "Screens/FileShareUi.h"

namespace flamingoadmin {

class FeatureSwitchPanel;
class CloudRecordQuotaPanel;
class OverlayScrollbar;

// 企业详情屏「功能开关」子 tab 的内容页：按类别分卡片放全部功能开关，可滚动。
class FeatureSwitchesPage : public balloonwjui::DuiControl
{
public:
    // 卡片（类别）的下标，自上而下、自左而右的顺序
    enum Group
    {
        kGroupAccount  = 0,   // 「账号与会话」：自助注册、邀请入群需同意、单聊密聊、帖子群
        kGroupTransfer = 1,   // 「文件与传输」：文件共享、点对点直连
        kGroupMedia    = 2,   // 「音视频」：群通话、语音字幕、云端录制，以及云端录像空间
        kGroupBot      = 3,   // 「机器人」：四个机器人开关
        kGroupCount    = 4,   // 卡片个数
    };

    // 开关的下标（Panel 的参数），按页面上的排列顺序
    enum Item
    {
        kItemRegister         = 0,    // 允许用户自助注册（register.enabled）
        kItemInviteConfirm    = 1,    // 邀请入群需被邀请人同意（group.inviteconfirm）
        kItemSecretChat       = 2,    // 单聊密聊（secretchat.enabled）
        kItemThreadGroup      = 3,    // 帖子群（threadgroup.enabled）
        kItemFileShare        = 4,    // 文件共享（fileshare.enabled）
        kItemP2P              = 5,    // 客户端点对点直连（p2p.enabled）
        kItemGroupCall        = 6,    // 群语音 / 群视频通话（groupcall.enabled）
        kItemCaption          = 7,    // 语音字幕（caption.enabled）
        kItemCloudRecord      = 8,    // 云端录制（cloudrecord.enabled），云端录像空间面板紧跟其下
        kItemBot              = 9,    // 机器人总开关（bot.enabled）
        kItemBotGroup         = 10,   // 允许群主添加群机器人（bot.groupbot）
        kItemBotPersonal      = 11,   // 允许员工配置个人机器人（bot.personal）
        kItemBotGroupExternal = 12,   // 允许群主给群机器人配置外部服务（bot.groupexternal）
        kItemCount            = 13,   // 开关个数
    };

    // 构造时建好全部面板与滚动条，此时不发请求。
    FeatureSwitchesPage();

    // 设置全部面板的请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user);
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user);

    // 读某个企业的全部设置（每个面板各发一个 setting/get.do），并滚回顶部。
    //   enterpriseId：平台超管要设置的企业（大于 0）；企业管理员传 0，由服务端按本企业处理。
    // 副作用：重排并重绘本页。
    void Load(long long enterpriseId);

    // 当前作用的企业（最近一次 Load 传入的值，企业管理员为 0）。
    long long EnterpriseId() const { return m_enterpriseId; }

    // 接收转交来的异步接口结果，交给读写标签与之相同的面板。
    //   tag：结果标签。http：HTTP 往返结果（仅本次调用内有效）。
    // 返回值：标签属于本页的某个面板时为 true（已处理），否则为 false，宿主继续往下分发。
    bool OnApiResult(ApiTag tag, const HttpResult& http);

    // 某个开关被拨动了（开关的 DUIN_VALUECHANGED）：交给它所在的面板弹确认框。
    //   ctrlId：通知里的控件 ID。on：开关拨动后的状态。
    // 返回值：ctrlId 是本页某个开关时为 true（已处理），否则为 false。
    bool OnSwitchToggled(UINT ctrlId, bool on);

    // 「云端录像空间」面板的「保存」被点了（DUIN_CLICK）。
    //   ctrlId：通知里的控件 ID。
    // 返回值：ctrlId 是该「保存」按钮时为 true（已交给面板处理），否则为 false。
    bool OnQuotaSaveClicked(UINT ctrlId);

    // ---- 只读访问（宿主与单元测试用）----

    // 某个开关的面板（借用，所有权在本页的控件树中）；item 越界时返回 nullptr。
    FeatureSwitchPanel* Panel(int item) const;

    // 「云端录像空间」面板（借用，所有权在本页的控件树中）。
    CloudRecordQuotaPanel* GetCloudQuotaPanel() const { return m_cloudQuotaPanel; }

    // 某个开关属于哪张卡片（静态函数，便于单元测试）；item 越界时返回 -1。
    static int GroupOf(int item);

    // 某张卡片的标题（静态函数，便于单元测试）；group 越界时返回空串。
    static LPCTSTR GroupTitle(int group);

    // 打开本页时传给 Load 的企业 ID（静态函数，便于单元测试）。
    //   platformAdmin：登录者是否为平台超管。detailEnterpriseId：企业详情屏当前企业的 ID 文字。
    // 返回值：平台超管返回该企业的 ID（文字不是正整数时为 0）；企业管理员一律返回 0，由服务端按本企业处理
    //         （企业管理员不能指定企业，与原平台设置页「功能开关」卡的做法相同）。
    static long long EnterpriseIdForLoad(bool platformAdmin, const CString& detailEnterpriseId);

    // 某张卡片当前的矩形（host-client 坐标，已减去滚动偏移）；须在 Layout 之后读取，group 越界时返回空矩形。
    RECT GroupRect(int group) const;

    // 当前是否排成两栏（须在 Layout 之后读取）。
    bool IsTwoColumns() const { return m_twoColumns; }

    // 当前滚动偏移（像素，0 表示在顶部）。
    int GetScroll() const { return m_scroll; }

    // 最大滚动偏移（像素）：内容总高减去本页高度，内容放得下时为 0。须在 Layout 之后读取。
    int GetScrollMax() const { return m_scrollMax; }

    // 滚动到指定偏移并重新摆放子控件。
    //   scroll：目标偏移（像素），超出 [0, GetScrollMax()] 时夹取到边界。
    // 副作用：偏移确有变化时重排并重绘本页，同步滚动条。
    void ScrollTo(int scroll);

    // ---- DuiControl 覆写 ----
    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 滚轮：内容放不下时滚动本页并返回 true；放得下时返回 false，交给上层处理。
    //   pt：host-client 坐标（不使用）；zDelta：滚轮增量（WHEEL_DELTA 的倍数或分数）；mkFlags：不使用。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 某张卡片内容区的高度（不含卡片的上下边距）：标题 + 各面板 + 面板之间的间距。
    static int GroupContentHeight(int group);

    // 某张卡片的总高度（像素）。
    static int GroupHeight(int group);

    // 把某张卡片里的面板按 cardRect 依次摆放。
    //   group：卡片下标。cardRect：卡片矩形（host-client 坐标，已减去滚动偏移）。
    void LayoutGroup(int group, const RECT& cardRect);

    // 滚动条拖动回调（DuiScrollBar::OnScrollFn）：ctx 为本页，newScroll 为新的滚动偏移。
    static void OnScrollbarScroll(void* ctx, int newScroll);

    FeatureSwitchPanel*    m_panels[kItemCount];          // 各开关的面板，下标见 Item（借用，所有权在控件树中）
    CloudRecordQuotaPanel* m_cloudQuotaPanel;             // 云端录像空间面板（借用，所有权在控件树中）
    OverlayScrollbar*      m_scrollbar;                   // 右缘的悬浮滚动条（借用，所有权在控件树中）
    RECT                   m_groupRects[kGroupCount];     // 各卡片的矩形（host-client 坐标），每次 Layout 时重算
    bool                   m_twoColumns;                  // 本次排版是否为两栏，每次 Layout 时重算
    long long              m_enterpriseId;                // 最近一次 Load 的企业；企业管理员为 0，未加载时为 0
    int                    m_scroll;                      // 当前滚动偏移（像素）；Load 时归零
    int                    m_scrollMax;                   // 最大滚动偏移（像素），每次 Layout 时重算；内容放得下时为 0
};

} // namespace flamingoadmin
