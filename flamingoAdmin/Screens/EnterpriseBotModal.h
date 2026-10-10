/**
 *  flamingoAdmin —— 新建 / 编辑企业机器人的编辑窗 EnterpriseBotModal（docs/机器人方案.md 4.1）。
 *
 *  机器人页点「新建机器人」或某一行的「编辑」时弹出，左右两栏：
 *    - 左栏：名称（最多 20 字）、头像（文字最多 2 字 + 8 个底色色块，选法与新建服务号相同）、简介（最多 100 字）、
 *      状态（启用 / 停用）与「允许群主在本群关闭」、应用范围（全部群 / 所选的群，下拉框加带勾选框的群列表，
 *      与企业问答的编辑窗相同）；
 *    - 右栏：规则列表（每行一条规则摘要，按判定顺序排列；按钮「添加」「编辑」「上移」「下移」「删除」，双击一行也是
 *      编辑）、外部系统推送（已保存的机器人显示 Webhook 地址、令牌与签名密钥，可「复制」「重新生成」）、
 *      「试一试」（输入一段文字、是否 @、可选的群，按编辑窗里当前的内容判定会命中哪条规则、回复什么）。
 *  点「保存」时先在本地校验（botrule::ValidateBot），通过后由 AdminFrame 交给机器人页提交；服务端拒绝时在按钮栏
 *  左侧用红字提示，编辑窗不关闭。规则的添加与编辑在规则编辑窗（BotRuleModal）里做：AdminFrame 先调 BeginAddRule /
 *  BeginEditRule 取得要编辑的规则，规则编辑窗保存后再调 ApplyRuleEdit 写回本窗口。
 *
 *  实现与企业问答的编辑窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；点遮罩不关闭。
 *  控件的位置与字段标题都在 Layout 里算好，OnPaint 只按算好的位置画。单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      m_botModal->ShowForAdd(screen->Groups());
 *      m_root->ShowModal(m_botModal);
 *      //点「保存」时
 *      CString error;
 *      if (!m_botModal->Validate(&error)) { m_botModal->SetError(error); return; }
 *      m_botModal->SetBusy(true);
 *      screen->SubmitBot(m_botModal->GetBot());
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <set>
#include <vector>

#include "DuiControl.h"
#include "Util/AppConfig.h"
#include "Util/BotAdminApi.h"
#include "Util/BotRuleText.h"

namespace balloonwjui { class DuiEdit; class DuiButton; class DuiComboBox; class DuiListBox; }

namespace flamingoadmin {

// 新建 / 编辑企业机器人的编辑窗。
class EnterpriseBotModal : public balloonwjui::DuiControl
{
public:
    // Webhook 区的文字行数：地址、令牌、签名密钥各一行
    static const int kWebhookRowCount = 3;

    EnterpriseBotModal();

    // 切到「新建」：清空表单，头像底色取第一个色块，状态为启用，应用范围为「全部群」，没有规则。
    //   groups：本企业的群（可能还没拉到，为空；到了之后调 SetGroups 补上）。
    void ShowForAdd(const std::vector<botapi::BotGroup>& groups);

    // 切到「编辑」：用 bot（带规则与密钥的详情）预填表单，群列表按 groups 填好并勾选 bot 选中的群。
    void ShowForEdit(const botapi::BotItem& bot, const std::vector<botapi::BotGroup>& groups);

    // 群列表到了（或刷新了）：按 groups 重填应用范围的群列表与「试一试」的群下拉框，保留已勾选与已选的群。
    void SetGroups(const std::vector<botapi::BotGroup>& groups);

    // 外部服务的域名白名单到了（或改了）：本地校验外部服务地址时用它。
    //   domains：白名单；known：是否已经拉到（false 时本地不检查白名单，交给服务端）。
    void SetDomains(const std::vector<CString>& domains, bool known);

    // 控制台连接服务器所用的配置：拼 Webhook 完整地址用（botapi::WebhookUrl）。
    void SetServer(const ServerConfig& server);

    // 应用范围下拉框变了（kIdBotModalScope 的 DUIN_VALUECHANGED）：群列表只在「所选的群」时可用。
    void OnScopeChanged();

    // 规则列表的选中行变了（kIdBotModalRules 的 DUIN_VALUECHANGED）：刷新「编辑」「上移」「下移」「删除」是否可用。
    void OnRuleSelectionChanged();

    // 是否处于编辑态（false 为新建态）。
    bool IsEditing() const { return m_editingId > 0; }

    // 编辑态下正在编辑的机器人 ID；新建态返回 0。
    long long EditingId() const { return m_editingId; }

    // 表单当前的内容：名称、简介、头像文字已去掉首尾空白，应用范围为「全部群」时不带群，规则按列表顺序。
    botapi::BotItem GetBot() const;

    // 本地校验（botrule::ValidateBot；白名单已拉到时一并检查外部服务地址）。
    //   error：[出参，可为 NULL] 不通过时的中文原因。
    //   返回：通过返回 true。
    bool Validate(CString* error) const;

    // 设置按钮栏左侧的红字错误提示并重绘；空串表示不显示。
    void SetError(const CString& text);

    // 当前的错误提示。
    const CString& ErrorText() const { return m_error; }

    // 设置「提交中」状态：提交中「保存」按钮不可用并显示「保存中…」。
    void SetBusy(bool busy);

    // 是否处于提交中。
    bool IsBusy() const { return m_busy; }

    // ---- 规则列表 ----

    // 规则条数。
    int RuleCount() const { return static_cast<int>(m_rules.size()); }

    // 第 index 条规则（调用方保证 index 在 [0, RuleCount()) 内）。
    const botapi::BotRule& RuleAt(int index) const { return m_rules[static_cast<size_t>(index)]; }

    // 规则列表当前选中的行；没有选中时为 -1。
    int SelectedRuleIndex() const;

    // 选中规则列表的第 index 行（越界时取消选中），并刷新按钮是否可用。
    void SelectRule(int index);

    // 开始添加一条规则：返回新规则的初始内容（被 @ 时回复文字、启用），之后的 ApplyRuleEdit 追加到末尾。
    //   ruleOut：[出参] 新规则的初始内容。
    //   返回：规则已达 50 条时返回 false 并在本窗口提示，不能再添加。
    bool BeginAddRule(botapi::BotRule* ruleOut);

    // 开始编辑选中的规则：之后的 ApplyRuleEdit 替换这一条。
    //   ruleOut：[出参] 选中的规则。
    //   返回：没有选中规则时返回 false 并在本窗口提示。
    bool BeginEditRule(botapi::BotRule* ruleOut);

    // 规则编辑窗保存了：按 BeginAddRule / BeginEditRule 记下的位置追加或替换，并选中这一条。
    void ApplyRuleEdit(const botapi::BotRule& rule);

    // 把选中的规则上移（delta 为 -1）或下移（delta 为 1）一位，保持选中。
    //   返回：没有选中或已在顶 / 底时返回 false。
    bool MoveSelectedRule(int delta);

    // 删除选中的规则，选中它的下一条（没有下一条时选中上一条）。
    //   返回：没有选中规则时返回 false。
    bool DeleteSelectedRule();

    // ---- Webhook ----

    // 是否有可显示的令牌与密钥（已保存的机器人才有）。
    bool HasSecrets() const { return m_editingId > 0 && !m_token.IsEmpty(); }

    // 令牌与密钥重新生成了：botId 与正在编辑的机器人一致时更新显示，否则不做任何事。
    void SetSecrets(long long botId, const CString& token, const CString& secret);

    // Webhook 完整地址（新建态返回空串）。
    CString WebhookUrlText() const;

    // 「复制」按钮放进剪贴板的文字：地址、令牌、签名密钥各一行（新建态返回空串）。
    CString WebhookCopyText() const;

    // ---- 试一试 ----

    // 「试一试」的输入：输入框文字、是否 @、所选的群（选「不指定群」时为 0）。
    botapi::BotTryInput GetTryInput() const;

    // 显示「试一试」的结果并重绘。isError：是否为请求失败（红字显示）。
    void SetTryResult(const CString& text, bool isError);

    // 当前显示的「试一试」结果。
    const CString& TryResultText() const { return m_tryResult; }

    // ---- 头像底色 ----

    // 选中第 index 个色块（越界时不做任何事）。
    void SelectColor(int index);

    // 当前选中的色块下标；机器人原有的底色不在色块里时为 -1（保存时原样保留）。
    int ColorIndex() const { return m_colorIndex; }

    // ---- 应用范围（单元测试用）----

    // 群列表里的行数。
    int GroupRowCount() const;

    // 第 index 行是否勾选（越界返回 false）。
    bool IsGroupRowChecked(int index) const;

    // 勾选或取消第 index 行（越界时什么都不做）。
    void SetGroupRowChecked(int index, bool checked);

    // 选中应用范围：kBotScopeAll 或 kBotScopeSelected。
    void SetScope(int scope);

    // 「编辑」「上移」「下移」「删除」「添加」按钮此刻是否可用（单元测试用）。按钮 ID 取 kIdBotModalRule*。
    bool IsRuleButtonEnabled(UINT ctrlId) const;

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    // Layout 算出、OnPaint 照着画的一处文字（字段标题、说明等）
    struct LabelSpot
    {
        RECT     m_rc;      // 文字所在矩形
        CString  m_text;    // 文字
        COLORREF m_color;   // 文字颜色
        bool     m_smallFont; // 是否用小号字
    };

    // 按 m_groups 重填应用范围的群列表（勾选 checked 里的群）与「试一试」的群下拉框（选中 tryGroupId）
    void FillGroups(const std::set<long long>& checked, long long tryGroupId);

    // 应用范围群列表里当前勾选的群
    std::set<long long> CheckedGroups() const;

    // 「试一试」下拉框当前选中的群；选「不指定群」时为 0
    long long TryGroupId() const;

    // 按 m_rules 重填规则列表，选中 selected 行（越界时不选）
    void FillRules(int selected);

    // 按选中行与规则条数刷新规则按钮是否可用
    void UpdateRuleButtons();

    // 按是否已保存刷新「复制」「重新生成」是否可用
    void UpdateSecretButtons();

    // 记下一处要画的文字。smallFont：是否用小号字
    void AddLabel(const RECT& rc, const CString& text, COLORREF color, bool smallFont);

    long long                      m_editingId;        // 编辑态下的机器人 ID；0 表示新建态
    botapi::BotItem                m_original;         // 打开时的机器人（保留界面上不编辑的字段，如来源）
    CString                        m_error;            // 红字错误提示，空串表示不显示
    bool                           m_busy;             // 是否处于提交中
    RECT                           m_panelRect;        // 卡片矩形
    std::vector<LabelSpot>         m_labels;           // Layout 算出的字段标题与说明
    RECT                           m_swatchRects[botrule::kPaletteCount]; // 色块的矩形（Layout 算出）
    int                            m_colorIndex;       // 选中的色块；-1 表示原有底色不在色块里
    CString                        m_customColor;      // 原有底色不在色块里时的颜色，保存时原样提交
    RECT                           m_webhookRows[kWebhookRowCount]; // Webhook 地址、令牌、签名密钥三行文字的矩形
    RECT                           m_tryResultRect;    // 「试一试」结果的矩形
    RECT                           m_errorRect;        // 按钮栏左侧错误提示的矩形
    std::vector<botapi::BotGroup>  m_groups;           // 群列表与「试一试」下拉框里的群，下标与列表行一致
    std::vector<botapi::BotRule>   m_rules;            // 规则，按判定顺序排列，下标与规则列表行一致
    int                            m_editingRuleIndex; // BeginEditRule 记下的规则下标；-1 表示正在添加
    std::vector<CString>           m_domains;          // 外部服务的域名白名单
    bool                           m_domainsKnown;     // 白名单是否已经拉到
    ServerConfig                   m_server;           // 控制台连接服务器所用的配置（拼 Webhook 地址）
    CString                        m_webhookPath;      // Webhook 路径（详情应答的 webhookpath）
    CString                        m_webhookUrl;       // Webhook 对外完整地址（详情应答的 webhookurl）；空串表示服务端没有配置
    CString                        m_token;            // Webhook 令牌
    CString                        m_secret;           // 签名密钥
    CString                        m_tryResult;        // 「试一试」的结果文字
    bool                           m_tryIsError;       // 「试一试」的结果是否为请求失败
    balloonwjui::DuiEdit*          m_name;             // 名称输入框；所有权在子控件列表
    balloonwjui::DuiEdit*          m_glyph;            // 头像文字输入框；所有权在子控件列表
    balloonwjui::DuiEdit*          m_desc;             // 简介输入框；所有权在子控件列表
    balloonwjui::DuiComboBox*      m_status;           // 状态下拉框（启用 / 停用）；所有权在子控件列表
    balloonwjui::DuiButton*        m_allowOff;         // 「允许群主在本群关闭」勾选框；所有权在子控件列表
    balloonwjui::DuiComboBox*      m_scope;            // 应用范围下拉框；所有权在子控件列表
    balloonwjui::DuiListBox*       m_groupList;        // 群勾选列表；所有权在子控件列表
    balloonwjui::DuiListBox*       m_ruleList;         // 规则列表；所有权在子控件列表
    balloonwjui::DuiButton*        m_btnRuleAdd;       // 「添加」规则
    balloonwjui::DuiButton*        m_btnRuleEdit;      // 「编辑」规则
    balloonwjui::DuiButton*        m_btnRuleUp;        // 「上移」规则
    balloonwjui::DuiButton*        m_btnRuleDown;      // 「下移」规则
    balloonwjui::DuiButton*        m_btnRuleDelete;    // 「删除」规则
    balloonwjui::DuiButton*        m_btnCopy;          // 「复制」Webhook 信息
    balloonwjui::DuiButton*        m_btnReset;         // 「重新生成」令牌与密钥
    balloonwjui::DuiEdit*          m_tryText;          // 「试一试」输入框
    balloonwjui::DuiButton*        m_tryAt;            // 「@ 机器人」勾选框
    balloonwjui::DuiComboBox*      m_tryGroup;         // 「试一试」的群下拉框（第一项为「不指定群」）
    balloonwjui::DuiButton*        m_btnTry;           // 「试一试」按钮
    balloonwjui::DuiButton*        m_btnClose;         // 右上角关闭按钮
    balloonwjui::DuiButton*        m_btnCancel;        // 「取消」按钮
    balloonwjui::DuiButton*        m_btnSave;          // 「保存」按钮
};

} // namespace flamingoadmin
