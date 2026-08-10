/**
 *  BalloonProfileLayout.cpp
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程共用。在 Flamingo.vcxproj 里本文件需
 *  标记 PrecompiledHeader=NotUsing。
 *
 *  控件树尺寸 / 字号严格按设计稿源(flamingo-im-pc/project/dialogs.jsx 的
 *  PersonalInfoDialog + components.jsx 的 Modal / FieldRow / Avatar / Btn)
 *  落地:
 *    · Modal 宽度 width=420,内容区 padding 18px 20px
 *    · 顶部 Banner:avatar size=64 rounded="22%"(圆角矩形 14px),
 *      昵称 17px/600,账号 12px/muted,胶囊行 height=22 borderRadius=999
 *    · FieldRow:padding 12px 0,标签 13.5px,值 13px,borderBottom 1px
 *    · Btn md:height=34,padding 0 16,fontSize 13.5,borderRadius 10
 *      primary 实心橙(--accent #FF7A45),ghost 透明底 1px 边
 *  px → pt 换算 ×(72/96)≈÷1.333:17→13,13.5→10,13→10,12→9,11→8。
 *
 *  balloonwj@qq.com   2026-05-28
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖(等价 NewChatDemo/stdafx.h) ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <string>

#include "BalloonProfileLayout.h"
#include "MainPanelUserData.h"   // GetUserAvatarColor
#include "../../balloonui/balloonui/DuiXmlBuilder.h"
#include "../../balloonui/balloonui/DuiResMgr.h"   // GetFontByPointSize
#include "../../balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"

namespace balloonmain {

namespace {

// 设计稿换算常量(像素)。注释里 CSS px 是设计稿值,作对照用。
const int kBannerHeight      = 64;   // 顶部 hbox = avatar 边长
const int kAvatarSize        = 64;   // avatar 64×64
const int kAvatarRadius      = 14;   // 22% × 64 ≈ 14 像素圆角(圆角矩形)
const int kFieldRowHeight    = 34;   // FieldRow 收紧:原 38,去掉上下各 2px 让 6 行更紧凑
const int kFieldEditHeight   = 28;   // edit 控件高度(略小于行高,内嵌)
// 按钮高度 30。注意:balloonui 子控件交叉轴默认 alignCross=AlignFill,会忽略
// fixedHeight 把控件拉满容器交叉轴(见 DuiXmlBuilder/DuiLayout);所以 footer 按钮
// 的 XML 必须额外写 alignCross="center",这个 fixedHeight 才真正生效、按钮才不会
// 被拉成 footer hbox 的整高。
const int kFooterButtonHeight = 30;  // Btn 高 30(配 alignCross=center 才生效)
// 设计稿 Btn md padding=0 16 自适应宽度。优化后按"文字 + 左右内边距"重定宽度,
// 把两组宽度差收窄、整体更舒展协调(原 61 / 107 落差过大显局促):
//   "关闭" / "取消" / "保存" 两字按钮 = 76 px
//   "编辑资料" 四字按钮(已去掉铅笔图标) = 96 px
const int kFooterButtonWidthShort = 76;
const int kFooterButtonWidthEdit  = 96;

// 字号(pt)
const int kFontTitle    = 10;  // 13.5 / 10pt (标签 / 值 / 按钮 / 标题)
const int kFontNickname = 13;  // 17px / 13pt
const int kFontAccount  = 9;   // 12px / 9pt

// ---- 行左侧字段名(标签列)的排版常量 ----
// 这一列原先在 XML 里写死 60 物理像素,而字体是随 DPI 放大的
// (DuiResMgr::GetFontByPointSize 里 lfHeight = -MulDiv(pt, dpi, 72))。于是 125%
// 缩放下 10pt 汉字宽约 17 像素、四个字的"个性签名"要 68 像素,塞不进 60 就被
// DrawText 从中间硬裁掉半个字(实测见 Tests/BalloonProfileLayoutTests.cpp 的 PW0)。
// 改为按实际字体现场测量,见 ComputeFieldLabelColumnWidth。
const int kFieldLabelPadding  = 8;    // 标签文字与右侧内容之间的最小留白(像素)
const int kFieldLabelWidthMin = 60;   // 列宽下限 = 原设计值,100% 缩放下的观感基准
// 列宽上限 88:客户区 420 减去外层 vbox 左右 padding 各 20,可用 380;一行里固定
// 占位的是右侧 View 标签 260,加上 hbox 两条 gap 16(label / spacer / view 共三个
// 子控件、两条间隙)共 32,380 - 260 - 32 = 88。再宽就要挤压右侧值列,故夹到这里。
const int kFieldLabelWidthMax = 88;

struct RowDef
{
    const char* labelText;   // 行左侧固定标签文字(UTF-8)
    UINT viewId;             // View 态 DuiLabel ctrlId
    UINT editId;             // Edit 态控件 ctrlId；取 0 表示本行只读、不生成编辑控件
    bool editIsCombo;        // true = DuiComboBox(性别)
};
// 字段行总表。BuildXml 按它生成控件树、ComputeFieldLabelColumnWidth 按它测量左列
// 宽度,两处共用同一张表 —— 将来加字段时不会出现"加了行却没算进列宽"的脱节。
// 末尾两行(部门 / 职位)的 editId 为 0：它们来自企业组织架构、由管理员在
// admin 控制台维护，本人改不了，故只有查看态，切到编辑态时原样保持只读。
const RowDef kFieldRows[] = {
    { u8"昵称",     kIdNicknameView,  kIdNicknameEdit,  false },
    { u8"个性签名", kIdSignatureView, kIdSignatureEdit, false },
    { u8"性别",     kIdGenderView,    kIdGenderEdit,    true  },
    { u8"生日",     kIdBirthdayView,  kIdBirthdayEdit,  false },
    { u8"所在地",   kIdLocationView,  kIdLocationEdit,  false },
    { u8"邮箱",     kIdEmailView,     kIdEmailEdit,     false },
    { u8"部门",     kIdDeptView,      0,                false },
    { u8"职位",     kIdPositionView,  0,                false },
};
const size_t kFieldRowCount = sizeof(kFieldRows) / sizeof(kFieldRows[0]);

// 控件树整体 XML。所有 ctrlId 与 ProfileCtrlId enum 一一对应;数值嵌入
// 时用 std::to_string,避免魔数。
//   fieldLabelWidth  行左侧标签列的宽度(像素),由 ComputeFieldLabelColumnWidth 算出。
std::string BuildXml(int fieldLabelWidth)
{
    std::string xml;
    // 整体 vbox padding:横向保持 20(对齐 width=420 设计),纵向由 18 收到 14 更紧凑
    xml += "<vbox padding=\"20,14,20,14\" gap=\"0\">\n";

    // ---- 顶部 Banner ----
    // hbox 高度 = avatar 边长 64(否则 AlignFill cross 拉伸成椭圆形/扁长)
    xml += "  <hbox fixedHeight=\"";
    xml += std::to_string(kBannerHeight);
    xml += "\" gap=\"14\">\n";

    // avatar:fixedWidth=64,fixedHeight 留默认让 AlignFill 拉到 hbox 高 64,
    // 形状 / 圆角在 BuildProfileBody 末尾用 setter 设(builder 不解析 shape)。
    xml += "    <avatar id=\"";
    xml += std::to_string((int)kIdProfileAvatar);
    xml += "\" fixedWidth=\"";
    xml += std::to_string(kAvatarSize);
    xml += "\"/>\n";

    // 右侧 vbox:昵称 / 账号,gap 4。原来下面还有一行"在线状态胶囊"(在线/离开/勿扰/
    // 隐身),按用户要求已去掉。
    xml += "    <vbox weight=\"1\" gap=\"4\">\n";
    xml += "      <label id=\"";
    xml += std::to_string((int)kIdProfileNickname);
    xml += "\" fixedHeight=\"22\"/>\n";
    xml += "      <label id=\"";
    xml += std::to_string((int)kIdProfileAccount);
    xml += "\" fixedHeight=\"16\"/>\n";
    xml += "      <control weight=\"1\"/>\n";   // 占位撑开,让昵称 / 账号在 banner 内靠上
    xml += "    </vbox>\n";
    xml += "  </hbox>\n";

    // ---- 分隔线 ----
    xml += "  <control fixedHeight=\"10\"/>\n";     // banner 与分隔线间距,原 14 收到 10
    xml += "  <separator fixedHeight=\"1\"/>\n";    // borderBottom 1px

    // ---- 6 行 FieldRow ----
    // 设计稿 rows 容器 padding 10 0:外加上下空白 → 用 spacer 模拟,收到 8px
    xml += "  <control fixedHeight=\"8\"/>\n";

    for (size_t i = 0; i < kFieldRowCount; ++i)
    {
        const RowDef& r = kFieldRows[i];
        xml += "  <hbox fixedHeight=\"";
        xml += std::to_string(kFieldRowHeight);
        xml += "\" gap=\"16\">\n";
        // 行左标签列宽:按实际字体量出(见 ComputeFieldLabelColumnWidth),不写死。
        xml += "    <label fixedWidth=\"";
        xml += std::to_string(fieldLabelWidth);
        xml += "\" text=\"";
        xml += r.labelText;
        xml += "\"/>\n";
        // 中间 spacer 拉撑,View / Edit 共同靠右
        xml += "    <control weight=\"1\"/>\n";
        // View 标签宽度可变,SetTextAlign(DT_RIGHT) 后跟随
        xml += "    <label id=\"";
        xml += std::to_string((int)r.viewId);
        xml += "\" fixedWidth=\"260\"/>\n";
        if (r.editId == 0)
        {
            // 只读行：不生成任何编辑控件，编辑态下这一行原样保持查看样子。
        }
        else if (r.editIsCombo)
        {
            xml += "    <combobox id=\"";
            xml += std::to_string((int)r.editId);
            xml += "\" fixedWidth=\"180\" fixedHeight=\"";
            xml += std::to_string(kFieldEditHeight);
            // alignCross=center:让编辑框按 fixedHeight 在行内居中(上下留白),呈
            // 嵌入式输入框而非满格白条;否则交叉轴默认 AlignFill 会把它拉满整行高。
            xml += "\" alignCross=\"center\"/>\n";
        }
        else
        {
            xml += "    <edit id=\"";
            xml += std::to_string((int)r.editId);
            xml += "\" fixedWidth=\"260\" fixedHeight=\"";
            xml += std::to_string(kFieldEditHeight);
            // alignCross=center:让编辑框按 fixedHeight 在行内居中(上下留白),呈
            // 嵌入式输入框而非满格白条;否则交叉轴默认 AlignFill 会把它拉满整行高。
            xml += "\" alignCross=\"center\"/>\n";
        }
        xml += "  </hbox>\n";

        // 每行 borderBottom 1px(除最后一行)
        if (i + 1 < kFieldRowCount)
        {
            xml += "  <separator fixedHeight=\"1\"/>\n";
        }
    }

    // rows 容器 padding 10 0:底部空白,收到 8px
    xml += "  <control fixedHeight=\"8\"/>\n";

    // 弹性空隙把底部按钮推到 modal 底部。客户区高度已收到贴合内容,此处余量
    // 只剩几像素,底部按钮基本贴着行块。
    xml += "  <control weight=\"1\"/>\n";

    // ---- 底部按钮 ----
    // footer:borderTop 1px + 按钮栏。leading 空白由 12 收到 10,按钮栏上下内边距
    // 由各 12 收到各 8(kFooterButtonHeight + 16)。
    xml += "  <control fixedHeight=\"10\"/>\n";
    xml += "  <separator fixedHeight=\"1\"/>\n";
    xml += "  <hbox fixedHeight=\"";
    xml += std::to_string(kFooterButtonHeight + 16);   // padding 8 上下
    xml += "\" gap=\"8\">\n";
    xml += "    <control weight=\"1\"/>\n";
    xml += "    <button id=\"";
    xml += std::to_string((int)kIdBtnClose);
    xml += "\" fixedWidth=\"";
    xml += std::to_string(kFooterButtonWidthShort);
    xml += "\" fixedHeight=\"";
    xml += std::to_string(kFooterButtonHeight);
    xml += "\" alignCross=\"center\" text=\"";
    xml += u8"关闭";
    xml += "\"/>\n";
    xml += "    <button id=\"";
    xml += std::to_string((int)kIdBtnEdit);
    xml += "\" fixedWidth=\"";
    xml += std::to_string(kFooterButtonWidthEdit);
    xml += "\" fixedHeight=\"";
    xml += std::to_string(kFooterButtonHeight);
    xml += "\" alignCross=\"center\" text=\"";
    xml += u8"编辑资料";
    xml += "\"/>\n";
    xml += "    <button id=\"";
    xml += std::to_string((int)kIdBtnCancel);
    xml += "\" fixedWidth=\"";
    xml += std::to_string(kFooterButtonWidthShort);
    xml += "\" fixedHeight=\"";
    xml += std::to_string(kFooterButtonHeight);
    xml += "\" alignCross=\"center\" text=\"";
    xml += u8"取消";
    xml += "\"/>\n";
    xml += "    <button id=\"";
    xml += std::to_string((int)kIdBtnSave);
    xml += "\" fixedWidth=\"";
    xml += std::to_string(kFooterButtonWidthShort);
    xml += "\" fixedHeight=\"";
    xml += std::to_string(kFooterButtonHeight);
    xml += "\" alignCross=\"center\" text=\"";
    xml += u8"保存";
    xml += "\"/>\n";
    xml += "  </hbox>\n";

    xml += "</vbox>\n";
    return xml;
}

// CustomFactory:接住 DuiXmlBuilder 不认识的 tag(<combobox> / <control>)。
std::unique_ptr<balloonwjui::DuiControl> CreateProfileCustomControl(
    const balloonwjui::DuiXmlBuilder::Node& n)
{
    if (n.tag == "combobox")
    {
        std::unique_ptr<balloonwjui::DuiComboBox> cb(new balloonwjui::DuiComboBox());
        std::map<std::string, std::string>::const_iterator it = n.attrs.find("id");
        if (it != n.attrs.end())
        {
            cb->SetCtrlId((UINT)atoi(it->second.c_str()));
        }
        return std::unique_ptr<balloonwjui::DuiControl>(cb.release());
    }
    if (n.tag == "control")
    {
        return std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl());
    }
    return nullptr;
}

// 把 root 下的指定 ctrlId 控件 SetVisible(false)。未找到不报错(布局已变
// 时容忍)。
void HideById(balloonwjui::DuiControl* root, UINT ctrlId)
{
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* c = root->FindCtrlById(ctrlId);
    if (c != NULL)
    {
        c->SetVisible(false);
    }
}

// Helper:dynamic_cast 找指定 ctrlId 控件;模板省去重复 cast。
template <typename T>
T* FindCtrl(balloonwjui::DuiControl* root, UINT ctrlId)
{
    if (root == NULL)
    {
        return NULL;
    }
    return dynamic_cast<T*>(root->FindCtrlById(ctrlId));
}

void SafeSetLabelText(balloonwjui::DuiLabel* lbl, LPCTSTR text)
{
    if (lbl != NULL)
    {
        lbl->SetText(text);
    }
}

void SafeSetEditText(balloonwjui::DuiEditHost* edt, LPCTSTR text)
{
    if (edt != NULL)
    {
        edt->SetText(text);
    }
}

// 把 YYYYMMDD 整数(如 19950402)格式化成 "YYYY-MM-DD"。0 或非法值返空串。
CString FormatBirthday(long birthday)
{
    if (birthday <= 0)
    {
        return CString();
    }
    const long year  = birthday / 10000;
    const long month = (birthday / 100) % 100;
    const long day   = birthday % 100;
    if (year < 1900 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > 31)
    {
        return CString();
    }
    CString out;
    out.Format(_T("%04ld-%02ld-%02ld"), year, month, day);
    return out;
}

// 从 s[start] 起读 count 个十进制数字累加到 out;遇非数字返 false。供
// ParseProfileBirthday 严格校验 "YYYY-MM-DD" 各段用,避免依赖 _stscanf 的平台细节。
bool ParseDecDigits(const CString& s, int start, int count, long& out)
{
    long v = 0;
    for (int i = 0; i < count; ++i)
    {
        TCHAR c = s[start + i];
        if (c < _T('0') || c > _T('9'))
        {
            return false;
        }
        v = v * 10 + (long)(c - _T('0'));
    }
    out = v;
    return true;
}

// 递归遍历控件树,对每个 DuiLabel 调 visitor;visitor 在 ctrlId == 0(行
// 标签未命名)时设默认字号 10pt,其余按 ctrlId 跳过(BuildProfileBody 末尾
// 已对每个命名 label 单独设字号 / 对齐)。
void WalkUnnamedLabelsSetDefaultFont(balloonwjui::DuiControl* node, HFONT defaultFont)
{
    if (node == NULL)
    {
        return;
    }
    balloonwjui::DuiLabel* lbl = dynamic_cast<balloonwjui::DuiLabel*>(node);
    if (lbl != NULL && lbl->GetCtrlId() == 0)
    {
        lbl->SetFont(defaultFont);
        // 兜底加省略号:列宽已按字体量过、正常放得下,但万一将来字段名更长、或者
        // 列宽被上限夹住,溢出时退化成"个性签…"而不是被 DrawText 从中间硬裁半个字。
        // DuiLabel 默认绘制标志不含 DT_END_ELLIPSIS(见 DuiLabel.h),故需显式设。
        lbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& children =
        node->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        WalkUnnamedLabelsSetDefaultFont(children[i].get(), defaultFont);
    }
}

} // anonymous namespace

bool ApplyProfileDataToView(balloonwjui::DuiControl* root,
                            const ProfileDataInput& info)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiLabel* topNickname =
        FindCtrl<balloonwjui::DuiLabel>(root, kIdProfileNickname);
    if (topNickname == NULL)
    {
        return false;
    }

    // ---- 顶部 Banner ----
    balloonwjui::DuiAvatar* avatar =
        FindCtrl<balloonwjui::DuiAvatar>(root, kIdProfileAvatar);
    if (avatar != NULL)
    {
        const CString avatarName = info.nickname.IsEmpty()
                                       ? info.account
                                       : info.nickname;
        avatar->SetName(avatarName);
        avatar->SetFallbackBgColor(GetUserAvatarColor(info.userid));
    }

    SafeSetLabelText(topNickname,
        info.nickname.IsEmpty() ? info.account : info.nickname);

    // 账号行显示登录账号名(m_strAccount,如 MY0308),与登录保持一致;此前误用 userid
    // 数字(如 5308)导致"账号信息和登录不一致"。account 兜底为空时退回 userid。
    CString acctLine;
    if (info.account.IsEmpty())
    {
        acctLine.Format(_T("账号 %u"), info.userid);
    }
    else
    {
        acctLine.Format(_T("账号 %s"), static_cast<LPCTSTR>(info.account));
    }
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdProfileAccount),
                     acctLine);

    // ---- 6 行 View 系列 DuiLabel ----
    const CString genderText = (info.gender == 0) ? _T("男") : _T("女");
    const CString bdayText   = FormatBirthday(info.birthday);

    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdNicknameView),
                     info.nickname);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdSignatureView),
                     info.signature);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdGenderView),
                     genderText);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdBirthdayView),
                     bdayText);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdLocationView),
                     info.address);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdEmailView),
                     info.email);
    // 部门 / 职位来自企业组织架构，只有查看态。取不到时显示"—"而不是留空 ——
    // 留空会让人分不清是"没有部门"还是"这行坏了"。
    const CString kEmptyFieldText = _T("—");
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdDeptView),
                     info.dept.IsEmpty() ? kEmptyFieldText : info.dept);
    SafeSetLabelText(FindCtrl<balloonwjui::DuiLabel>(root, kIdPositionView),
                     info.position.IsEmpty() ? kEmptyFieldText : info.position);

    // ---- Edit 系列控件初值（部门 / 职位只读，没有编辑控件，不在此列）----
    SafeSetEditText(FindCtrl<balloonwjui::DuiEditHost>(root, kIdNicknameEdit),
                    info.nickname);
    SafeSetEditText(FindCtrl<balloonwjui::DuiEditHost>(root, kIdSignatureEdit),
                    info.signature);
    SafeSetEditText(FindCtrl<balloonwjui::DuiEditHost>(root, kIdBirthdayEdit),
                    bdayText);
    SafeSetEditText(FindCtrl<balloonwjui::DuiEditHost>(root, kIdLocationEdit),
                    info.address);
    SafeSetEditText(FindCtrl<balloonwjui::DuiEditHost>(root, kIdEmailEdit),
                    info.email);

    balloonwjui::DuiComboBox* genderCombo =
        FindCtrl<balloonwjui::DuiComboBox>(root, kIdGenderEdit);
    if (genderCombo != NULL)
    {
        genderCombo->ResetContent();
        genderCombo->AddString(_T("男"));
        genderCombo->AddString(_T("女"));
        genderCombo->SetCurSel((info.gender == 0) ? 0 : 1, false);
    }

    return true;
}

long ParseProfileBirthday(const CString& text)
{
    CString s = text;
    s.Trim();
    if (s.IsEmpty())
    {
        return 0;   // 空串 = 未设生日(与 FormatBirthday(0)=="" 往返一致)
    }
    // 严格要求 "YYYY-MM-DD":长度正好 10,第 5、8 位是 '-',其余为数字。
    if (s.GetLength() != 10 || s[4] != _T('-') || s[7] != _T('-'))
    {
        return -1;
    }
    long year  = 0;
    long month = 0;
    long day   = 0;
    if (!ParseDecDigits(s, 0, 4, year) ||
        !ParseDecDigits(s, 5, 2, month) ||
        !ParseDecDigits(s, 8, 2, day))
    {
        return -1;
    }
    // 范围校验与 FormatBirthday 同口径。
    if (year < 1900 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > 31)
    {
        return -1;
    }
    return year * 10000 + month * 100 + day;
}

bool ApplyProfileButtonStyles(balloonwjui::DuiControl* root,
                              const ProfileVisualResources& res)
{
    if (root == NULL)
    {
        return false;
    }

    balloonwjui::DuiButton* btnClose  =
        FindCtrl<balloonwjui::DuiButton>(root, kIdBtnClose);
    balloonwjui::DuiButton* btnEdit   =
        FindCtrl<balloonwjui::DuiButton>(root, kIdBtnEdit);
    balloonwjui::DuiButton* btnCancel =
        FindCtrl<balloonwjui::DuiButton>(root, kIdBtnCancel);
    balloonwjui::DuiButton* btnSave   =
        FindCtrl<balloonwjui::DuiButton>(root, kIdBtnSave);

    if (btnClose == NULL || btnEdit == NULL || btnCancel == NULL || btnSave == NULL)
    {
        return false;
    }

    btnClose->SetVariant(balloonwjui::DuiButton::Variant::Default);
    btnCancel->SetVariant(balloonwjui::DuiButton::Variant::Default);

    btnEdit->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    btnSave->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    if (res.hPrimaryBg != NULL)
    {
        // 圆角橙底走 9-grid:四角 kProfilePrimaryBtnRadius 切片原样保留(圆角不被
        // stretch 拉直),边与中心拉伸,所以任意按钮宽高都保持 8px 圆角,与关闭 /
        // 取消(Variant::Default 内置 8px 圆角)统一。normal/hover/pressed 三态渐深;
        // disabled 传 NULL 由 DuiButton fallback 回 normal(这两个按钮不会 disabled)。
        const int radius = kProfilePrimaryBtnRadius;
        btnEdit->SetBgBitmap(res.hPrimaryBg, res.hPrimaryBgHover,
                             res.hPrimaryBgPressed, NULL);
        btnEdit->SetBgInsets(radius, radius, radius, radius);
        btnEdit->SetTextColor(RGB(0xFF, 0xFF, 0xFF));
        btnSave->SetBgBitmap(res.hPrimaryBg, res.hPrimaryBgHover,
                             res.hPrimaryBgPressed, NULL);
        btnSave->SetBgInsets(radius, radius, radius, radius);
        btnSave->SetTextColor(RGB(0xFF, 0xFF, 0xFF));
    }

    btnClose->SetTextPointSize(kFontTitle);
    btnEdit->SetTextPointSize(kFontTitle);
    btnCancel->SetTextPointSize(kFontTitle);
    btnSave->SetTextPointSize(kFontTitle);

    return true;
}

namespace {

// 把 c 加入 containers(跳 NULL、去重)。受影响容器数量极少(6 行 + footer),
// 线性查重即可,无需引入 set。cap 是 containers 数组容量。
void AddUniqueContainer(balloonwjui::DuiControl** containers, int& count,
                        int cap, balloonwjui::DuiControl* c)
{
    if (c == NULL)
    {
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        if (containers[i] == c)
        {
            return;
        }
    }
    if (count < cap)
    {
        containers[count] = c;
        ++count;
    }
}

} // anonymous namespace

bool SetProfileEditMode(balloonwjui::DuiControl* root, bool edit)
{
    if (root == NULL)
    {
        return false;
    }

    // 受影响容器(每行 hbox + footer hbox)收集到此,稍后逐个 ForceLayout。
    // 6 行 + 1 footer,开 8 足够。
    const int kMaxContainers = 8;
    balloonwjui::DuiControl* containers[kMaxContainers];
    int containerCount = 0;

    // ---- 6 行 view / edit 系列可见性翻转 ----
    struct Pair { UINT viewId; UINT editId; };
    const Pair pairs[] = {
        { kIdNicknameView,  kIdNicknameEdit  },
        { kIdSignatureView, kIdSignatureEdit },
        { kIdGenderView,    kIdGenderEdit    },
        { kIdBirthdayView,  kIdBirthdayEdit  },
        { kIdLocationView,  kIdLocationEdit  },
        { kIdEmailView,     kIdEmailEdit     },
    };
    const size_t pairCount = sizeof(pairs) / sizeof(pairs[0]);
    for (size_t i = 0; i < pairCount; ++i)
    {
        balloonwjui::DuiControl* v = root->FindCtrlById(pairs[i].viewId);
        balloonwjui::DuiControl* e = root->FindCtrlById(pairs[i].editId);
        if (v != NULL)
        {
            v->SetVisible(!edit);
        }
        if (e != NULL)
        {
            e->SetVisible(edit);
        }
        // 行 hbox = view / edit 控件的父(两者同父),取任一非空者。
        balloonwjui::DuiControl* row = NULL;
        if (v != NULL)
        {
            row = v->GetParent();
        }
        else if (e != NULL)
        {
            row = e->GetParent();
        }
        AddUniqueContainer(containers, containerCount, kMaxContainers, row);
    }

    // ---- 底部 4 按钮可见性翻转:查看态显 Close/Edit,编辑态显 Cancel/Save ----
    balloonwjui::DuiControl* btnClose  = root->FindCtrlById(kIdBtnClose);
    balloonwjui::DuiControl* btnEdit   = root->FindCtrlById(kIdBtnEdit);
    balloonwjui::DuiControl* btnCancel = root->FindCtrlById(kIdBtnCancel);
    balloonwjui::DuiControl* btnSave   = root->FindCtrlById(kIdBtnSave);
    if (btnClose  != NULL)
    {
        btnClose->SetVisible(!edit);
    }
    if (btnEdit   != NULL)
    {
        btnEdit->SetVisible(!edit);
    }
    if (btnCancel != NULL)
    {
        btnCancel->SetVisible(edit);
    }
    if (btnSave   != NULL)
    {
        btnSave->SetVisible(edit);
    }
    balloonwjui::DuiControl* footer = NULL;
    if (btnClose != NULL)
    {
        footer = btnClose->GetParent();
    }
    AddUniqueContainer(containers, containerCount, kMaxContainers, footer);

    // ---- 关键:逐个容器 ForceLayout(自身矩形),绕开 SetRect 的相等提前返回 ----
    // 行 / footer 容器自身矩形在两态间不变,根 ForceLayout 经 SetRect 摆放它们时
    // 因矩形相等提前返回(DuiControl.cpp),其 Layout 不重跑,刚可见的子控件就拿不到
    // 新矩形。ForceLayout 直接调 Layout、不经 SetRect 判等,故能强制重排。
    for (int i = 0; i < containerCount; ++i)
    {
        containers[i]->ForceLayout(containers[i]->GetRect());
    }

    return true;
}

int ComputeFieldLabelColumnWidth(HFONT hFont)
{
    // 拿不到字体就退回下限:宁可维持原来的观感,也不要返回 0 让整列塌掉。
    if (hFont == NULL)
    {
        return kFieldLabelWidthMin;
    }

    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return kFieldLabelWidthMin;
    }
    HGDIOBJ oldFont = ::SelectObject(hdc, hFont);

    // 量全表最宽的那个字段名 —— 所有行共用一个列宽,取最大值才不会有哪一行被裁。
    int widest = 0;
    for (size_t i = 0; i < kFieldRowCount; ++i)
    {
        // 表里的标签是 UTF-8 字面量,测量要用宽字符版本。
        const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, kFieldRows[i].labelText,
                                                  -1, NULL, 0);
        if (wideLen <= 1)
        {
            continue;   // 空串:MultiByteToWideChar 的返回值含结尾的 L'\0'
        }
        std::wstring wide;
        wide.resize((size_t)wideLen);
        const int written = ::MultiByteToWideChar(CP_UTF8, 0, kFieldRows[i].labelText,
                                                  -1, &wide[0], wideLen);
        if (written <= 1)
        {
            continue;
        }
        wide.resize((size_t)(written - 1));   // 去掉结尾的 L'\0'

        SIZE sz = { 0, 0 };
        if (::GetTextExtentPoint32W(hdc, wide.c_str(), (int)wide.size(), &sz))
        {
            if (sz.cx > widest)
            {
                widest = sz.cx;
            }
        }
    }

    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);

    int width = widest + kFieldLabelPadding;
    if (width < kFieldLabelWidthMin)
    {
        width = kFieldLabelWidthMin;
    }
    if (width > kFieldLabelWidthMax)
    {
        width = kFieldLabelWidthMax;
    }
    return width;
}

std::unique_ptr<balloonwjui::DuiControl> BuildProfileBody()
{
    // 左列宽度按<u>真正会被设上去的那个字体</u>现场测量 —— 下面
    // WalkUnnamedLabelsSetDefaultFont 设的就是同一个 viewFont,两处必须同源,
    // 否则量的是一套字体、画的是另一套,又会裁字。
    balloonwjui::DuiResMgr& res = balloonwjui::DuiResMgr::Inst();
    HFONT viewFont = res.GetFontByPointSize(kFontTitle, false);
    const int fieldLabelWidth = ComputeFieldLabelColumnWidth(viewFont);

    const std::string xml = BuildXml(fieldLabelWidth);
    std::unique_ptr<balloonwjui::DuiControl> root =
        balloonwjui::DuiXmlBuilder::FromString(xml.c_str(),
                                               &CreateProfileCustomControl);

    if (!root)
    {
        return root;
    }

    // ---- 初始可见性:查看态 ----
    HideById(root.get(), kIdNicknameEdit);
    HideById(root.get(), kIdSignatureEdit);
    HideById(root.get(), kIdGenderEdit);
    HideById(root.get(), kIdBirthdayEdit);
    HideById(root.get(), kIdLocationEdit);
    HideById(root.get(), kIdEmailEdit);
    HideById(root.get(), kIdBtnCancel);
    HideById(root.get(), kIdBtnSave);

    // ---- 顶部 avatar 形状:圆角矩形 14px(设计稿 rounded="22%" of 64) ----
    balloonwjui::DuiAvatar* avatar =
        FindCtrl<balloonwjui::DuiAvatar>(root.get(), kIdProfileAvatar);
    if (avatar != NULL)
    {
        avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
        avatar->SetCornerRadius(kAvatarRadius);
    }

    // ---- 字号设置 ----（res / viewFont 已在本函数开头取到,用于测量左列宽度）

    // 顶部昵称 13pt 600
    balloonwjui::DuiLabel* nickname =
        FindCtrl<balloonwjui::DuiLabel>(root.get(), kIdProfileNickname);
    if (nickname != NULL)
    {
        nickname->SetFont(res.GetFontByPointSize(kFontNickname, true));
        nickname->SetTextColor(RGB(0x1A, 0x1A, 0x1A));   // --text
    }

    // 顶部账号 9pt muted。设为 ModeLink 使其可点(点击发 DUIN_CLICK,对话框据此把账号
    // 复制到剪贴板);link/hover 三态色都设成 muted 灰,避免出现蓝色超链接观感(只在
    // hover 时略变深给一点"可点"暗示)。
    balloonwjui::DuiLabel* account =
        FindCtrl<balloonwjui::DuiLabel>(root.get(), kIdProfileAccount);
    if (account != NULL)
    {
        account->SetFont(res.GetFontByPointSize(kFontAccount, false));
        account->SetTextColor(RGB(0x6B, 0x6B, 0x6B));    // --text-muted
        account->SetMode(balloonwjui::DuiLabel::ModeLink);
        account->SetLinkColor(RGB(0x6B, 0x6B, 0x6B));    // 常态同 muted 灰
        account->SetHoverColor(RGB(0x4A, 0x4A, 0x4A));   // hover 略深,提示可点
        account->SetVisitedColor(RGB(0x6B, 0x6B, 0x6B)); // 不区分已访问
    }

    // 各行 View 系列:10pt + 右对齐,字色 muted(设计稿 fontSize:13)
    const UINT viewIds[] = {
        kIdNicknameView, kIdSignatureView, kIdGenderView,
        kIdBirthdayView, kIdLocationView,  kIdEmailView,
        kIdDeptView,     kIdPositionView
    };
    const DWORD kRightAlign = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
    for (size_t i = 0; i < sizeof(viewIds) / sizeof(viewIds[0]); ++i)
    {
        balloonwjui::DuiLabel* lbl = FindCtrl<balloonwjui::DuiLabel>(
            root.get(), viewIds[i]);
        if (lbl != NULL)
        {
            lbl->SetFont(viewFont);
            lbl->SetTextAlign(kRightAlign);
            // 个性签名 view 用 muted 字色(设计稿 PersonalInfoDialog 行:
            //   {edit ? <BareInput.../> : <span style={{... color:'var(--text-muted)'...
            // 其余 5 行用 --text 黑。
            const COLORREF c = (viewIds[i] == kIdSignatureView)
                                   ? RGB(0x6B, 0x6B, 0x6B)
                                   : RGB(0x1A, 0x1A, 0x1A);
            lbl->SetTextColor(c);
        }
    }

    // 行左标签(未命名 DuiLabel)默认字号 10pt + 字色 text 黑(设计稿 13.5)。
    // 用 walk 给所有 ctrlId==0 的 DuiLabel 统一设默认字号。
    WalkUnnamedLabelsSetDefaultFont(root.get(), viewFont);

    return root;
}

} // namespace balloonmain
