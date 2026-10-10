/**
 *  GroupBotSection.h
 *
 *  群聊窗成员页签顶部「机器人 N」区域的重建与重排（docs/机器人方案.md §4.2）。群聊窗算好要显示的内容（标题、
 *  各行文字）后交给这里：按 id 找到区域里的控件，重建各行、设置区域高度，再逐层重排。2026-10-08 起这一栏只用来查看，
 *  添加与管理群机器人的入口在群管理窗口的「群机器人」页（用户确认），那一页的机器人行也用本文件的 BuildGroupBotRow。
 *  单独成文件是为了让 FlamingoUnit 不建群聊窗也能覆盖「先排过版、再换一批内容」这条路径
 *  （Tests/GroupBotSectionTests.cpp，标签 [bot]）。
 *
 *  2026-10-08 实机联调发现：区域高度不变时（如群主「在本群关闭」后行数不变），只对成员容器 ForceLayout 不会让
 *  区域里新建的行排版，那一行矩形为空、显示不出来（开发约定文档 UI 约定第 5 条：ForceLayout 只作用于它那一层）。
 *  ApplyGroupBotSection 因此对成员容器、区域本身、行容器逐层 ForceLayout。
 *
 *  典型用法（群聊窗）：
 *      balloonchatwnd::GroupBotSectionView view;
 *      view.m_show = true;
 *      view.m_title = L"机器人 1";
 *      view.m_rows.push_back(balloonchatwnd::GroupBotSectionRow(L"IT助手（企业）", kGIdBotRowFirst));
 *      balloonchatwnd::ApplyGroupBotSection(GetClientContent(), view);
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

//区域里每个机器人一行的高度（像素），与群成员行 kGroupMemberRowHeight 一致（GroupBotSection.cpp 里有 static_assert）
const int kBotSectionRowHeight = 38;
//标题行「机器人 N」的高度（像素）
const int kBotSectionTitleHeight = 20;
//区域上下的内边距（像素），与 GroupChatWindowLayout 里区域容器的 padding 一致
const int kBotSectionPadV = 6;
//标题与各行、行与行之间的间距（像素），与区域容器的 gap 一致
const int kBotSectionGap = 2;
//机器人没有设置头像底色、调用方也没有给出时的默认底色（品牌蓝，与 FlamingoBot.dll 的 kBotDefaultAvatar 一致）
const COLORREF kBotSectionDefaultAvatarColor = RGB(0x2D, 0x6C, 0xDF);

/**
 *  区域里的一行：一个机器人。2026-10-08 起与成员行同一样式：头像、名字、右侧小标签（用户确认）。
 */
struct GroupBotSectionRow
{
    std::wstring m_text;         //名字，如「IT助手」
    UINT         m_ctrlId;       //控件 id（kGIdBotRowFirst 起）；群聊窗凭它知道点的是第几个机器人
    std::wstring m_avatarText;   //头像上的字；空串时取名字的第一个字
    COLORREF     m_avatarColor;  //头像底色
    std::wstring m_tag;          //右侧小标签，如「企业」「已停用」「本群已关闭」；空串表示没有标签

    GroupBotSectionRow()
        : m_ctrlId(0)
        , m_avatarColor(kBotSectionDefaultAvatarColor)
    {
    }

    GroupBotSectionRow(const std::wstring& text, UINT ctrlId)
        : m_text(text)
        , m_ctrlId(ctrlId)
        , m_avatarColor(kBotSectionDefaultAvatarColor)
    {
    }
};

/**
 *  区域要显示的内容。
 */
struct GroupBotSectionView
{
    bool                            m_show;        //是否显示整个区域；为 false 时区域高度为 0 并隐藏
    std::wstring                    m_title;       //标题，如「机器人 2」
    std::vector<GroupBotSectionRow> m_rows;        //各行；m_show 为 false 时忽略

    GroupBotSectionView()
        : m_show(false)
    {
    }
};

/**
 *  区域的总高度：上下内边距 + 标题行 + 间距 + 各行（行与行之间也有间距）。
 *    rowCount：行数，不小于 0。
 *  @return 高度（像素）
 */
int GroupBotSectionHeight(int rowCount);

/**
 *  小标签（DuiBadge）按文字排得下需要的宽度：按 balloonui 默认字体量出文字宽度，再加上 DuiBadge 在文字两侧留的空白与余量。
 *  DuiBadge 没有报告期望尺寸，放进水平布局时要用这个宽度给它固定宽度。机器人资料卡的徽标用它定宽。
 *    text：标签文字。
 *  @return 宽度（像素）；空串返回 0
 */
int BotBadgeWidth(const std::wstring& text);

/**
 *  建一行机器人：头像、名字、右侧小标签，样式与群成员行一致；整行接收点击，点了向父控件发 DUIN_CLICK（控件 id 为 r.m_ctrlId）。
 *  成员页签的「机器人」区域与群管理窗口的「群机器人」页共用。
 *    r：这一行的内容，只读。
 *  @return 行控件（DuiHBox 子类），所有权交给调用方；放进竖直布局时用固定高度 kBotSectionRowHeight
 */
std::unique_ptr<balloonwjui::DuiControl> BuildGroupBotRow(const GroupBotSectionRow& r);

/**
 *  按 view 重建区域并逐层重排：摘掉旧行、按 view.m_rows 建新行（头像 + 名字 + 小标签，整行可点），设置标题、
 *  区域与行容器的高度，再对成员容器、区域、行容器依次 ForceLayout 并请求重画。
 *    root：在其下按 id（kGIdBotSection 等）查找控件的根，不持有；为 NULL 或找不到控件时什么都不做。
 *    view：要显示的内容，只读。
 *  @return 找到了全部控件并完成重建时返回 true
 */
bool ApplyGroupBotSection(balloonwjui::DuiControl* root, const GroupBotSectionView& view);

} // namespace balloonchatwnd
