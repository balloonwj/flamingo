/**
 *  聊天内容的剪贴板模型：把聊天记录里的文字、表情、图片拆成有序片段，并在片段与
 *  剪贴板数据之间来回转换，ChatClipboardContent.h
 *
 *  复制聊天记录时，剪贴板里同时放两份内容：
 *    · 纯文字（CF_UNICODETEXT）：表情写成"[表情]"、图片写成"[图片]"，粘到其它程序里
 *      看到的就是这段文字，与以往的复制结果一致；
 *    · Flamingo 自己的格式（名为 kChatClipFormatName，用 RegisterClipboardFormatW 注册）：
 *      记下"这里是一段文字 / 第几号表情 / 哪张图片"。Flamingo 的输入框认得它，粘贴时按
 *      顺序把文字、表情、图片还原出来。跨聊天窗口、跨 Flamingo 进程都有效。
 *  拖拽聊天内容时用的也是同一套片段与数据格式。
 *
 *  本模块只做纯计算：拆片段、序列化 / 解析、转纯文字，不碰剪贴板与窗口，由 FlamingoUnit
 *  直接断言（Tests/ChatClipboardContentTests.cpp）。真正读写剪贴板的代码在调用方。
 *
 *  典型用法（复制整条文字消息）：
 *      std::vector<ChatClipSegment> segs = BuildClipSegmentsFromBody(msg.text);
 *      std::wstring plain = ClipSegmentsToPlainText(segs);     // 写 CF_UNICODETEXT
 *      std::string  rich  = SerializeClipSegments(segs);       // 写 Flamingo 自己的格式
 *  典型用法（粘贴）：
 *      std::vector<ChatClipSegment> segs;
 *      if (ParseClipSegments(data, segs)) { 按顺序插入文字 / 表情 / 图片; }
 *
 *  本文件不 include stdafx.h（PCH 设 NotUsing），与 FlamingoUnit 测试工程共用；包含它之前
 *  调用方须已引入 ATL 的 CString。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <string>
#include <vector>

#include "../BalloonChat/BalloonChatMsgFormat.h"   // balloonchat::ChatSegment（文字 / 表情段）
#include "ChatFaceTag.h"                            // kFaceTagRange（表情编号的合法范围）

namespace balloonchatwnd {

// Flamingo 聊天内容在剪贴板里的格式名，交给 RegisterClipboardFormatW 注册。名字里带版本号：
// 日后数据结构有不兼容的变化时换一个名字，老版本程序就不会误读新数据。
extern const wchar_t* const kChatClipFormatName;

// 表情与图片在纯文字里写成的文字，与会话列表预览、以往的复制结果同一口径。粘贴时表情图片或
// 图片文件找不到，也以这两段文字占位。
extern const wchar_t* const kChatClipFacePlaceholder;    // L"[表情]"
extern const wchar_t* const kChatClipImagePlaceholder;   // L"[图片]"

// 数据里记录的格式版本。解析时版本不符按整体失败处理。
const int kChatClipVersion = 1;

// 解析时最多接受的片段数，防止异常数据占用过多内存。超出的部分丢弃。
const size_t kChatClipMaxSegments = 1000;

// 表情编号的上限（不含）。取 ChatFaceTag.h 里表情标记可容纳的编号个数，超出的编号插不进
// 输入框，按非法片段丢弃。
const int kChatClipFaceIdLimit = (int)kFaceTagRange;

/**
 *  剪贴板聊天内容里一个片段的类型。
 */
enum ChatClipKind
{
    // 一段文字。
    kChatClipText  = 0,

    // 一个系统表情，编号见 Face/FaceConfig.xml。
    kChatClipFace  = 1,

    // 一张图片，以本地文件的完整路径表示。
    kChatClipImage = 2,
};

/**
 *  剪贴板聊天内容里的一个片段。
 */
struct ChatClipSegment
{
    // 片段类型。
    ChatClipKind m_kind;

    // kChatClipText 时有效：文字原文（换行、空格原样保留，不含 /f["id"] 这类内部标记）。
    std::wstring m_text;

    // kChatClipFace 时有效：表情编号。
    int          m_faceId;

    // kChatClipImage 时有效：图片本地文件的完整路径。
    std::wstring m_imagePath;

    ChatClipSegment()
        : m_kind(kChatClipText)
        , m_faceId(0)
    {
    }
};

/**
 *  把一条文字消息的整条正文拆成片段。
 *    body：消息正文（发送正文格式，表情以 /f["id"] 内联；用户手打的字面 /f["id"] 以
 *          //f["id"] 转义，其余位置的 "//"（如网址）原样保留，规则见 ParseSendBody）。
 *  返回：按原文顺序排列的片段；相邻文字合并为一段；正文为空时返回空序列。
 */
std::vector<ChatClipSegment> BuildClipSegmentsFromBody(const CString& body);

/**
 *  取消息段序列里被选中的那一部分，拆成片段。
 *    segs：    消息段序列（ParseSendBody 的结果，文字 / 表情混排）。
 *    selStart / selEnd：选区在"扁平串"上的下标区间 [selStart, selEnd)。扁平串的构成与
 *              ChatInlineText.h 的 BuildFlatText 相同：文字段贡献其原文，每个表情贡献一个
 *              占位字符。区间越界时按扁平串的实际长度截断；selStart >= selEnd 时视为没有选区。
 *  返回：选区里的片段，顺序与原文一致；相邻文字合并为一段；没有选区时返回空序列。
 */
std::vector<ChatClipSegment> BuildClipSegmentsFromSelection(
    const std::vector<balloonchat::ChatSegment>& segs, int selStart, int selEnd);

/**
 *  把一张图片消息拆成片段。
 *    imagePath：图片本地文件的完整路径。
 *  返回：路径非空时返回只含一个图片片段的序列；路径为空（图片还没下载到本地）时返回空
 *        序列，调用方此时只写纯文字。
 */
std::vector<ChatClipSegment> BuildClipSegmentsForImage(const CString& imagePath);

/**
 *  把片段序列转成纯文字：文字原样，表情写成"[表情]"，图片写成"[图片]"。
 *    segs：片段序列。
 *  返回：纯文字；序列为空时返回空串。
 */
std::wstring ClipSegmentsToPlainText(const std::vector<ChatClipSegment>& segs);

/**
 *  把片段序列序列化成剪贴板数据（UTF-8 编码的 JSON 文本）。
 *    segs：片段序列。
 *  返回：序列化结果，交给 ParseClipSegments 可还原出同样的序列。
 */
std::string SerializeClipSegments(const std::vector<ChatClipSegment>& segs);

/**
 *  解析剪贴板数据，还原片段序列。
 *    data：SerializeClipSegments 的输出（UTF-8 JSON 文本）。
 *    out： [出参] 还原出的片段序列；解析失败时被清空。
 *  返回：数据结构合法（是 JSON、版本号相符、片段列表存在）时返回 true；否则返回 false。
 *        结构合法但个别片段非法（表情编号越界、图片路径为空、类型不认识）时丢弃这些片段，
 *        其余照常还原，仍返回 true；片段数超过 kChatClipMaxSegments 时只取前面那些。
 */
bool ParseClipSegments(const std::string& data, std::vector<ChatClipSegment>& out);

} // namespace balloonchatwnd
