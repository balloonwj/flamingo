/**
 *  flamingoAdmin —— 修改员工资料对话框 EmployeeEditDialog 实现。
 *
 *  balloonwj@qq.com   2026-05-28
 */
#include "stdafx.h"
#include "Screens/EmployeeEditDialog.h"
#include "AdminFrame.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/Widgets.h"          // SolidPanel / SolidHBox
#include "Widgets/UiHelpers.h"        // UiFont

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiEditHost.h"
#include "DuiNotify.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

// theme:: 在 namespace flamingoadmin 内部直接解析到 flamingoadmin::theme,
// 不需要 alias —— AdminTheme.h 已在更上层声明过同名 alias,这里再写一次
// 会触发 C2386 重复声明。

namespace {

// ===========================================================================
// 布局常量(96-dpi 逻辑像素;按设计稿 user-tabs.jsx UserTabBasic
// 的 grid 2 列 + 个性签名整行 textarea 排版还原)
// ===========================================================================
const int kDlgClientWidth   = 580;  // 客户区宽度(不含标题栏)
const int kDlgClientHeight  = 446;  // 客户区高度(不含标题栏);bar 矮 14 跟着减

const int kTitleBarHeight   = 36;   // 自绘标题栏高度

const int kBodyPad          = 20;   // 客户区四周内距
const int kRowGap           = 18;   // 字段行之间间距
const int kColGap           = 16;   // 双列间距
const int kFieldLabelH      = 16;   // 字段标签行高
const int kFieldLabelGap    = 6;    // 标签到输入控件之间间距
const int kFieldCtrlH       = 34;   // 单行输入控件高度(以 DuiEditHost 为准)
const int kFieldRowH        = kFieldLabelH + kFieldLabelGap + kFieldCtrlH; // 56

// 个性签名是 multiline textarea,高度比单行 EDIT 多一倍(对应设计稿 rows=2)。
const int kSignatureCtrlH   = 64;
const int kSignatureRowH    = kFieldLabelH + kFieldLabelGap + kSignatureCtrlH;

const int kBtnBarH          = 50;   // 底部按钮栏高度(给按钮上下各留 ~10px 留白)
const int kBtnH             = 30;   // 按钮控件本身高度(交叉轴 fixed,Bar 高里居中)
const int kBtnW             = 80;   // 按钮控件宽度
const int kBtnGap           = 10;   // 取消 / 保存之间间距

// 字号(磅)沿用 AdminTheme.
const int kFontSizeNormal   = theme::kFontSizeNormal;
const int kFontSizeSmall    = theme::kFontSizeSmall;

// ---------------------------------------------------------------------------
// 小工具:批量构造控件树时减少样板代码。
// ---------------------------------------------------------------------------

// 造一个文字标签。
//   text  :文字内容(可在后续 SetText 改);
//   pt    :字号(磅);
//   bold  :是否半粗;
//   color :文字颜色。
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, int pt, bool bold,
                                    COLORREF color)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetFont(ui::UiFont(pt, bold));
    lbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return lbl;
}

// 造一个输入框。输入框是无窗口控件,构造完即可读写文本,不依赖窗口是否已经
// 建好,也没有需要在窗口就绪后补做的步骤。
//   ctrlId      :控件 ID(UiIds.h);
//   placeholder :空值占位字。
std::unique_ptr<DuiEditHost> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    std::unique_ptr<DuiEditHost> e(new DuiEditHost());
    e->SetCtrlId(ctrlId);
    e->SetPlaceholder(placeholder);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    return e;
}

// 造一个「字段列」:上下两段 —— 顶部是字段名 label,底部是输入控件(以裸
// 指针 borrowOutEdit 输出供外层 cache)。multiLine=true 时输入控件高度变
// 为 kSignatureCtrlH,EDIT 用多行样式(对应设计稿 textarea rows=2)。
//   label         :字段名(如「姓名 *」);
//   ctrlId        :输入框 ID;
//   placeholder   :输入框占位字,可为空字符串;
//   multiLine     :是否多行 textarea;
//   borrowOutEdit :输出参数 —— 拿到 DuiEditHost 裸指针,供外层 SetText
//                  / GetText 用。所有权仍在控件树里。
std::unique_ptr<DuiControl> MakeFieldColumn(LPCTSTR label, int ctrlId,
                                            LPCTSTR placeholder,
                                            bool multiLine,
                                            DuiEditHost** borrowOutEdit)
{
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kFieldLabelGap);

    col->AddChild(MakeLabel(label, kFontSizeSmall, false, theme::kInk500),
                  DuiLayout::Hint().Fixed(kFieldLabelH));

    std::unique_ptr<DuiEditHost> e = MakeEdit(ctrlId, placeholder);
    if (multiLine)
    {
        e->SetMultiLine(true);
    }
    *borrowOutEdit = e.get();
    col->AddChild(std::move(e),
                  DuiLayout::Hint().Fixed(multiLine ? kSignatureCtrlH
                                                    : kFieldCtrlH));

    return std::unique_ptr<DuiControl>(col.release());
}

