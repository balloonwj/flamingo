/**
 *  ChatCardModel.h
 *
 *  聊天卡片的纯逻辑部分（docs/聊天卡片美化方案.md 第 3、5 节）：一条消息属于哪种卡片、该用什么类型色与图标、
 *  底部类型标签写什么。绘制代码（ChatBubble、ChatCardPaint）只照这里给出的结论画，不在绘制时另做判断。
 *
 *  不碰窗口、不碰 GDI，可被 FlamingoUnit 测试工程直接调用（用例 [cardstyle]）。典型用法：
 *      const ChatCardTypeStyle style = ChatCardTypeStyleOf(msg);
 *      if (style.m_type != kCardTypeNone) { ... 按 style.m_tileFill / style.m_glyphColor 画图标块 ... }
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（要用 CString）。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <vector>

#include "ChatMockData.h"   // ChatMessage

namespace balloonchatwnd {

/**
 *  卡片种类。比 ChatMsgKind 细：kMsgScreenShare 一种消息里有屏幕共享、一对一通话、群通话、群公告、群备忘、
 *  云端录像六种卡片，样式各不相同。
 */
enum ChatCardType
{
    kCardTypeNone = 0,          // 不是卡片：文字、大表情、图片、语音、时间分隔行等
    kCardTypeFile = 1,          // 文件（kMsgFile）：标题前画文档图标而不是图标块
    kCardTypeFolder = 2,        // 文件夹整体传输（kMsgFolder）
    kCardTypeMerged = 3,        // 合并转发的聊天记录（kMsgMerged）
    kCardTypeRelay = 4,         // 群接龙（kMsgRelay）
    kCardTypeFileShare = 5,     // 文件共享（kMsgFileShare）
    kCardTypeScreenShare = 6,   // 屏幕共享（kMsgScreenShare，下面几种标志都不是时）
    kCardTypeCall = 7,          // 一对一通话（kMsgScreenShare，screenShareIsCallCard）
    kCardTypeGroupCall = 8,     // 群通话（kMsgScreenShare，screenShareIsGroupCall）
    kCardTypeAnnouncement = 9,  // 群公告（kMsgScreenShare，screenShareIsAnnouncement）
    kCardTypeMemo = 10,         // 群备忘（kMsgScreenShare，screenShareIsMemo）
    kCardTypeCloudRecord = 11,  // 云端录像（kMsgScreenShare，screenShareIsCloudRecord）
    kCardTypeGroupInvite = 12,  // 入群邀请（kMsgGroupInvite）
    kCardTypeSecretInvite = 13, // 密聊邀请（kMsgSecretInvite）
    kCardTypeMarkdown = 14,     // Markdown 文档（kMsgText，isMarkdown）
    kCardTypeCode = 15,         // 代码片段（kMsgText，isCode）：头部是语言名与行数，没有图标块
};

/**
 *  判断一条消息画成哪种卡片。
 *  @param msg 消息
 *  @return 卡片种类；不是卡片时为 kCardTypeNone。Markdown 与代码片段只看消息标志，不管 FlamingoMarkdown.dll
 *          是否可用（DLL 不可用时气泡按普通文字显示，调用方自己判断）
 */
ChatCardType ChatCardTypeOf(const ChatMessage& msg);

/**
 *  一张卡片的类型样式：图标块、底部类型标签用什么颜色与图标。
 */
struct ChatCardTypeStyle
{
    ChatCardType m_type;        // 卡片种类
    bool         m_hasTile;     // 标题前是否画图标块；文件（画文档图标）与代码片段（头部是语言名）为 false
    bool         m_ended;       // 是否按「已结束」画成灰色图标块（只有已结束的屏幕共享与群通话）
    COLORREF     m_tileFill;    // 图标块底色：类型色按比例混入白色后的不透明色；已结束时为灰色
    COLORREF     m_glyphColor;  // 图标块里线条图标的颜色；已结束时为灰色
    COLORREF     m_labelGlyphColor;   // 底部类型标签前图标的颜色：文件为灰色，其余与 m_glyphColor 相同（已结束时同样为灰）
    int          m_glyph;       // 图标种类，取值见 ChatIconKind；没有图标块时仍给出底部标签用的图标
};

