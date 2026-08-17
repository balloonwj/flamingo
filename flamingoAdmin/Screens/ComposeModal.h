/**
 *  flamingoAdmin —— 新建公告弹窗 ComposeModal。
 *
 *  公告中心点击「新建公告」时弹出。填写公告标题、正文、严重等级（3 档，对齐
 *  服务端 0 普通 / 1 提醒 / 2 警告）、受众类型（5 类，对齐服务端 audienceType）、
 *  是否置顶、发送时间（立即 / 定时）。受众选「指定企业 / 部门 / 员工」时，下方
 *  出现目标选择器：先选一个企业，再在其组织树里勾选部门 / 员工，已选目标可跨
 *  企业累加。提交后由 AdminFrame 走 save.do + send.do 落库并发布。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中表单卡片，常驻于 RootContainer
 *  弹窗层，开 / 关只切显隐；点遮罩不关闭。标题 / 正文 / 定时时间为
 *  DuiEdit 真输入框；严重等级 / 受众 / 置顶 / 发送方式 / 目标选择器为
 *  自绘可点区。目标选择器需要某企业的组织树时，经 WM_DUI_NOTIFY
 *  （ctrlId = kIdComposeModal）上报 AdminFrame 拉取，再经 SetOrgTree 回填。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"
#include "Util/AdminApi.h"   // AnnTarget

#include <vector>

namespace balloonwjui { class DuiEdit; }

namespace flamingoadmin {

class OverlayScrollbar;   // 投放目标选择器的悬浮滚动条子控件

// 新建公告弹窗。
class ComposeModal : public balloonwjui::DuiControl
{
public:
    ComposeModal();

    // 清空表单到初始状态（每次打开前调用）。
    void Reset();

    // 把弹窗切到「编辑既有公告」模式并回填表单（公告中心点「编辑」后，由
    // AdminFrame 取到 detail.do 结果后调用）。内部会先 Reset 再回填。
    //   announcementId：要编辑的公告 ID（提交时作为 save.do 的 announcementid）。
    //   title / body：标题与正文。
    //   severity：级别 0/1/2；audienceType：受众类型 0-4。
    //   pinned：是否置顶；targets：已选投放目标。
    void LoadForEdit(long long announcementId, const CString& title,
                     const CString& body, int severity, int audienceType,
                     bool pinned, const std::vector<AnnTarget>& targets);

    // 校验必填项：标题与正文非空；受众为「指定企业 / 部门 / 员工」时目标非空。
    bool Validate() const;

    // ---- 表单字段读取（供 AdminFrame 提交 save.do / send.do）----
    CString GetTitle() const;                  // 标题
    CString GetBody() const;                   // 正文

    // 严重等级：0 普通 / 1 提醒 / 2 警告，直接对齐服务端 severity。
    int GetSeverityCode() const { return m_severity; }

    // 受众类型：0-4，对齐服务端 audienceType。
    int GetAudienceType() const { return m_audience; }

    // 已选投放目标集（受众为指定企业 / 部门 / 员工时有值）。
    const std::vector<AnnTarget>& GetTargets() const { return m_targets; }

    bool    GetPinned() const    { return m_pinned; }
    bool    GetScheduled() const { return m_sendMode == 1; }
    CString GetScheduledTime() const;          // 定时发送时间文字

    // 提交时作为 save.do 的 announcementid：0 = 新建，非 0 = 编辑既有公告。
    long long GetEditAnnouncementId() const { return m_editAnnId; }

    // ---- 目标选择器与 AdminFrame 的数据交互 ----

    // 返回待拉取组织树的企业 ID（目标选择器选「指定部门 / 员工」并切换企业时
    // 置位）；为空表示当前无待拉取请求。AdminFrame 收到 kIdComposeModal 通知后
    // 读取它、发起 dept/tree.do。
    CString GetPendingTreeEnterpriseId() const { return m_pendingTreeEntId; }

    // AdminFrame 在 dept/tree.do 应答后回填某企业的组织树。
    //   enterpriseId：组织树所属企业 ID。
    //   root：组织树根节点。
    void SetOrgTree(const CString& enterpriseId, const OrgNode& root);

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool                     OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 投放目标悬浮滚动条拖动滑块时的回调（注册给 OverlayScrollbar）：把新
    // 滚动偏移写回本弹窗并刷新投放目标面板。
    //   ctx：本 ComposeModal 实例；newScroll：拖动产生的新滚动偏移。
    static void OnPickerSbScroll(void* ctx, int newScroll);
    // 一个可点击命中区。
    struct Hit
    {
        RECT    m_rc;       // 命中矩形
        int     m_kind;     // 0=选等级 1=选受众 2=切置顶 3=选发送方式
                            // 4=选目标作用域企业 5=勾选/取消一个目标
        int     m_arg;      // kind 0/1/3：下标
        int     m_tgtType;  // kind=5：目标类型（1 企业 / 2 部门 / 3 员工）
        CString m_tgtId;    // kind=4：企业 ID；kind=5：目标 ID
    };

    // 重新计算各表单区块的纵向位置（受众 / 发送方式变化会增删区块）。
    void RecomputeLayout();

    // 目标选择器是否可见（受众为指定企业 / 部门 / 员工时可见）。
    bool TargetPickerVisible() const;

    // 把 node 子树里与当前受众类型匹配的节点（部门或员工）平铺到 outList。
    void CollectScopeNodes(const OrgNode& node,
                           std::vector<const OrgNode*>& outList) const;

    // 判断某目标是否已在 m_targets 中。
    bool IsTargetChecked(int type, const CString& id) const;

    // 勾选 / 取消一个目标。
    void ToggleTarget(int type, const CString& id);

    // 自绘目标选择器面板。
    void PaintTargetPicker(HDC hdc, const RECT& rc);

    // 只把投放目标面板那块矩形（含其上方标签行）标记为脏并刷新。滚动列表、
    // 勾选目标、切换作用域企业、组织树回填等仅影响该面板的操作都走这里，
    // 不必整窗 Invalidate 把标题 / 正文 / 定时时间三个 DuiEdit 牵连进重绘。
    // 这个做法最初是为了消除那三处输入框的闪烁（当时它们内嵌真 Win32 输入框
    // 子窗口），改为无窗口控件之后闪烁已不存在，现在只用于减少重绘量。
    void InvalidatePicker();

    // 投放目标面板矩形（host-client 坐标）。
    RECT PickerRect() const;

    RECT m_panelRect{};                // 居中卡片矩形

    long long m_editAnnId = 0;         // 编辑模式的公告 ID（0 = 新建）
    int  m_severity = 0;               // 严重等级：0 普通 / 1 提醒 / 2 警告
    int  m_audience = 0;               // 受众类型：0-4
    bool m_pinned   = false;           // 是否置顶
    int  m_sendMode = 0;               // 发送方式：0 立即 / 1 定时

    std::vector<AnnTarget> m_targets;  // 已选投放目标（跨企业累加）

    // ---- 目标选择器作用域状态 ----
    CString m_scopeEntId;              // 当前作为目标作用域的企业 ID
    OrgNode m_scopeTree;               // m_scopeEntId 的组织树（SetOrgTree 回填）
    bool    m_scopeTreeLoaded = false; // m_scopeTree 是否已就绪
    CString m_pendingTreeEntId;        // 待 AdminFrame 拉取组织树的企业 ID
    int     m_pickerScroll = 0;        // 目标选择器列表滚动偏移
    int     m_pickerContentH = 0;      // 目标选择器列表内容总高

    // 投放目标悬浮滚动条（auto-hide 悬浮式子控件，见 Widgets/OverlayScrollbar）。
    OverlayScrollbar* m_pickerSb = nullptr;

    // ---- 各表单区块纵向位置（RecomputeLayout 算出，相对卡片 body 顶）----
    int  m_yTitle  = 0;                // 标题区
    int  m_yBody   = 0;                // 正文区
    int  m_ySev    = 0;                // 严重等级区
    int  m_yAud    = 0;                // 受众区
    int  m_yPicker = 0;                // 目标选择器区（不可见时与下一区块重合）
    int  m_yPin    = 0;                // 置顶区
    int  m_ySend   = 0;                // 发送方式区
    int  m_ySched  = 0;                // 定时时间区

    // 正文输入框的实际高度(像素)。RecomputeLayout 按"其余字段高度固定、正文
    // 吸收对话框剩余竖直空隙"算出 —— 让表单铺满对话框、正文框尽量大;最满
    // 情形(目标选择器 + 定时均显示)下退回默认高度。初值为默认高度。
    int  m_bodyEditH = 84;

    std::vector<Hit> m_hits;           // 本帧命中区集合

    balloonwjui::DuiEdit* m_editTitle    = nullptr;  // 标题输入框
    balloonwjui::DuiEdit* m_editBody     = nullptr;  // 正文输入框
    balloonwjui::DuiEdit* m_editSchedule = nullptr;  // 定时发送时间输入框
};

} // namespace flamingoadmin
