/**
 *  聊天记录管理器「本机存储占用」页的右侧界面，HistoryStorageLayout.h
 *
 *  点左栏最下面的「本机存储占用」后，分栏右侧显示本页（docs/聊天记录管理器方案.md §12.6）：
 *      标题区     [»] 本机存储占用        只统计聊天里的图片、文件、语音在本机的缓存
 *      汇总行     缓存合计 1.2 GB    图片 800 MB · 文件 380 MB · 语音 20 MB · 聊天记录库 46 MB
 *      按钮行     [重新统计]  [清理全部会话的缓存]
 *      列表表头   会话 | 图片 | 文件 | 语音 | 合计 |
 *      列表       每个会话一行，按合计倒序，行尾「清理缓存」
 *  本文件只建控件、做局部更新，不碰数据与窗口；窗口（BalloonHistoryWindowStorage.cpp）把统计结果翻译成
 *  HistoryStorageViewData 交给这里。控件编号在 kIdStor* 段（8400 起），接在文件页「复制」按钮段之后。
 *  布局单测见 Tests/HistoryStorageTests.cpp（标签 [histstorage]）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonhistory {

//会话行最多显示多少个（编号段的容量）；更多的会话不列出，页面上注明
const int kIdStorRowCapacity = 200;

//本机存储占用页的控件编号
enum HistoryStorageCtrlId
{
    kIdStorBody       = 8400,   //整个右侧界面的根容器（与聊天记录页、文件页互相替换显示）
    kIdStorHeaderRow  = 8401,   //标题区那一行；「»」显隐变化后按它重排
    kIdStorExpandLeft = 8402,   //标题区最左边的「»」：左栏折叠后出现，点它重新展开左栏
    kIdStorTitle      = 8403,   //标题「本机存储占用」
    kIdStorSubtitle   = 8404,   //标题右侧的灰色说明
    kIdStorSummary    = 8405,   //汇总行的文字（缓存合计与各类分计）
    kIdStorRefresh    = 8406,   //「重新统计」
    kIdStorCleanAll   = 8407,   //「清理全部会话的缓存」
    kIdStorButtonRow  = 8408,   //按钮行容器
    kIdStorScroll     = 8410,   //列表滚动视口
    kIdStorList       = 8411,   //滚动视口里的内容容器
    kIdStorEmpty      = 8412,   //列表为空、正在统计或正在清理时的提示文字
    kIdStorRowBase    = 8500,   //第 i 个会话行的编号 = 基址 + i（i < kIdStorRowCapacity）
    kIdStorCleanBase  = 8700,   //第 i 个会话行「清理缓存」按钮的编号 = 基址 + i
};

//一个会话行显示什么
struct HistoryStorageRowView
{
    CString m_name;         //会话名
    CString m_imageText;    //图片占用（如「12.3 MB」）；没有时为「-」
    CString m_fileText;     //文件占用
    CString m_voiceText;    //语音占用
    CString m_totalText;    //合计
};

//整页渲染数据
struct HistoryStorageViewData
{
    bool                               m_showExpandLeft;   //是否显示「»」（左栏折叠时）
    CString                            m_subtitle;         //标题右侧的灰色说明
    CString                            m_summary;          //汇总行文字
    bool                               m_busy;             //正在统计或正在清理：两个按钮不可点，列表显示 m_emptyText
    bool                               m_canCleanAll;      //「清理全部会话的缓存」是否可点（有缓存且不忙时）
    std::vector<HistoryStorageRowView> m_rows;             //会话行，按合计倒序
    CString                            m_emptyText;        //列表为空、正在统计或正在清理时的提示

    HistoryStorageViewData()
        : m_showExpandLeft(false)
        , m_busy(false)
        , m_canCleanAll(false)
    {
    }
};

/**
 *  建本机存储占用页的整套右侧界面。
 *  @param data              整页渲染数据
 *  @param listRightInsetPx  列表滚动视口右侧给窗口缩放边框让出的宽度（物理像素，同 BuildHistoryLibraryBody）
 *  @return 根容器（id = kIdStorBody），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryStorageBody(const HistoryStorageViewData& data,
                                                                 int listRightInsetPx = 0);

/**
 *  建列表内容（滚动视口的内容）：表头与会话行；忙或没有会话时是一行提示文字。统计结果到达、清理完成时用它换掉
 *  滚动视口的内容。
 *  @param data  整页渲染数据
 *  @return 内容容器（id = kIdStorList），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryStorageList(const HistoryStorageViewData& data);

/**
 *  局部更新：标题区「»」、说明、汇总行、两个按钮的可用状态，并换掉列表内容。
 *  @param root  本页根容器或它的祖先（不持有），可为 NULL
 *  @param data  整页渲染数据
 */
void ApplyHistoryStorageData(balloonwjui::DuiControl* root, const HistoryStorageViewData& data);

} // namespace balloonhistory
