/**
 *  flamingoAdmin —— 公告中心右栏「公告详情」面板 AnnouncementPreviewPanel。
 *
 *  AnnouncementsScreen 把"公告预览"整块的视觉与交互整体外包给本控件,自身
 *  退回到"页头 + 左侧列表 + 滚动条 + 选公告分发"这层骨架。
 *
 *  目的:让公告详情里的文字字段(标题 / 正文 / 日期 / 发送者 / 受众 / 已读
 *  进度 / 推送描述)都能被鼠标拖选 + Ctrl+C 复制,告别原 PaintPreview 全自绘
 *  导致"看得到但选不中"的现状。
 *
 *  实现思路 —— 借鉴 PageHeader,装饰图形 + 文字子控件【混合】:
 *    · 文字字段全部外化为 balloonwjui 现有子控件:
 *        DuiLabel(单行 selectable) —— 日期 / 发送者+受众 / 进度数字 /
 *                                       推送描述 / 进度卡 / 调度卡的小标题;
 *        DuiRichEditHost(SetReadOnly + MultiLine + WordWrap +
 *                        SetShowBorder(false) + SetShowVScroll(false) +
 *                        SetDefaultFontFromHFONT) —— 标题 / 正文(多行可选
 *                        可复制,与卡片底融合无边框无滚动条,字体跟随
 *                        admin 业务字号);
 *        DuiButton ×5  —— 编辑 / 复制公告 / 重新推送 / 置顶 / 撤回 (删除)。
 *    · 装饰性图形(级别 / 已置顶 / 定时发送徽章圆角底、发送者头像、分隔线、
 *      进度条轨道与填充、调度卡左侧图标底)继续由 PreviewPanel::OnPaint 接管,
 *      在子控件 layout 后按子控件矩形位置画 —— 与 PageHeader 同 pattern。
 *
 *  外壳与 AnnouncementsScreen / AdminFrame 的衔接:
 *    · 滚动仍由 AnnouncementsScreen 管(OverlayScrollbar + m_previewScroll);
 *      本控件只暴露 GetDesiredSize().cy 让外层算 contentH / 滚动 range,
 *      外层 Layout 时按 (rcAvail.top - scroll) 偏移摆放本控件。
 *    · 5 个操作按钮各持有独立 ctrlId(UiIds.h::kIdAnnPreview*),DuiButton
 *      默认 NotifyParent(DUIN_CLICK) 直接一路冒到 AdminFrame 的 WM_DUI_NOTIFY
 *      switch,与 kIdAnnExport / kIdAnnNew 同 pattern。本控件【不】拦截
 *      DUIN_CLICK(balloonui 的 DuiControl 无 OnDuiNotify 中间 hook,只有
 *      host HWND 父接得到),保持架构直白。AdminFrame switch 在按钮 case
 *      里通过 m_announcementsScreen->GetSelectedId() 取当前 annId。
 *
 *  balloonwj@qq.com   2026-05-26
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Mock/MockData.h"

// balloonui 控件前置声明 —— 私有成员仅持有裸指针,所有权在 DuiVBox 基类的
// m_children 里;.cpp 再 include 完整头。
namespace balloonwjui
{
class DuiLabel;
class DuiButton;
class DuiRichEditHost;
class DuiControl;
}

namespace flamingoadmin {

// 正文区悬浮滚动条(与左侧公告列表同款 auto-hide 细条)。
class OverlayScrollbar;

// 公告中心右栏的「公告详情」面板。
class AnnouncementPreviewPanel : public balloonwjui::DuiVBox
{
public:
    // 构造时把卡片底 + 全部子控件就位;子控件文字初始为空,SetAnnouncement
    // 第一次调用前面板呈现"未选择公告"占位行为(m_emptyHint 可见,其它子
    // 全部隐藏)。
    AnnouncementPreviewPanel();

    // 绑定 / 解除当前展示的公告对象。
    //   a:公告对象指针(借用,本控件不持有);nullptr = 切回"未选择公告"
    //     状态(m_emptyHint 可见,其它子隐藏)。
    //   一次调用会:
    //     1) 转写 7 个文字子控件的 GetText() = 公告对应字段;
    //     2) 按 a->m_pinned / a->m_scheduled 切换 2 个徽章 placeholder 的可见性;
    //     3) 按 a->m_scheduled 在 progress / schedule 两组子控件上做整组
    //        SetVisible 切换(同时只有一组可见);
    //     4) 按 a->m_pinned 改置顶按钮文字("置顶" / "取消置顶");
    //     5) 按 a->m_status 改最后一个按钮文字("撤回" / "删除");
    //     6) 缓存 a 指针到 m_current 供 OnPaint 装饰阶段读 severity / sender
    //        等字段画徽章 / 头像。
    void SetAnnouncement(const Announcement* a);

    // DuiControl / DuiVBox 覆写:
    //   Layout :调基类之前,按 rcAvail 可用宽度量算 m_titleEdit / m_bodyEdit
    //           应得高度(RichEdit pre-HWND 拿不到准确行高,用 DrawText
    //           DT_CALCRECT 兜底估算),再 SetHint 更新 Fixed,让基类按新
    //           高度排版。SetAnnouncement(nullptr) 时两个 Fixed 设 0(可见
    //           性也为 false,不参与排版)。
    //   OnPaint:基类先画卡片底 + 转发全部子控件,然后装饰阶段按各占位子
    //           的 m_rcItem 画徽章圆角底 / 头像 / 分隔线 / 进度条 / 调度卡
    //           图标底等图形元素。m_current == nullptr 时跳过装饰阶段。
    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    // 把正文 RichEdit 当前的竖直滚动度量(内容总高 / 偏移)同步给悬浮滚动条
    // m_bodySb。在正文滚动回调、SetAnnouncement 收尾、Layout 收尾各调一次,
    // 保证细条的滑块大小 / 位置与正文实际内容一致。HWND 未建时为空操作。
    void UpdateBodyScrollbar();

    // 正文 RichEdit 因滚轮 / 键盘滚动导致位置变化时的回调(注册给
    // DuiRichEditHost::SetOnVScrollChanged)。ctx 为本面板 this。
    static void OnBodyRichEditScrolled(void* ctx);

    // 悬浮滚动条被拖动时的回调(注册给 OverlayScrollbar::SetOnScroll)。
    //   ctx:本面板 this;newScroll:拖动产生的新像素偏移。
    static void OnBodyScrollbarDrag(void* ctx, int newScroll);

    // 当前展示公告的【本地拷贝】(panel 自有,生命周期与 panel 同步)。
    // 历史教训:之前缓存的是外部 DataStore vector 元素的裸指针;DataStore
    // 在 SetAnnouncements 时整体替换 vector,旧指针即刻悬空,等用户切到
    // 公告中心首帧 OnPaint 就 AV(2026-05-26 19:17:07 那次崩溃)。
    // 改为复制一份进来,与 DataStore 解耦。
    Announcement m_currentData;

    // m_currentData 是否有效。false = 未选中,m_current 同时为 nullptr,
    // 走"未选中公告"占位分支(m_emptyHint 可见,其它子全隐)。
    bool m_hasCurrent = false;

    // 装饰阶段(OnPaint / Layout)读取当前公告的统一入口:
    //   - m_hasCurrent=true  时指向 &m_currentData;
    //   - m_hasCurrent=false 时为 nullptr。
    // 保留指针形态是为了把原 m_current==nullptr 判空分支整片代码保留下来,
    // 不动 OnPaint / Layout 的判空骨架。
    const Announcement* m_current = nullptr;

    // ---- 顶行(HeaderRow):级别 / 已置顶 / 定时发送徽章 + 弹性占位 + 日期 ----
    balloonwjui::DuiControl* m_severityChipSlot  = nullptr;  // 级别徽章占位(装饰画在 m_rcItem 里)
    balloonwjui::DuiControl* m_pinnedChipSlot    = nullptr;  // "已置顶"徽章占位(可见性切换)
    balloonwjui::DuiControl* m_scheduledChipSlot = nullptr;  // "定时发送"徽章占位(可见性切换)
    balloonwjui::DuiLabel*   m_dateLabel         = nullptr;  // 日期 / 计划时间(selectable 单行)★ 强 7

    // ---- 标题 ----
    balloonwjui::DuiRichEditHost* m_titleEdit = nullptr;     // 标题(只读多行可复制)★ 强 7

    // ---- 发送者行(SenderRow):头像占位 + "发送者  ·  受众:xxx" ----
    balloonwjui::DuiControl* m_avatarSlot       = nullptr;   // 头像占位(装饰画在 m_rcItem 里)
    balloonwjui::DuiLabel*   m_senderMetaLabel  = nullptr;   // "发送者  ·  受众:xxx" 单行截断 ★ 强 7×2

    // ---- 分隔线 ----
    balloonwjui::DuiControl* m_dividerSlot = nullptr;        // 1px 灰线占位(装饰画在 m_rcItem 里)

    // ---- 正文 ----
    // 正文放在一个 HBox 行里:[ 正文 RichEdit(Weight 1) ][ 悬浮滚动条(固定窄带) ]。
    // 正文关掉原生滚动条(SetShowVScroll(false)),改由 m_bodySb 这条悬浮细条
    // 承载滚动显示,风格与左侧公告列表一致;细条画在 RichEdit 子窗口矩形之外
    // 的右侧窄带里,不会被 HWND 盖住。
    balloonwjui::DuiRichEditHost* m_bodyEdit = nullptr;      // 正文(只读多行可复制,固定高度+内部滚动)★ 强 7
    OverlayScrollbar*             m_bodySb   = nullptr;      // 正文悬浮滚动条(auto-hide,与列表同款)

    // ---- 已发布 → 已读进度卡(ProgressCard):带卡片底 + 内嵌进度条 ----
    balloonwjui::DuiVBox*    m_progressCard       = nullptr; // 进度卡容器(卡片底)
    balloonwjui::DuiLabel*   m_progressLeftLabel  = nullptr; // "送达 · 已读"
    balloonwjui::DuiLabel*   m_progressRightLabel = nullptr; // "X / Y 人 (Z%)" ★ 强 7
    balloonwjui::DuiControl* m_progressBarSlot    = nullptr; // 进度条占位(在 ProgressCard 内,装饰)

    // ---- 定时 → 调度队列卡(ScheduleCard):带卡片底 + 内嵌图标占位 ----
    balloonwjui::DuiVBox*    m_scheduleCard      = nullptr;  // 调度卡容器(卡片底)
    balloonwjui::DuiControl* m_scheduleIconSlot  = nullptr;  // 图标占位(在 ScheduleCard 内,装饰)
    balloonwjui::DuiLabel*   m_scheduleTopLabel  = nullptr;  // "尚未发送 · 已加入调度队列"
    balloonwjui::DuiLabel*   m_scheduleSubLabel  = nullptr;  // "将于 X 推送至 Y 名用户" ★ 强 7

    // ---- 操作按钮(ActionRow):5 个 DuiButton + 1 弹性占位 ----
    //
    // 每个按钮持有独立 ctrlId (UiIds.h::kIdAnnPreview*),DuiButton 默认
    // NotifyParent(DUIN_CLICK) 一路冒到 AdminFrame 的 WM_DUI_NOTIFY switch;
    // 本控件不拦截,与 admin 现有 ctrlId 分发 pattern 一致。
    balloonwjui::DuiButton*  m_btnEdit      = nullptr;       // 编辑
    balloonwjui::DuiButton*  m_btnCopy      = nullptr;       // 复制公告
    balloonwjui::DuiButton*  m_btnRepush    = nullptr;       // 重新推送
    balloonwjui::DuiButton*  m_btnTogglePin = nullptr;       // 置顶 / 取消置顶
    balloonwjui::DuiButton*  m_btnDeleteAnn = nullptr;       // 删除公告(任意状态物理删除,Danger)
    balloonwjui::DuiButton*  m_btnDelete    = nullptr;       // 撤回(仅已发布显示,Danger,右对齐)

    // ---- 未选中占位 ----
    balloonwjui::DuiLabel*   m_emptyHint    = nullptr;       // "未选择公告"(SetAnnouncement(nullptr) 时唯一可见的子)
};

} // namespace flamingoadmin
