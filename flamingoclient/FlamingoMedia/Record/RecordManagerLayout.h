/**
 *  录制管理窗口的控件树与尺寸，RecordManagerLayout.h
 *
 *  录制管理窗口（docs/屏幕共享与录制方案.md 第 11.6 节 F9）的客户区：顶部一行操作按钮（播放、打开所在文件夹、发送到会话…、
 *  重命名、删除）与右侧的「共 N 个」，下面是录像列表（CRecordManagerList，占满剩余高度）；没有录像时列表换成一行提示。
 *  语音字幕可用时，删除之后还有一个「编辑字幕」（默认隐藏，由窗口按字幕是否可用显示）。
 *  按钮宽度按当前界面语言的译文实际字宽计算，窗口最小宽度据此算出（RecordManagerMinWidth）。
 *  第 27 期 27d 起最上面多一行「本机 / 云端」页签：上面说的工具栏与列表是「本机」页；「云端」页的工具栏为播放、下载…、
 *  发送到会话…、重命名、删除，右侧是用量条与用量文字，下面是云端录像列表（每行右侧有状态列）、没有录像时的提示与「加载更多」。
 *  本文件只负责搭控件树，数据与操作在 RecordManagerWindow 里。布局单测见 Source/Tests/RecordManagerLayoutTests.cpp
 *  （标签 [recordmgr-layout]）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      screenrecord::RecordManagerBody body = screenrecord::BuildRecordManagerBody();
 *      m_list = body.m_list;
 *      SetClientContent(std::move(body.m_root));
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include <string>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiLabel;
    class DuiProgressBar;
    class DuiTab;
}

namespace screenrecord {

class CRecordManagerList;

//---- 录制管理窗口的尺寸（逻辑像素）----
//标题栏高
const int kRecordMgrTitleBarH = 32;
//默认整窗宽、高（含标题栏）
const int kRecordMgrDefaultW = 720;
const int kRecordMgrDefaultH = 480;
//最小整窗高（含标题栏）；最小整窗宽按按钮字宽算，见 RecordManagerMinWidth
const int kRecordMgrMinH = 320;
//列表行高与行图标边长
const int kRecordMgrRowH = 40;
const int kRecordMgrIconPx = 20;
//客户区四周的内边距、各行之间的间距
const int kRecordMgrPadding = 16;
const int kRecordMgrGap = 10;
//工具栏按钮之间的间距
const int kRecordMgrButtonGap = 8;
//工具栏按钮的高度
const int kRecordMgrButtonH = 32;
//「本机 / 云端」页签行的高度（第 27 期 27d）
const int kRecordMgrTabH = 32;
//云端页右上角用量条的宽、高（像素）
const int kRecordMgrUsageBarW = 90;
const int kRecordMgrUsageBarH = 6;
//云端页「加载更多」一行的高度
const int kRecordMgrMoreH = 28;
//两个页签的下标
const int kRecordMgrPageLocal = 0;
const int kRecordMgrPageCloud = 1;

//控件 id：窗口按它分发通知，单元测试按它找控件。从 31 起取，避开 DuiFrameWindow 标题栏按钮占用的 1~3
enum RecordMgrCtrlId
{
    kRecordMgrIdPlay       = 31,   //「播放」
    kRecordMgrIdOpenFolder = 32,   //「打开所在文件夹」
    kRecordMgrIdSend       = 33,   //「发送到会话…」
    kRecordMgrIdRename     = 34,   //「重命名」
    kRecordMgrIdDelete     = 35,   //「删除」；选中行的文件已不存在时文字换成「从列表移除」
    kRecordMgrIdCount      = 36,   //右侧「共 N 个」
    kRecordMgrIdList       = 37,   //录像列表
    kRecordMgrIdEmpty      = 38,   //没有录像时代替列表显示的提示
    kRecordMgrIdRenameEdit = 39,   //原地重命名的输入框（由 CRecordManagerList::BeginEdit 创建）
    kRecordMgrIdCaption    = 40,   //「编辑字幕」（语音字幕可用时才显示）
    kRecordMgrIdTabs       = 41,   //「本机 / 云端」页签（第 27 期 27d）
    kRecordMgrIdCloudPlay  = 42,   //云端页「播放」
    kRecordMgrIdCloudDownload = 43,   //云端页「下载…」
    kRecordMgrIdCloudSend  = 44,   //云端页「发送到会话…」
    kRecordMgrIdCloudRename = 45,  //云端页「重命名」
    kRecordMgrIdCloudDelete = 46,  //云端页「删除」
    kRecordMgrIdCloudUsage = 47,   //云端页右上角的用量文字
    kRecordMgrIdCloudList  = 48,   //云端录像列表
    kRecordMgrIdCloudEmpty = 49,   //云端页没有录像（或企业没有开通）时代替列表显示的提示
    kRecordMgrIdCloudMore  = 50,   //云端页「加载更多」
    kRecordMgrIdCloudRenameEdit = 51,   //云端页原地重命名的输入框
    kRecordMgrIdCloudUsageBar = 52,     //云端页右上角的用量条
};

//BuildRecordManagerBody 的结果：控件树的根与窗口要操作的几个控件（借用，归控件树所有）
struct RecordManagerBody
{
    std::unique_ptr<balloonwjui::DuiControl> m_root;   //客户区内容的根，交给 SetClientContent
    CRecordManagerList*     m_list;         //录像列表
    balloonwjui::DuiLabel*  m_empty;        //没有录像时的提示（默认隐藏）
    balloonwjui::DuiLabel*  m_count;        //「共 N 个」
    balloonwjui::DuiButton* m_play;         //「播放」
    balloonwjui::DuiButton* m_openFolder;   //「打开所在文件夹」
    balloonwjui::DuiButton* m_send;         //「发送到会话…」
    balloonwjui::DuiButton* m_rename;       //「重命名」
    balloonwjui::DuiButton* m_delete;       //「删除 / 从列表移除」
    balloonwjui::DuiButton* m_caption;      //「编辑字幕」（默认隐藏）
    //第 27 期 27d：页签与两个页面
    balloonwjui::DuiTab*     m_tabs;         //「本机 / 云端」页签
    balloonwjui::DuiControl* m_localPage;    //本机页（上面这些控件的容器）
    balloonwjui::DuiControl* m_cloudPage;    //云端页（默认隐藏）
    CRecordManagerList*      m_cloudList;    //云端录像列表（带状态列）
    balloonwjui::DuiLabel*   m_cloudEmpty;   //云端页的提示（默认隐藏）
    balloonwjui::DuiLabel*   m_cloudUsage;   //用量文字
    balloonwjui::DuiProgressBar* m_cloudUsageBar;   //用量条
    balloonwjui::DuiButton*  m_cloudPlay;    //「播放」
    balloonwjui::DuiButton*  m_cloudDownload;   //「下载…」
    balloonwjui::DuiButton*  m_cloudSend;    //「发送到会话…」
    balloonwjui::DuiButton*  m_cloudRename;  //「重命名」
    balloonwjui::DuiButton*  m_cloudDelete;  //「删除」
    balloonwjui::DuiButton*  m_cloudMore;    //「加载更多」（默认隐藏）

    RecordManagerBody();
};

/**
 *  搭录制管理窗口的客户区控件树。五个按钮初始都不可用（还没有选中行），提示默认隐藏、「共 N 个」写 0。
 *  @return 控件树与各控件的指针
 */
RecordManagerBody BuildRecordManagerBody();

/**
 *  录制管理窗口的最小整窗宽：本机页工具栏六个按钮（含「编辑字幕」）与「共 N 个」、云端页工具栏五个按钮与用量条、用量文字，
 *  都按当前界面语言的字宽排得下，且不小于默认宽度的三分之二。
 */
int RecordManagerMinWidth();

/**
 *  云端页状态列的宽度（像素）：按当前界面语言放得下最长的状态文字（如「上传中 100%」「没有录到内容」）。
 */
int RecordManagerTrailingWidth();

/**
 *  「共 N 个」的文字。
 *  @param count 录像条数
 */
std::wstring RecordManagerCountText(int count);

} // namespace screenrecord
