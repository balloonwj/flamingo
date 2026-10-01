/**
 *  ChatFileCard.h
 *
 *  文件卡片：聊天窗文件气泡里那张卡片（类型色块、文件名、"大小 · 状态"、进度条、底部操作
 *  链接）的尺寸计算、绘制与点击判断。聊天气泡（ChatBubble）与聊天记录查看窗里的文件行
 *  共用这一份，两处外观与行为一致。卡片底色由调用方画（气泡按收发方向取色），本文件只画
 *  卡片里的内容。
 *
 *  绘制分两层，与气泡的绘制顺序对应：先用 GDI+ 画图形（色块、角标、进度条），再用 GDI
 *  画文字（类型字、文件名、状态行、操作链接）。文字层同时排出各操作链接的点击矩形，调用方
 *  保存下来，鼠标按下时交给 FileCardLinkAt 判断点中了哪一个。
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

#include "ChatMockData.h"         // ChatMessage
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
 *  按当前状态算卡片高度（逻辑像素）：头部区，有进度时加进度条，有操作时加按钮行。
 *  @param m 文件消息（取 fileState 与 FileCardSkipsReject 的结果）
 *  @return 卡片高度
 */
int FileCardHeight(const ChatMessage& m);

//卡片底部各操作链接的排版结果：由 PaintFileCardText 写出，供点击判断与悬停反馈使用。
struct FileCardLinks
{
    int  m_count;                          //链接个数，0 ~ kMaxChatFileActions
    RECT m_rect[kMaxChatFileActions];      //各链接的点击矩形（与绘制坐标同一坐标系），整行高度可点
    int  m_action[kMaxChatFileActions];    //各链接的动作，取值见 ChatFileActionKind

    FileCardLinks();
};

/**
 *  画卡片的图形部分：类型色块（已删除时为灰色并画垃圾桶）、状态角标、进度条。
 *  @param g    目标 GDI+ 画布（调用方已设好抗锯齿）
 *  @param card 卡片矩形，宽应为 kFileCardWidth、高应为 FileCardHeight(m)
 *  @param m    文件消息
 */
void PaintFileCardShapes(Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m);

/**
 *  画卡片的文字部分：类型字、文件名、"大小 · 状态"（失败与已删除前带小图标）、底部操作
 *  链接，并写出各链接的点击矩形。
 *  @param hdc      目标设备上下文
 *  @param g        同一块画布上的 GDI+ 画布（状态行前的小图标用它画）
 *  @param card     卡片矩形，与 PaintFileCardShapes 收到的相同
 *  @param m        文件消息
 *  @param hoverIdx 鼠标悬停的链接下标；-1 表示没有悬停。悬停的链接颜色加深并加下划线
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
