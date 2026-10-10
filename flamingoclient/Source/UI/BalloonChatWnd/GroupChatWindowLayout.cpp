/**
 *  GroupChatWindowLayout.cpp
 *
 *  群聊窗客户区控件树的构造实现。布局以内嵌 XML 常量为唯一真值源，交给
 *  balloonui 的 DuiXmlBuilder 解析。大量复用单聊窗的 Chat* 自绘控件。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  G-P1a 范围：产出"控件点名契约"完整的结构化控件树（头部按钮 / 消息区 /
 *  输入框 / 工具栏 / 发送按钮 / 边栏 Tab 与成员列表齐备、可按 id 取回、
 *  类型正确）。群头部自绘、带发送者名的气泡、成员列表内容等留待 G-P1b。
 *
 *  balloonwj@qq.com   2026-05-22
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

#include "GroupChatWindowLayout.h"
#include "ChatWindowLayout.h"     // Pin 提示带的样式 / 文案与单聊窗共用
#include "ChatInputRichEdit.h"   // 聊天输入框控件（与单聊窗共用）
#include "GroupChatHeaderInfo.h"
#include "GroupSideTab.h"
#include "../BalloonMemo/MemoCardList.h"   // 「备忘」面板的备忘卡片（memo-cardscroll）
#include "GroupMemberItem.h"
#include "../../Session/GroupRoleRules.h"   // NetGroupRole：成员行的角色取值
#include "../../net/Protocol.h"            // presence_type：调试入口示例成员的在线状态
#include "GroupMemberList.h"
#include "GroupFileItem.h"
#include "GroupFileList.h"
#include "GroupMentionButton.h"
#include "ChatRootPanel.h"
#include "ChatBannerLabel.h"
#include "ChatIconButton.h"
#include "ChatSendButton.h"
#include "ChatIcons.h"
#include "ChatMessageList.h"
#include "ChatBubble.h"
#include "ChatMockData.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // 引用卡片的灰底圆角
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView（文件列表滚动视口）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"      // 分类筛选 chips 变体设置
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"       // 分页信息文字
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/I18n/I18nXml.h"   // TranslateXml：翻译内嵌 XML 里的界面文字
#include "GroupNotifyOnly.h"             // 仅通知模式下代替输入区的提示文字（docs/群管理接入方案.md §14）

namespace balloonchatwnd {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
//引用条的取消按钮要按 DuiLabel::ModeLink 设成链接样式，故把它也引进来
using balloonwjui::DuiLabel;

namespace {

//屏幕共享横幅的配色：极淡的绿底（与共享边框的绿色呼应，表示「正在共享」），文字为深绿，悬停时加深
const COLORREF kShareBannerBg        = RGB(0xEC, 0xF8, 0xF0);
const COLORREF kShareBannerBorder    = RGB(0xCC, 0xEA, 0xD6);
const COLORREF kShareBannerText      = RGB(0x1E, 0x8C, 0x46);
const COLORREF kShareBannerTextHover = RGB(0x14, 0x6E, 0x34);

//仅通知模式下代替输入区的提示文字颜色：与输入区左下角「提示：按 Ctrl+Enter 换行」同一档灰
const COLORREF kNotifyOnlyBarTextColor = RGB(0x99, 0x99, 0x99);

//把屏幕共享横幅调成链接样式、初始隐藏（XML 没有 visible 属性）
void ApplyScreenShareBannerStyle(DuiControl* banner)
{
    ChatBannerLabel* label = dynamic_cast<ChatBannerLabel*>(banner);
    if (label == NULL)
    {
        return;
    }
    label->SetVisible(false);
    label->SetBannerColors(kShareBannerBg, kShareBannerBorder);
    //ModeLink 才会在悬停时变手型、点击时发 DUIN_CLICK；不打开浏览器，点击交给窗口自己处理
    label->SetMode(DuiLabel::ModeLink);
    label->SetAutoNavigate(false);
    label->SetLinkColor(kShareBannerText);
    label->SetHoverColor(kShareBannerTextHover);
    label->SetVisitedColor(kShareBannerText);
}

} // namespace

namespace {

// =====================================================================
// 群聊窗客户区布局（内嵌 XML）
//
// 外层 chat-root（白底）自上而下：
//   · 头部 hbox —— 左侧群名/人数/公告占位（G-P1b 替换为自绘群头部），
//     右侧语音 / 视频 / 共享屏幕 / 文件共享 / 发起接龙 / 记录 / 群管理 / 更多八个图标按钮；
//   · 主体 hbox（weight=1）——
//       左列 vbox：一根横向 splitter（id=540）把"消息区(chat-msglist)"与
//         "输入区块"上下叠放，中间 1px 细线落在工具栏上边缘 —— 拖它即可在
//         消息区与输入区之间重分高度（两侧都是 DUI 控件，不碰 HWND 死区，
//         无须改 balloonui）；
//       右栏 vbox（fixedWidth=240）：Tab 行 + 成员搜索框 + 成员列表容器。
//     输入区块高度的会话内记忆与单聊共用、由群聊窗在布局时套用（见
//     ChatWindowLayout 的 GetRememberedChatInputHeightPx /
//     RememberChatInputHeightPx 与 CBalloonGroupChatWindow::OnRelayout）。
//
// 复用单聊窗自定义标签：chat-root / chat-iconbtn / chat-sendbtn /
// chat-msglist / control，由 CreateGroupCustomControl 接入。
// =====================================================================
const char* const kGroupChatWindowBodyXml = u8R"xml(
<chat-root padding="0" gap="0">
  <chat-banner id="505" fixedHeight="26" padding="14,0,14,0" text=""/>
  <hbox fixedHeight="60" padding="14,0,10,0" gap="2">
    <group-headerinfo id="580" weight="1"/>
    <chat-iconbtn id="510" kind="phone"    fixedWidth="38"/>
    <chat-iconbtn id="511" kind="video"    fixedWidth="38"/>
    <chat-iconbtn id="517" kind="screenshare" fixedWidth="38"/>
    <chat-iconbtn id="529" kind="share"    fixedWidth="38"/>
    <chat-iconbtn id="527" kind="relay"    fixedWidth="38"/>
    <chat-iconbtn id="536" kind="subtitle" fixedWidth="38"/>
    <chat-iconbtn id="512" kind="history"  fixedWidth="38"/>
    <chat-iconbtn id="513" kind="settings" fixedWidth="38"/>
    <chat-iconbtn id="514" kind="more"     fixedWidth="38"/>
  </hbox>
  <chat-banner id="519" fixedHeight="28" padding="14,0,14,0" text=""/>
  <chat-banner id="518" fixedHeight="28" padding="14,0,14,0" text=""/>
  <chat-banner id="515" fixedHeight="28" padding="14,0,14,0" text=""/>
  <hbox weight="1" padding="0" gap="0">
    <vbox weight="1" padding="0" gap="0">
      <splitter id="540" orientation="horizontal" bar-thickness="1" weight="1"
                split-fraction="0.62" min-size-0="140" min-size-1="120">
        <hbox padding="0,0,12,0" gap="0">
          <chat-msglist id="500" weight="1"/>
        </hbox>
        <vbox padding="12,8,12,12" gap="6">
          <hbox id="568" fixedHeight="30" gap="2">
            <chat-iconbtn id="520" kind="emoji" fixedWidth="32"/>
            <chat-iconbtn id="521" kind="image" fixedWidth="32"/>
            <chat-iconbtn id="522" kind="file"  fixedWidth="32"/>
            <chat-iconbtn id="523" kind="cut"   fixedWidth="32"/>
            <chat-iconbtn id="537" kind="caret" fixedWidth="14"/>
            <chat-iconbtn id="528" kind="record" fixedWidth="32"/>
            <chat-iconbtn id="524" kind="mic"   fixedWidth="32"/>
            <chat-iconbtn id="526" kind="font"  fixedWidth="32"/>
            <chat-iconbtn id="538" kind="markdown" fixedWidth="32"/>
            <chat-iconbtn id="567" kind="code" fixedWidth="32"/>
            <chat-iconbtn id="525" kind="plus"  fixedWidth="32"/>
            <control weight="1"/>
            <group-mention id="503" fixedWidth="64"/>
          </hbox>
          <vbox id="506" fixedHeight="48" gap="0" padding="10,5,6,5">
            <hbox weight="1" gap="6">
              <vbox weight="1" gap="2">
                <label id="507" text="" fixedHeight="17"/>
                <label id="509" text="" fixedHeight="17"/>
              </vbox>
              <label id="508" text="⊗" fixedWidth="22"/>
            </hbox>
          </vbox>
          <vbox id="595" fixedHeight="30" gap="0" padding="10,0,10,0">
            <hbox weight="1" gap="12">
              <label id="596" text="看起来是一段代码" weight="1"/>
              <label id="597" text="以代码片段发送" fixedWidth="110"/>
              <label id="598" text="不用了" fixedWidth="50"/>
            </hbox>
          </vbox>
          <chat-input id="501" placeholder="输入消息，Enter 发送，Ctrl+Enter 换行" multi-line="true" word-wrap="true" weight="1"/>
          <hbox id="541" weight="1"/>
          <label id="569" text="" weight="1"/>
          <hbox id="544" fixedHeight="34" gap="8">
            <label id="504" text="提示：按 Ctrl+Enter 换行" weight="1"/>
            <chat-sendbtn id="502" fixedWidth="104"/>
          </hbox>
        </vbox>
      </splitter>
    </vbox>
    <vbox fixedWidth="240" padding="0" gap="0">
      <hbox fixedHeight="38" padding="0" gap="0">
        <!-- 成员 / 群文件两个 Tab 的文案带真实数量（"成员 12" / "群文件 8"），由
             CBalloonGroupChatWindow::RefreshSideTabCounts 在成员到达、文件库拉全
             或变更推送后刷新。这里只给光秃秃的标题作"数量尚未取到"的起始态。 -->
        <group-sidetab id="530" text="成员"   active="true" weight="1"/>
        <group-sidetab id="531" text="群文件" weight="1"/>
        <group-sidetab id="532" text="公告"   weight="1"/>
        <group-sidetab id="535" text="备忘"   weight="1"/>
        <group-sidetab id="581" text="问答"   weight="1"/>
      </hbox>
      <!-- 成员内容容器（成员 Tab 选中时显示） -->
      <vbox id="550" weight="1" padding="0" gap="0">
        <!-- 搜索框 + 「按部门 / 平铺」切换按钮（docs/群成员部门树方案.md）：组织架构图标，部门树视图下高亮，
             高亮状态与悬停提示由 ApplyMemberViewToggle 按当前视图设置（2026-10-07 起取代原来的文字链接） -->
        <hbox fixedHeight="30" padding="0" gap="0">
          <edit id="534" placeholder="搜索成员" weight="1"/>
          <chat-iconbtn id="539" kind="orgtree" fixedWidth="32"/>
        </hbox>
        <!-- 机器人区域（docs/机器人方案.md §4.2）：标题「机器人 N」，下面每个机器人一行；起始隐藏，2801 到达且本群有机器人时
             由 RefreshBotMemberSection 显示并填行。添加与管理群机器人的入口 2026-10-08 起在群管理窗口的「群机器人」页 -->
        <vbox id="620" padding="10,6,10,6" gap="2" fixedHeight="0">
          <hbox fixedHeight="20" padding="0" gap="0">
            <label id="621" text="机器人" weight="1"/>
          </hbox>
          <vbox id="623" padding="0" gap="0"/>
        </vbox>
        <group-memberlist id="533" weight="1"/>
      </vbox>
      <!-- 群文件内容容器（群文件 Tab 选中时显示；起始隐藏，由代码 SetVisible(false)）。
           对照 design/群文件.png：搜索框 + 分类筛选 chips + 文件列表滚动视口 +
           分页栏 + 上传按钮。 -->
      <vbox id="551" weight="1" padding="12,10,12,10" gap="8">
        <edit id="552" placeholder="搜索文件名" fixedHeight="32"/>
        <hbox fixedHeight="26" gap="5">
          <button id="553" text="全部" buttonType="push" weight="1"/>
          <button id="554" text="PDF"  buttonType="push" weight="1"/>
          <button id="555" text="文档" buttonType="push" weight="1"/>
          <button id="556" text="表格" buttonType="push" weight="1"/>
          <button id="557" text="设计" buttonType="push" weight="1"/>
        </hbox>
        <group-filescroll id="558" weight="1"/>
        <hbox fixedHeight="24" gap="6">
          <label  id="560" text="共 0 · 第 1/1 页" weight="1"/>
          <button id="561" text="‹" buttonType="push" fixedWidth="28"/>
          <button id="562" text="›" buttonType="push" fixedWidth="28"/>
        </hbox>
        <button id="563" text="上传文件" buttonType="push" fixedHeight="34"/>
      </vbox>
      <!-- 公告内容容器（公告 Tab 选中时显示；起始隐藏，由代码 SetVisible(false)）。
           对照 design/群公告.png：顶部「群公告 · N 条」+「‹ x/y ›」翻页，中间一张
           公告卡（标题 / 正文 / 分隔线 / 发布人 + 时间）。这里只读 —— 发布与撤回
           在群管理窗的「群公告」页，普通成员本来也进不去那里。 -->
      <vbox id="570" weight="1" padding="12,10,12,10" gap="8">
        <hbox fixedHeight="22" gap="4">
          <label  id="571" text="群公告" weight="1"/>
          <button id="572" text="‹" buttonType="push" fixedWidth="24"/>
          <label  id="573" text="1/1" fixedWidth="40"/>
          <button id="574" text="›" buttonType="push" fixedWidth="24"/>
        </hbox>
        <!-- 公告卡。圆角浅底与字号 / 换行由代码配置（DuiVBox 与 DuiLabel 都有现成
             的 setter，XML builder 的通用属性只认 id 与 padding / gap，故不在这里写）。 -->
        <vbox id="575" weight="1" padding="14,14,14,12" gap="8">
          <label id="576" text="" fixedHeight="24"/>
          <label id="577" text="" weight="1"/>
          <separator fixedHeight="1"/>
          <label id="578" text="" fixedHeight="20"/>
        </vbox>
        <label id="579" text="本群还没有群公告" fixedHeight="40"/>
      </vbox>
      <!-- 备忘内容容器（备忘 Tab 选中时显示；起始隐藏，由代码 SetVisible(false)）。
           空状态提示（没有备忘时才显示，在最上方）、备忘卡片、底部「＋ 新建群备忘」（只有群主、管理员可见）。
           条数写在页签标题「备忘 N」上，面板里不再重复。 -->
      <vbox id="590" weight="1" padding="12,10,12,10" gap="8">
        <label id="593" text="本群还没有群备忘" fixedHeight="40"/>
        <memo-cardscroll id="592" weight="1"/>
        <button id="594" text="＋ 新建群备忘" buttonType="push" fixedHeight="34"/>
      </vbox>
      <!-- 问答内容容器（问答 Tab 选中时显示；起始隐藏，由代码 SetVisible(false)）。
           搜索框、空状态提示、问答卡片、底部「＋ 新建问答」（只有群主、管理员可见），docs/群问答方案.md。 -->
      <vbox id="600" weight="1" padding="12,10,12,10" gap="8">
        <edit id="601" placeholder="搜索问题或答案" fixedHeight="32"/>
        <label id="602" text="正在加载问答…" fixedHeight="40"/>
        <memo-cardscroll id="603" weight="1"/>
        <button id="604" text="＋ 新建问答" buttonType="push" fixedHeight="34"/>
      </vbox>
    </vbox>
  </hbox>
</chat-root>
)xml";

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// UTF-8 字符串转 CString（宽字符）—— XML 属性值是 UTF-8，控件 SetText
// 要宽字符。
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

// 把 <chat-iconbtn> 的 kind 属性翻译成 ChatIconKind。
int GroupIconKindFromAttr(const std::string& kind)
{
    if (kind == "phone")    { return kChatIconPhone; }
    if (kind == "video")    { return kChatIconVideo; }
    if (kind == "screenshare") { return kChatIconScreenShare; }   // 共享屏幕（头部 id=517，紧挨在视频通话右边）
    if (kind == "history")  { return kChatIconHistory; }
    if (kind == "subtitle") { return kChatIconSubtitle; }   // 字幕模式（头部 id=536，紧挨在消息记录左边）
    if (kind == "settings") { return kChatIconSettings; }
    if (kind == "clear")    { return kChatIconClear; }   // 清空聊天记录（扫帚）
    if (kind == "more")     { return kChatIconMore; }
    if (kind == "emoji")    { return kChatIconEmoji; }
    if (kind == "image")    { return kChatIconImage; }
    if (kind == "file")     { return kChatIconFile; }
    if (kind == "cut")      { return kChatIconCut; }
    if (kind == "record")   { return kChatIconRecord; }   // 屏幕录制（工具栏 id=528，紧挨在截图右边）
    // 截图选项下拉按钮（工具栏 id=537，紧挨在截图右边）
    if (kind == "caret")
    {
        return kChatIconCaret;
    }
    if (kind == "mic")      { return kChatIconMic; }
    if (kind == "plus")     { return kChatIconPlus; }
    if (kind == "font")     { return kChatIconFont; }
    // Markdown 编辑器按钮（工具栏 id=538，紧挨在字体设置右边，点了打开独立的 Markdown 编辑器；docs/Markdown消息方案.md 第 11.3 节）
    if (kind == "markdown")
    {
        return kChatIconMarkdown;
    }
    // 发送代码（工具栏 id=567，紧挨在 Markdown 开关右边；docs/代码片段方案.md 第 3.1 节）
    if (kind == "code")
    {
        return kChatIconCode;
    }
    if (kind == "relay")    { return kChatIconRelay; }   // 发起接龙（写字板）
    if (kind == "share")    { return kChatIconShare; }   // 文件共享（头部 id=529，紧挨在共享屏幕右边）
    // 成员搜索框右边的「按部门 / 平铺」切换按钮（id=539，组织架构图标）
    if (kind == "orgtree")
    {
        return kChatIconOrgTree;
    }
    return kChatIconMore;
}

// 自定义标签工厂 —— 复用单聊窗的 chat-root / chat-iconbtn / chat-sendbtn /
// chat-msglist 控件，外加 <control>（不可见 spacer）。未知标签返回
// nullptr 由 builder 跳过（走内置 vbox/hbox/label/button/edit 处理）。
std::unique_ptr<DuiControl> CreateGroupCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "chat-root")
    {
        return std::unique_ptr<DuiControl>(new ChatRootPanel());
    }

    if (n.tag == "chat-banner")
    {
        // 顶部状态提示带（离线 / 正在连接），与单聊窗共用同一个控件类。底色与
        // 文字色由 ApplyConnectionState 按当前连接态设置，这里只造控件。
        return std::unique_ptr<DuiControl>(new ChatBannerLabel());
    }

    if (n.tag == "group-headerinfo")
    {
        return std::unique_ptr<DuiControl>(new GroupChatHeaderInfo());
    }

    if (n.tag == "chat-iconbtn")
    {
        std::unique_ptr<ChatIconButton> btn(new ChatIconButton());
        btn->SetIconKind(GroupIconKindFromAttr(GetNodeAttr(n, "kind")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "chat-sendbtn")
    {
        return std::unique_ptr<DuiControl>(new ChatSendButton());
    }

    if (n.tag == "chat-msglist")
    {
        return std::unique_ptr<DuiControl>(new ChatMessageList());
    }

    if (n.tag == "chat-input")
    {
        // 与单聊窗同一个控件（见 ChatWindowLayout.cpp 里同名分支的说明，
        // 那里也讲了为什么属性要在工厂里自己解析）。
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

    if (n.tag == "group-sidetab")
    {
        std::unique_ptr<GroupSideTab> tab(new GroupSideTab());
        tab->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        if (GetNodeAttr(n, "active") == "true")
        {
            tab->SetActive(true);
        }
        return std::unique_ptr<DuiControl>(tab.release());
    }

    if (n.tag == "memo-cardscroll")
    {
        // 备忘卡片的滚动视口；ctrlId 由 XML 的 id 属性设置
        return std::unique_ptr<DuiControl>(balloonmemo::CreateMemoCardScroll(0).release());
    }

    if (n.tag == "group-memberlist")
    {
        return std::unique_ptr<DuiControl>(new GroupMemberList());
    }

    if (n.tag == "group-filescroll")
    {
        // 文件列表滚动视口：balloonui DuiScrollView 装一个 GroupFileList 堆叠容器作内容，
        // 内置 auto-hide 滚动条由 DuiScrollView 负责（UI 规约第 1 条「复用 balloonui 滚动控件」）。
        // 内容总高度由 PopulateGroupFileList 增删行后显式同步，故关掉自动测高。
        std::unique_ptr<balloonwjui::DuiScrollView> sv(new balloonwjui::DuiScrollView());
        std::unique_ptr<GroupFileList> list(new GroupFileList());
        list->SetCtrlId(kGIdFileList);
        sv->SetContent(std::unique_ptr<DuiControl>(list.release()));
        sv->SetAutoContentHeight(false);
        // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
        return std::unique_ptr<DuiControl>(sv.release());
    }

    if (n.tag == "group-mention")
    {
        // 白底描边「@提醒」按钮（文字由控件构造时默认设为"@提醒"）。
        return std::unique_ptr<DuiControl>(new GroupMentionButton());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }
    return nullptr;
}

// 用 mock 群消息（MockGroupMessages）逐条建成 ChatBubble，挂进消息列表，
// 再把列表滚到底部（显示最新消息）。
void PopulateGroupMessageList(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    ChatMessageList* list =
        dynamic_cast<ChatMessageList*>(root->FindCtrlById(kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    const std::vector<ChatMessage>& msgs = MockGroupMessages();
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        std::unique_ptr<ChatBubble> bubble(new ChatBubble());
        bubble->SetMessage(msgs[i]);
        list->AddChild(std::unique_ptr<DuiControl>(bubble.release()));
    }
    list->ScrollToBottom();
}

// 用 mock 群成员（MockGroupMembers）逐条建成 GroupMemberItem，挂进右侧
// 边栏的成员列表容器。
void PopulateGroupMemberList(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    GroupMemberList* list =
        dynamic_cast<GroupMemberList*>(root->FindCtrlById(kGIdMemberList));
    if (list == NULL)
    {
        return;
    }
    // 调试入口没有通讯录可查，按下标轮流套用五种在线状态（未知 / 在线 / 离开 / 忙碌 / 离线），
    // 好在调试入口里核对头像角上各种徽标的样子。取值同协议的 presence_type
    const int kMockPresences[] = { kMemberPresenceUnknown, ::presence_online, ::presence_away,
                                   ::presence_busy, ::presence_offline };
    const size_t kMockPresenceCount = sizeof(kMockPresences) / sizeof(kMockPresences[0]);
    const std::vector<GroupMember>& members = MockGroupMembers();
    for (size_t i = 0; i < members.size(); ++i)
    {
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        // mock 数据只区分"是不是群主"，转成网络侧角色取值交给成员行。
        item->SetMember(members[i].name, members[i].avatarText, members[i].avatarColor,
                        members[i].isOwner ? grouprole::kNetRoleOwner
                                           : grouprole::kNetRoleMember);
        item->SetPresence(kMockPresences[i % kMockPresenceCount]);
        list->AddChild(std::unique_ptr<DuiControl>(item.release()));
    }
}

// 设置某个分类筛选 chip 的选中外观：选中=品牌实心（Primary，蓝）；未选=白底灰边
// （Default）。balloonui DuiButton 无橙色 / 任意填充色变体，故选中态用品牌蓝。
void SetFileChipSelected(DuiControl* root, UINT chipId, bool selected)
{
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiButton* btn =
        dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(chipId));
    if (btn == NULL)
    {
        return;
    }
    btn->SetVariant(selected ? balloonwjui::DuiButton::Variant::Primary
                             : balloonwjui::DuiButton::Variant::Default);
}

// 群文件面板的起始默认外观（无论是否 mock 都应用）：起始隐藏群文件容器（成员 Tab
// 默认选中）；chips 默认选中「全部」。真实数据 / 真实切换由窗口类后续接管。
void ConfigureFilePanelDefaults(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    // 起始隐藏群文件内容容器 —— 成员 Tab 默认选中，群文件面板待切换到才显示。
    DuiControl* filesPanel = root->FindCtrlById(kGIdFilesPanel);
    if (filesPanel != NULL)
    {
        filesPanel->SetVisible(false);
    }
    // chips 默认「全部」选中、其余未选。
    SetFileChipSelected(root, kGIdFileChipAll,    true);
    SetFileChipSelected(root, kGIdFileChipPdf,    false);
    SetFileChipSelected(root, kGIdFileChipDoc,    false);
    SetFileChipSelected(root, kGIdFileChipSheet,  false);
    SetFileChipSelected(root, kGIdFileChipDesign, false);
}

// 公告面板的起始默认外观：起始隐藏（成员 Tab 默认选中）；公告卡的圆角浅底、
// 各行的字号与颜色在这里配好 —— XML builder 的通用属性只认 id 与 padding /
// gap，配色字号得走控件自己的 setter。对照 design/群公告.png。
// 备忘面板的起始默认外观：起始隐藏（成员 Tab 默认选中）；空状态提示用弱化的灰字。
void ConfigureMemoPanelDefaults(DuiControl* root)
{
    const COLORREF kMemoHintColor = RGB(0x9A, 0x98, 0x93);   // 空状态提示
    DuiControl* panel = root->FindCtrlById(kGIdMemoPanel);
    if (panel != NULL)
    {
        panel->SetVisible(false);
    }
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdMemoEmpty));
    if (empty != NULL)
    {
        empty->SetTextColor(kMemoHintColor);
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// 问答面板的起始默认外观：起始隐藏（成员 Tab 默认选中）；空状态提示用弱化的灰字；
// 「＋ 新建问答」起始隐藏，群聊窗知道本人是群主或管理员后再显示。
void ConfigureQaPanelDefaults(DuiControl* root)
{
    const COLORREF kQaHintColor = RGB(0x9A, 0x98, 0x93);   // 空状态提示
    DuiControl* panel = root->FindCtrlById(kGIdQaPanel);
    if (panel != NULL)
    {
        panel->SetVisible(false);
    }
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdQaEmpty));
    if (empty != NULL)
    {
        empty->SetTextColor(kQaHintColor);
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    DuiControl* add = root->FindCtrlById(kGIdQaAdd);
    if (add != NULL)
    {
        add->SetVisible(false);
    }
}

void ConfigureNoticePanelDefaults(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }

    // ---- 配色与字号（像素）----
    const COLORREF kNoticeCardBg     = RGB(0xFA, 0xF9, 0xF6);   // 公告卡浅底
    const COLORREF kNoticeCardBorder = RGB(0xEC, 0xEA, 0xE6);   // 公告卡描边
    const COLORREF kNoticeTitleColor = RGB(0x2A, 0x2A, 0x2A);   // 标题
    const COLORREF kNoticeBodyColor  = RGB(0x55, 0x55, 0x55);   // 正文
    const COLORREF kNoticeMetaColor  = RGB(0x9A, 0x98, 0x93);   // 发布人 · 时间 / 顶部计数
    const int kNoticeCardCorner = 10;   // 公告卡圆角

    DuiControl* noticePanel = root->FindCtrlById(kGIdNoticePanel);
    if (noticePanel != NULL)
    {
        noticePanel->SetVisible(false);
    }

    balloonwjui::DuiVBox* card =
        dynamic_cast<balloonwjui::DuiVBox*>(root->FindCtrlById(kGIdNoticeCard));
    if (card != NULL)
    {
        card->SetBgColor(kNoticeCardBg);
        card->SetBorderColor(kNoticeCardBorder);
        card->SetBorderWidth(1.0f);
        card->SetCornerRadius(kNoticeCardCorner);
    }

    balloonwjui::DuiLabel* title =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeTitle));
    if (title != NULL)
    {
        title->SetTextColor(kNoticeTitleColor);
        //标题通常一行放得下，放不下也不换行，末尾省略即可 —— 换行会把正文挤下去
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    balloonwjui::DuiLabel* body =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeBody));
    if (body != NULL)
    {
        body->SetTextColor(kNoticeBodyColor);
        //正文是公告的主体，必须自动换行完整显示
        body->SetWordWrap(true);
        body->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    balloonwjui::DuiLabel* meta =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeMeta));
    if (meta != NULL)
    {
        meta->SetTextColor(kNoticeMetaColor);
    }

    balloonwjui::DuiLabel* count =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeCount));
    if (count != NULL)
    {
        count->SetTextColor(kNoticeMetaColor);
    }

    balloonwjui::DuiLabel* index =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeIndex));
    if (index != NULL)
    {
        index->SetTextColor(kNoticeMetaColor);
        index->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    balloonwjui::DuiLabel* empty =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNoticeEmpty));
    if (empty != NULL)
    {
        empty->SetTextColor(kNoticeMetaColor);
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        //起始态：公告卡与空状态提示只显示一个。此刻还没拉过公告，先显示空状态。
        empty->SetVisible(true);
    }

    //公告卡起始隐藏 —— 有公告了 RenderNoticePanel 会把它显出来
    if (card != NULL)
    {
        card->SetVisible(false);
    }
}

// i18n-ignore-begin: 调试入口的模拟群文件列表
// 用 mock 群文件逐条建成 GroupFileItem 挂进文件列表容器，并同步滚动视口内容高度
// 与分页信息。仅视觉核对（loadMock）下调用；真实群的文件由增量3 的
// cmd_get_group_files 应答填充。对照 design/群文件.png。
void PopulateGroupFileList(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    GroupFileList* list =
        dynamic_cast<GroupFileList*>(root->FindCtrlById(kGIdFileList));
    if (list == NULL)
    {
        return;
    }

    // mock 文件清单（名称 / 扩展名 / 「上传者·时间·大小」说明）。
    struct MockFile
    {
        const wchar_t* name;
        const wchar_t* ext;
        const wchar_t* meta;
    };
    static const MockFile kMock[] =
    {
        { L"v3.2-发布计划.xlsx",  L"xlsx",   L"谢知行 · 今天 14:31 · 128 KB" },
        { L"登录页 final.fig",     L"fig",    L"我 · 今天 11:02 · 12.4 MB" },
        { L"群文件功能 PRD.docx",  L"docx",   L"方亦初 · 昨天 · 88 KB" },
        { L"品牌色规范.pdf",       L"pdf",    L"江屿白 · 周一 · 1.8 MB" },
        { L"API 接口文档 v3.docx", L"docx",   L"宋微之 · 周一 · 342 KB" },
        { L"用户研究报告.pdf",     L"pdf",    L"许清欢 · 5/15 · 5.6 MB" },
        { L"原型交互稿.sketch",    L"sketch", L"林知夏 · 5/12 · 9.2 MB" },
        { L"测试用例清单.xlsx",    L"xlsx",   L"周慎 · 5/10 · 64 KB" },
    };
    const int kMockCount = (int)(sizeof(kMock) / sizeof(kMock[0]));
    for (int i = 0; i < kMockCount; ++i)
    {
        std::unique_ptr<GroupFileItem> item(new GroupFileItem());
        item->SetFile(kMock[i].name, kMock[i].meta, kMock[i].ext);
        list->AddChild(std::unique_ptr<DuiControl>(item.release()));
    }

    // 同步滚动视口的内容高度（行数 × 行高），使滚动范围正确。
    balloonwjui::DuiScrollView* sv =
        dynamic_cast<balloonwjui::DuiScrollView*>(root->FindCtrlById(kGIdFileScroll));
    if (sv != NULL)
    {
        sv->SetContentHeight(list->ContentHeight());
    }

    // 分页信息文字（mock 单页）。
    balloonwjui::DuiLabel* pageInfo =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdFilePageInfo));
    if (pageInfo != NULL)
    {
        CString text;
        text.Format(L"共 %d · 第 1/1 页", kMockCount);
        pageInfo->SetText(text);
    }
}
// i18n-ignore-end

} // anonymous namespace

std::unique_ptr<DuiControl> BuildGroupChatWindowBody(bool loadMock)
{
    // 内嵌 XML 里的界面文字先经 TranslateXml 换成当前语言的译文；FromString 在调用内解析完毕，临时串的生命周期足够
    std::unique_ptr<DuiControl> root =
        DuiXmlBuilder::FromString(i18n::TranslateXml(kGroupChatWindowBodyXml).c_str(),
                                  &CreateGroupCustomControl);

    // 顶部离线提示条建好就先收起来 —— 与单聊窗 BuildChatWindowBody 同一处理，
    // 原因见那里的说明（XML 无 visible 属性可声明初始不可见，而连接态只在变化时
    // 才推过来，不收它就会在标题栏下面留一条空白）。
    if (root)
    {
        DuiControl* banner = root->FindCtrlById(kGIdOfflineBanner);
        if (banner != NULL)
        {
            banner->SetVisible(false);
        }

        // 「已 Pin N 条」提示带同理，样式与单聊窗共用一份实现（见 ChatWindowLayout.h
        // 里 ApplyPinBannerStyle 的说明），保证两个窗口长得一模一样。
        ApplyPinBannerStyle(root->FindCtrlById(kGIdPinBanner));

        // 粘贴识别提示条：样式与单聊窗共用一份实现（见 ChatWindowLayout.h），初始隐藏
        ApplyCodePasteBarStyle(root.get(), kGIdCodePasteBar, kGIdCodePasteText, kGIdCodePasteSend,
                               kGIdCodePasteDismiss);

        // 屏幕共享横幅同理：只在本群有进行中的共享时显示（见 UpdateScreenShareBanner）
        ApplyScreenShareBannerStyle(root->FindCtrlById(kGIdScreenShareBanner));

        // 群通话横幅与屏幕共享横幅同一样式：只在本群有进行中的群通话时显示（见 UpdateGroupCallBanner）
        ApplyScreenShareBannerStyle(root->FindCtrlById(kGIdGroupCallBanner));

        // 录音条占位同理：只在录音时代替输入框显示（语音消息第 3b 步，见 VoiceRecordBar.h）
        DuiControl* voiceBar = root->FindCtrlById(kGIdVoiceBar);
        if (voiceBar != NULL)
        {
            voiceBar->SetVisible(false);
        }

        // 仅通知模式的提示行同理：只在本群开启了仅通知模式、本人又是普通成员时代替输入区显示（见 ApplyNotifyOnlyInput）。
        // 灰字、在输入区块里水平垂直居中
        balloonwjui::DuiLabel* notifyOnlyBar = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdNotifyOnlyBar));
        if (notifyOnlyBar != NULL)
        {
            notifyOnlyBar->SetText(NotifyOnlyInputHintText());
            notifyOnlyBar->SetTextColor(kNotifyOnlyBarTextColor);
            notifyOnlyBar->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            notifyOnlyBar->SetVisible(false);
        }

        // 引用条同理：XML 没有 visible 属性，只能建好之后收起来。它只在用户点了
        // 气泡右键「引用回复」时才出现，不收的话输入框上方会一直杵着一条空白。
        balloonwjui::DuiVBox* replyBar = dynamic_cast<balloonwjui::DuiVBox*>(root->FindCtrlById(kGIdReplyBar));
        if (replyBar != NULL)
        {
            replyBar->SetVisible(false);
            // 灰底圆角卡片。DuiLayout 本身就支持底色与圆角，不必自绘一个控件
            // （开发约定文档：新增控件优先直接用 balloonui 现成能力）。
            replyBar->SetBgColor(RGB(0xF0, 0xF0, 0xF0));
            replyBar->SetCornerRadius(6);
        }

        // 取消按钮做成链接样式：DuiLabel 的 ModeText 是纯绘制控件、不发通知也不变
        // 光标，只有 ModeLink 才会在悬停时变手型、并在点击时发 DUIN_CLICK。
        // XML 暂不暴露 mode 属性（见 DuiLabel.h 的说明），故在此设。
        //本文件只 using 了 DuiControl / DuiXmlBuilder，其余 balloonui 类型都要带
        //命名空间限定。
        balloonwjui::DuiLabel* replyClose =
            dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdReplyClose));
        if (replyClose != NULL)
        {
            replyClose->SetMode(balloonwjui::DuiLabel::ModeLink);
            replyClose->SetAutoNavigate(false);   // 不打开浏览器，点击交给本窗处理
        }

        // 成员搜索框自带的拖放要关掉，而且必须在控件挂进窗口<u>之前</u>关。
        //
        // 操作系统的拖放目标按窗口注册、一个窗口只能有一个，而 balloonui 里需要
        // 接收拖放的控件会在第一次布局时让宿主替自己注册一个分发器。群聊窗要的是
        // 整窗接收（拖文件进来就发送），窗口里任何控件抢走这份注册，聊天窗那份就
        // 再也注册不上，症状是拖什么进来都没反应、且没有任何提示。
        //
        // 搜索框早先用的是内嵌真 Win32 输入框子窗口的实现、不参与这件事；
        // 2026-08-17 起它改用无窗口的富文本控件，于是跟着具备了接收拖放的行为，
        // 守卫用例 CDR4 当场失败。输入框那一侧是在 ChatInputRichEdit 的构造函数里
        // 关的，本控件由 XML 建出来、没有可插手的构造函数，所以建完之后就地关。
        balloonwjui::DuiRichEdit* memberSearch =
            dynamic_cast<balloonwjui::DuiRichEdit*>(root->FindCtrlById(kGIdMemberSearch));
        if (memberSearch != NULL)
        {
            memberSearch->SetDragDropEnabled(false);
        }

        // 「按部门 / 平铺」切换按钮：起始按平铺视图设置（不高亮），群聊窗读到记住的视图后再调一次
        // ApplyMemberViewToggle；点击发 DUIN_CLICK，交给群聊窗处理
        ApplyMemberViewToggle(root.get(), false);
    }

    // 注意：记住的输入区块高度不在这里套用（依赖 splitter 实际高度，须等布局后才
    // 知道）—— 群聊窗在每次布局（OnRelayout）里据 GetRememberedChatInputHeightPx
    // 把输入区固定，与单聊共用同一记忆。构建期仅保留 XML split-fraction 作首帧默认。

    // 群文件面板起始默认外观（隐藏文件容器 + chips 默认选「全部」）——无论是否 mock
    // 都应用，使切到群文件 Tab 时外观正确。
    if (root)
    {
        ConfigureFilePanelDefaults(root.get());
        // 公告面板起始默认外观（隐藏公告容器 + 公告卡配色字号）——同样无论是否 mock
        // 都应用，使切到公告 Tab 时外观正确。
        ConfigureNoticePanelDefaults(root.get());
        // 备忘面板起始隐藏（成员 Tab 默认选中）
        ConfigureMemoPanelDefaults(root.get());
        // 问答面板起始隐藏
        ConfigureQaPanelDefaults(root.get());
    }

    // 仅调试入口 / 视觉核对用 mock 群消息 / 群成员 / 群文件预填；真实群起始为空，由真实
    // 收发、cmd_get_group_members 与 cmd_get_group_files 填充。
    if (root && loadMock)
    {
        PopulateGroupMessageList(root.get());
        PopulateGroupMemberList(root.get());
        PopulateGroupFileList(root.get());
    }
    return root;
}

std::wstring MakeScreenShareBannerText(bool selfIsHost, const std::wstring& hostName, bool locked, bool forced,
                                       const std::wstring& title)
{
    //有共享标题时写成「正在共享：周会演示」（第 15 期）；老服务端没有标题时写「正在共享屏幕」
    if (selfIsHost)
    {
        return title.empty() ? std::wstring(TR(L"你正在共享屏幕")) : TRF(L"你正在共享：{0}", title);
    }
    std::wstring sharing = title.empty() ? std::wstring(TR(L"正在共享屏幕")) : TRF(L"正在共享：{0}", title);
    //设了加入密码时标出「需要密码」，不显示密码本身（第 12 期）
    if (locked)
        sharing += TR(L"（需要密码）");
    // 末尾的「›」与 Pin 提示带一致，表示这一条可以点
    //{0} 为发起人名字，{1} 为上面拼好的「正在共享……」；不知道发起人时用「有人」开头的另一句
    std::wstring text = hostName.empty() ? TRF(L"有人{0}，点击加入 ›", sharing)
                                         : TRF(L"{0} {1}，点击加入 ›", hostName, sharing);
    //强制模式写明效果，接在「点击加入 ›」后面：横幅一行放不下时省略的是说明的末尾，可点的提示总能看到（第 14 期）
    if (forced)
        text += TR(L"　强制模式：加入后全屏观看、不能退出，直到共享结束");
    return text;
}

bool UpdateScreenShareBanner(DuiControl* root, const std::wstring& text)
{
    if (root == NULL)
    {
        return false;
    }
    ChatBannerLabel* label = dynamic_cast<ChatBannerLabel*>(root->FindCtrlById(kGIdScreenShareBanner));
    if (label == NULL)
    {
        return false;
    }
    const bool wasVisible = label->IsVisible();
    if (text.empty())
    {
        label->SetVisible(false);
    }
    else
    {
        label->SetText(text.c_str());
        label->SetVisible(true);
    }
    return label->IsVisible() != wasVisible;
}

std::wstring MakeGroupCallBannerText(bool selfInCall, bool video, int joinedCount)
{
    const std::wstring kind = video ? std::wstring(TR(L"群视频通话")) : std::wstring(TR(L"群语音通话"));
    if (selfInCall)
        return TRF(L"你正在{0}中", kind);
    // 末尾的「›」与 Pin 提示带一致，表示这一条可以点
    if (joinedCount > 0)
        return TRF(L"{0}进行中 · {1} 人，点击加入 ›", kind, joinedCount);
    return TRF(L"{0}进行中，点击加入 ›", kind);
}

bool UpdateGroupCallBanner(DuiControl* root, const std::wstring& text)
{
    if (root == NULL)
    {
        return false;
    }
    ChatBannerLabel* label = dynamic_cast<ChatBannerLabel*>(root->FindCtrlById(kGIdGroupCallBanner));
    if (label == NULL)
    {
        return false;
    }
    const bool wasVisible = label->IsVisible();
    if (text.empty())
    {
        label->SetVisible(false);
    }
    else
    {
        label->SetText(text.c_str());
        label->SetVisible(true);
    }
    return label->IsVisible() != wasVisible;
}

void ApplyGroupSideTabWeights(DuiControl* root)
{
    //2026-10-07 起边栏有五个 Tab（加了「问答」），右栏宽度五等分时「群文件 N」放不下，简体中文也按文字宽度分配
    if (root == NULL)
    {
        return;
    }
    //Tab 文字两侧各留的空白（像素），避免两个 Tab 的文字贴在一起
    const int kTabTextPadding = 8;
    const UINT kTabIds[] = { kGIdTabMembers, kGIdTabFiles, kGIdTabNotice, kGIdTabMemo, kGIdTabQa };
    for (size_t i = 0; i < sizeof(kTabIds) / sizeof(kTabIds[0]); ++i)
    {
        GroupSideTab* tab = dynamic_cast<GroupSideTab*>(root->FindCtrlById(kTabIds[i]));
        if (tab == NULL)
        {
            continue;
        }
        balloonwjui::DuiLayout* row = dynamic_cast<balloonwjui::DuiLayout*>(tab->GetParent());
        if (row == NULL)
        {
            continue;
        }
        //权重取文字宽度：右栏宽度按各 Tab 文字的长短成比例分配
        const int width = GroupSideTab::MeasureTabTextWidth((LPCWSTR)tab->GetText()) + 2 * kTabTextPadding;
        balloonwjui::DuiLayout::Hint hint = row->GetHint(tab);
        hint.weight = (width > 0) ? width : 1;
        row->SetHint(tab, hint);
    }
}

bool ApplyNotifyOnlyInput(DuiControl* root, bool notifyOnly)
{
    if (root == NULL)
    {
        return false;
    }
    DuiControl* bar = root->FindCtrlById(kGIdNotifyOnlyBar);
    DuiControl* input = root->FindCtrlById(kGIdInputEdit);
    if (bar == NULL || input == NULL)
    {
        return false;
    }
    //常驻的三块：仅通知模式下隐藏，恢复时显示
    const UINT kRegularIds[] = { kGIdToolbarRow, kGIdInputEdit, kGIdInputBottomRow };
    for (size_t i = 0; i < sizeof(kRegularIds) / sizeof(kRegularIds[0]); ++i)
    {
        DuiControl* c = root->FindCtrlById(kRegularIds[i]);
        if (c != NULL)
        {
            c->SetVisible(!notifyOnly);
        }
    }
    //临时出现的三块（引用条、粘贴识别提示条、录音条）：进入仅通知模式时收起，恢复时也保持收起，由各自的逻辑再打开
    if (notifyOnly)
    {
        const UINT kTransientIds[] = { kGIdReplyBar, kGIdCodePasteBar, kGIdVoiceBar };
        for (size_t i = 0; i < sizeof(kTransientIds) / sizeof(kTransientIds[0]); ++i)
        {
            DuiControl* c = root->FindCtrlById(kTransientIds[i]);
            if (c != NULL)
            {
                c->SetVisible(false);
            }
        }
    }
    bar->SetVisible(notifyOnly);
    //SetVisible 不会重排：对发生显隐变化的那一层（输入区块）按它当前的矩形 ForceLayout（开发约定文档 UI 约定 5）
    DuiControl* block = bar->GetParent();
    if (block != NULL)
    {
        block->ForceLayout(block->GetRect());
        block->Invalidate();
    }
    return true;
}

bool ApplyMemberViewToggle(DuiControl* root, bool treeMode)
{
    if (root == NULL)
    {
        return false;
    }
    ChatIconButton* toggle = dynamic_cast<ChatIconButton*>(root->FindCtrlById(kGIdMemberViewToggle));
    if (toggle == NULL)
    {
        return false;
    }
    //悬停提示说明点了之后切到哪种视图：部门树视图（高亮）下点了回到平铺，平铺视图下点了按部门显示。
    //每次都重设两条提示：重复登记同一控件时 DuiToolTipMgr 覆盖原文字，代价可以忽略
    toggle->SetToggleTips(TR(L"平铺显示"), TR(L"按部门显示"));
    //图标宽度固定，切换视图只改高亮状态，不需要重排；状态有变化时 SetActive 内部会重画
    toggle->SetActive(treeMode);
    return true;
}

} // namespace balloonchatwnd
