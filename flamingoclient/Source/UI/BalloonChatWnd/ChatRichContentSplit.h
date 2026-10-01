/**
 *  ChatRichContentSplit.h
 *
 *  富文本输入框（ChatInputRichEdit）内容的"有序拆分"纯逻辑：把输入框按<u>文档顺序</u>
 *  枚举出的原始段（文本段、表情、截图等内嵌图）聚合成一串待发送部分——连续的文字与表情
 *  是一个文字部分，每张非表情的内嵌图是一个图片部分。聊天窗发送时据此逐段发出（文字→
 *  文字消息、图→图片消息），实现"全保真按文档顺序发送"。
 *
 *  本模块只做原始段→部分序列的纯运算，不碰 RichEdit / OLE / 网络，便于 FlamingoUnit
 *  直接断言（Tests/ChatRichContentSplitTests.cpp 的 RS、P7、ROP 各组用例）。从 OLE 对象
 *  抽位图、逐段发消息等运行期逻辑在聊天窗里做。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include <vector>

namespace balloonchatwnd {

// 待发送部分的类型。
enum RichPartKind
{
    kRichPartText  = 0,   // 文字部分（按原文发文字消息）
    kRichPartImage = 1,   // 图片部分（按 imageIndex 取对应内嵌图发图片消息）
};

// 输入框内容拆出的一个"待发送部分"。
struct RichOutgoingPart
{
    RichPartKind kind;        // 本部分类型
    CString      text;        // kRichPartText 时有效：该段原文（保留内部换行 / 空格）
    int          imageIndex;  // kRichPartImage 时有效：该图在聊天窗待发图片列表里的下标（带图片
                              // 标记的取标记里记的下标，未带标记的按出现先后从 0 递增）
};

// 输入框按文档顺序枚举出的一个原始段（DuiRichEdit::EnumContent 的逐段结果）。
struct RichInputSegment
{
    bool      isImage;   // true=内嵌图片对象，false=纯文本段
    CString   text;      // isImage=false 时有效：该段文本（含换行 / 空格）
    DWORD_PTR tag;       // isImage=true 时有效：对象的 dwUser 标记（表情按 ChatFaceTag
                         // 约定编码 faceId；0=非表情内嵌图，如截图）
};

// 把 EnumContent 枚举出的原始段聚合成待发送部分序列，单聊窗与群聊窗发送时都走这里。
// 按原始段而不是按 RichEdit 纯文本拆分，是因为纯文本里所有内嵌对象都是同一个占位符，
// 分不出"表情"与"截图"。
//   规则：
//     · 文本段与<u>表情段</u>聚合成一个 text 部分：组内含表情时正文按
//       balloonchat::BuildSendBody 编码（文本 '/'→'//'，表情 → /f["id"]），随文本
//       消息一起发出；组内无表情时保持原文（不转义，与历史行为 / 他端互通一致），
//       但若原文恰好能被解析出 /f["N"] 标签（用户手打），仍走编码转义以免对端误判；
//     · <u>非表情</u>内嵌图 → 一个 image 部分。带图片标记的（ChatFaceTag.h 的
//       ImageTagFromIndex，截图 / 粘贴 / 拖入的图都带）取标记里记的源文件下标，与它在
//       文档里排第几无关；未带标记的（tag==0，旧方式插入）按这类图出现先后从 0 递增
//       （与聊天窗 m_pendingShotPaths 的插入序对应）；
//     · 无表情且"只含空白"的 text 组被丢弃；含表情的组即使无文字也照发。
//   segs：EnumContent 按文档顺序收集的原始段。
//   返回：按文档顺序排列的待发送部分；输入为空 / 全空白时返回空 vector。
std::vector<RichOutgoingPart> BuildOutgoingParts(const std::vector<RichInputSegment>& segs);

} // namespace balloonchatwnd