/**
 *  取一条消息的类型样式（docs/聊天卡片美化方案.md 3.1 节的十种类型色）。
 *  @param msg 消息
 *  @return 类型样式；不是卡片时 m_type 为 kCardTypeNone、m_hasTile 为 false，其余字段无意义
 */
ChatCardTypeStyle ChatCardTypeStyleOf(const ChatMessage& msg);

/**
 *  底部类型标签的文字（已经过 TR 翻译），如「屏幕共享」「群公告」。一对一通话按媒体类型写「语音通话」或「视频通话」。
 *  @param msg 消息
 *  @return 标签文字；不是卡片时为空串
 */
CString ChatCardTypeLabel(const ChatMessage& msg);

/**
 *  状态标签的语义（docs/聊天卡片美化方案.md 3.4 节），决定标签的文字色与底色。
 */
enum ChatCardChipTone
{
    kCardChipNone = 0,      // 不画标签
    kCardChipLive = 1,      // 进行中（绿）：正在共享、通话中、可用、对方已接收、全部接收完成
    kCardChipPending = 2,   // 待处理、倒计时（橙）：待接收、下载中、剩余 N 秒、邀请 7 天内有效
    kCardChipBad = 3,       // 拒绝、重要、失败（红）：对方已拒绝、优先级重要、传输失败
    kCardChipEnded = 4,     // 已结束（灰）：已结束、已取消、已过期
    kCardChipNeutral = 5,   // 中性提示（蓝）：提醒全体成员、提醒 N 人
};

/**
 *  卡片按钮的级别（3.5 节）。
 */
enum ChatCardButtonLevel
{
    kCardButtonPrimary = 0,     // 主要：改变状态的操作（接收、加入、同意、参与接龙），实心橙色胶囊
    kCardButtonView = 1,        // 查看：打开、查看、播放，橙色描边胶囊
    kCardButtonSecondary = 2,   // 次要：拒绝、忽略、取消、另存为，灰色文字
};

/**
 *  按钮的显示状态，决定绘制颜色与是否参与命中。
 */
enum ChatCardButtonState
{
    kCardButtonNormal = 0,      // 正常
    kCardButtonHover = 1,       // 鼠标悬停
    kCardButtonDisabled = 2,    // 暂时不可用：置灰，不参与命中（永久不可用的按钮直接不出现，见方案第 1 节第 4 条）
};

/**
 *  卡片底部的一个按钮。各类卡片按自己的状态生成按钮列表，按显示顺序从左到右排列；绘制与命中都照这份列表。
 */
struct ChatCardButton
{
    CString m_text;       // 按钮文字（已翻译）
    int     m_level;      // 级别，取值见 ChatCardButtonLevel
    bool    m_enabled;    // 是否可用；false 表示暂时不可用，画成置灰
    int     m_action;     // 点击后的动作，取值由各类卡片自己定义（如文件卡片为 ChatFileActionKind）

    ChatCardButton() : m_level(kCardButtonView), m_enabled(true), m_action(0)
    {
    }

    /**
     *  @param text    按钮文字（已翻译）
     *  @param level   级别，取值见 ChatCardButtonLevel
     *  @param enabled 是否可用
     *  @param action  点击后的动作（各类卡片自己的取值）
     */
    ChatCardButton(const CString& text, int level, bool enabled, int action)
        : m_text(text), m_level(level), m_enabled(enabled), m_action(action)
    {
    }
};

