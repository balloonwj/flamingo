/**
 *  ChatFileCard.h
 *
 *  文件卡片：聊天窗文件气泡里那张卡片（白色底板、文档图标、文件名、大小与状态标签、进度条、底部一行）的尺寸计算、
 *  绘制与点击判断。聊天气泡（ChatBubble）与消息记录窗、聊天记录管理器、聊天记录查看窗里的文件行共用这一份，几处外观
 *  与行为一致。2026-10-14 起按聊天卡片美化方案换成新版式（docs/聊天卡片美化方案.md 5.1），卡片自己画白色底板，调用方
 *  不再画底色。
 *
 *  绘制分两层，与气泡的绘制顺序对应：先用 GDI+ 画图形（底板、文档图标、进度条），再画文字与底部一行（类型字、文件名、
 *  大小、状态标签、进度文字、类型标签与按钮）。第二层同时排出各按钮的点击矩形，调用方保存下来，鼠标按下时交给
 *  FileCardLinkAt 判断点中了哪一个。
 *
 *  典型用法：
 *      RECT card = { x, y, x + balloonchatwnd::kFileCardWidth, y + FileCardHeight(msg) };
 *      PaintFileCardShapes(g, card, msg);
 *      PaintFileCardText(hdc, g, card, msg, hoverIndex, links);
 *      ...
 *      int idx = FileCardLinkAt(links, pt);   // -1 表示没点在链接上
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL；调用绘制函数的 .cpp
 *  要自己包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <vector>

#include "ChatMockData.h"         // ChatMessage
#include "ChatCardModel.h"        // ChatCardButton
#include "ChatFileStateModel.h"   // kMaxChatFileActions / 文件状态取值

//只用到 Graphics 的引用，前置声明即可，包含本文件的地方不必先包含 <gdiplus.h>
namespace Gdiplus {
class Graphics;
}

namespace balloonchatwnd {

//文件卡片的固定宽度（逻辑像素），与聊天窗文件气泡同宽
const int kFileCardWidth = 300;

/**
 *  这张卡片的待接收态是否省掉「拒绝」、只给一个「下载」。自己发出的文件谈不上拒绝，
 *  群聊与聊天记录里的文件（fileDirectDownload）没有接收流程，三处（高度、按钮动作、按钮
 *  文案）都取这里的结果。
 *  @param m 文件消息
 *  @return 省掉「拒绝」时返回 true
 */
bool FileCardSkipsReject(const ChatMessage& m);

/**
 *  这张卡片的「重试」是重新发送还是重新下载。重发需要与"发送中"气泡对回的 clientMsgId
 *  以及仍在原处的本地源文件，只有本端刚发出的那笔同时具备；其余只能从服务器重新下载。
 *  @param m 文件消息
 *  @return 可以重新发送时返回 true
 */
bool FileCardCanResend(const ChatMessage& m);

/**
 *  按当前状态算卡片高度（逻辑像素）：头部区与卡片底部（含常驻的底部一行），有进度时加进度块。
 *  @param m 文件消息（取 fileState 与 FileCardSkipsReject 的结果）
 *  @return 卡片高度
 */
int FileCardHeight(const ChatMessage& m);

/**
 *  卡片底部某个按钮的文字（已经过 TR 翻译）。绘制与单测共用这一处，按钮文字只在这里定义。
 *  @param m   文件消息（「接收」与「下载」、「重新发送」与「重新下载」要看它）
 *  @param act 链接的动作，取值见 ChatFileActionKind
 *  @return 链接文字；未知动作按「重试」处理
 */
CString FileCardLinkLabel(const ChatMessage& m, int act);

/**
 *  某个动作的按钮级别（docs/聊天卡片美化方案.md 5.1）：接收 / 下载、发送离线文件为主要，打开文件、重新发送 / 重新下载为
 *  查看，其余（打开所在文件夹、另存为、取消、拒绝）为次要。
 *  @param act 动作，取值见 ChatFileActionKind
 *  @return 级别，取值见 ChatCardButtonLevel
 */
int FileCardActionLevel(int act);

/**
 *  卡片底部的按钮列表，按显示顺序从左到右：次要在左、主要在最右（ChatCardButtonsOf 对文件卡片转调这里）。
 *  @param m 文件消息
 *  @return 按钮列表；m_action 为 ChatFileActionKind 的取值；没有按钮时为空
 */
std::vector<ChatCardButton> FileCardButtons(const ChatMessage& m);

/**
 *  卡片副标题行里状态标签的文字与语义（docs/聊天卡片美化方案.md 5.1，2026-10-14 起）。规则与原来的状态行相同：先按 fileState
 *  定，在线直传有自己的写法，本端发出的文件只要对方有过动作就改说对方那边的进展；传输中的标签不带百分比（百分比画在
 *  进度条下面那一行）。
 *  @param m    文件消息
 *  @param text [出参] 标签文字（已翻译）
 *  @param tone [出参] 标签语义，取值见 ChatCardChipTone
 */
void FileCardStatus(const ChatMessage& m, CString& text, int& tone);

//卡片底部各按钮的排版结果：由 PaintFileCardText 写出，供点击判断与悬停反馈使用。
struct FileCardLinks
{
    int  m_count;                          //链接个数，0 ~ kMaxChatFileActions
    RECT m_rect[kMaxChatFileActions];      //各按钮的点击矩形（与绘制坐标同一坐标系），按显示顺序从左到右
    int  m_action[kMaxChatFileActions];    //各链接的动作，取值见 ChatFileActionKind

    FileCardLinks();
};

/**
 *  画卡片的图形部分：白色底板、文档图标（本机没有文件时为灰色，服务器上已删除时再画垃圾桶）、进度条（品牌橙）。
 *  @param g    目标 GDI+ 画布（调用方已设好抗锯齿）
 *  @param card 卡片矩形，宽应为 kFileCardWidth、高应为 FileCardHeight(m)
 *  @param m    文件消息
 */
void PaintFileCardShapes(Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m);

/**
 *  画卡片的文字与底部一行：类型字、文件名、大小、状态标签与「P2P」徽章、进度文字、类型标签与按钮，并写出各按钮的
 *  点击矩形。
 *  @param hdc      目标设备上下文
 *  @param g        同一块画布上的 GDI+ 画布（状态标签与按钮的底用它画）
 *  @param card     卡片矩形，与 PaintFileCardShapes 收到的相同
 *  @param m        文件消息
 *  @param hoverIdx 鼠标悬停的按钮下标；-1 表示没有悬停
 *  @param links    [出参] 各操作链接的点击矩形与动作
 */
void PaintFileCardText(HDC hdc, Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m,
                       int hoverIdx, FileCardLinks& links);

/**
 *  判断一个点落在哪个操作链接上。
 *  @param links PaintFileCardText 写出的排版结果
 *  @param pt    与绘制坐标同一坐标系的点
 *  @return 链接下标（0 起）；没落在任何链接上时返回 -1
 */
int FileCardLinkAt(const FileCardLinks& links, POINT pt);

} // namespace balloonchatwnd
