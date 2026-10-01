/**
 *  BalloonChatMsgFormat.h
 *
 *  聊天消息的纯逻辑处理：发送前校验、协议标签剥离、显示行格式化。
 *  从对话框类里抽出来单独成模块，使这部分逻辑可被 FlamingoUnit 单元
 *  测试直接调用 —— 不碰窗口、不碰网络。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方必须已经把 ATL
 *  （<atlstr.h> 提供的 CString 等）引入（Flamingo 侧由 stdafx.h 保证，
 *  FlamingoUnit 侧由其 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#pragma once

#include <ctime>   // time_t
#include <vector>

namespace balloonchat {

/**
 *  SendValidateResult —— ValidateOutgoing 的判定结果。
 */
enum SendValidateResult
{
    kSendOk      = 0,   // 内容合法，可以发送
    kSendEmpty   = 1,   // 内容为空（去除首尾空白后长度为 0），不允许发送
    kSendTooLong = 2,   // 内容超过单条消息字符上限，不允许发送
};

/**
 *  单条聊天消息允许的最大字符数。
 *
 *  取值 1800：沿用旧版 CBuddyChatDlg::OnBtn_Send 的硬上限 —— 早期协议
 *  里聊天消息体经压缩 + 包长字段限制，1800 字符是作者验证过的安全值，
 *  超出会提示"请分条发送"。
 */
const int kMaxOutgoingChars = 1800;

/**
 *  校验一条待发送的消息文本是否可发送。
 *    text：用户在输入框里键入的原始文本。
 *  返回：见 SendValidateResult。空白判定会先去除首尾空白字符。
 */
SendValidateResult ValidateOutgoing(const CString& text);

/**
 *  剥离聊天消息体里的协议控制标签，返回可直接显示的纯文本。
 *
 *  协议把字体等元信息以 "/x[...]" 形式拼在正文里（x 为单个字母，例如
 *  字体信息 /o["宋体,9,..."]、窗口抖动 /s["1"]、回执 /r["1"] 等，详见
 *  聊天消息格式.txt）。直接显示会把这些标签暴露给用户，故先剥离。
 *
 *    raw：可能含协议标签的消息体。
 *  返回：去掉所有 "/字母[...]" 片段后的纯文本（其余字符原样保留）。
 */
CString StripControlTags(const CString& raw);

/**
 *  把一条消息格式化成接收区显示用的多行文本。
 *
 *  形如：
 *      <发送者>  YYYY-MM-DD HH:MM:SS<CRLF>
 *      <正文><CRLF>
 *
 *    sender：发送者昵称。
 *    when：  消息时间（time_t，按本地时区格式化 —— 聊天界面展示本地时间）。
 *    text：  已剥离协议标签的纯文本正文。
 *  返回：可直接 AppendText 到接收区 RichEdit 的字符串。
 */
CString FormatDisplayLine(const CString& sender, time_t when, const CString& text);

/**
 *  ChatSegment —— 一条待发送消息拆出来的"一段"内容。
 *
 *  聊天输入框里文字与系统表情图文混排，发送前按出现顺序拆成若干段：每段
 *  要么是一串纯文本，要么是一个系统表情。BuildSendBody 把这样一串段编码
 *  成发送正文字符串。段序列通常来自输入框 DuiRichEdit::EnumContent
 *  按文档顺序的回调（文本段 + 带 faceId 的内联表情段）。
 */
struct ChatSegment
{
    bool    m_bFace;     // true=表情段（取 m_nFaceId）；false=文本段（取 m_strText）
    int     m_nFaceId;   // 系统表情 id（m_bFace=true 时有效，对应 FaceConfig.xml 的 id）
    CString m_strText;   // 文本段原始文本（m_bFace=false 时有效，未做协议转义）

    // 构造一个文本段。
    //   text：原始文本（调用方无需转义，BuildSendBody 内部处理）。
    static ChatSegment Text(const CString& text)
    {
        ChatSegment seg;
        seg.m_bFace = false;
        seg.m_nFaceId = -1;
        seg.m_strText = text;
        return seg;
    }

    // 构造一个表情段。
    //   faceId：系统表情 id（FaceConfig.xml 的 id，即 face<id>.png 的编号）。
    static ChatSegment Face(int faceId)
    {
        ChatSegment seg;
        seg.m_bFace = true;
        seg.m_nFaceId = faceId;
        return seg;
    }
};

/**
 *  把消息段序列编码成"发送正文"字符串，交给发送管线
 *  （CSendMsgThread::CreateMsgContent）二次解析成 CContent 列表。
 *
 *  编码规则（与 CreateMsgContent / HandleSysFaceId 的解析口径严格对齐）：
 *    · 文本段：把其中每个 '/' 转义成 '//'。发送管线会把 '//' 还原成单个
 *      '/'，从而避免用户输入里的 '/'（尤其是手打的 "/f["、"/o[" 等）被
 *      误判成协议标签起始。
 *    · 表情段：输出 /f["<faceId>"]。发送管线据此还原成 CONTENT_TYPE_FACE。
 *
 *    segs：按出现顺序排列的消息段（文本 / 表情混排），可为空。
 *  返回：可直接交给 CFlamingoClient::SendBuddyMsg 的正文字符串；segs 为空
 *        时返回空串。
 */