// 造一个按钮。
std::unique_ptr<DuiButton> MakeButton(int ctrlId, LPCTSTR text,
                                       DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetCtrlId(ctrlId);
    btn->SetText(text);
    btn->SetVariant(variant);
    btn->SetTextPointSize(kFontSizeNormal);
    return btn;
}

} // anonymous namespace

// ===========================================================================
// EmployeeEditDialog 公开接口
// ===========================================================================

EmployeeEditDialog::EmployeeEditDialog(AdminFrame* frame, HWND parentHwnd,
                                       const OrgNode& node)
    : m_frame(frame)
    , m_parentHwnd(parentHwnd)
    , m_node(node)
    , m_busy(false)
    , m_editName(nullptr)
    , m_editTitle(nullptr)
    , m_editEmployeeNo(nullptr)
    , m_editJoinDate(nullptr)
    , m_editEmail(nullptr)
    , m_editPhone(nullptr)
    , m_editLandline(nullptr)
    , m_editWorkstation(nullptr)
    , m_editSignature(nullptr)
    , m_btnSave(nullptr)
{
}

EmployeeEditDialog::~EmployeeEditDialog() = default;

bool EmployeeEditDialog::CreateAndShow()
{
    // ---- 1. 创建顶层窗口(WS_OVERLAPPEDWINDOW,后续 SetResizable(false) 锁死)
    HWND hw = Create(NULL, CWindow::rcDefault, _T("FlamingoIM Admin"),
                     WS_OVERLAPPEDWINDOW, 0);
    if (hw == NULL || !::IsWindow(hw))
    {
        return false;
    }

    SetTitle(_T("修改员工资料"));
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);   // 仅保留关闭按钮
    SetResizable(false);

    // ---- 2. 构造客户区控件树 + SetClientContent
    BuildClientContent();

    // ---- 3. 灌数据
    LoadNodeIntoUi();

    // ---- 4. 锁死窗口尺寸 + 居中 + 真模态(禁主窗) + 显示
    ResizeClient(kDlgClientWidth, kDlgClientHeight + kTitleBarHeight);
    CenterWindow();

    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, FALSE);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    return true;
}

void EmployeeEditDialog::RequestClose()
{
    if (::IsWindow(m_hWnd))
    {
        ::PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }
}

CString EmployeeEditDialog::GetUserId() const        { return m_node.m_id; }
CString EmployeeEditDialog::GetName() const          { return m_editName        ? m_editName->GetText()        : CString(); }
CString EmployeeEditDialog::GetTitle() const         { return m_editTitle       ? m_editTitle->GetText()       : CString(); }
CString EmployeeEditDialog::GetEmployeeNo() const    { return m_editEmployeeNo  ? m_editEmployeeNo->GetText()  : CString(); }
CString EmployeeEditDialog::GetJoinDate() const      { return m_editJoinDate    ? m_editJoinDate->GetText()    : CString(); }
CString EmployeeEditDialog::GetEmail() const         { return m_editEmail       ? m_editEmail->GetText()       : CString(); }
CString EmployeeEditDialog::GetPhone() const         { return m_editPhone       ? m_editPhone->GetText()       : CString(); }
CString EmployeeEditDialog::GetLandline() const      { return m_editLandline    ? m_editLandline->GetText()    : CString(); }
CString EmployeeEditDialog::GetWorkstation() const   { return m_editWorkstation ? m_editWorkstation->GetText() : CString(); }
CString EmployeeEditDialog::GetSignature() const     { return m_editSignature   ? m_editSignature->GetText()   : CString(); }

void EmployeeEditDialog::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSave != nullptr)
    {
        DuiButton* btn = dynamic_cast<DuiButton*>(m_btnSave);
        if (btn != nullptr)
        {
            btn->SetText(busy ? _T("保存中…") : _T("保存"));
            btn->SetEnabled(!busy);
        }
    }
}

// ===========================================================================
// EmployeeEditDialog 私有实现
// ===========================================================================

