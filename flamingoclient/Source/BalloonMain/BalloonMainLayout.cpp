/**
 *  BalloonMainLayout.cpp
 *
 *  主面板客户区控件树的构造实现。布局以 XML 为真值源交给 balloonui 的
 *  DuiXmlBuilder 解析。骨架部分（Header / 搜索 / Tab / 底部栏）是固定
 *  XML；三个 Tab 内容面板的列表行由 mock 数据动态拼成 XML 片段（本阶段
 *  P1b 用 mock 数据，真实数据接入留作后续阶段）。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用，所以自己
 *  把 balloonui 头文件所需的 ATL/WTL 前置依赖引进来。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  注：mock 数据里的名称 / 文字均不含 XML 特殊字符（< > & " '），故拼接
 *  XML 时未做转义；将来接入真实数据时需补转义。
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
#include <map>
#include <stdlib.h>
#include <stdio.h>

#include "BalloonMainLayout.h"
#include "MainPanelHeader.h"
#include "MainPanelTab.h"
#include "MainPanelButtons.h"
#include "MainPanelListItem.h"
#include "MainPanelSectionHeader.h"
#include "MainPanelIcons.h"
#include "MainPanelMockData.h"
#include "MainPanelScrollView.h"
#include "MainPanelTreeView.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox

namespace balloonmain {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiVBox;
using balloonwjui::DuiLayout;
using balloonwjui::DuiScrollView;

namespace {

// ---- 列表行高度（逻辑像素）---------------------------------------------

const int kContactRowHeight = 54;   // 联系人 / 服务号行高
const int kGroupRowHeight   = 58;   // 群行高
const int kConvRowHeight    = 58;   // 会话行高
const int kSectionHeight    = 32;   // 分组标题行高
// 原先这里还有一个 kPaneTitleHeight(26) —— 联系人页"通讯录 · N 人"与群页
// "我的群聊 · N"两行页标题的高度。两行标题都已去掉(人数 / 群数改为跟在树的
// 根节点名字后面),常量随之删除;滚动区高度那侧对应的是 BalloonMainPanel.cpp
// 里的 kContactsPaneTitleHeight / kGroupsPaneTitleHeight,现在都取 0。

// =====================================================================
// 字符串 / XML 拼接小工具
// =====================================================================

// CString（宽字符）转 UTF-8 —— 拼进 XML 串里给 DuiXmlBuilder 解析。
std::string WideToUtf8(const CString& s)
{
    if (s.IsEmpty())
    {
        return std::string();
    }
    int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)s, -1,
                                    NULL, 0, NULL, NULL);
    if (len <= 0)
    {
        return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)s, -1, &out[0], len, NULL, NULL);
    if (!out.empty() && out.back() == '\0')
    {
        out.pop_back();
    }
    return out;
}

// COLORREF 转 "r,g,b" 字符串。
std::string RgbToStr(COLORREF c)
{
    char buf[32] = { 0 };
    ::sprintf_s(buf, "%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
    return std::string(buf);
}

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// UTF-8 字符串转 CString（宽字符）。
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

// 解析 "r,g,b" 颜色字符串；失败返回 def。
COLORREF ParseRgbColor(const std::string& s, COLORREF def)
{
    int r = 0, g = 0, b = 0;
    if (sscanf_s(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3)
    {
        return RGB(r, g, b);
    }
    return def;
}

// 把 <main-tab> 的 kind 属性翻译成 IconKind。
int TabIconKindFromAttr(const std::string& kind)
{
    if (kind == "messages")
    {
        return kIconHistory;
    }
    if (kind == "groups")
    {
        return kIconGroup;
    }
    return kIconUser;   // 默认 / "contacts"
}

// =====================================================================
// 由 mock 数据拼装列表面板 XML 片段
// =====================================================================

// 拼一行 <main-listitem>。
void AppendListItem(std::string& xml, int rowHeight,
                    const CString& name, const CString& sub,
                    const CString& avatarText, COLORREF avatarColor,
                    bool showStatus, int status,
                    const CString& rightText, int badge)
{
    xml += "    <main-listitem fixedHeight=\"";
    xml += std::to_string(rowHeight);
    xml += "\" name=\"";
    xml += WideToUtf8(name);
    xml += "\" sub=\"";
    xml += WideToUtf8(sub);
    xml += "\" avatar=\"";
    xml += WideToUtf8(avatarText);
    xml += "\" avatar-color=\"";
    xml += RgbToStr(avatarColor);
    xml += "\" show-status=\"";
    xml += (showStatus ? "true" : "false");
    xml += "\" status=\"";
    xml += std::to_string(status);
    xml += "\"";
    if (!rightText.IsEmpty())
    {
        xml += " right-text=\"";
        xml += WideToUtf8(rightText);
        xml += "\"";
    }
    if (badge > 0)
    {
        xml += " badge=\"";
        xml += std::to_string(badge);
        xml += "\"";
    }
    xml += "/>\n";
}

// 拼一行分组标题 <main-section>。
void AppendSection(std::string& xml, const char* titleUtf8, int online, int total)
{
    xml += "    <main-section fixedHeight=\"";
    xml += std::to_string(kSectionHeight);
    xml += "\" title=\"";
    xml += titleUtf8;
    xml += "\" online=\"";
    xml += std::to_string(online);
    xml += "\" total=\"";
    xml += std::to_string(total);
    xml += "\"/>\n";
}

// 拼一个联系人行（名称取备注优先，副行取职位）。
void AppendContactRow(std::string& xml, const MockContact& c)
{
    const CString display = c.remark.IsEmpty() ? c.name : c.remark;
    AppendListItem(xml, kContactRowHeight, display, c.title,
                   c.avatarText, c.avatarColor, true, c.status,
                   CString(), 0);
}

// 拼"消息"面板内容:<treeview>(节点由 C++ 侧 FillMessagesTree 填充)。
// 19c:跟 19a / 19b 同套路,会话扁平列(无层级,每会话作为一个根)。
std::string BuildMessagesListXml()
{
    std::string xml = "<vbox padding=\"6,4,6,4\" gap=\"0\">\n";
    xml += "    <main-treeview id=\"";
    xml += std::to_string((int)kIdMessagesTree);
    xml += "\" weight=\"1\" row-height=\"48\"/>\n";
    xml += "</vbox>\n";
    return xml;
}

// 拼"联系人"面板内容:只有 <treeview>(节点由 C++ 侧 FillContactsTree 填充,
// 见 BalloonMainPanel.cpp)。
// 19a:把原来的 <main-section> + <main-listitem> 列表换成 DuiTreeView,
// 节点数据(分组与好友项)在 InitUi 拿到 root 后用 DuiTreeView API 添加。
//
// 这里不再放"通讯录 · N 人"页标题:人数改为跟在树里公司根节点的名字后面
// (形如"明远科技有限公司 · 161 人",见 OnContactsArrived),页顶再挂一行
// 总数标题就重复了。同时人数口径也随之改成"公司这棵树下的去重人数",不再
// 是资料池总人数。
std::string BuildContactsListXml()
{
    std::string xml = "<vbox padding=\"6,4,6,4\" gap=\"0\">\n";

    // DuiTreeView 占满剩余高度;节点由 C++ 填(FillContactsTree)。
    xml += "    <main-treeview id=\"";
    xml += std::to_string((int)kIdContactsTree);
    xml += "\" weight=\"1\" row-height=\"40\"/>\n";

    xml += "</vbox>\n";
    return xml;
}

// 拼"群"面板内容:只有 <treeview>(节点由 C++ 侧 FillGroupsTree 填充,
// 见 BalloonMainPanel.cpp)。
// 19b:同 Contacts 页,换 DuiTreeView。
//
// 这里刻意不像 Contacts 页那样再加一行 <label> 页标题:群页只有"我的群聊"
// 一个分组,树根节点本身就叫"我的群聊 · N"(FillGroupsTree 里按真实群数
// 生成),再加页标题就会出现两行同名的"我的群聊",且 label 里的数字只能取
// mock 值、登录后不会刷新,两个数字还会对不上。Contacts 页不冲突是因为它
// 的页标题叫"通讯录 · N 人",树根叫"我的好友"/"服务号",名字本来就不同。
std::string BuildGroupsListXml()
{
    std::string xml = "<vbox padding=\"6,4,6,4\" gap=\"0\">\n";

    xml += "    <main-treeview id=\"";
    xml += std::to_string((int)kIdGroupsTree);
    xml += "\" weight=\"1\" row-height=\"40\"/>\n";

    xml += "</vbox>\n";
    return xml;
}

// =====================================================================
// 自定义标签工厂
// =====================================================================

// 把 XML 里 builder 不认识的标签翻译成对应控件实例：
//   <main-header>   渐变 Banner
//   <main-tab>      图标 Tab
//   <main-iconbtn>  透明底图标按钮
//   <main-action>   底部主操作按钮
//   <main-section>  联系人分组标题
//   <main-listitem> 通用列表行
//   <control>       不可见 spacer
std::unique_ptr<DuiControl> CreateMainCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "main-header")
    {
        return std::unique_ptr<DuiControl>(new MainPanelHeader());
    }

    if (n.tag == "main-treeview")
    {
        // MainPanelTreeView(继承 DuiTreeView),职责是行右侧列的绘制与会话行
        // 装饰(时间 / 未读红点 / 置顶与免打扰标记),详见 MainPanelTreeView.h。
        // 滚轮不需要它管:单列 DuiTreeView::OnMouseWheel 如实返回 false,
        // DuiHost::DispatchMouseWheel 随即沿父链上冒到外层 MainPanelScrollView。
        //
        // 不能直接用 <treeview> 标签:内置工厂(DuiXmlBuilder::BuildOne)
        // 在 BUI_FEATURE_TREEVIEW 段就把 <treeview> 截住建裸 DuiTreeView,
        // 不会走 CustomFactory。改用业务自定义 tag <main-treeview> 绕开。
        //
        // 这里只解析 id 与 row-height 两个属性 —— 业务侧 Fill*Tree 会再
        // 调 SetRowHeight,所以 row-height 属性纯属可读性留存。其它布局
        // hint(weight / padding / margin / fixedXxx)由父容器解析,与
        // 控件自身无关。
        std::unique_ptr<MainPanelTreeView> tv(new MainPanelTreeView());
        const std::string idStr = GetNodeAttr(n, "id");
        if (!idStr.empty())
        {
            tv->SetCtrlId((UINT)atoi(idStr.c_str()));
        }
        const std::string rowH = GetNodeAttr(n, "row-height");
        if (!rowH.empty())
        {
            tv->SetRowHeight(atoi(rowH.c_str()));
        }
        return std::unique_ptr<DuiControl>(tv.release());
    }

    if (n.tag == "main-scroll")
    {
        // Tab 内容面板 —— MainPanelScrollView(继承 DuiScrollView),提供:
        //   (1) 外层滚动:单列 DuiTreeView 自己不创建滚动条(EnsureScrollRanges
        //       在 !IsMultiCol 时直接 return),且 OnPaint 单列路径按
        //       m_rcItem.top + r * m_rowH 直接铺行、无裁剪。所以必须由外层
        //       scrollview 提供滚动 + IntersectClipRect 硬裁剪,否则行数溢出
        //       会画到容器外的底部栏区域,造成花屏(详见 M7 用例的根因注释)。
        //   (2) overlay 自动隐藏滚动条:符合开发约定文档 UI 约定 1。
        // 内容高度走业务侧手动算(方案 1a):FillContactsTree / FillGroupsTree /
        // FillMessagesTree 末尾把 "label 高 + treeview.GetContentHeight() +
        // vbox padding" 喂给 scrollview->SetContentHeight。所以 MainPanelScrollView
        // 默认 SetAutoContentHeight(false)。
        return std::unique_ptr<DuiControl>(new MainPanelScrollView());
    }

    if (n.tag == "main-tab")
    {
        std::unique_ptr<MainPanelTab> tab(new MainPanelTab());
        tab->SetIconKind(TabIconKindFromAttr(GetNodeAttr(n, "kind")));
        tab->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        tab->SetBadgeCount(atoi(GetNodeAttr(n, "badge", "0").c_str()));
        if (GetNodeAttr(n, "active") == "true")
        {
            tab->SetActive(true);
        }
        return std::unique_ptr<DuiControl>(tab.release());
    }

    if (n.tag == "main-iconbtn")
    {
        std::unique_ptr<MainPanelIconButton> btn(new MainPanelIconButton());
        const std::string kind = GetNodeAttr(n, "kind");
        btn->SetIconKind(kind == "plus" ? kIconPlus : kIconList);
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "main-action")
    {
        std::unique_ptr<MainPanelActionButton> btn(new MainPanelActionButton());
        btn->SetText(Utf8ToWide(GetNodeAttr(n, "text")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "main-section")
    {
        std::unique_ptr<MainPanelSectionHeader> sec(new MainPanelSectionHeader());
        sec->SetTitle(Utf8ToWide(GetNodeAttr(n, "title")));
        sec->SetCount(atoi(GetNodeAttr(n, "online", "0").c_str()),
                      atoi(GetNodeAttr(n, "total", "0").c_str()));
        return std::unique_ptr<DuiControl>(sec.release());
    }

    if (n.tag == "main-listitem")
    {
        std::unique_ptr<MainPanelListItem> item(new MainPanelListItem());
        item->SetName(Utf8ToWide(GetNodeAttr(n, "name")));
        item->SetSubText(Utf8ToWide(GetNodeAttr(n, "sub")));
        item->SetAvatar(Utf8ToWide(GetNodeAttr(n, "avatar")),
                        ParseRgbColor(GetNodeAttr(n, "avatar-color"),
                                      RGB(0xFF, 0x7A, 0x45)));
        item->SetStatus(GetNodeAttr(n, "show-status") == "true",
                        atoi(GetNodeAttr(n, "status", "4").c_str()));
        item->SetRightText(Utf8ToWide(GetNodeAttr(n, "right-text")));
        item->SetBadgeCount(atoi(GetNodeAttr(n, "badge", "0").c_str()));
        return std::unique_ptr<DuiControl>(item.release());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }
    return nullptr;
}

// 把一个 Tab 内容面板装填好:用 listXml 构建内容 vbox,作为 scrollview 的
// 单一内容子(SetContent 拿走所有权)装到对应 id 的 Pane(MainPanelScrollView)下。
//   · 滚动机制:scrollview 通过 ApplyScrollToContent 在 m_content 上做
//     SetRect 偏移,再用 IntersectClipRect 硬裁剪 —— 即便里面的单列
//     DuiTreeView 自己按 m_rcItem.top + r * m_rowH 铺行,溢出可视区的行
//     也会被外层 clip 掉。
//   · 内容高度:scrollview 默认 SetAutoContentHeight(false),业务侧
//     (FillContactsTree 等)在节点填完 / 节点状态变更时调
//     scrollview->SetContentHeight 同步,见方案 1a 说明。
void FillScrollPane(DuiControl* root, UINT paneId, const std::string& listXml)
{
    if (root == NULL)
    {
        return;
    }
    DuiScrollView* pane = dynamic_cast<DuiScrollView*>(root->FindCtrlById(paneId));
    if (pane == NULL)
    {
        return;
    }
    std::unique_ptr<DuiControl> content =
        DuiXmlBuilder::FromString(listXml.c_str(), &CreateMainCustomControl);
    if (content)
    {
        pane->SetContent(std::move(content));
    }
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildMainPanelBody()
{
    // 外层 vbox 自上而下：渐变 Header / 搜索框 / 三图标 Tab / 内容区
    // （三个 Tab 内容面板）/ 底部栏。内容面板的列表行由 mock 数据拼成。
    std::string xml;
    xml += "<vbox padding=\"0\" gap=\"0\">\n";
    xml += "  <main-header id=\"240\" fixedHeight=\"108\"/>\n";
    xml += "  <edit id=\"200\" placeholder=\"";
    xml += u8"搜索联系人、群、消息、文件…";
    xml += "\" fixedHeight=\"36\"/>\n";
    xml += "  <hbox fixedHeight=\"48\" gap=\"0\">\n";
    xml += "    <main-tab id=\"210\" kind=\"messages\" text=\"";
    xml += u8"消息";
    // 角标初值 0（不显示）；登录后由 RefreshMessagesTabBadge 用真实未读总数刷新。
    xml += "\" badge=\"0\" weight=\"1\"/>\n";
    xml += "    <main-tab id=\"211\" kind=\"contacts\" text=\"";
    xml += u8"联系人";
    xml += "\" active=\"true\" weight=\"1\"/>\n";
    xml += "    <main-tab id=\"212\" kind=\"groups\" text=\"";
    xml += u8"群组";
    xml += "\" weight=\"1\"/>\n";
    xml += "  </hbox>\n";

    // 内容区：三个 Tab 内容面板，各是一个滚动视口（同 weight，Tab 切换
    // 时由对话框显隐）。视口内容（列表）在 FromString 之后另行装入。
    xml += "  <vbox weight=\"1\" padding=\"0\" gap=\"0\">\n";
    xml += "    <main-scroll id=\"220\" weight=\"1\"/>\n";
    xml += "    <main-scroll id=\"221\" weight=\"1\"/>\n";
    xml += "    <main-scroll id=\"222\" weight=\"1\"/>\n";
    xml += "  </vbox>\n";

    // 底部栏。带 id 是为了让主面板能取到它的矩形——底部栏上除两个按钮之外的空白
    // 支持"按住拖动整个窗口"，可拖区就是这条 hbox 的矩形减去两个按钮的矩形。
    xml += "  <hbox id=\"232\" fixedHeight=\"44\" gap=\"6\" padding=\"8,0,8,0\">\n";
    xml += "    <main-iconbtn id=\"230\" kind=\"list\" fixedWidth=\"34\"/>\n";
    xml += "    <control weight=\"1\"/>\n";
    xml += "    <main-action id=\"231\" text=\"";
    xml += u8"查找";
    xml += "\" fixedWidth=\"80\"/>\n";
    xml += "  </hbox>\n";
    xml += "</vbox>\n";

    std::unique_ptr<DuiControl> root =
        DuiXmlBuilder::FromString(xml.c_str(), &CreateMainCustomControl);

    // 把三个滚动视口的列表内容装进去（mock 数据拼成的 vbox）。
    if (root)
    {
        FillScrollPane(root.get(), kIdPaneMessages, BuildMessagesListXml());
        FillScrollPane(root.get(), kIdPaneContacts, BuildContactsListXml());
        FillScrollPane(root.get(), kIdPaneGroups,   BuildGroupsListXml());
    }
    return root;
}

} // namespace balloonmain
