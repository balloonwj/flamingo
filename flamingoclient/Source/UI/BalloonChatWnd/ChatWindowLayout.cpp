/**
 *  ChatWindowLayout.cpp
 *
 *  单聊窗客户区控件树的构造实现。布局以内嵌 XML 常量为唯一真值源
 *  （随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder 解析。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用，所以自己
 *  把 balloonui 头文件所需的 ATL/WTL 前置依赖引进来。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  P1a 范围：产出"控件点名契约"完整的结构化控件树（头部按钮 / 消息区 /
 *  输入框 / 工具栏 / 发送按钮齐备、可按 id 取回、类型正确）。气泡自绘、
 *  渐变头部、图标按钮等视觉细节留待后续阶段。
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
#include <map>
#include <cstdio>       // swprintf_s：拼「已 Pin N 条」提示带文字

#include "ChatWindowLayout.h"
#include "ChatRootPanel.h"
#include "ChatBannerLabel.h"
#include "ChatHeaderInfo.h"
#include "ChatIconButton.h"
#include "ChatSendButton.h"
#include "ChatIcons.h"
#include "ChatMessageList.h"
#include "ChatInputRichEdit.h"   // 聊天输入框控件（无窗口富文本的聊天定制子类）
#include "ChatBubble.h"
#include "ChatMockData.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // 引用卡片的灰底圆角
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 引用条取消按钮设 ModeLink
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nXml.h"   // TranslateXml：翻译内嵌 XML 里的界面文字
#include "../../Common/I18n/I18nLayout.h"   // GrowFixedWidthToText：粘贴识别提示条的链接按译文加宽

namespace balloonchatwnd {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;

namespace {

// 「已 Pin N 条」提示带的配色。取暖琥珀系，与气泡上那枚图钉、消息记录窗里的
// Pin 标记同一色系，一眼能对上是同一件事；底色压得很淡，不与消息区抢注意力。
const COLORREF kPinBannerBg        = RGB(0xFF, 0xF8, 0xEC);   // 极淡的琥珀底
const COLORREF kPinBannerBorder    = RGB(0xF2, 0xE4, 0xCC);   // 下边缘分隔线
const COLORREF kPinBannerText      = RGB(0xB4, 0x6A, 0x14);   // 文字（深琥珀）
const COLORREF kPinBannerTextHover = RGB(0x8A, 0x50, 0x0C);   // 悬停时加深

// 会话内记住的"输入区块高度"（像素）。进程级、不落盘：进程重启自然复位为
// 默认高度。单聊 / 群聊共用本变量。聊天窗每次布局时据它把输入区块稳定在该
// 高度（消息区吃掉窗口高度变化）。
int s_rememberedChatInputHeightPx = kChatInputBlockDefaultPx;

// =====================================================================
// 单聊窗客户区布局（内嵌 XML）
//
// 外层 vbox 自上而下：
//   · 头部 hbox —— 左侧昵称/状态/签名占位（P1c 替换为自绘头部），右侧
//     语音 / 视频 / 记录 / 更多 四个图标按钮；
//   · 横向 splitter（id=430，weight=1）—— 把"消息区"与"输入区块"上下叠放，
//     中间一根 1px 细线落在工具栏上边缘：
//       pane0 = 消息区 hbox（内含气泡消息列表 chat-msglist）；
//       pane1 = 输入区块 vbox（工具栏 + 输入框 + 底部提示/发送行）。
//     拖这根细线即可在消息区与输入区之间重新分配高度；两侧都是 DUI 控件
//     （消息列表非 HWND 寄宿），故分隔条上下都能正常收到鼠标、可直接拖动，
//     无须改 balloonui。默认 split-fraction=0.62 让输入区块首帧 ≈212px；窗口
//     布局时由聊天窗（OnRelayout）按 GetRememberedChatInputHeightPx 把输入区
//     固定在用户选定（默认 kChatInputBlockDefaultPx）的高度，缩放时消息区吃掉
//     高度变化 —— 与改造前"输入区固定、消息区撑满"的手感一致。
//
// builder 内置标签 vbox / hbox / label / button / edit 已够用；工具栏
// 末尾的弹性占位用自定义标签 <control/> 经 CreateChatCustomControl 接入。
// 控件公共属性（id / fixedWidth / fixedHeight / weight）由 builder 的
// ApplyCommon 统一接住。
// =====================================================================
const char* const kChatWindowBodyXml = u8R"xml(
<chat-root padding="0" gap="0">
  <chat-banner id="405" fixedHeight="26" padding="14,0,14,0" text=""/>
  <hbox fixedHeight="60" padding="14,0,10,0" gap="2">
    <chat-headerinfo weight="1"/>
    <chat-iconbtn id="410" kind="phone"   fixedWidth="38"/>
    <chat-iconbtn id="411" kind="video"   fixedWidth="38"/>
    <chat-iconbtn id="412" kind="remotedesktop" fixedWidth="38"/>
    <chat-iconbtn id="417" kind="screenshare" fixedWidth="38"/>
    <chat-iconbtn id="431" kind="share"   fixedWidth="38"/>
    <chat-iconbtn id="441" kind="subtitle" fixedWidth="38"/>
    <chat-iconbtn id="414" kind="history" fixedWidth="38"/>
    <chat-iconbtn id="466" kind="lock"    fixedWidth="38"/>
    <chat-iconbtn id="413" kind="more"    fixedWidth="38"/>
  </hbox>
  <vbox id="467" fixedHeight="34" gap="0" padding="14,0,14,0">
    <hbox weight="1" gap="14">
      <label id="468" text="" weight="1"/>
      <label id="470" text="安全码" fixedWidth="52"/>
      <label id="471" text="重新邀请" fixedWidth="64"/>
      <label id="472" text="退出密聊" fixedWidth="64"/>
    </hbox>
    <label id="469" text="" fixedHeight="20"/>
  </vbox>
  <chat-banner id="415" fixedHeight="28" padding="14,0,14,0" text=""/>
  <splitter id="430" orientation="horizontal" bar-thickness="1" weight="1"
            split-fraction="0.62" min-size-0="140" min-size-1="120">
    <hbox padding="0,0,12,0" gap="0">
      <chat-msglist id="400" weight="1"/>
    </hbox>
    <vbox padding="12,8,12,12" gap="6">
      <hbox fixedHeight="30" gap="2">
        <chat-iconbtn id="420" kind="emoji" fixedWidth="32"/>
        <chat-iconbtn id="421" kind="image" fixedWidth="32"/>
        <chat-iconbtn id="422" kind="file"  fixedWidth="32"/>
        <chat-iconbtn id="428" kind="folder" fixedWidth="32"/>
        <chat-iconbtn id="423" kind="cut"   fixedWidth="32"/>
        <chat-iconbtn id="442" kind="caret" fixedWidth="14"/>
        <chat-iconbtn id="429" kind="record" fixedWidth="32"/>
        <chat-iconbtn id="424" kind="mic"   fixedWidth="32"/>
        <chat-iconbtn id="427" kind="font"  fixedWidth="32"/>
        <chat-iconbtn id="443" kind="markdown" fixedWidth="32"/>
        <chat-iconbtn id="448" kind="code" fixedWidth="32"/>
        <chat-iconbtn id="426" kind="shake" fixedWidth="32"/>
        <chat-iconbtn id="425" kind="plus"  fixedWidth="32"/>
        <control weight="1"/>
      </hbox>
      <vbox id="406" fixedHeight="48" gap="0" padding="10,5,6,5">
        <hbox weight="1" gap="6">
          <vbox weight="1" gap="2">
            <label id="407" text="" fixedHeight="17"/>
            <label id="408" text="" fixedHeight="17"/>
          </vbox>
          <label id="409" text="⊗" fixedWidth="22"/>
        </hbox>
      </vbox>
      <vbox id="462" fixedHeight="30" gap="0" padding="10,0,10,0">
        <hbox weight="1" gap="12">
          <label id="463" text="看起来是一段代码" weight="1"/>
          <label id="464" text="以代码片段发送" fixedWidth="110"/>
          <label id="465" text="不用了" fixedWidth="50"/>
        </hbox>
      </vbox>
      <chat-input id="401" placeholder="输入消息，Enter 发送，Ctrl+Enter 换行" multi-line="true" word-wrap="true" weight="1"/>
      <hbox id="432" weight="1"/>
      <hbox id="435" fixedHeight="34" gap="8">
        <label id="404" text="提示：按 Ctrl+Enter 换行" weight="1"/>
        <chat-sendbtn id="402" fixedWidth="104"/>
      </hbox>
    </vbox>
  </splitter>
</chat-root>
)xml";

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// 把 <chat-iconbtn> 的 kind 属性翻译成 ChatIconKind。
int ChatIconKindFromAttr(const std::string& kind)
{
    if (kind == "phone")   { return kChatIconPhone; }
    if (kind == "video")   { return kChatIconVideo; }
    if (kind == "remotedesktop") { return kChatIconRemoteDesktop; }
    if (kind == "screenshare") { return kChatIconScreenShare; }   // 共享屏幕（头部 id=417，紧挨在远程桌面右边）
    if (kind == "more")    { return kChatIconMore; }
    if (kind == "emoji")   { return kChatIconEmoji; }
    if (kind == "image")   { return kChatIconImage; }
    if (kind == "file")    { return kChatIconFile; }
    if (kind == "folder")  { return kChatIconFolder; }   // 发送文件夹（工具栏 id=428，排在文件按钮之后）
    if (kind == "share")   { return kChatIconShare; }    // 文件共享（头部 id=431，紧挨在共享屏幕右边）
    if (kind == "cut")     { return kChatIconCut; }
    if (kind == "record")  { return kChatIconRecord; }   // 屏幕录制（工具栏 id=429，紧挨在截图右边）
    // 截图选项下拉按钮（工具栏 id=442，紧挨在截图右边）
    if (kind == "caret")
    {
        return kChatIconCaret;
    }
    if (kind == "mic")     { return kChatIconMic; }
    if (kind == "plus")    { return kChatIconPlus; }
    if (kind == "shake")   { return kChatIconShake; }
    if (kind == "font")    { return kChatIconFont; }
    // Markdown 编辑器按钮（工具栏 id=443，紧挨在字体设置右边，点了打开独立的 Markdown 编辑器；docs/Markdown消息方案.md 第 11.3 节）
    if (kind == "markdown")
    {
        return kChatIconMarkdown;
    }
    // 发送代码（工具栏 id=448，紧挨在 Markdown 开关右边；docs/代码片段方案.md 第 3.1 节）
    if (kind == "code")
    {
        return kChatIconCode;
    }
    if (kind == "history") { return kChatIconHistory; }   // 与群聊一致：消息记录用时钟图标（此前缺本分支被画成“更多”⋯）
    if (kind == "subtitle") { return kChatIconSubtitle; } // 字幕模式（头部 id=441，紧挨在消息记录左边）
    // 密聊（头部 id=466，紧挨在「更多」左边；docs/密聊方案.md 6.2）
    if (kind == "lock")
    {
        return kChatIconLock;
    }
    if (kind == "clear")   { return kChatIconClear; }     // 清空聊天记录（扫帚）
    return kChatIconMore;
}

// 自定义标签工厂 —— 接 <chat-headerinfo>（头部信息块）、<chat-iconbtn>
// （图标按钮）、<control>（不可见 spacer）。其它标签走 builder 内置处理；
// 未知标签返回 nullptr 由 builder 跳过。
// 把 UTF-8 的属性值转成宽字符串。内嵌 XML 是 UTF-8 的，而控件接口收宽字符。
// 群聊那份布局里有一份同名函数 —— 两个文件各持一份、互不可见，与本文件里
// 其它小helper 的处置方式一致。
//   s：UTF-8 字符串；空串直接返回空。
//   返回：转换后的宽字符串；转换失败时返回空串。
CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }
    const int nWideLen = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (nWideLen <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.GetBuffer(nWideLen), nWideLen);
    out.ReleaseBuffer();
    return out;
}

std::unique_ptr<DuiControl> CreateChatCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "chat-root")
    {
        return std::unique_ptr<DuiControl>(new ChatRootPanel());
    }

    if (n.tag == "chat-banner")
    {
        // 顶部状态提示带（离线 / 正在连接）。底色与文字色由 ApplyConnectionState
        // 按当前连接态设置，这里只造控件。
        return std::unique_ptr<DuiControl>(new ChatBannerLabel());
    }

    if (n.tag == "chat-headerinfo")
    {
        std::unique_ptr<ChatHeaderInfo> info(new ChatHeaderInfo());
        info->SetCtrlId(kIdHeaderInfo);   // 命名，便于点击后路由打开对方资料窗
        return std::unique_ptr<DuiControl>(info.release());
    }

    if (n.tag == "chat-iconbtn")
    {
        std::unique_ptr<ChatIconButton> btn(new ChatIconButton());
        btn->SetIconKind(ChatIconKindFromAttr(GetNodeAttr(n, "kind")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "chat-msglist")
    {
        return std::unique_ptr<DuiControl>(new ChatMessageList());
    }

    if (n.tag == "chat-input")
    {
        // 聊天输入框。用自定义标签而不是内置的 <richtext>，是因为这里要的是
        // 带聊天定制的子类 —— 回车发送、正在输入通知、焦点区域上报、群聊的
        // @ 提醒转发都在它里面（见 ChatInputRichEdit.h）。内置标签只会建出
        // 基类，那些定制就落不下来。
        //
        // **属性要在这里自己解析。** builder 只对自定义标签统一处理编号与布局
        // 提示（id / weight / fixedWidth 等），placeholder 这类是各标签各自解析
        // 的，走自定义工厂之后就没有别人代劳了。漏解析不会报错 —— 只是那条
        // 属性静默失效（占位文字不见了）。
        std::unique_ptr<ChatInputRichEdit> edit(new ChatInputRichEdit());
        const std::string strPlaceholder = GetNodeAttr(n, "placeholder");
        if (!strPlaceholder.empty())
        {
            edit->SetPlaceholder(Utf8ToWide(strPlaceholder));
        }
        edit->SetMultiLine(GetNodeAttr(n, "multi-line") != "false");
        edit->SetWordWrap(GetNodeAttr(n, "word-wrap") != "false");
        return std::unique_ptr<DuiControl>(edit.release());
    }

    if (n.tag == "chat-sendbtn")
    {
        // 橙色「发送」按钮（文字由控件构造时默认设为"发送"）。
        return std::unique_ptr<DuiControl>(new ChatSendButton());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }
    return nullptr;
}

// 把 mock 会话消息（ChatMessages）逐条建成 ChatBubble，挂进消息列表，
// 再把列表滚到底部（显示最新消息）。
void PopulateMessageList(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    ChatMessageList* list =
        dynamic_cast<ChatMessageList*>(root->FindCtrlById(kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    const std::vector<ChatMessage>& msgs = ChatMessages();
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        std::unique_ptr<ChatBubble> bubble(new ChatBubble());
        bubble->SetMessage(msgs[i]);
        list->AddChild(std::unique_ptr<DuiControl>(bubble.release()));
    }
    list->ScrollToBottom();
}

} // anonymous namespace

int GetRememberedChatInputHeightPx()
{
    return s_rememberedChatInputHeightPx;
}

void RememberChatInputHeightPx(int px)
{
    s_rememberedChatInputHeightPx = (px > 0) ? px : kChatInputBlockDefaultPx;
}

std::wstring MakePinBannerText(int count)
{
    if (count <= 0)
    {
        return std::wstring();
    }

    // 末尾那个"›"是"点进去还有内容"的通用暗示（与主面板各处的展开箭头同一符号），
    // 配合 ModeLink 的下划线一起告诉用户这条是可以点的。
    wchar_t buf[64] = { 0 };
    ::swprintf_s(buf, TR(L"已 Pin %d 条 ›"), count);
    return std::wstring(buf);
}

void ApplyPinBannerStyle(DuiControl* banner)
{
    // 设成 ModeLink 才能点 —— DuiLabel 的 ModeText 是纯绘制控件，不发通知也不变
    // 光标，点了没反应（与引用条上的取消按钮 kIdReplyClose 同一处理）。
    ChatBannerLabel* label = dynamic_cast<ChatBannerLabel*>(banner);
    if (label == NULL)
    {
        return;
    }

    label->SetVisible(false);
    label->SetBannerColors(kPinBannerBg, kPinBannerBorder);
    label->SetMode(balloonwjui::DuiLabel::ModeLink);
    label->SetAutoNavigate(false);   // 不打开浏览器，点击交给窗口自己处理
    label->SetLinkColor(kPinBannerText);
    label->SetHoverColor(kPinBannerTextHover);
    // 访问过之后仍用同一颜色：这条不是"链接看过就变灰"的语义，它每次都指向
    // 同一个地方（消息记录窗的 Pin 分类），变色只会让人以为状态变了。
    label->SetVisitedColor(kPinBannerText);
}

// 粘贴识别提示条的配色与尺寸（docs/代码片段方案.md 第 3.1 节）
static const COLORREF kCodePasteBarBg   = RGB(0xEA, 0xF2, 0xFD);   // 浅蓝底，与灰底的引用条区分开
static const COLORREF kCodePasteBarText = RGB(0x55, 0x55, 0x55);   // 说明文字
static const int      kCodePasteBarRadius = 6;                     // 圆角半径（像素），与引用条相同
static const int      kCodePasteLinkPadPx = 8;                     // 链接文字左右留白合计（像素），译文加宽时计入

void ApplyCodePasteBarStyle(DuiControl* root, UINT barId, UINT textId, UINT sendId, UINT dismissId)
{
    if (root == NULL)
    {
        return;
    }
    // XML 没有 visible 属性，只能建好之后收起来；只在往空输入框里粘贴了像代码的文字时才出现
    // 底色与圆角只有 DuiVBox 支持：提示条与引用条一样是 vbox 套一行 hbox
    balloonwjui::DuiVBox* bar = dynamic_cast<balloonwjui::DuiVBox*>(root->FindCtrlById(barId));
    if (bar != NULL)
    {
        bar->SetVisible(false);
        bar->SetBgColor(kCodePasteBarBg);
        bar->SetCornerRadius(kCodePasteBarRadius);
    }
    balloonwjui::DuiLabel* text = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(textId));
    if (text != NULL)
    {
        text->SetTextColor(kCodePasteBarText);
        text->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    // 两个链接：ModeLink 才会悬停变手型、点击发 DUIN_CLICK（与引用条上的取消按钮同一处理）
    const UINT links[] = { sendId, dismissId };
    for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); ++i)
    {
        balloonwjui::DuiLabel* link = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(links[i]));
        if (link == NULL)
        {
            continue;
        }
        link->SetMode(balloonwjui::DuiLabel::ModeLink);
        link->SetAutoNavigate(false);   // 不打开浏览器，点击交给窗口处理
        link->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 译文比中文长时按文字宽度加宽（简体中文界面不变）
        const CString linkText = link->GetText();
        i18n::GrowFixedWidthToText(link, (LPCWSTR)linkText, i18n::DefaultFontPixelHeight(), false,
                                   kCodePasteLinkPadPx);
    }
}

// 密聊头部条的配色（docs/密聊方案.md 4.7：窗口顶部出现深色的密聊头部条）
static const COLORREF kSecretBannerBg       = RGB(0x2B, 0x2F, 0x36);   // 深灰底，与白色的聊天区明显区分
static const COLORREF kSecretBannerText     = RGB(0xF2, 0xF2, 0xF2);   // 状态文字
static const COLORREF kSecretBannerWarnText = RGB(0xFF, 0xB0, 0x3B);   // 防截图提示：醒目的橙色
static const COLORREF kSecretLinkText       = RGB(0x8C, 0xC8, 0xFF);   // 链接文字：深底上的浅蓝
static const COLORREF kSecretLinkHover      = RGB(0xFF, 0xFF, 0xFF);   // 链接悬停时变白
static const int      kSecretLinkPadPx      = 8;                       // 链接文字左右留白合计（像素），译文加宽时计入

void ApplySecretBannerStyle(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    // XML 没有 visible 属性，只能建好之后收起来；底色只有 DuiVBox 支持
    balloonwjui::DuiVBox* banner = dynamic_cast<balloonwjui::DuiVBox*>(root->FindCtrlById(kIdSecretBanner));
    if (banner != NULL)
    {
        banner->SetVisible(false);
        banner->SetBgColor(kSecretBannerBg);
    }
    DuiControl* secretBtn = root->FindCtrlById(kIdHdrSecret);
    if (secretBtn != NULL)
    {
        secretBtn->SetVisible(false);
    }
    balloonwjui::DuiLabel* text = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdSecretBannerText));
    if (text != NULL)
    {
        text->SetTextColor(kSecretBannerText);
        text->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    balloonwjui::DuiLabel* warn = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdSecretBannerWarn));
    if (warn != NULL)
    {
        warn->SetVisible(false);
        warn->SetTextColor(kSecretBannerWarnText);
        warn->SetTextAlign(DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    // 三个链接：ModeLink 才会悬停变手型、点击发 DUIN_CLICK
    const UINT links[] = { kIdSecretSafety, kIdSecretReinvite, kIdSecretExit };
    for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); ++i)
    {
        balloonwjui::DuiLabel* link = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(links[i]));
        if (link == NULL)
        {
            continue;
        }
        link->SetMode(balloonwjui::DuiLabel::ModeLink);
        link->SetAutoNavigate(false);   // 不打开浏览器，点击交给窗口处理
        link->SetLinkColor(kSecretLinkText);
        link->SetHoverColor(kSecretLinkHover);
        link->SetVisitedColor(kSecretLinkText);
        link->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 译文比中文长时按文字宽度加宽（简体中文界面不变）
        const CString linkText = link->GetText();
        i18n::GrowFixedWidthToText(link, (LPCWSTR)linkText, i18n::DefaultFontPixelHeight(), false, kSecretLinkPadPx);
    }
}

bool UpdatePinBanner(DuiControl* root, UINT bannerId, int count)
{
    if (root == NULL)
    {
        return false;
    }

    ChatBannerLabel* label =
        dynamic_cast<ChatBannerLabel*>(root->FindCtrlById(bannerId));
    if (label == NULL)
    {
        return false;
    }

    const bool wasVisible = label->IsVisible();
    const std::wstring text = MakePinBannerText(count);
    if (text.empty())
    {
        label->SetVisible(false);
    }
    else
    {
        label->SetText(text.c_str());
        label->SetVisible(true);
    }

    return (label->IsVisible() != wasVisible);
}

std::unique_ptr<DuiControl> BuildChatWindowBody(bool loadMockMessages)
{
    // 内嵌 XML 里的界面文字先经 TranslateXml 换成当前语言的译文；FromString 在调用内解析完毕，临时串的生命周期足够
    std::unique_ptr<DuiControl> root =
        DuiXmlBuilder::FromString(i18n::TranslateXml(kChatWindowBodyXml).c_str(), &CreateChatCustomControl);

    // 顶部离线提示条建好就先收起来。它的契约是"平时不占位，离线 / 正在连接时才显示"
    // （见 ChatWindowLayout.h 的 kIdOfflineBanner 注释），但 DuiXmlBuilder 没有
    // visible 属性可声明初始不可见，而 ApplyConnectionState 只在连接态<u>变化</u>时
    // 才会被调 —— 窗口刚建好、且此刻本来就在线的话没有谁来收它，它就以"空文字 +
    // 固定 26px"的形态一直占着标题栏下面那一条，看起来是凭空多出一片空白。
    if (root)
    {
        DuiControl* banner = root->FindCtrlById(kIdOfflineBanner);
        if (banner != NULL)
        {
            banner->SetVisible(false);
        }

        // 「已 Pin N 条」提示带同理：XML 没有 visible 属性，只能建好之后收起来。
        // 它只在本会话确有被 Pin 的消息时才出现（见 UpdatePinBanner）。
        ApplyPinBannerStyle(root->FindCtrlById(kIdPinBanner));

        // 密聊头部条与头部「密聊」按钮：建好之后先收起来，进入密聊状态、入口可用时才显示（docs/密聊方案.md 4.7）
        ApplySecretBannerStyle(root.get());

        // 粘贴识别提示条：样式与群聊窗共用一份实现，初始隐藏（docs/代码片段方案.md 第 3.1 节）
        ApplyCodePasteBarStyle(root.get(), kIdCodePasteBar, kIdCodePasteText, kIdCodePasteSend,
                               kIdCodePasteDismiss);

        // 引用条同理：XML 没有 visible 属性，只能建好之后收起来。它只在用户点了
        // 气泡右键「引用回复」时才出现，不收的话输入框上方会一直杵着一条空白。
        // 与群聊窗 BuildGroupChatWindowBody 同一处理。
        balloonwjui::DuiVBox* replyBar = dynamic_cast<balloonwjui::DuiVBox*>(root->FindCtrlById(kIdReplyBar));
        if (replyBar != NULL)
        {
            replyBar->SetVisible(false);
            // 灰底圆角卡片。DuiLayout 本身就支持底色与圆角，不必自绘一个控件
            // （开发约定文档：新增控件优先直接用 balloonui 现成能力）。
            replyBar->SetBgColor(RGB(0xF0, 0xF0, 0xF0));
            replyBar->SetCornerRadius(6);
        }

        // 录音条占位同理：只在录音时代替输入框显示（语音消息第 3b 步，见 VoiceRecordBar.h）
        DuiControl* voiceBar = root->FindCtrlById(kIdVoiceBar);
        if (voiceBar != NULL)
        {
            voiceBar->SetVisible(false);
        }

        // 取消按钮做成链接样式：DuiLabel 的 ModeText 是纯绘制控件、不发通知也不变
        // 光标，点了没反应；只有 ModeLink 才会悬停变手型、点击发 DUIN_CLICK。
        balloonwjui::DuiLabel* replyClose =
            dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdReplyClose));
        if (replyClose != NULL)
        {
            replyClose->SetMode(balloonwjui::DuiLabel::ModeLink);
            replyClose->SetAutoNavigate(false);   // 不打开浏览器，点击交给本窗处理
        }
    }

    // 注意：记住的输入区块高度不在这里套用 —— 它依赖 splitter 的实际高度，须等
    // 窗口布局后才知道。聊天窗在每次布局（CBalloonChatWindow::OnRelayout，初次
    // 显示与缩放都经过）里据 GetRememberedChatInputHeightPx 把输入区固定。构建期
    // 仅保留 XML 的 split-fraction 作为首帧默认外观。

    // 真实会话起始为空；仅调试/视觉核对时用 mock 会话消息预填消息列表。
    if (root && loadMockMessages)
    {
        PopulateMessageList(root.get());
    }
    return root;
}

} // namespace balloonchatwnd