void EmployeeEditDialog::BuildClientContent()
{
    // ---- 根 vbox:客户区填满 ----
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kBodyPad);
    root->SetGap(kRowGap);

    // ---- 字段区:5 行(4 行双列 + 1 行整行 multiline) ----
    // 设计稿 UserTabBasic 的字段排列(grid 2 列 + 个性签名 full row textarea),
    // 顺序为:姓名 / 职位 / 工号 / 入职日期 / 邮箱 / 手机号 / 座机 / 工位地址 /
    // 个性签名。占位字按设计稿原文取用(座机 / 工位地址 / 个性签名 给了示例文本,
    // 其它字段不指定 cue,留空即可)。姓名 + 邮箱后缀 * 表示客户端校验必填项,
    // 设计稿没标星但实际提交 user/update.do 要求二者非空,保留 * 让用户知情。

    // 第 1 行:姓名* / 职位
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kColGap);
        row->AddChild(MakeFieldColumn(_T("姓名 *"), kIdEmpEditName,
                                      _T(""), false, &m_editName),
                      DuiLayout::Hint().Weight(1));
        row->AddChild(MakeFieldColumn(_T("职位"), kIdEmpEditTitle,
                                      _T(""), false, &m_editTitle),
                      DuiLayout::Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                       DuiLayout::Hint().Fixed(kFieldRowH));
    }

    // 第 2 行:工号 / 入职日期
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kColGap);
        row->AddChild(MakeFieldColumn(_T("工号"), kIdEmpEditEmployeeNo,
                                      _T(""), false, &m_editEmployeeNo),
                      DuiLayout::Hint().Weight(1));
        row->AddChild(MakeFieldColumn(_T("入职日期"), kIdEmpEditJoinDate,
                                      _T(""), false, &m_editJoinDate),
                      DuiLayout::Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                       DuiLayout::Hint().Fixed(kFieldRowH));
    }

    // 第 3 行:邮箱* / 手机号
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kColGap);
        row->AddChild(MakeFieldColumn(_T("邮箱 *"), kIdEmpEditEmail,
                                      _T(""), false, &m_editEmail),
                      DuiLayout::Hint().Weight(1));
        row->AddChild(MakeFieldColumn(_T("手机号"), kIdEmpEditPhone,
                                      _T(""), false, &m_editPhone),
                      DuiLayout::Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                       DuiLayout::Hint().Fixed(kFieldRowH));
    }

    // 第 4 行:座机 / 工位地址(此 2 个字段的占位字按设计稿给示例文本)
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kColGap);
        row->AddChild(MakeFieldColumn(_T("座机"), kIdEmpEditLandline,
                                      _T("例如:021-6868-8888 转 1024"),
                                      false, &m_editLandline),
                      DuiLayout::Hint().Weight(1));
        row->AddChild(MakeFieldColumn(_T("工位地址"), kIdEmpEditWorkstation,
                                      _T("例如:上海·张江园区 3F 西区 B12"),
                                      false, &m_editWorkstation),
                      DuiLayout::Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                       DuiLayout::Hint().Fixed(kFieldRowH));
    }

    // 第 5 行:个性签名 —— 整行 multiline textarea(对应设计稿 rows=2)
    root->AddChild(MakeFieldColumn(_T("个性签名"), kIdEmpEditSignature,
                                   _T("员工本人也可以在客户端中修改"),
                                   true, &m_editSignature),
                   DuiLayout::Hint().Fixed(kSignatureRowH));

    // ---- 弹性 spacer:把按钮栏顶到最底 ----
    root->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                   DuiLayout::Hint().Weight(1));

    // ---- 按钮栏:上边线 + 右对齐 取消 + 保存 ----
    {
        std::unique_ptr<ui::SolidHBox> bar(new ui::SolidHBox());
        bar->SetEdgeBorder(ui::SolidHBox::kEdgeTop, theme::kInk100);
        bar->SetGap(kBtnGap);
        // 弹性 spacer 把按钮顶到右
        bar->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                      DuiLayout::Hint().Weight(1));
        // 按钮:主轴 fixed 80(宽),交叉轴 fixed 30(高),交叉轴居中 ——
        // 不指定 cross 高度时默认 AlignFill 会让按钮撑满 bar(60px 太高)。
        bar->AddChild(MakeButton(kIdEmpEditCancel, _T("取消"),
                                 DuiButton::Variant::Default),
                      DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                        .AlignC(DuiLayout::AlignCenter));
        std::unique_ptr<DuiButton> save = MakeButton(kIdEmpEditSubmit,
                                                     _T("保存"),
                                                     DuiButton::Variant::Primary);
        m_btnSave = save.get();
        bar->AddChild(std::move(save),
                      DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                        .AlignC(DuiLayout::AlignCenter));

        root->AddChild(std::unique_ptr<DuiControl>(bar.release()),
                       DuiLayout::Hint().Fixed(kBtnBarH));
    }

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
}