/**
 *  一次填好卡片的状态标签与信息行（ChatMessage 的 cardChipTone / cardChipText / cardMetaText），供各卡片的 ApplyXxx 与
 *  状态刷新函数调用。
 *  @param msg      [入出参] 要填写的消息
 *  @param chipText 标签文字（已翻译）；为空时不画标签
 *  @param tone     标签语义，取值见 ChatCardChipTone
 *  @param metaText 标签右侧的信息行文字（已翻译）；默认为空
 */
void SetCardChip(ChatMessage& msg, const CString& chipText, int tone, const CString& metaText = CString());

/**
 *  卡片底部一行要不要显示（3.6 节）：标题就是类型名（如「语音通话」）且没有按钮时不显示，其余都显示。
 *  @param titleIsTypeName 卡片标题是否就是类型名
 *  @param buttonCount     按钮个数
 *  @return 要显示返回 true
 */
bool ChatCardFooterVisible(bool titleIsTypeName, size_t buttonCount);

/**
 *  卡片标题是否就是类型名（如一对一通话的「语音通话」、没有标题的屏幕共享「屏幕共享」）。与 ChatCardFooterVisible 配合：
 *  这种卡片没有按钮时不画底部一行，免得同一个词在标题和底部各写一遍。
 *  @param msg 消息
 *  @return 是类型名返回 true
 */
bool ChatCardTitleIsTypeName(const ChatMessage& msg);

/**
 *  卡片底部的按钮列表（docs/聊天卡片美化方案.md 第 5 节），按显示顺序从左到右。按方案第 1 节第 4 条：永久不可用的操作
 *  不出现在列表里，暂时不可用的照常列出、m_enabled 为 false（画成置灰）。依据是各卡片 ApplyXxx 与状态刷新函数已经填好的
 *  字段，本函数不解析 content。
 *  单按钮卡片（屏幕共享、群通话、群公告、群备忘、云端录像、文件共享）的 m_action 为 0，点击后照旧投递该卡片原有的请求。
 *  @param msg 消息
 *  @return 按钮列表；没有按钮或不是卡片时为空
 */
std::vector<ChatCardButton> ChatCardButtonsOf(const ChatMessage& msg);

/**
 *  把按「最常用的在前」排好的按钮重排成显示顺序：底部一行从右往左排，最重要的离右手最近。做法是先倒过来，再按级别
 *  稳定排序（次要、查看、主要），得到如「拒绝、接收」「另存为、打开所在文件夹、打开文件」（docs/聊天卡片美化方案.md 5.1）。
 *  @param buttons [入出参] 按钮列表
 */
void ChatCardArrangeButtons(std::vector<ChatCardButton>& buttons);

/**
 *  文件夹卡片的状态标签与信息行（docs/聊天卡片美化方案.md 5.1）：待接收时接收端标签写「待接收」、信息行是原来的说明，
 *  传输中不画标签（进度写在进度条下面），失败时标签为红、宿主带回了错误码时信息行写原因，其余状态标签就是原来的状态文字。
 *  @param msg      文件夹消息（kMsgFolder）
 *  @param chipText [出参] 标签文字（已翻译）；为空时不画标签
 *  @param tone     [出参] 标签语义，取值见 ChatCardChipTone
 *  @param metaText [出参] 信息行文字（已翻译）；为空时没有
 */
void FolderCardStatus(const ChatMessage& msg, CString& chipText, int& tone, CString& metaText);

/**
 *  文件夹卡片进度条下面那一行的左右两段：左边传输中写「正在传输 文件名」，失败、已过保留期、已取消写「已传 / 总大小」；
 *  右边写「第几个 / 共几个 · 百分比」（还不知道传到第几个时只写百分比）。只在 FolderStateHasProgress 为真时有意义。
 *  @param msg   文件夹消息
 *  @param left  [出参] 左段文字
 *  @param right [出参] 右段文字
 */
void FolderCardProgressText(const ChatMessage& msg, CString& left, CString& right);

} // namespace balloonchatwnd
