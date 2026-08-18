/**
 *  ChatQuoteModel.h
 *
 *  引用回复的「协议结构 → 展示结构」换算，以及被引用消息的摘要生成。
 *
 *  存在的意义是<u>统一入口</u>：实时推送、历史渲染、本端发出的乐观气泡，三条路都必须
 *  经同一个函数把 net::ReplyQuote 填进 ChatMessage，否则迟早跑偏 —— @提醒 就吃过这个
 *  亏：历史渲染那条漏了填 ats，导致重开聊天窗后 @ 的高亮全部消失，而实时收到时是好的，
 *  排查了很久才发现是两条路各写各的。新增用到引用数据的场景一律调这里，不要再各自拼装。
 *
 *  摘要（digest）的口径也集中在本文件：文本取正文、图片给占位、文件给文件名。发送方在
 *  发送时生成一次快照写进协议，收方原样显示 —— 不在收方重新生成，因为收方未必拿得到
 *  原消息（可能根本没加载到那么早）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（要用 CString）。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include "ChatMockData.h"                  // ChatMessage
#include "../../net/MessageProtocolClient.h"  // net::ReplyQuote

namespace balloonchatwnd {

// 引用摘要的最大字符数（UTF-16）。超出部分截断并加省略号 —— 引用块只有一行高，
// 再长也显示不下，而快照是要随每条消息落库的，没必要把整段正文都塞进去。
const int kQuoteDigestMaxChars = 40;

/**
 *  气泡的 ChatMsgKind → 协议的 message_type 换算。
 *
 *  <u>这两套取值不是一回事</u>，必须显式换算：ChatMsgKind 里 kMsgImage 是 3、kMsgFile
 *  是 4，而协议里 message_type_image 是 2、message_type_file 是 3 —— 直接赋值会把
 *  「引用了一张图片」写成「引用了一个文件」，收方按文件渲染引用块，而且这种错在界面上
 *  极难察觉（引用块只是个占位文字）。
 *    kind：ChatMsgKind 取值。
 *  @return message_type 取值；无对应关系的 kind（如时间分隔行）返回 message_type_text。
 */
int MsgKindToProtocolType(int kind);

/**
 *  协议的 message_type → 气泡的 ChatMsgKind 换算（上一个函数的逆向）。
 *    msgtype：message_type 取值。
 *  @return ChatMsgKind 取值；未知类型返回 kMsgText。
 */
int ProtocolTypeToMsgKind(int msgtype);

/**
 *  把协议侧的引用快照填进展示结构。
 *
 *  <u>三条路（实时推送 / 历史渲染 / 本端乐观气泡）都必须调本函数</u>，不要各自拼装。
 *    quote：协议侧快照；m_valid 为假时把 out 的 hasQuote 置假、其余字段清空。
 *    out：  [出参] 待填的消息结构；本函数只动 quote* 那几个字段，其余原样不碰。
 *    cachedImagePath：被引用的是图片、且原图已在本地缓存里时，调用方解析好的完整路径；
 *                     命中就填进 quoteImagePath，引用块直接画缩略图，未命中传空串、画占位块。
 *                     <u>为什么由调用方解析而不是本函数自己查</u>：查缓存要用 net::ImageTransfer，
 *                     那是整条网络栈的入口，把它拖进本模块会让 FlamingoUnit 测试工程也要链接
 *                     CIUSocket —— 本模块是纯逻辑、要能被单测直接调用，不该有这种依赖。
 */
void FillQuoteForRender(const net::ReplyQuote& quote, ChatMessage& out,
                        const CString& cachedImagePath = CString());

/**
 *  为「即将被引用的那条消息」生成协议侧快照，供发送时写进 content.quote。
 *
 *  摘要按被引用消息的类型生成：文本走 BodyToDigest（内联表情替换成 [表情]，不暴露
 *  /f["5"] 原文），图片给固定占位，文件给文件名。图片还会带上 fileid / md5 / 宽高，
 *  收方据此在引用块里画缩略图。
 *
 *    src：      被引用的那条消息（必须已有服务器 msgid，否则原消息还没落库、无从定位）。
 *    senderName：被引用消息发送者的显示名；调用方按会话上下文解析好传进来
 *                （群聊取群名片 / 昵称，单聊取对端昵称或本人昵称）。
 *    imageFileId / imageMd5：被引用消息为图片时，原图的 fileid 与 md5；由调用方从
 *                自己的发送 / 接收记录里取（ChatMessage 本身不持有这两项）。非图片
 *                消息传空串即可。
 *  @return 协议侧快照；src 没有 msgid 时返回 m_valid 为假的空快照。
 */
net::ReplyQuote BuildQuoteFromMessage(const ChatMessage& src,
                                      const CString& senderName,
                                      const CString& imageFileId = CString(),
                                      const CString& imageMd5 = CString());

/**
 *  生成一条消息的引用摘要文本（按类型分派，见上）。单独暴露出来是为了能被单测直接
 *  断言，也便于输入区的引用条与气泡里的引用块共用同一口径。
 *    src：被引用的消息。
 *  @return 摘要文本；已按 kQuoteDigestMaxChars 截断。
 */
CString BuildQuoteDigest(const ChatMessage& src);

/**
 *  拼出「引用回复」要塞进输入框的那段文本（分隔线 + 头部 + 内容）。
 *
 *  形如：
 *      ------------------ 原消息 ------------------
 *      发件人：张三
 *      时间：2026-08-04 10:23
 *
 *      你好
 *
 *      ───────────────────────
 *
 *  末尾留一个空行，插入后光标停在最末 —— 用户接着打字就是回复正文，不必自己先敲回车。
 *
 *  <u>这段文本就是最终发出去的正文的一部分</u>，收方看到的与本端输入框里一模一样，
 *  没有任何结构化信息 —— 这是"塞进输入框"式引用与结构化引用（content.quote）的根本
 *  区别，也意味着收方无法据此跳回原消息。
 *
 *    senderName：  被引用消息发送者的显示名；空串时该行留空但仍占位，保持格式整齐。
 *    timestampMs： 被引用消息的时间（Unix 毫秒）；0 表示时间未知，此时不输出"时间："那一行。
 *    digest：      被引用消息的摘要（见 BuildQuoteDigest，已按上限截断）。
 *  @return 拼好的多行文本，以换行结尾。
 */
CString BuildQuotedReplyText(const CString& senderName, long long timestampMs,
                             const CString& digest);

} // namespace balloonchatwnd
