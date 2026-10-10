/**
 *  GroupLinkLayout.h
 *
 *  「生成加群链接」窗口（docs/邀请入群方案.md §12）的客户区控件树与尺寸常量。控件树单独成文件，
 *  窗口（GroupLinkDialog）与布局单测（Source/Tests/GroupLinkTests.cpp 的 GL-C4、GL-C7）共用。
 *
 *  从上到下：群名、说明（两行）、「有效期」下拉与「生成链接」按钮一行、链接文字、状态行（到期时间、失败原因、
 *  作废结果）、「我生成的链接」标题与列表（滚动视图，占满余下的高度）、按钮行（「复制链接」「关闭」）。
 *  「我生成的链接」2026-10-07 补上：普通成员进不了群管理窗，在这里查看并作废自己生成的链接。
 *
 *  典型用法：
 *      SetClientContent(balloonmain::BuildGroupLinkBody());
 *      ResizeClient(kGroupLinkWindowWidth, kGroupLinkWindowHeight);
 *      //收到 1341 应答、ApplyMyGroupLinks 之后：
 *      balloonmain::FillMyGroupLinkList(GetClientContent(), m_myLinks);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stddef.h>
#include <memory>

#include "GroupLinkFlow.h"   // MyGroupLinkList、kMyGroupLinkRowMax

namespace balloonwjui { class DuiControl; }

namespace balloonmain {

// 标题栏高（逻辑像素）
const int kGroupLinkTitleBarH = 32;
// 整窗宽（逻辑像素）
const int kGroupLinkWindowWidth = 440;
// 整窗高（逻辑像素，含标题栏）。加了「我生成的链接」之后由 330 改为 500，列表区域约能完整显示 3 行
const int kGroupLinkWindowHeight = 500;
// 「我生成的链接」列表区域至少要能完整显示的行数，布局单测按它检查窗口高度够不够
const int kGroupLinkMinVisibleRows = 3;

// 窗口里各控件的 ctrlId
enum GroupLinkCtrlId
{
    // 群名（DuiLabel）
    kGroupLinkIdGroupName = 1,
    // 说明（DuiLabel，两行）
    kGroupLinkIdHint = 2,
    // 有效期下拉（DuiComboBox）：1 天 / 7 天 / 永久有效
    kGroupLinkIdValidity = 3,
    // 「生成链接」按钮
    kGroupLinkIdCreate = 4,
    // 链接文字（DuiLabel，单行，放不下时中间省略）
    kGroupLinkIdUrl = 5,
    // 状态行（DuiLabel）：到期时间、正在生成、失败原因
    kGroupLinkIdStatus = 6,
    // 「复制链接」按钮：生成之前不可用
    kGroupLinkIdCopy = 7,
    // 「关闭」按钮
    kGroupLinkIdClose = 8,
    // 按钮行（DuiHBox），布局单测用它检查按钮没有被挤出窗口
    kGroupLinkIdButtonRow = 9,
    // 「我生成的链接」标题（DuiLabel）
    kGroupLinkIdMyLinksTitle = 10,
    // 「我生成的链接」列表的滚动视图（DuiScrollView），占满余下的高度
    kGroupLinkIdMyLinksScroll = 11,
    // 列表里没有链接可列时的占位文字（DuiLabel）：正在加载、没有加载出来、还没有生成过
    kGroupLinkIdMyLinksPlaceholder = 12,
    // 列表第 i 行（DuiVBox 卡片，浅底圆角，里面横排链接文字与两个按钮）的 ctrlId 为 kGroupLinkIdRowBase + i
    kGroupLinkIdRowBase = 100,
    // 列表第 i 行「复制」按钮的 ctrlId 为 kGroupLinkIdRowCopyBase + i
    kGroupLinkIdRowCopyBase = kGroupLinkIdRowBase + kMyGroupLinkRowMax,
    // 列表第 i 行「作废」按钮的 ctrlId 为 kGroupLinkIdRowRevokeBase + i
    kGroupLinkIdRowRevokeBase = kGroupLinkIdRowCopyBase + kMyGroupLinkRowMax
};

/**
 *  建窗口的客户区控件树（标题栏以下）。有效期下拉已填好三项并选中默认的 7 天；「我生成的链接」列表处
 *  显示「正在加载…」。
 *  @return 控件树根节点，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildGroupLinkBody();

/**
 *  按列表数据重建「我生成的链接」的内容：每行左边是链接文字与「生成时间 · 到期说明」，右边是「复制」「作废」；
 *  没有链接可列时显示占位文字。重建后更新滚动范围，并对滚动视图强制重排一次，使新加的行能被点中
 *  （开发约定文档 UI 约定 12）。旧的行控件会被销毁，因此不能在行内按钮自己的点击通知里调用（UI 约定 13）。
 *    root：BuildGroupLinkBody 建出的控件树根（借用，不转移所有权）；为空或找不到列表时什么都不做；
 *    list：列表数据。
 */
void FillMyGroupLinkList(balloonwjui::DuiControl* root, const MyGroupLinkList& list);

/**
 *  列表内容的总高度（逻辑像素）：rowCount 行加上行间距；为 0 时是占位文字一行的高度。
 *  FillMyGroupLinkList 按它设置滚动范围，布局单测按它检查列表区域能显示几行。
 */
int MyGroupLinkListContentHeight(size_t rowCount);

} // namespace balloonmain
