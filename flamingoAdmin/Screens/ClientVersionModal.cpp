/**
 *  flamingoAdmin —— 新建客户端版本弹窗 ClientVersionModal 实现。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Screens/ClientVersionModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/IconRenderer.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Basic/DuiButton.h"
#include "Widgets/OverlayScrollbar.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 卡片与内部布局常量（96 dpi 逻辑像素）----
const int kPanelW    = 620;  // 卡片宽度
//两个高度都比字段总高多留出一行复选框（kCheckRowH + kFieldGap）。
const int kPanelHNew  = 638; // 新建态卡片高度（含候选包列表与版本号框、强制升级复选框）
const int kPanelHEdit = 466; // 编辑态卡片高度（没有候选包列表与版本号框）
const int kInfoH      = 48;  // 编辑态那行只读信息（版本号 · 平台 · 包名）的高度
const int kHeaderH   = 56;   // 标题栏高度
const int kFooterH   = 64;   // 按钮栏高度
const int kPadH      = 20;   // 卡片左右内边距
const int kBackdropAlpha = 110;  // 遮罩不透明度（0-255）

const int kModalTitlePt = 13;   // 标题字号（磅）

const int kLabelH    = 20;   // 字段标题行高
const int kEditH     = 32;   // 单行输入框高度
const int kEditNoteH = 96;   // 更新说明多行框高度（约 4 行）
const int kFieldGap  = 10;   // 字段之间的纵向间距
const int kCheckRowH = 32;   // 「强制升级」复选框行高
const int kListH     = 168;  // 候选包列表区高度
const int kRowH      = 40;   // 候选包行高

const int kBtnH      = 34;   // 底部按钮高度
const int kBtnGap    = 8;    // 底部按钮间距
const int kBtnWCancel  = 72;   // 「取消」按钮宽度
const int kBtnWDraft   = 96;   // 「存为草稿」按钮宽度
const int kBtnWPublish = 96;   // 「直接发布」按钮宽度
const int kBtnWRescan  = 88;   // 「重新扫描」按钮宽度
const int kCloseBtnSize = 28;  // 右上角关闭按钮边长

const int kSbInset = 3;   // 悬浮滚动条距列表区右边缘的内缩

} // namespace

ClientVersionModal::ClientVersionModal()
    : m_selected(-1)
    , m_editingId(0)
    , m_scroll(0)
    , m_scrollMax(0)
    , m_editVersion(nullptr)
    , m_editMinVer(nullptr)
    , m_chkForced(nullptr)
    , m_editNote(nullptr)
    , m_btnClose(nullptr)
    , m_btnRescan(nullptr)
    , m_btnCancel(nullptr)
    , m_btnDraft(nullptr)
    , m_btnPublish(nullptr)
    , m_scrollbar(nullptr)
{
    SetCtrlId(kIdCvModal);
    ::SetRectEmpty(&m_panelRect);
    ::SetRectEmpty(&m_listRc);

    std::unique_ptr<DuiEdit> version = std::make_unique<DuiEdit>();
    version->SetCtrlId(kIdCvModalVersion);
    version->SetPlaceholder(_T("例如：3.3.0（须与客户端内置版本号一致）"));
    version->SetShowBorder(true);
    version->SetBgColor(theme::kInk0);
    m_editVersion = version.get();
    AddChild(std::move(version));

    std::unique_ptr<DuiEdit> minVer = std::make_unique<DuiEdit>();
    minVer->SetCtrlId(kIdCvModalMinVer);
    minVer->SetPlaceholder(_T("例如：3.1.0（低于它的客户端会被强制升级；留空表示不设门槛）"));
    minVer->SetShowBorder(true);
    minVer->SetBgColor(theme::kInk0);
    m_editMinVer = minVer.get();
    AddChild(std::move(minVer));

    std::unique_ptr<DuiEdit> note = std::make_unique<DuiEdit>();
    note->SetCtrlId(kIdCvModalNote);
    note->SetPlaceholder(_T("更新说明，会原样显示在客户端的升级窗里，可分多行写"));
    note->SetShowBorder(true);
    note->SetBgColor(theme::kInk0);
    //更新说明通常要分条写（"新增 X；修复 Y；已知问题 Z"），单行框写起来很别扭，
    //故设为多行 + 按宽度自动换行。这两个开关在运行期任何时候都可以改。
    note->SetMultiLine(true);
    note->SetWordWrap(true);
    m_editNote = note.get();
    AddChild(std::move(note));

    //「强制升级」复选框。勾上之后客户端的升级窗不给"稍后"出口、也不放行登录。
    //它与"最低版本号"是两件事：后者是版本门槛（低于它连登录都会被服务端拒绝），
    //前者是发布人对这一个版本的主观决定，两者任一命中客户端都按强制处理。
    std::unique_ptr<DuiButton> forced = std::make_unique<DuiButton>();
    forced->SetButtonType(DuiButton::StyleCheckbox);
    //Variant 必须选一个透明的，否则 DuiButton 会按普通按钮那套填充 + 边框来画，
    //勾选框行上会多出一个跟输入框一样宽的方框（登录页那个复选框也是这么处理的）。
    forced->SetVariant(DuiButton::Variant::Text);
    forced->SetCtrlId(kIdCvModalForced);
    forced->SetText(_T("强制升级（客户端不能跳过，装完才能继续用）"));
    m_chkForced = forced.get();
    AddChild(std::move(forced));

    // 候选包列表的悬浮滚动条。须在几个按钮之前 AddChild —— 按钮要压在
    // 滚动条之上参与命中，顺序反了会让贴边的按钮点不动。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_scrollbar = sb.get();
    AddChild(std::move(sb));

    auto closeBtn = std::make_unique<DuiButton>();
    closeBtn->SetCtrlId(kIdCvModalClose);
    closeBtn->SetText(_T("×"));
    closeBtn->SetVariant(DuiButton::Variant::Ghost);
    closeBtn->SetTextPointSize(theme::kFontSizeHeading);
    m_btnClose = closeBtn.get();
    AddChild(std::move(closeBtn));

    auto rescanBtn = std::make_unique<DuiButton>();
    rescanBtn->SetCtrlId(kIdCvModalRescan);
    rescanBtn->SetText(_T("重新扫描"));
    rescanBtn->SetVariant(DuiButton::Variant::Default);
    rescanBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnRescan = rescanBtn.get();
    AddChild(std::move(rescanBtn));

    auto cancelBtn = std::make_unique<DuiButton>();
    cancelBtn->SetCtrlId(kIdCvModalCancel);
    cancelBtn->SetText(_T("取消"));
    cancelBtn->SetVariant(DuiButton::Variant::Default);
    cancelBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnCancel = cancelBtn.get();
    AddChild(std::move(cancelBtn));

    auto draftBtn = std::make_unique<DuiButton>();
    draftBtn->SetCtrlId(kIdCvModalSaveDraft);
    draftBtn->SetText(_T("存为草稿"));
    draftBtn->SetVariant(DuiButton::Variant::Default);
    draftBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnDraft = draftBtn.get();
    AddChild(std::move(draftBtn));

    auto publishBtn = std::make_unique<DuiButton>();
    publishBtn->SetCtrlId(kIdCvModalPublish);
    publishBtn->SetText(_T("直接发布"));
    publishBtn->SetVariant(DuiButton::Variant::Primary);
    publishBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnPublish = publishBtn.get();
    AddChild(std::move(publishBtn));
}

void ClientVersionModal::SetPackages(const adminapi::ScanPackagesResult& scan)
{
    m_packages = scan.m_list;
    m_dir = scan.m_dir;
    m_scroll = 0;
    m_selected = -1;

    // 默认选中第一个「是 zip 且尚未被引用」的包：那多半就是刚放进去的新包。
    // 找不到就退而选第一个 zip；一个 zip 都没有则保持未选中。
    for (size_t i = 0; i < m_packages.size(); ++i)
    {
        if (m_packages[i].m_isZip && !m_packages[i].m_used)
        {
            m_selected = static_cast<int>(i);
            break;
        }
    }
    if (m_selected < 0)
    {
        for (size_t i = 0; i < m_packages.size(); ++i)
        {
            if (m_packages[i].m_isZip)
            {
                m_selected = static_cast<int>(i);
                break;
            }
        }
    }

    if (m_editVersion != nullptr)
    {
        m_editVersion->SetText(_T(""));
    }
    if (m_chkForced != nullptr)
    {
        m_chkForced->SetCheck(false, false);
    }
    if (m_editMinVer != nullptr)
    {
        m_editMinVer->SetText(_T(""));
    }
    if (m_editNote != nullptr)
    {
        m_editNote->SetText(_T(""));
    }

    // 切回新建态：把编辑态藏起来的那几个控件恢复出来
    m_editingId = 0;
    m_editInfo.Empty();
    if (m_editVersion != nullptr)
    {
        m_editVersion->SetVisible(true);
    }
    if (m_btnRescan != nullptr)
    {
        m_btnRescan->SetVisible(true);
    }
    if (m_btnPublish != nullptr)
    {
        m_btnPublish->SetVisible(true);
    }
    if (m_btnDraft != nullptr)
    {
        m_btnDraft->SetText(_T("存为草稿"));
    }

    // 形态变了必须重排：卡片高度、候选包列表高度、各输入框的纵向位置都随形态变，
    // 而 ShowModal 只切显隐、不会重新布局。不重排的话，画的是新形态、控件却还摆在
    // 上一形态的位置上，表现为标签与输入框整体错开一档。
    Layout(m_rcItem);

    ClampScroll();
}

void ClientVersionModal::SetEditTarget(const adminapi::ClientVersionItem& item)
{
    m_editingId = item.m_id;

    // 编辑态不给换包、不给改版本号：这两样一旦发布就可能已被客户端下载。
    // 故候选包列表整块不显示，改为一行只读信息把它们摆出来给人看。
    m_packages.clear();
    m_rowHits.clear();
    m_selected = -1;
    m_scroll = 0;
    m_scrollMax = 0;
    m_editInfo.Format(_T("%s  ·  %s  ·  升级包 %s（不可更换）"),
                      item.m_version.GetString(),
                      item.m_platform == 2 ? _T("Android") : _T("Windows PC"),
                      item.m_packageName.GetString());

    if (m_chkForced != nullptr)
    {
        m_chkForced->SetCheck(item.m_forced, false);
    }
    if (m_editMinVer != nullptr)
    {
        m_editMinVer->SetText(item.m_minVersion);
    }
    if (m_editNote != nullptr)
    {
        m_editNote->SetText(item.m_releaseNote);
    }

    // 版本号框、「重新扫描」「直接发布」在编辑态没有意义，藏起来；
    // 「存为草稿」复用成「保存」，避免再多一个按钮。
    if (m_editVersion != nullptr)
    {
        m_editVersion->SetVisible(false);
    }
    if (m_btnRescan != nullptr)
    {
        m_btnRescan->SetVisible(false);
    }
    if (m_btnPublish != nullptr)
    {
        m_btnPublish->SetVisible(false);
    }
    if (m_btnDraft != nullptr)
    {
        m_btnDraft->SetText(_T("保存"));
    }

    // 同上：切到编辑态后必须重排，否则卡片仍是新建态的高度、
    // 信息块仍占着候选包列表那么大一块，输入框也还在原来的档位上。
    Layout(m_rcItem);
}

bool ClientVersionModal::Validate() const
{
    // 编辑态只改最低版本与更新说明，两者都允许为空，故无须校验
    if (IsEditing())
    {
        return true;
    }

    if (m_selected < 0 || m_selected >= static_cast<int>(m_packages.size()))
    {
        return false;
    }
    if (!m_packages[m_selected].m_isZip)
    {
        return false;
    }
    return !GetVersion().IsEmpty();
}

CString ClientVersionModal::GetSelectedMd5() const
{
    if (m_selected >= 0 && m_selected < static_cast<int>(m_packages.size()))
    {
        return m_packages[m_selected].m_md5;
    }
    return CString();
}

CString ClientVersionModal::GetVersion() const
{
    if (m_editVersion == nullptr)
    {
        return CString();
    }
    CString text = m_editVersion->GetText();
    text.Trim();
    return text;
}

CString ClientVersionModal::GetMinVersion() const
{
    if (m_editMinVer == nullptr)
    {
        return CString();
    }
    CString text = m_editMinVer->GetText();
    text.Trim();
    return text;
}

CString ClientVersionModal::GetReleaseNote() const
{
    if (m_editNote == nullptr)
    {
        return CString();
    }
    return m_editNote->GetText();
}

bool ClientVersionModal::IsForced() const
{
    if (m_chkForced == nullptr)
    {
        return false;
    }
    return m_chkForced->IsChecked();
}

void ClientVersionModal::ClampScroll()
{
    const int viewH = m_listRc.bottom - m_listRc.top;
    const int contentH = static_cast<int>(m_packages.size()) * kRowH;
    m_scrollMax = (contentH > viewH) ? (contentH - viewH) : 0;
    if (m_scroll > m_scrollMax)
    {
        m_scroll = m_scrollMax;
    }
    if (m_scroll < 0)
    {
        m_scroll = 0;
    }
}

void ClientVersionModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 卡片居中。编辑态没有候选包列表与版本号框，卡片矮一截。
    const int panelH = IsEditing() ? kPanelHEdit : kPanelHNew;
    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect = RECT{ cx - kPanelW / 2, cy - panelH / 2,
                        cx + kPanelW / 2, cy + panelH / 2 };

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    // 右上角关闭按钮。
    if (m_btnClose != nullptr)
    {
        m_btnClose->ForceLayout(RECT{ m_panelRect.right - kPadH - kCloseBtnSize,
                                      m_panelRect.top + (kHeaderH - kCloseBtnSize) / 2,
                                      m_panelRect.right - kPadH,
                                      m_panelRect.top + (kHeaderH + kCloseBtnSize) / 2 });
    }

    // 内容区自上而下：候选包列表 → 版本号 → 最低版本 → 更新说明。
    int y = m_panelRect.top + kHeaderH + 12 + kLabelH;   //列表标题之下

    // 编辑态把列表区缩成一行只读信息；新建态才是真正的候选包列表。
    m_listRc = RECT{ L, y, R, y + (IsEditing() ? kInfoH : kListH) };
    if (m_scrollbar != nullptr)
    {
        m_scrollbar->ForceLayout(RECT{
            m_listRc.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
            m_listRc.top,
            m_listRc.right - kSbInset,
            m_listRc.bottom });
    }
    // 「重新扫描」按钮摆在列表标题行的右端（编辑态该按钮已隐藏）
    if (m_btnRescan != nullptr)
    {
        m_btnRescan->ForceLayout(RECT{ R - kBtnWRescan, m_listRc.top - kLabelH - 4,
                                       R, m_listRc.top - kLabelH - 4 + 26 });
    }

    y = m_listRc.bottom + kFieldGap + kLabelH;
    // 编辑态不给改版本号，该框已隐藏，也不占纵向位置
    if (!IsEditing())
    {
        if (m_editVersion != nullptr)
        {
            m_editVersion->ForceLayout(RECT{ L, y, R, y + kEditH });
        }
        y += kEditH + kFieldGap + kLabelH;
    }

    if (m_editMinVer != nullptr)
    {
        m_editMinVer->ForceLayout(RECT{ L, y, R, y + kEditH });
    }

    y += kEditH + kFieldGap + kLabelH;
    if (m_editNote != nullptr)
    {
        m_editNote->ForceLayout(RECT{ L, y, R, y + kEditNoteH });
    }

    //「强制升级」复选框紧跟在更新说明下面。它自带文字，不需要单独的标题行。
    y += kEditNoteH + kFieldGap;
    if (m_chkForced != nullptr)
    {
        m_chkForced->ForceLayout(RECT{ L, y, R, y + kCheckRowH });
    }

    // 底部按钮：自右向左依次是 直接发布 / 存为草稿 / 取消。
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    int bx = R;
    if (m_btnPublish != nullptr)
    {
        m_btnPublish->ForceLayout(RECT{ bx - kBtnWPublish, by, bx, by + kBtnH });
        bx -= kBtnWPublish + kBtnGap;
    }
    if (m_btnDraft != nullptr)
    {
        m_btnDraft->ForceLayout(RECT{ bx - kBtnWDraft, by, bx, by + kBtnH });
        bx -= kBtnWDraft + kBtnGap;
    }
    if (m_btnCancel != nullptr)
    {
        m_btnCancel->ForceLayout(RECT{ bx - kBtnWCancel, by, bx, by + kBtnH });
    }

    ClampScroll();
}

void ClientVersionModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }

    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    const int L = m_panelRect.left + kPadH;
    const int R = m_panelRect.right - kPadH;

    // 标题。
    ui::DrawTextLine(hdc, IsEditing() ? _T("编辑客户端版本") : _T("新建客户端版本"),
                     RECT{ L, m_panelRect.top, R - kCloseBtnSize, m_panelRect.top + kHeaderH },
                     theme::kInk700, ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelW, 1, theme::kInk100);

    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);

    if (IsEditing())
    {
        // 编辑态：版本号与升级包只读展示，一行说清楚在改的是哪一条。
        ui::DrawTextLine(hdc, _T("正在编辑"),
                         RECT{ L, m_listRc.top - kLabelH - 4, R, m_listRc.top },
                         theme::kInk400, labelFont,
                         DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        ui::FillRoundRect(hdc, m_listRc, theme::kInk50, theme::kRadiusSm);
        ui::DrawTextLine(hdc, m_editInfo,
                         RECT{ m_listRc.left + 12, m_listRc.top,
                               m_listRc.right - 12, m_listRc.bottom },
                         theme::kInk700, ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        // 候选包列表标题：把目录写进去，管理员一眼知道这些包是从哪儿来的。
        CString listLabel;
        if (m_dir.IsEmpty())
        {
            listLabel = _T("选择升级包");
        }
        else
        {
            listLabel.Format(_T("选择升级包（来自服务器 %s）"), m_dir.GetString());
        }
        ui::DrawTextLine(hdc, listLabel,
                         RECT{ L, m_listRc.top - kLabelH - 4, R - kBtnWRescan - 8, m_listRc.top },
                         theme::kInk400, labelFont,
                         DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);

        ui::FillRoundRect(hdc, m_listRc, theme::kInk50, theme::kRadiusSm);
        PaintPackageList(hdc, m_listRc);
    }

    // 字段标题。编辑态没有版本号那一档，后面两档整体上移。
    int minVerLabelTop = m_listRc.bottom + kFieldGap;
    if (!IsEditing())
    {
        ui::DrawTextLine(hdc, _T("版本号（须与客户端内置版本号一致）"),
                         RECT{ L, m_listRc.bottom + kFieldGap, R,
                               m_listRc.bottom + kFieldGap + kLabelH },
                         theme::kInk400, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        minVerLabelTop += kLabelH + kEditH + kFieldGap;
    }
    ui::DrawTextLine(hdc, _T("最低可用版本（低于它的客户端会被拒绝登录，并在检查更新时判为强制升级）"),
                     RECT{ L, minVerLabelTop, R, minVerLabelTop + kLabelH },
                     theme::kInk400, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int noteLabelTop = minVerLabelTop + kLabelH + kEditH + kFieldGap;
    ui::DrawTextLine(hdc, _T("更新说明（可分多行写，会原样显示在客户端升级窗里）"),
                     RECT{ L, noteLabelTop, R, noteLabelTop + kLabelH },
                     theme::kInk400, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 子控件（输入框、按钮、滚动条）画在最上层。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void ClientVersionModal::PaintPackageList(HDC hdc, const RECT& rc)
{
    m_rowHits.clear();
    m_rowHits.resize(m_packages.size());
    for (size_t i = 0; i < m_rowHits.size(); ++i)
    {
        ::SetRectEmpty(&m_rowHits[i]);
    }

    if (m_packages.empty())
    {
        ui::DrawTextLine(hdc, _T("升级包目录里没有文件 —— 先把 zip 放进去再点「重新扫描」"),
                         rc, theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const int oldClip = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);

    HFONT rowFont = ui::UiFont(theme::kFontSizeSmall, false);
    for (int i = 0; i < static_cast<int>(m_packages.size()); ++i)
    {
        const int top = rc.top + i * kRowH - m_scroll;
        if (top + kRowH < rc.top || top > rc.bottom)
        {
            continue;
        }

        const adminapi::UpdatePackageItem& pkg = m_packages[i];
        const RECT row = { rc.left, top, rc.right, top + kRowH };
        m_rowHits[i] = row;

        if (i == m_selected)
        {
            ui::FillSolidRect(hdc, row, theme::kFlamingo50);
            ui::FillSolidRect(hdc, RECT{ rc.left, top, rc.left + 3, top + kRowH },
                              theme::kFlamingo500);
        }

        // 不是 zip 的项标灰：多半是把说明文档或别的东西误放进了升级包目录
        const COLORREF nameColor = pkg.m_isZip ? theme::kInk700 : theme::kInk400;
        ui::DrawTextLine(hdc, pkg.m_fileName,
                         RECT{ rc.left + 12, top, rc.right - 220, top + kRowH },
                         nameColor, rowFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        CString meta = adminapi::FormatPackageSize(pkg.m_size);
        if (!pkg.m_isZip)
        {
            meta += _T("  ·  不是 zip");
        }
        else if (pkg.m_used)
        {
            meta += _T("  ·  已建过版本");
        }
        ui::DrawTextLine(hdc, meta,
                         RECT{ rc.right - 210, top, rc.right - 12, top + kRowH },
                         pkg.m_isZip ? theme::kInk400 : theme::kColorWarn, rowFont,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        ui::FillLine(hdc, rc.left + 12, row.bottom - 1, rc.right - rc.left - 24,
                     1, theme::kInk100);
    }

    ::RestoreDC(hdc, oldClip);
}

bool ClientVersionModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 同 ClientVersionsScreen：不要在这里调基类，它会把落在卡片内的任何点击
    // 当成命中并返回 true，候选包一行都选不中。子控件走宿主的 HitTest 派发。

    // 选择候选包。不是 zip 的行点不中 —— 选了也建不出记录，服务端会拒。
    for (int i = 0; i < static_cast<int>(m_rowHits.size()); ++i)
    {
        if (::IsRectEmpty(&m_rowHits[i]) || !::PtInRect(&m_rowHits[i], pt))
        {
            continue;
        }
        if (i < static_cast<int>(m_packages.size()) && m_packages[i].m_isZip)
        {
            m_selected = i;
            Invalidate();
        }
        return true;
    }

    // 点遮罩不关闭（与其它弹窗一致），但要把点击吞掉，别落到底下的界面上
    return true;
}

bool ClientVersionModal::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (!::PtInRect(&m_listRc, pt) || m_scrollMax <= 0)
    {
        return false;
    }

    m_scroll -= (zDelta / WHEEL_DELTA) * kRowH * 2;
    ClampScroll();

    if (m_scrollbar != nullptr)
    {
        m_scrollbar->SetMetrics(static_cast<int>(m_packages.size()) * kRowH, m_scroll);
        m_scrollbar->Flash();
    }

    Invalidate();
    return true;
}

} // namespace flamingoadmin
