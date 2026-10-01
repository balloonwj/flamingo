/**
 *  BalloonLoginLayout.cpp
 *
 *  登录对话框客户区控件树的构造实现。布局以内嵌 XML 常量为唯一真值源
 *  （随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder 解析。
 *  XML 里只能写 builder 内置标签（vbox / hbox / label / button / edit），
 *  账号下拉框 DuiComboBox 与 spacer 通过自定义标签 + CustomFactory 接入。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用，所以自己
 *  把 balloonui 头文件所需的 ATL/WTL 前置依赖引进来。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  P1a 范围：产出"控件点名契约"完整的控件树（账号 / 密码 / 复选框 /
 *  登录按钮 / 底部链接齐备、可按 id 取回、类型正确）。视觉保真细节
 *  （橙色渐变 Banner、圆角输入框、头像压 Banner 等自绘控件）留待后续
 *  绘制阶段。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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
#include <vector>
#include <memory>
#include <tchar.h>
#include <stdio.h>

#include "BalloonLoginLayout.h"
#include "AuthBanner.h"
#include "AuthButtons.h"
#include "AuthInputField.h"
#include "../BalloonMain/AvatarPalette.h"   // 头像底色调色板（与主面板共用一份）
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 状态区文字（ApplyLoginStatusView）

namespace balloonauth {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiEdit;

namespace {

// =====================================================================
// 登录窗客户区布局（内嵌 XML）
//
// 外层 vbox 自上而下（左、右、下各留 1 像素内边距，露出窗体背景图四周的灰边，
// 见 CreateLoginFrameBgBitmap）：
//   · 顶部品牌 Banner（AuthBanner 自绘控件：橙色渐变 + 火烈鸟 + 标题）。
//   · 表单 vbox（weight=1，吃掉 Banner 以下的全部高度）：
//       账号下拉框 / 密码框 / 「记住密码·自动登录·忘记密码」一行 /
//       底部 vbox：登录按钮 / 状态区 / 「注册账号·网络设置」一行。
//
// 底部 vbox 的间距是 8，比表单其余部分的 12 紧凑：这三行视觉上是一组，
// 2026-09-30 之前这里还夹着一块弹性空白，链接行离登录按钮 106 像素，显得松散。
// 表单里<u>没有</u>弹性空白，窗口高度 kLoginWindowHeight 按内容算得正好，
// 多给的高度会整块留在链接行下方（由 LSA14 看住）。
//
// 状态区（id 150）<u>一直占位</u>：平时文字为空、进度条隐藏；登录期间显示
// 「正在登录…」加一条不确定进度条，登录失败时只显示失败原因。容器本身从不隐藏，
// 所以切换状态时登录按钮与链接行都不移动。切换一律经 ApplyLoginStatusView，
// 它负责在显隐进度条之后重排状态区（理由见该函数的注释）。
//
// 两行链接（「忘记密码？」与底部两个）行高都取 22：AuthLinkLabel 的字号随行高
// 按比例算出，22 高对应 13 像素字号。
//
// 进度条高 14 像素并关掉百分比文字（show-percent="false"）。DuiProgressBar 默认
// 会在条上画 "NN%"，而 marquee 模式下压根没有确定的进度值，那个数字既无意义、
// 在原先 4 像素的条高下又挤成一团看不清（2026-08-14 用户反馈）。说明文字由上方
// 的状态标签承担，条本身只表达"正在进行中"。
//
// builder 内置标签只有 vbox / hbox / label / button / edit；品牌 Banner、
// 账号下拉框、底部弹性占位都不是内置标签，通过自定义标签经
// CreateLoginCustomControl 接入：
//   <auth-banner/>     —— 橙色渐变 + 火烈鸟品牌 Banner（AuthBanner）
//   <auth-button/>     —— 橙色圆角主按钮（AuthPrimaryButton）
//   <auth-link/>       —— 无边框文字链接（AuthLinkLabel）
//   <auth-check/>      —— 复选框（AuthCheckBox）
//   <auth-field/>      —— 圆角输入框容器（AuthInputField，内嵌下拉框 / 密码框）
//   <control/>         —— 不可见 spacer（裸 DuiControl）
// 控件公共属性（id / fixedWidth / fixedHeight / weight）由 builder 的
// ApplyCommon 统一接住，自定义工厂只负责造控件本体。
// =====================================================================
const char* const kLoginBodyXml = u8R"xml(
<vbox padding="1,0,1,1" gap="0">
  <auth-banner id="140" fixedHeight="240"/>
  <vbox weight="1" padding="24,14,24,20" gap="12">
    <auth-field kind="account" fixedHeight="40"/>
    <auth-field kind="password" fixedHeight="40"/>
    <hbox fixedHeight="22" gap="12">
      <auth-check id="110" text="记住密码" weight="1"/>
      <auth-check id="111" text="自动登录" weight="1"/>
      <auth-link id="132" text="忘记密码？" link-color="255,122,69" weight="1"/>
    </hbox>
    <vbox weight="1" padding="0" gap="8">
      <auth-button id="120" text="登 录" fixedHeight="42"/>
      <vbox id="150" fixedHeight="44" padding="0,8,0,0" gap="6">
        <label id="151" fixedHeight="16" text="" text-color="107,107,107"/>
        <progress id="152" fixedHeight="14" marquee="true" show-percent="false"/>
      </vbox>
      <hbox fixedHeight="22" gap="12">
        <auth-link id="130" text="注册账号" link-color="255,122,69" weight="1"/>
        <auth-link id="131" text="网络设置" link-color="107,107,107" weight="1"/>
      </hbox>
    </vbox>
  </vbox>
</vbox>
)xml";

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// UTF-8 字符串转 CString（宽字符）。XML 属性值是 UTF-8，而控件 SetText
// 需要宽字符。空串原样返回空。
CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.GetBuffer(wlen), wlen);
    out.ReleaseBuffer();
    return out;
}

// 解析 "r,g,b" 三段式颜色字符串；解析失败返回 def。
COLORREF ParseRgbColor(const std::string& s, COLORREF def)
{
    int r = 0, g = 0, b = 0;
    if (sscanf_s(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3)
    {
        return RGB(r, g, b);
    }
    return def;
}

// 圆角输入框内嵌控件的填充色 #F5F4F1（design Input 背景 --bg-list）。
const COLORREF kFieldFillColor = RGB(0xF5, 0xF4, 0xF1);

// 账号下拉框的占位文字。与密码框的"密码"对称，账号框为空时显示。
const wchar_t* const kAccountPlaceholder = L"账号";

// 取出下拉框内嵌的那个输入框子控件。
//
// DuiComboBox 在 editable 风格下会把一个 DuiEdit 子类挂到自己名下（见
// balloonui 的 DuiComboBox::EnsureEditChild），但它既没有公开取用该子控件的
// 接口，也没有转发 SetPlaceholder。这里顺着公开的 Children() 走一层把它找
// 出来，从而在不改动 balloonui 的前提下给账号框设占位文字。
//   pCombo：下拉框，可为 NULL。
// 返回：内嵌的输入框子控件；下拉框为 NULL、或还没有内嵌控件（read-only
//       风格下就没有）时返回 NULL。所有权属于下拉框，调用方不得释放。
DuiEdit* FindComboEditHost(DuiComboBox* pCombo)
{
    if (pCombo == NULL)
        return NULL;

    const std::vector<std::unique_ptr<DuiControl> >& children = pCombo->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        DuiEdit* pEdit = dynamic_cast<DuiEdit*>(children[i].get());
        if (pEdit != NULL)
            return pEdit;
    }
    return NULL;
}

// 自定义标签工厂 —— 把 XML 里 builder 不认识的标签翻译成对应控件实例。
// 未知标签返回 nullptr，让 builder 走默认的"跳过未知标签"路径。
//   n：builder 解析出的标签节点（含 tag 名与属性表）。
// 返回：新建的控件（所有权移交 builder）；不识别该标签时返回 nullptr。
std::unique_ptr<DuiControl> CreateLoginCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "control")
    {
        // 不可见 spacer —— 仅靠 weight 占位。登录页自 2026-09-30 起不再使用
        // （窗口高度按内容算得正好），保留该标签供以后调整布局时使用。
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    if (n.tag == "auth-banner")
    {
        // 顶部品牌 Banner —— 橙色渐变 + 火烈鸟 + 标题（纯绘制控件）。
        return std::unique_ptr<DuiControl>(new AuthBanner());
    }

    if (n.tag == "auth-button")
    {
        // 橙色圆角主操作按钮。文字经 text 属性传入（UTF-8 → 宽字符）。
        std::unique_ptr<AuthPrimaryButton> btn(new AuthPrimaryButton());
        btn->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "auth-link")
    {
        // 无边框文字链接。文字 + 颜色经 text / link-color 属性传入。
        std::unique_ptr<AuthLinkLabel> link(new AuthLinkLabel());
        link->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        link->SetLinkColor(ParseRgbColor(GetNodeAttr(n, "link-color"),
                                         RGB(0xFF, 0x7A, 0x45)));
        return std::unique_ptr<DuiControl>(link.release());
    }

    if (n.tag == "auth-check")
    {
        // 复选框。文字经 text 属性传入。
        std::unique_ptr<AuthCheckBox> chk(new AuthCheckBox());
        chk->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        return std::unique_ptr<DuiControl>(chk.release());
    }

    if (n.tag == "auth-field")
    {
        // 圆角输入框容器：内嵌账号下拉框（kind=account）或密码框
        // （kind=password）。内嵌控件的 id 在此硬编码为 CtrlId 约定值
        // —— XML 不写 id（写了会落到容器上而非内嵌控件）。内嵌控件底色
        // 设为 #F5F4F1、关闭自身边框，与圆角容器无缝融合。
        const std::string kind = GetNodeAttr(n, "kind");
        std::unique_ptr<AuthInputField> field(new AuthInputField());

        if (kind == "password")
        {
            field->SetIconKind(AuthInputField::IconLock);
            std::unique_ptr<DuiEdit> edit(new DuiEdit());
            edit->SetCtrlId(kIdPasswordEdit);
            edit->SetPassword(true);
            edit->SetPlaceholder(L"密码");
            edit->SetBgColor(kFieldFillColor);
            edit->SetShowBorder(false);
            field->AddChild(std::unique_ptr<DuiControl>(edit.release()));
        }
        else
        {
            // 默认 / kind="account"：可编辑账号下拉框（保留历史账号下拉）。
            // 显示右侧下拉箭头，让账号框一眼可辨认为"可下拉的输入框"。
            field->SetIconKind(AuthInputField::IconUser);
            std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
            combo->SetCtrlId(kIdAccountCombo);
            combo->SetEditable(true);
            // 下拉里每个历史账号右侧带一个删除叉，让用户清掉不再用的账号。
            // 点它只发 DUICBN_ITEMDELETE 通知，真删与否由登录窗二次确认后决定
            // （见 CBalloonLoginDlg::OnDuiNotify）。
            combo->SetShowItemDelete(true);
            combo->SetBgColor(kFieldFillColor);
            combo->SetShowBorder(false);

            // 占位文字。上面的 SetEditable(true) 已经把内嵌的输入框控件建好，
            // 此刻取得到，可以直接设置占位文字。
            DuiEdit* accountEdit = FindComboEditHost(combo.get());
            if (accountEdit != NULL)
            {
                accountEdit->SetPlaceholder(kAccountPlaceholder);
            }

            field->AddChild(std::unique_ptr<DuiControl>(combo.release()));
        }
        return std::unique_ptr<DuiControl>(field.release());
    }

    return nullptr;
}

// 判断字符串是否为空或纯空白（全部是空格 / 制表符等空白字符）。
bool IsBlank(const CString& s)
{
    int len = s.GetLength();
    for (int i = 0; i < len; ++i)
    {
        if (!_istspace(s[i]))
            return false;
    }
    return true;
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildLoginBody()
{
    return DuiXmlBuilder::FromString(kLoginBodyXml, &CreateLoginCustomControl);
}

LoginInputResult ValidateLoginInput(const CString& account, const CString& password)
{
    if (IsBlank(account))
        return kLoginInputNoAccount;
    if (IsBlank(password))
        return kLoginInputNoPassword;
    return kLoginInputOk;
}

bool ShouldAutoStartLogin(bool requested, const CString& account, const CString& password)
{
    //"账号密码都有"与手动点登录时的校验同一口径：纯空白也算没有。
    return requested && ValidateLoginInput(account, password) == kLoginInputOk;
}

PostRegisterFill MakePostRegisterFill(const CString& registeredAccount,
                                      const CString& registeredPassword)
{
    PostRegisterFill fill;
    // 两个勾选框一律回到未勾选：用户还没表达过"记住密码"的意愿，且要与调用方
    // 写进历史账号列表的那条记录（空密码 + 两项皆否）对得上。
    fill.m_shouldFill  = false;
    fill.m_rememberPwd = false;
    fill.m_autoLogin   = false;

    // 账号是这次回填的锚点 —— 没有账号，密码填给谁都说不清，索性整个界面不动。
    if (IsBlank(registeredAccount))
    {
        return fill;
    }

    fill.m_shouldFill = true;
    fill.m_account    = registeredAccount;
    // 密码原样搬运，不 Trim：首尾空格在密码里是有效字符，去掉了就登不上去。
    // 传进来的密码为空时这里也照样赋空 —— 密码框被清空，好过把上一个账号的
    // 密码留在框里让用户误以为那就是自己刚设的密码。
    fill.m_password   = registeredPassword;
    return fill;
}

COLORREF ChooseBannerAvatarColor(const CString& account, bool isKnownAccount)
{
    // 没登录过、或账号为空 —— 用默认橙色方块（调色板第 0 个），即"现在
    // 默认的头像"底色。
    if (!isKnownAccount || account.IsEmpty())
    {
        return balloonmain::AvatarColorByIndex(0);
    }

    // 登录过的账号 —— 按账号字符串稳定取一个调色板色，使不同账号头像
    // 可凭颜色区分，同一账号每次恒定同色。
    //
    // 注意这里取色用的是账号而非姓名：姓名(昵称)是用户随时可改的，若按
    // 姓名取色，用户改一次昵称头像就换个颜色，反而认不出来了。
    return balloonmain::AvatarColorForText((LPCTSTR)account);
}

CString ChooseBannerDisplayName(const CString& account, const CString& nickName)
{
    // 姓名两端可能带空白（服务器侧未必 trim 过），全是空白的姓名视同没有。
    CString trimmedNickName = nickName;
    trimmedNickName.Trim();
    if (!trimmedNickName.IsEmpty())
    {
        return trimmedNickName;
    }

    return account;
}

bool ShouldReadAccountFromComboItem(int notifyExtra, int itemCount)
{
    // 索引越界时退回读输入框：通知里的索引与列表内容理论上总是同步的，但真
    // 出现对不上（列表在通知途中被改过）时，输入框里至少还是个像样的账号，
    // 比取到空串强。
    return notifyExtra >= 0 && notifyExtra < itemCount;
}

HBITMAP CreateLoginFrameBgBitmap()
{
    // 位图与窗口一样大，九宫格的每一格都按 1:1 画、不经拉伸。不能做成几个像素宽的小图
    // 再拉伸：DuiNinePatch 拉伸时用 GDI+ 双线性插值，拉伸格的边缘会混入源图里相邻格的
    // 像素，灰边紧挨着可拉伸区时，整片白色区都会被混成灰白渐变（LSA15）。登录窗不可
    // 调整大小，位图尺寸固定即可；380×565×4 字节约 0.8 MB。
    const int kBmpW = kLoginWindowWidth;
    const int kBmpH = kLoginWindowHeight;
    // 标题栏那一条的橙色，与 AuthBanner 渐变起点同色，两者在视觉上连成一片。
    const COLORREF kTitleOrange = RGB(0xFF, 0xB7, 0x93);
    // 表单区底色。
    const COLORREF kFormWhite = RGB(0xFF, 0xFF, 0xFF);

    HDC hdcScreen = ::GetDC(NULL);
    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, kBmpW, kBmpH);
    if (hbm != NULL)
    {
        HDC mem = ::CreateCompatibleDC(hdcScreen);
        HGDIOBJ old = ::SelectObject(mem, hbm);

        // 先整张涂成灰色，再在里面填橙、白两块，四周自然留下 1 像素灰边。
        RECT rcAll = { 0, 0, kBmpW, kBmpH };
        HBRUSH brBorder = ::CreateSolidBrush(kLoginFrameBorderColor);
        ::FillRect(mem, &rcAll, brBorder);
        ::DeleteObject(brBorder);

        RECT rcOrange = { kLoginFrameBorderPx, kLoginFrameBorderPx,
                          kBmpW - kLoginFrameBorderPx, kLoginTitleBarHeight };
        HBRUSH brOrange = ::CreateSolidBrush(kTitleOrange);
        ::FillRect(mem, &rcOrange, brOrange);
        ::DeleteObject(brOrange);

        RECT rcWhite = { kLoginFrameBorderPx, kLoginTitleBarHeight,
                         kBmpW - kLoginFrameBorderPx, kBmpH - kLoginFrameBorderPx };
        HBRUSH brWhite = ::CreateSolidBrush(kFormWhite);
        ::FillRect(mem, &rcWhite, brWhite);
        ::DeleteObject(brWhite);

        ::SelectObject(mem, old);
        ::DeleteDC(mem);
    }
    ::ReleaseDC(NULL, hdcScreen);
    return hbm;
}

RECT LoginFrameBgInsets()
{
    // 左、右、下三边的边距正好是灰边宽度，灰边不会被拉伸成一片；
    // 上边距是整条标题栏，橙色条保持原高度、不随窗口高度拉伸。
    RECT rc = { kLoginFrameBorderPx, kLoginTitleBarHeight, kLoginFrameBorderPx, kLoginFrameBorderPx };
    return rc;
}

void ApplyLoginStatusView(balloonwjui::DuiControl* root, LoginStatusView view, LPCTSTR text)
{
    if (root == NULL)
    {
        return;
    }

    DuiControl* area = root->FindCtrlById(kIdStatusArea);
    balloonwjui::DuiLabel* label = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdStatusText));
    DuiControl* bar = root->FindCtrlById(kIdStatusProgress);

    // 空闲态不显示任何文字；其余两种状态显示调用方给的文字（NULL 按空串处理）。
    if (label != NULL)
    {
        label->SetText((view == kLoginStatusIdle || text == NULL) ? _T("") : text);
    }

    // 进度条只在登录中显示；失败提示只留一行文字。
    if (bar != NULL)
    {
        bar->SetVisible(view == kLoginStatusLogining);
    }

    if (area == NULL)
    {
        return;
    }

    // 容器始终可见、始终占位，切换状态时上下的控件不移动。
    area->SetVisible(true);

    // 必须由本函数重排状态区，且只能用 ForceLayout。容器自己的矩形从头到尾不变，
    // 对根控件重排时一路下传的 SetRect 会在这一层因矩形未变而直接返回，里面的进度条
    // 不会被重新计算位置；而竖直布局又会跳过隐藏的子控件，于是一个从未以可见状态
    // 参与过排版的进度条，显示出来时矩形仍是空的，界面上什么也画不出（LSA10）。
    // 容器还没被布过局时矩形为空，这一步只是把子控件排成空矩形，没有副作用，
    // 首次布局时它们会随容器一起排好。
    const RECT rcArea = area->GetRect();
    area->ForceLayout(rcArea);
    area->Invalidate();
}

} // namespace balloonauth