void EmployeeEditDialog::LoadNodeIntoUi()
{
    // 设计稿 UserTabBasic 没有"员工:xxx" banner 横幅,身份信息由 admin 现有
    // 详情面板的 profile head 区(头像 + 姓名 + 状态 chip + 邮箱/手机/工号一行)
    // 承载。本对话框只装编辑表单,标题栏写了"修改员工资料"已经够清楚。
    // 同理设计稿也没有"登录账号(只读)"灰条 —— 登录账号在 profile head 里
    // 看得到,不需要塞进编辑表单。

    // ---- 9 个字段 ----
    if (m_editName        != nullptr) { m_editName->SetText(m_node.m_name);               }
    if (m_editTitle       != nullptr) { m_editTitle->SetText(m_node.m_title);             }
    if (m_editEmployeeNo  != nullptr) { m_editEmployeeNo->SetText(m_node.m_employeeNo);   }
    if (m_editJoinDate    != nullptr) { m_editJoinDate->SetText(m_node.m_joined);         }
    if (m_editEmail       != nullptr) { m_editEmail->SetText(m_node.m_email);             }
    if (m_editPhone       != nullptr) { m_editPhone->SetText(m_node.m_phone);             }
    if (m_editLandline    != nullptr) { m_editLandline->SetText(m_node.m_landline);       }
    if (m_editWorkstation != nullptr) { m_editWorkstation->SetText(m_node.m_workstation); }
    if (m_editSignature   != nullptr) { m_editSignature->SetText(m_node.m_signature);     }
}

void EmployeeEditDialog::SubmitOrShake()
{
    if (m_busy)
    {
        return;
    }

    // 校验:姓名 + 邮箱必填。
    CString name = GetName();
    CString mail = GetEmail();
    name.Trim();
    mail.Trim();
    if (name.IsEmpty() || mail.IsEmpty())
    {
        if (m_frame != nullptr)
        {
            m_frame->ShowToast(_T("请填写员工姓名与邮箱"),
                               AdminFrame::ToastKind::Warn);
        }
        return;
    }

    // 调 AdminFrame 提交入口,本对话框等 HTTP reply 期间 Busy。
    if (m_frame != nullptr)
    {
        SetBusy(true);
        m_frame->OnEmployeeEditDialogSubmit(this);
    }
}

LRESULT EmployeeEditDialog::OnDuiNotify(UINT, WPARAM, LPARAM lParam,
                                        BOOL& bHandled)
{
    // balloonui 的 WM_DUI_NOTIFY:lParam = const DuiNotify*,结构里同时带
    // code(通知码)+ ctrlId(控件 ID)+ extra(额外数据)。注意【不是】把
    // 这俩 LOWORD/HIWORD 编进 wParam —— admin AdminFrame::OnDuiNotify 也是
    // 这么解码的(AdminFrame.cpp:554)。
    const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
    if (n == nullptr || n->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    case kIdEmpEditCancel:
        // 取消按钮 → 关闭(不提交)。
        RequestClose();
        bHandled = TRUE;
        return 0;
    case kIdEmpEditSubmit:
        // 保存按钮 → 校验 + 调 AdminFrame 提交。
        SubmitOrShake();
        bHandled = TRUE;
        return 0;
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT EmployeeEditDialog::OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // WM_CLOSE 收口:DestroyWindow 触发 WM_DESTROY,真模态解除 + 通知
    // AdminFrame 清自己指针都在 OnDestroyMsg 里做。
    DestroyWindow();
    bHandled = TRUE;
    return 0;
}

LRESULT EmployeeEditDialog::OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // ---- 解除真模态:主窗口恢复 Enable 并交焦点回去 ----
    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, TRUE);
        ::SetForegroundWindow(m_parentHwnd);
    }
    // ---- 通知 AdminFrame 清自己指针(它持有的 m_employeeEditDialog) ----
    if (m_frame != nullptr)
    {
        m_frame->OnEmployeeEditDialogDestroyed(this);
    }
    bHandled = FALSE;   // 让 DuiFrameWindow 的 WM_DESTROY 清理继续执行
    return 0;
}

} // namespace flamingoadmin
