/**
 *  BotCardLayout.h
 *
 *  群聊窗里点了某个机器人后弹出的资料卡小窗（CBotCardWindow，docs/机器人方案.md §4.2）的客户区控件树与尺寸常量。
 *  2026-10-08 起资料卡由系统消息框改成应用风格的小窗（用户确认）。控件树单独成文件，窗口与布局单测
 *  （Source/Tests/BotCardLayoutTests.cpp，标签 [bot]）共用。
 *
 *  从上到下：头部（头像、名字，名字下方一行「机器人」徽标与「企业」「已停用」「本群已关闭」标签）、分隔线、
 *  详情（「简介」「用法」「来源」三段，内容多时可以滚动）、按钮行（右侧「关闭」）。
 *  窗口宽度固定，高度按内容定：FitBotCardBody 按实际字体量出各段换行后的高度，BotCardWindowHeight 再限制在上下限之间，
 *  超过上限的部分由详情区滚动显示。
 *
 *  典型用法：
 *      SetClientContent(balloonchatwnd::BuildBotCardBody(view));
 *      const int bodyH = balloonchatwnd::FitBotCardBody(GetClientContent(), kBotCardWindowWidth);
 *      ResizeClient(kBotCardWindowWidth, balloonchatwnd::BotCardWindowHeight(bodyH));
 *      balloonchatwnd::RelayoutBotCardBody(GetClientContent());
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace balloonwjui { class DuiControl; }

namespace balloonchatwnd {

// 标题栏高（像素）
const int kBotCardTitleBarH = 32;
// 整窗宽（像素，含标题栏所在的整窗，DuiFrameWindow::ResizeClient 设的就是整窗）
const int kBotCardWindowWidth = 360;
// 整窗高的下限（像素，含标题栏）：内容很少时也不低于这个高度
const int kBotCardMinWindowHeight = 280;
// 整窗高的上限（像素，含标题栏）：内容更多时详情区出现滚动条
const int kBotCardMaxWindowHeight = 560;

// 资料卡里各控件的 ctrlId
enum BotCardCtrlId
{
    // 头部一行（DuiHBox）：头像、名字与徽标
    kBotCardIdHeader = 1,
    // 名字（DuiLabel，单行，过长时以省略号结尾）
    kBotCardIdName = 2,
    // 「机器人」徽标（DuiBadge）
    kBotCardIdBotBadge = 3,
    // 「企业」「已停用」「本群已关闭」标签（DuiBadge）；没有标签时不建
    kBotCardIdTag = 4,
    // 详情区的滚动视图（DuiScrollView）
    kBotCardIdScroll = 5,
    // 详情区的内容（DuiVBox，滚动视图的内容控件）
    kBotCardIdDetail = 6,
    // 简介正文（DuiLabel，自动换行）；没有简介时不建
    kBotCardIdDesc = 7,
    // 「暂无可用的触发方式」提示（DuiLabel）；有用法时不建
    kBotCardIdNoUsage = 8,
    // 来源正文（DuiLabel，自动换行）
    kBotCardIdSource = 9,
    // 按钮行（DuiHBox），布局单测用它检查按钮没有被挤出窗口
    kBotCardIdButtonRow = 10,
    // 「关闭」按钮
    kBotCardIdClose = 11,
    // 用法说明的第一行（DuiLabel，自动换行）；第 i 行的 id 为 kBotCardIdUsageFirst + i
    kBotCardIdUsageFirst = 100
};

/**
 *  资料卡要显示的内容，由 GroupBotModel 的 MakeBotCardView 从 2801 下发的机器人信息换算而来。
 */
struct BotCardView
{
    std::wstring              m_name;         // 名称，如「IT助手」
    std::wstring              m_avatarText;   // 头像上的字；空串时取名称的第一个字
    COLORREF                  m_avatarColor;  // 头像底色
    std::wstring              m_tag;          // 名字下方「机器人」徽标后面的标签，如「企业」；空串表示没有
    std::wstring              m_desc;         // 简介；空串时不显示「简介」一段
    std::vector<std::wstring> m_usageLines;   // 用法说明，每项一句；为空时显示「暂无可用的触发方式」
    std::wstring              m_source;       // 来源，如「企业管理员配置」「群主配置」

    BotCardView()
        : m_avatarColor(RGB(0x2D, 0x6C, 0xDF))
    {
    }
};

/**
 *  建资料卡的客户区控件树（标题栏以下）。各段自动换行文字的高度先按默认字体量一次，
 *  控件挂到窗口之后要再调 FitBotCardBody 按窗口的实际字体重新量。
 *    view：要显示的内容，只读。
 *  @return 控件树根节点，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildBotCardBody(const BotCardView& view);

/**
 *  按窗口宽度重新量详情区里各段自动换行文字的高度并写回布局，返回客户区（标题栏以下）完整显示全部内容需要的高度。
 *    root：       BuildBotCardBody 建出的根节点，不持有；为 NULL 或不是资料卡的控件树时返回 0。
 *    windowWidth：整窗宽（像素），一般为 kBotCardWindowWidth。
 *  @return 客户区需要的高度（像素）
 *  副作用：修改详情区各行的布局参数，之后须重新排版（RelayoutBotCardBody）。
 */
int FitBotCardBody(balloonwjui::DuiControl* root, int windowWidth);

/**
 *  由客户区需要的高度算整窗高：加上标题栏，再限制在 kBotCardMinWindowHeight 与 kBotCardMaxWindowHeight 之间。
 *    bodyHeight：FitBotCardBody 的返回值（像素）。
 *  @return 整窗高（像素）
 */
int BotCardWindowHeight(int bodyHeight);

/**
 *  按根节点、详情区滚动视图、详情内容当前的矩形逐层重新排版。矩形没变时上一层的排版不会往下排
 *  （开发约定文档 UI 约定第 5、12 条），所以 FitBotCardBody 改了布局参数、或窗口尺寸没变时都要调用。
 *    root：BuildBotCardBody 建出的根节点，不持有；为 NULL 时什么都不做。
 */
void RelayoutBotCardBody(balloonwjui::DuiControl* root);

} // namespace balloonchatwnd