CString BuildSendBody(const std::vector<ChatSegment>& segs);

/**
 *  BuildSendBody 的逆运算：把发送正文字符串解析回消息段序列，供接收方 / 回显方
 *  把"文字 + 内联表情"渲染出来（气泡窗的文本消息把表情以 /f["id"] 内联在
 *  content.text 里随消息传递，接收端据本函数还原）。
 *
 *  解析规则（与 BuildSendBody / CreateMsgContent 口径一致）：
 *    · //f["<整数>"]  → 文本里字面的 /f["<整数>"]（"//" 只有紧跟表情标记时才是转义；
 *                       其余位置的 "//"，如网址里的 http://，原样保留）；
 *    · /f["<整数>"]   → 一个系统表情段（faceId 为该整数）；
 *    · 其余字符        → 文本，原样保留；残缺 / 无法识别的 "/x..." 当普通文本，不吞正文。
 *
 *    body：发送正文字符串（可为空）。
 *  返回：按出现顺序排列的消息段（文本 / 表情）；空串返回空 vector。相邻文本不会被
 *        拆成多段（连续文本合并为一个文本段）。
 */
std::vector<ChatSegment> ParseSendBody(const CString& body);

/**
 *  算出「发送正文下标 → 扁平文本下标」的换算表。
 *
 *  两套下标口径的由来：
 *    · 发送正文（body）里，一个表情写成 `/f["5"]` 这样的转义标记，占好几个字符；
 *      字面的 '/' 又写成 `//`，占两个字符。@提醒 的片段偏移记的就是<u>这套</u>下标
 *      —— 它是在发送前对正文重扫得到的（见 GroupMentionModel 的 RescanMentions）。
 *    · 扁平文本（气泡排版 / 选中复制用的口径）里，每个表情折叠成<u>一个</u>占位字符、
 *      `//` 还原成一个 '/'。气泡的字符级排版模型按的是<u>这套</u>下标。
 *  两者只在正文里出现表情或转义斜杠时才不一致，但一旦不一致，@ 的高亮区间就会
 *  整体错位，故换算必须显式做，不能想当然地认为两套下标相同。
 *
 *  返回的表长度为 body.GetLength() + 1，末元素即扁平文本的总长度，故区间
 *  [off, off+len) 可直接换算成 [map[off], map[off+len])。落在一个转义序列<u>中间</u>
 *  的下标会映射到该序列所对应的那个扁平字符上（不会越过它）。
 *    body：发送正文字符串（可为空，此时返回只含一个 0 的表）。
 *  @return 换算表；下标 i 处的值是 body 前 i 个字符对应的扁平文本长度。
 */
std::vector<int> BuildFlatIndexMap(const CString& body);

/**
 *  把发送正文转成"会话列表摘要"用的纯文本：表情段替换成固定占位 "[表情]"，文本段
 *  的 "//" 还原成 "/"。用于会话列表 / 通知里不暴露 /f["id"] 原始标记。
 *    body：发送正文字符串。
 *  返回：可直接展示的摘要文本。
 */
CString BodyToDigest(const CString& body);

/**
 *  生成"消息已被撤回"的提示文案，供聊天窗把原气泡就地替换成居中提示行、
 *  以及会话列表把该会话的最后一条消息预览改成同一句话。两处用同一函数，
 *  保证措辞一致。
 *
 *  文案规则：
 *    · 撤回者是本人               → "你撤回了一条消息"
 *    · 撤回者是他人且拿得到昵称   → "<昵称>撤回了一条消息"（群聊里常见：群主/管理员撤他人消息）
 *    · 撤回者是他人但昵称为空     → "对方撤回了一条消息"（拿不到昵称时的退化文案）
 *
 *    bIsSelf：      撤回者是否为本端登录用户（由调用方比较推送里的 operator 与自己的 userid 得出）。
 *    operatorName： 撤回者昵称；bIsSelf 为 true 时忽略该参数。可传空串，
 *                   此时按上面第三条退化处理。
 *  返回：可直接显示的提示文案。
 */
CString BuildRecallTipText(bool bIsSelf, const CString& operatorName);

/**
 *  生成删除消息前的确认文案。单聊窗、群聊窗的「删除」与消息记录窗的「删除」共用这一函数，
 *  保证措辞一致（docs/清理聊天记录方案.md §10）。删除是本人视角的：本机与本人其他设备都不再
 *  显示，对方不受影响，文案须把这一点说清楚。
 *
 *    count：要删除的消息条数。1（及非法的 0）时说"这条消息"，大于 1 时说"这 N 条消息"。
 *  返回：可直接用作消息框正文的文案（含换行）。
 */
CString BuildDeleteMessagesConfirmText(size_t count);

} // namespace balloonchat
