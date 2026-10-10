/**
 *  帖子群的文字处理（纯函数，docs/帖子群方案.md 4.2~4.4）：
 *    - UTF-8 与 UTF-16 互转；
 *    - 按码点计字数（与服务端 ThreadProtocol::utf8Chars 的口径一致：一个汉字、一个字母、一个 4 字节字符都算 1）；
 *    - 表情：正文在线上以 /f["编号"] 编码（与聊天消息相同，见 Source/UI/BalloonChat/BalloonChatMsgFormat.cpp），
 *      输入框与帖子里显示为「[表情名]」。WireToDisplay / DisplayToWire 互转，字面的 /f["n"] 用 //f["n"] 转义；
 *    - 截取摘要：帖子卡片的正文截取、多行折成一行。
 *
 *  表情名表由宿主提供（IThreadGroupHost::GetFaceInfo），这里用 FaceTable 接收，与界面无关，便于单元测试。
 *
 *  典型用法：
 *      threadgroup::FaceTable faces;
 *      faces.Add(0, L"微笑");
 *      std::wstring wire = threadgroup::DisplayToWire(L"你好[微笑]", faces);   // L"你好/f[\"0\"]"
 *      std::wstring shown = threadgroup::WireToDisplay(wire, faces, L"[表情]");
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace threadgroup {

/**
 *  表情编号与表情名的对照表。
 */
class FaceTable
{
public:
    /**
     *  登记一个表情。同一编号或同名重复登记时以最后一次为准。
     *  @param faceId 表情编号（0~999）
     *  @param name   表情名（不含方括号），为空时忽略
     */
    void Add(int faceId, const std::wstring& name);

    /**
     *  按编号取表情名。
     *  @return 找到返回 true
     */
    bool NameOf(int faceId, std::wstring& name) const;

    /**
     *  按表情名取编号。
     *  @return 找到返回 true
     */
    bool IdOf(const std::wstring& name, int& faceId) const;

    //登记的表情个数
    size_t Count() const;

private:
    std::map<int, std::wstring> m_names;   //编号 → 名字
    std::map<std::wstring, int> m_ids;     //名字 → 编号
};

//正文里的一段：文字或一个表情
struct TextSegment
{
    bool         m_face;    //true 为表情，false 为文字
    int          m_faceId;  //表情编号（m_face 为 true 时有效）
    std::wstring m_text;    //文字（m_face 为 false 时有效）

    TextSegment()
        : m_face(false)
        , m_faceId(0)
    {
    }
};

/**
 *  UTF-8 → UTF-16。非法字节按系统转换规则替换。
 */
std::wstring Utf8ToWide(const std::string& text);

/**
 *  UTF-16 → UTF-8。
 */
std::string WideToUtf8(const std::wstring& text);

/**
 *  按码点计字数：代理对算 1 个字符，与服务端按 UTF-8 码点计数的结果相同。
 */
int CountChars(const std::wstring& text);

/**
 *  把线上正文拆成文字段与表情段（//f["n"] 还原为字面的 /f["n"]）。
 */
std::vector<TextSegment> ParseWire(const std::wstring& wire);

/**
 *  从 text[i] 起尝试解析一个 /f["编号"] 表情标签（ParseWire 用的同一套规则，@ 片段的位置换算也要用，ThreadMention.cpp）。
 *  @param faceId [出参] 表情编号
 *  @param next   [出参] 标签之后的位置
 *  @return 命中返回 true
 */
bool ParseFaceTagAt(const std::wstring& text, size_t i, int& faceId, size_t& next);

/**
 *  线上正文 → 显示文字：表情换成「[表情名]」，表情库里没有的编号换成 unknownFace。
 *  @param wire        线上正文
 *  @param faces       表情名表
 *  @param unknownFace 不认识的表情显示成什么（通常是 TR 过的「[表情]」）
 */
std::wstring WireToDisplay(const std::wstring& wire, const FaceTable& faces, const std::wstring& unknownFace);

/**
 *  显示文字（输入框里的内容）→ 线上正文：「[表情名]」中名字在表情库里的换成 /f["编号"]，其余原样；
 *  文字里本来就有、会被误认成表情编码的 /f["n"] 在前面补一个 '/' 转义；换行统一为 LF（CR、CRLF 都换成 LF）。
 */
std::wstring DisplayToWire(const std::wstring& display, const FaceTable& faces);

/**
 *  判断文字是否只有空白（半角空格、制表、回车、换行与全角空格），与服务端判定「空内容」的口径一致。
 */
bool IsBlank(const std::wstring& text);

/**
 *  生成一行摘要：换行与连续空白折成一个空格，去掉首尾空白，最多保留 maxChars 个字符（按码点计）。
 *  @param text     原文
 *  @param maxChars 最多保留的字符数（大于 0）
 *  @param cut      [出参] 是否截断了
 */
std::wstring MakeDigest(const std::wstring& text, int maxChars, bool& cut);

/**
 *  截取前 maxChars 个字符（按码点计，不拆开代理对），保留换行。
 *  @param cut [出参] 是否截断了
 */
std::wstring TruncateChars(const std::wstring& text, int maxChars, bool& cut);

/**
 *  64 位 ID 写成十进制文本（协议里的 ID 一律是十进制字符串）。
 */
std::string IdToString(int64_t id);

} // namespace threadgroup
