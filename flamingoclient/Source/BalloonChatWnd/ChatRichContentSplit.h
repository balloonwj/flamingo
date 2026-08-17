/**
 *  ChatRichContentSplit.h
 *
 *  富文本输入框（ChatInputRichEdit）内容的"有序拆分"纯逻辑：把图文混排的输入按
 *  <u>文档顺序</u>拆成一串待发送部分——连续文字是一个文字部分，每个内嵌图（在
 *  RichEdit 纯文本里表现为占位符 WCH_EMBEDDING 0xFFFC）是一个图片部分。聊天窗
 *  发送时据此逐段发出（文字→文字消息、图→图片消息），实现"全保真按文档顺序发送"。
 *
 *  本模块只做字符串→部分序列的纯运算，不碰 RichEdit / OLE / 网络，便于 FlamingoUnit
 *  直接断言（对应用例 P6）。从 OLE 对象抽位图、逐段发消息等运行期逻辑在聊天窗里做。
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
    int          imageIndex;  // kRichPartImage 时有效：第几张内嵌图（按文档顺序 0,1,2…）
};

// 把富文本输入框的纯文本（内嵌对象处为占位符 WCH_EMBEDDING 0xFFFC）拆成有序部分。
//   规则：
//     · 连续的非 0xFFFC 字符 → 一个 text 部分，text 为该段原文（含换行）；
//     · 每个 0xFFFC → 一个 image 部分，imageIndex 按出现先后从 0 递增；
//     · "只含空白"（空格 / 制表 / 回车 / 换行）的 text 部分被丢弃，避免发出空消息。
//   textWithEmbeds：RichEdit 的纯文本（可为 NULL，按空串处理）。
//   返回：按文档顺序排列的部分序列；输入为空 / 全空白时返回空 vector。
std::vector<RichOutgoingPart> SplitRichInputToParts(const wchar_t* textWithEmbeds);

// 输入框按文档顺序枚举出的一个原始段（DuiRichEdit::EnumContent 的逐段结果）。
struct RichInputSegment
{
    bool      isImage;   // true=内嵌图片对象，false=纯文本段
    CString   text;      // isImage=false 时有效：该段文本（含换行 / 空格）
    DWORD_PTR tag;       // isImage=true 时有效：对象的 dwUser 标记（表情按 ChatFaceTag
                         // 约定编码 faceId；0=非表情内嵌图，如截图）
};

// 把 EnumContent 枚举出的原始段聚合成待发送部分序列（取代上面的 SplitRichInputToParts
// 成为发送主路径 —— 后者无法区分"表情"与"截图"两种内嵌对象）。
//   规则：
//     · 文本段与<u>表情段</u>聚合成一个 text 部分：组内含表情时正文按
//       balloonchat::BuildSendBody 编码（文本 '/'→'//'，表情 → /f["id"]），随文本
//       消息一起发出；组内无表情时保持原文（不转义，与历史行为 / 他端互通一致），
//       但若原文恰好能被解析出 /f["N"] 标签（用户手打），仍走编码转义以免对端误判；
//     · <u>非表情</u>内嵌图（截图等，tag==0）→ 一个 image 部分，imageIndex 只按
//       非表情图出现先后从 0 递增（与聊天窗 m_pendingShotPaths 的插入序对应）；
//     · 无表情且"只含空白"的 text 组被丢弃；含表情的组即使无文字也照发。
//   segs：EnumContent 按文档顺序收集的原始段。
//   返回：按文档顺序排列的待发送部分；输入为空 / 全空白时返回空 vector。
std::vector<RichOutgoingPart> BuildOutgoingParts(const std::vector<RichInputSegment>& segs);

} // namespace balloonchatwnd
