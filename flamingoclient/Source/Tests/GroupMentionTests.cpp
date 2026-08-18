/**
 *  GroupMentionTests.cpp
 *
 *  群聊「@提醒」纯逻辑（BalloonChatWnd/GroupMentionModel.h）的测试，覆盖
 *  docs/群聊@提醒方案.md §10 列出的用例：
 *    · 偏移换算 —— UTF-8 字节偏移 → UTF-16 字符下标，含中英混排、emoji、越界。
 *    · 文本重扫 —— 发送前按最终文本重新定位 @ 片段，含名字被改、同名成员、邮箱误判。
 *    · 是否@我 —— 只看 uid 不看文本，含 @所有人 与未登录态。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/GroupMentionModel.h"
#include "../net/MessageProtocolClient.h"

using namespace balloonchatwnd;

namespace {

// 造一条协议侧片段，省得每个用例都写四行。
MentionSpan MakeSpan(long long uid, int off, int len)
{
    MentionSpan s;
    s.m_uid = uid;
    s.m_off = off;
    s.m_len = len;
    return s;
}

// 造一条重扫候选。
MentionCandidate MakeCandidate(long long uid, const char* name)
{
    MentionCandidate c;
    c.m_uid = uid;
    c.m_displayName = name;
    return c;
}

// 本文件里用到的测试文本一律显式写成 UTF-8 字节，<u>不</u>依赖源文件编码 ——
// 这个测试工程要在不同机器上编译，源文件编码一旦被工具改掉，字面量里的中文
// 就会变成别的字节，断言里的偏移数字随之失效且极难排查。
// "张三" = E5 BC A0 E4 B8 89（6 字节）
const char* kZhangSan = "\xE5\xBC\xA0\xE4\xB8\x89";
// "张三丰" = 张三 + E4 B8 B0（9 字节）
const char* kZhangSanFeng = "\xE5\xBC\xA0\xE4\xB8\x89\xE4\xB8\xB0";
// "李四" = E6 9D 8E E5 9B 9B（6 字节）
const char* kLiSi = "\xE6\x9D\x8E\xE5\x9B\x9B";
// "你好" = E4 BD A0 E5 A5 BD（6 字节）
const char* kNiHao = "\xE4\xBD\xA0\xE5\xA5\xBD";
// U+1F600 GRINNING FACE = F0 9F 98 80（4 字节 UTF-8 = 2 个 UTF-16 代理项）
const char* kEmoji = "\xF0\x9F\x98\x80";

// 本人 uid，用一个明显的常量避免与 0（未登录）混淆。
const long long kMyUid    = 10001;
const long long kOtherUid = 10002;

} // anonymous namespace

// =====================================================================
// MENTION1-5. 偏移换算（UTF-8 字节 → UTF-16 字符）
// =====================================================================

TEST_CASE("MENTION1 纯 ASCII 文本，字节偏移与字符下标一致", "[mention]")
{
    const std::string text = "@abc hi";

    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 0) == 0);
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 4) == 4);
    // 等于串长表示"末尾之后"，是合法边界。
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, (int)text.size()) == (int)text.size());
}

TEST_CASE("MENTION2 纯中文文本，1 汉字 = 3 字节 = 1 个 UTF-16 字符", "[mention]")
{
    const std::string text = std::string("@") + kZhangSan;   // '@' + 张三 = 1 + 6 字节

    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 0) == 0);
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 1) == 1);   // '@' 之后
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 4) == 2);   // "张"之后
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 7) == 3);   // "张三"之后 = 整串末尾
}

TEST_CASE("MENTION3 中英混排，@ 出现在中文之后偏移仍正确", "[mention]")
{
    // "你好 @张三" ：你好(6) + 空格(1) + '@'(1) + 张三(6) = 14 字节
    const std::string text = std::string(kNiHao) + " @" + kZhangSan;

    REQUIRE((int)text.size() == 14);
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 7) == 3);    // '@' 所在字节 → 第 3 个字符
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 14) == 6);   // 末尾：你好(2)+空格(1)+@(1)+张三(2)
}

TEST_CASE("MENTION4 含 emoji 时按代理对算 2 个 UTF-16 字符", "[mention]")
{
    // emoji(4 字节) + "@" + 张三(6 字节)
    const std::string text = std::string(kEmoji) + "@" + kZhangSan;

    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 0) == 0);
    // emoji 之后：UTF-8 走了 4 字节，UTF-16 走了 2 个单元（代理对）。
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 4) == 2);
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 5) == 3);    // '@' 之后
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 11) == 5);   // 末尾
}

TEST_CASE("MENTION5 偏移越界或落在多字节序列中间时返回 -1", "[mention]")
{
    const std::string text = std::string("@") + kZhangSan;   // 7 字节

    REQUIRE(Utf8ByteOffsetToUtf16Index(text, -1) == -1);              // 负偏移
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 8) == -1);               // 超出串长
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 2) == -1);               // 落在"张"的中间
    REQUIRE(Utf8ByteOffsetToUtf16Index(text, 3) == -1);               // 同上
}

// =====================================================================
// MENTION6-10. 发送前重扫
// =====================================================================

TEST_CASE("MENTION6 单个 @ 能扫出正确的偏移与长度", "[mention]")
{
    // "@张三 你好"
    const std::string text = std::string("@") + kZhangSan + " " + kNiHao;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 1);
    REQUIRE(spans[0].m_uid == kMyUid);
    REQUIRE(spans[0].m_off == 0);
    REQUIRE(spans[0].m_len == 7);   // '@'(1) + 张三(6)，尾随空格不计入
}

TEST_CASE("MENTION7 连续两个 @ 各自偏移正确", "[mention]")
{
    // "@张三 @李四 开会" —— 只验证两个片段的位置，正文尾部内容无关紧要
    const std::string text = std::string("@") + kZhangSan + " @" + kLiSi + " ok";

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));
    cands.push_back(MakeCandidate(kOtherUid, kLiSi));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].m_uid == kMyUid);
    REQUIRE(spans[0].m_off == 0);
    REQUIRE(spans[0].m_len == 7);
    REQUIRE(spans[1].m_uid == kOtherUid);
    REQUIRE(spans[1].m_off == 8);   // 0 + 7 + 空格(1)
    REQUIRE(spans[1].m_len == 7);
}

TEST_CASE("MENTION8 名字被删掉一半时该候选作废", "[mention]")
{
    // 用户把 "@张三" 改成了 "@张" —— 候选记的是"张三"，扫不到就该丢弃
    const std::string text = "@\xE5\xBC\xA0 hi";   // '@' + "张" + " hi"

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.empty());
}

TEST_CASE("MENTION9 同名成员按出现顺序各归各的 uid", "[mention]")
{
    // 群里有两个都叫"张三"的人，文本里 @ 了两次 —— 第 N 次归第 N 条同名候选
    const std::string text = std::string("@") + kZhangSan + " @" + kZhangSan;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));
    cands.push_back(MakeCandidate(kOtherUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].m_uid == kMyUid);
    REQUIRE(spans[1].m_uid == kOtherUid);
    REQUIRE(spans[0].m_off == 0);
    REQUIRE(spans[1].m_off == 8);
}

TEST_CASE("MENTION9b 前缀同名时取最长匹配，不被短名抢走", "[mention]")
{
    // 文本 @ 的是"张三丰"，候选里同时有"张三"和"张三丰"
    const std::string text = std::string("@") + kZhangSanFeng;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));         // 短名在前，容易误抢
    cands.push_back(MakeCandidate(kOtherUid, kZhangSanFeng));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 1);
    REQUIRE(spans[0].m_uid == kOtherUid);
    REQUIRE(spans[0].m_len == 10);   // '@'(1) + 张三丰(9)
}

TEST_CASE("MENTION10 邮箱里的 @ 不被误判成 mention", "[mention]")
{
    // '@' 前面既不是文本开头也不是空白，不该当成 mention 起点
    const std::string text = std::string("mail abc@") + kZhangSan;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.empty());
}

// =====================================================================
// MENTION11-15. 是否 @ 到我
// =====================================================================

TEST_CASE("MENTION11 片段含本人 uid 时命中", "[mention]")
{
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kOtherUid, 0, 7));
    spans.push_back(MakeSpan(kMyUid, 8, 7));

    REQUIRE(IsMentioningMe(spans, false, kMyUid));
}

TEST_CASE("MENTION12 片段不含本人 uid 时不命中", "[mention]")
{
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kOtherUid, 0, 7));

    REQUIRE_FALSE(IsMentioningMe(spans, false, kMyUid));
}

TEST_CASE("MENTION13 @所有人 一律命中", "[mention]")
{
    std::vector<MentionSpan> spans;   // 故意留空：@所有人 不依赖 users 列表

    REQUIRE(IsMentioningMe(spans, true, kMyUid));
}

TEST_CASE("MENTION14 既无片段又非@所有人时不命中", "[mention]")
{
    const std::vector<MentionSpan> spans;

    REQUIRE_FALSE(IsMentioningMe(spans, false, kMyUid));
}

TEST_CASE("MENTION15 本人 uid 为 0（未登录）时一律不命中", "[mention]")
{
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMyUid, 0, 7));

    REQUIRE_FALSE(IsMentioningMe(spans, false, 0));
    REQUIRE_FALSE(IsMentioningMe(spans, true, 0));   // @所有人 也不例外
}

// =====================================================================
// MENTION16-18. 协议片段 → 渲染片段的换算
// =====================================================================

TEST_CASE("MENTION16 换算成字符口径并标好 isMe", "[mention]")
{
    // "@张三 @李四" ：两个片段，第一个是我
    const std::string text = std::string("@") + kZhangSan + " @" + kLiSi;

    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMyUid, 0, 7));
    spans.push_back(MakeSpan(kOtherUid, 8, 7));

    const std::vector<RenderSpan> out = ConvertSpansForRender(text, spans, kMyUid);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].m_off == 0);
    REQUIRE(out[0].m_len == 3);      // '@' + 张 + 三 = 3 个 UTF-16 字符
    REQUIRE(out[0].m_isMe);
    REQUIRE(out[1].m_off == 4);      // @张三(3) + 空格(1)
    REQUIRE(out[1].m_len == 3);
    REQUIRE_FALSE(out[1].m_isMe);
}

TEST_CASE("MENTION17 越界或长度非正的片段被丢弃，不影响其余片段", "[mention]")
{
    const std::string text = std::string("@") + kZhangSan;   // 7 字节

    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMyUid, 0, 7));      // 正常
    spans.push_back(MakeSpan(kOtherUid, 0, 0));   // 长度非正
    spans.push_back(MakeSpan(kOtherUid, 5, 99));  // 长度越界
    spans.push_back(MakeSpan(kOtherUid, 2, 3));   // 起点落在"张"中间

    const std::vector<RenderSpan> out = ConvertSpansForRender(text, spans, kMyUid);

    REQUIRE(out.size() == 1);
    REQUIRE(out[0].m_uid == kMyUid);
}

TEST_CASE("MENTION18 未登录（uid 为 0）时片段一律不标 isMe", "[mention]")
{
    const std::string text = std::string("@") + kZhangSan;

    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMyUid, 0, 7));

    const std::vector<RenderSpan> anon = ConvertSpansForRender(text, spans, 0);
    REQUIRE(anon.size() == 1);
    REQUIRE_FALSE(anon[0].m_isMe);
}

// =====================================================================
// MENTION19-22. 协议往返（content.at 的构造与解析）
// =====================================================================

namespace {

// 走一遍"构造发送请求 → 当成推送体解析回来"。发送请求与推送体的 content 结构
// 一致（服务端原样转发 content），故可用解析推送的入口验证构造结果。
//
// 构造出来的请求缺 msgid / from（那两个字段是服务端回填的），解析推送时它们缺失
// 会被判非法。这里不引 jsoncpp 重新序列化，直接在紧凑 JSON 的开头花括号之后插入
// 这两个字段 —— buildSendRequest 产出的必定是以 '{' 开头的紧凑对象，插入合法。
// 返回解析是否成功；出参给出解析到的 @ 信息。
bool RoundTripGroupText(const std::string& text,
                        const std::vector<MentionSpan>& ats,
                        bool atAll,
                        net::MessageNotify& out)
{
    std::string req = net::MessageProtocolClient::buildSendGroupTextRequest(
        2001, "cmid-1", text, ats, atAll);
    if (req.empty() || req[0] != '{')
        return false;

    req.insert(1, "\"msgid\":\"9001\",\"from\":\"10002\",");
    return net::MessageProtocolClient::parseMessageNotify(req, out);
}

} // anonymous namespace

TEST_CASE("MENTION19 带两个 @ 的 content 往返后字段一致", "[mention]")
{
    const std::string text = std::string("@") + kZhangSan + " @" + kLiSi + " ok";

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));
    ats.push_back(MakeSpan(kOtherUid, 8, 7));

    net::MessageNotify out;
    REQUIRE(RoundTripGroupText(text, ats, false, out));

    REQUIRE(out.m_text == text);
    REQUIRE_FALSE(out.m_atAll);
    REQUIRE(out.m_ats.size() == 2);
    REQUIRE(out.m_ats[0].m_uid == kMyUid);
    REQUIRE(out.m_ats[0].m_off == 0);
    REQUIRE(out.m_ats[0].m_len == 7);
    REQUIRE(out.m_ats[1].m_uid == kOtherUid);
    REQUIRE(out.m_ats[1].m_off == 8);
    REQUIRE(out.m_ats[1].m_len == 7);
}

TEST_CASE("MENTION20 @所有人 往返后 atAll 为真", "[mention]")
{
    const std::vector<MentionSpan> ats;   // @所有人 可以不带 users

    net::MessageNotify out;
    REQUIRE(RoundTripGroupText("hello", ats, true, out));

    REQUIRE(out.m_atAll);
    REQUIRE(out.m_ats.empty());
}

TEST_CASE("MENTION21 不含 at 字段的老消息解析后为空，不报错", "[mention]")
{
    // 走不带 @ 的旧重载构造，content 里根本没有 at 字段
    const std::string req = net::MessageProtocolClient::buildSendGroupTextRequest(
        2001, "cmid-2", "hello");
    REQUIRE(req.find("\"at\"") == std::string::npos);   // 确认压根没写 at

    // 空 ats + 非 @所有人 的新重载也不该写 at，两条路径行为一致
    const std::vector<MentionSpan> empty;
    const std::string req2 = net::MessageProtocolClient::buildSendGroupTextRequest(
        2001, "cmid-3", "hello", empty, false);
    REQUIRE(req2.find("\"at\"") == std::string::npos);

    net::MessageNotify out;
    REQUIRE(RoundTripGroupText("hello", empty, false, out));
    REQUIRE_FALSE(out.m_atAll);
    REQUIRE(out.m_ats.empty());
}

TEST_CASE("MENTION22 at 字段类型异常时安全降级为空，不崩", "[mention]")
{
    // users 不是数组、uid 不是字符串、off/len 缺失 —— 各种脏数据混在一起
    const std::string body =
        "{\"msgid\":\"9001\",\"from\":\"10002\",\"ctype\":2,\"cid\":\"2001\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\",\"at\":{\"all\":\"x\",\"users\":123}}}";

    net::MessageNotify out;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(body, out));
    REQUIRE(out.m_text == "hi");
    REQUIRE_FALSE(out.m_atAll);    // all 是字符串 → 按 false
    REQUIRE(out.m_ats.empty());    // users 不是数组 → 按空

    // users 是数组但元素畸形（非对象 / 缺 off / len 为负）
    const std::string body2 =
        "{\"msgid\":\"9001\",\"from\":\"10002\",\"ctype\":2,\"cid\":\"2001\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\",\"at\":{\"all\":0,\"users\":"
        "[5,{\"uid\":\"1\"},{\"uid\":\"2\",\"off\":0,\"len\":-3},"
        "{\"uid\":\"3\",\"off\":0,\"len\":2}]}}}";

    net::MessageNotify out2;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(body2, out2));
    REQUIRE(out2.m_ats.size() == 1);   // 只有最后那条合法
    REQUIRE(out2.m_ats[0].m_uid == 3);
}

// =====================================================================
// MENTION23-29. @所有人
// =====================================================================

TEST_CASE("MENTION23 只有群主与管理员能 @所有人", "[mention]")
{
    REQUIRE_FALSE(CanMentionAll(kGroupRoleMemberVal));   // 普通成员不行
    REQUIRE(CanMentionAll(kGroupRoleAdminVal));          // 管理员可以
    REQUIRE(CanMentionAll(kGroupRoleOwnerVal));          // 群主可以

    // 越界 / 未知角色一律按普通成员处理，宁可少给权限也不错给。
    REQUIRE_FALSE(CanMentionAll(-1));
    REQUIRE_FALSE(CanMentionAll(99));
}

TEST_CASE("MENTION24 「@所有人」能被重扫出来，uid 为 0", "[mention]")
{
    // "@所有人 明天开会"：候选表里「@所有人」与普通成员并存
    const std::string text = std::string("@") + kMentionAllNameUtf8 + " ok";

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMentionUidAll, kMentionAllNameUtf8));
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 1);
    REQUIRE(spans[0].m_uid == kMentionUidAll);
    REQUIRE(spans[0].m_off == 0);
    REQUIRE(spans[0].m_len == 10);   // '@'(1) + 所有人(9)
}

TEST_CASE("MENTION25 「@所有人」与具体成员可以同时出现", "[mention]")
{
    // "@所有人 @张三 辛苦了"
    const std::string text = std::string("@") + kMentionAllNameUtf8 + " @" + kZhangSan + " ok";

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMentionUidAll, kMentionAllNameUtf8));
    cands.push_back(MakeCandidate(kOtherUid, kZhangSan));

    const std::vector<MentionSpan> spans = RescanMentions(text, cands);

    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].m_uid == kMentionUidAll);
    REQUIRE(spans[0].m_off == 0);
    REQUIRE(spans[1].m_uid == kOtherUid);
    REQUIRE(spans[1].m_off == 11);   // 0 + 10 + 空格(1)
}

TEST_CASE("MENTION26 「@所有人」那一段标成 isMe，@别人 那段不标", "[mention]")
{
    // 关键语义：整条带 @所有人 <u>不能</u>把 @别人 的片段也染成"@我"色 ——
    // "@所有人 @张三 辛苦了" 里那句 `@张三` 说的是张三，不是我。
    const std::string text = std::string("@") + kMentionAllNameUtf8 + " @" + kZhangSan + " ok";

    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMentionUidAll, 0, 10));
    spans.push_back(MakeSpan(kOtherUid, 11, 7));

    const std::vector<RenderSpan> out = ConvertSpansForRender(text, spans, kMyUid);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0].m_isMe);          // @所有人 这段确实点到了我
    REQUIRE_FALSE(out[1].m_isMe);    // @张三 这段与我无关
}

TEST_CASE("MENTION27 atAll 标志为真即命中，不依赖片段", "[mention]")
{
    const std::vector<MentionSpan> spans;   // 故意留空

    REQUIRE(IsMentioningMe(spans, true, kMyUid));
    REQUIRE_FALSE(IsMentioningMe(spans, true, 0));   // 未登录仍不命中
}

TEST_CASE("MENTION28 正文含「@所有人」片段时命中，即便 atAll 标志漏写", "[mention]")
{
    // 防御性：不依赖对端一定把 all 标志位写对，只要片段里有 uid=0 就算 @ 到我。
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMentionUidAll, 0, 10));

    REQUIRE(IsMentioningMe(spans, false, kMyUid));
}

TEST_CASE("MENTION29 @所有人 的协议往返：all=1 且片段 uid 为 0", "[mention]")
{
    const std::string text = std::string("@") + kMentionAllNameUtf8 + " ok";

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMentionUidAll, 0, 10));

    net::MessageNotify out;
    REQUIRE(RoundTripGroupText(text, ats, true, out));

    REQUIRE(out.m_atAll);
    REQUIRE(out.m_ats.size() == 1);
    REQUIRE(out.m_ats[0].m_uid == kMentionUidAll);
    REQUIRE(out.m_ats[0].m_off == 0);
    REQUIRE(out.m_ats[0].m_len == 10);
    REQUIRE(IsMentioningMe(out.m_ats, out.m_atAll, kMyUid));
}

// =====================================================================
// MENTION30-36. 历史消息的 @ 还原
//
//   背景：重开聊天窗后，历史里带 @ 的消息高亮全没了。根因是群聊窗渲染历史时
//   只填了文本 / 字体 / msgid / 发送人，唯独漏了 ats，而实时推送那条路是填了的
//   —— 两条路各写各的，迟早跑偏。修复的做法是把「取 @ 数据」收敛成
//   BuildRenderSpansForMessage 一个入口，两条路都调它。
//
//   本组用例覆盖两件事：一是历史响应<u>确实</u>把 content.at 带回来了（否则光改
//   客户端没用，得动服务端），二是这个统一入口的行为契约。
// =====================================================================

namespace {

// 把一条群文本消息包成一个「拉历史」响应的 JSON。消息对象的形状与
// RoundTripGroupText 里那条一致 —— 历史响应的每个数组元素走的也是
// fillMessageFromObject，与实时推送同一个解析函数。
std::string MakeHistoryResponseWithOneMessage(const std::string& text,
                                              const std::vector<MentionSpan>& ats,
                                              bool atAll)
{
    std::string msg = net::MessageProtocolClient::buildSendGroupTextRequest(
        2001, "cmid-h1", text, ats, atAll);
    if (msg.empty() || msg[0] != '{')
    {
        return std::string();
    }
    msg.insert(1, "\"msgid\":\"9001\",\"from\":\"10002\",\"timestamp\":\"1700000000000\",");

    std::string resp = "{\"code\":0,\"ctype\":2,\"cid\":\"2001\",\"hasmore\":false,\"messages\":[";
    resp += msg;
    resp += "]}";
    return resp;
}

} // anonymous namespace

TEST_CASE("MENTION30 拉历史的响应里 content.at 原样带得回来", "[mention][history]")
{
    const std::string text = std::string("@") + kZhangSan + " " + kNiHao;

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));   // "@张三" = 1 + 6 字节

    const std::string resp = MakeHistoryResponseWithOneMessage(text, ats, false);
    REQUIRE_FALSE(resp.empty());

    net::SyncMessagesResult out;
    REQUIRE(net::MessageProtocolClient::parseSyncMessagesResponse(resp, out));
    REQUIRE(out.m_code == 0);
    REQUIRE(out.m_messages.size() == 1);

    // 关键：历史条目里的 @ 数据与发出去时一致 —— 服务端把整个 content 原样落库
    // 并原样返回，所以这条链路上不需要任何服务端改动。
    REQUIRE(out.m_messages[0].m_text == text);
    REQUIRE(out.m_messages[0].m_ats.size() == 1);
    CHECK(out.m_messages[0].m_ats[0].m_uid == kMyUid);
    CHECK(out.m_messages[0].m_ats[0].m_off == 0);
    CHECK(out.m_messages[0].m_ats[0].m_len == 7);
}

TEST_CASE("MENTION31 历史整链：响应解析后能还原出渲染片段", "[mention][history]")
{
    const std::string text = std::string("@") + kZhangSan + " " + kNiHao;

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));

    const std::string resp = MakeHistoryResponseWithOneMessage(text, ats, false);
    REQUIRE_FALSE(resp.empty());

    net::SyncMessagesResult out;
    REQUIRE(net::MessageProtocolClient::parseSyncMessagesResponse(resp, out));
    REQUIRE(out.m_messages.size() == 1);

    // 群聊窗渲染历史时就该这么做（漏了这一步就是用户报的「重开窗口 @ 没颜色」）。
    const net::MessageNotify& m = out.m_messages[0];
    std::vector<RenderSpan> render =
        BuildRenderSpansForMessage(m.m_text, m.m_ats, m.m_atAll, kMyUid);

    REQUIRE(render.size() == 1);
    CHECK(render[0].m_uid == kMyUid);
    CHECK(render[0].m_off == 0);
    CHECK(render[0].m_len == 3);    // "@张三" 三个 UTF-16 字符
    CHECK(render[0].m_isMe);        // @ 的正是本人 → 用醒目配色
}

TEST_CASE("MENTION32 不带 @ 的消息还原出空片段", "[mention][history]")
{
    std::vector<RenderSpan> render =
        BuildRenderSpansForMessage(kNiHao, std::vector<MentionSpan>(), false, kMyUid);

    CHECK(render.empty());
}

TEST_CASE("MENTION33 只置了 @所有人 标志但没有片段时还原出空片段", "[mention][history]")
{
    // 老版本 / 异常发送方可能只写了 all=1 而没写片段。此时没有任何可高亮的区间，
    // 按纯文本渲染即可，不能因为标志位为真就凭空造出片段。
    std::vector<RenderSpan> render =
        BuildRenderSpansForMessage(kNiHao, std::vector<MentionSpan>(), true, kMyUid);

    CHECK(render.empty());
}

TEST_CASE("MENTION34 有片段时与 ConvertSpansForRender 产出完全一致", "[mention][history]")
{
    // BuildRenderSpansForMessage 只比 ConvertSpansForRender 多一层「本条带没带 @」
    // 的判断，换算本身不能有任何差异 —— 否则实时与历史两条路又会看起来不一样。
    const std::string text = std::string("@") + kZhangSan + " @" + kLiSi + " ok";

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));
    ats.push_back(MakeSpan(kOtherUid, 8, 7));

    std::vector<RenderSpan> viaBuild = BuildRenderSpansForMessage(text, ats, false, kMyUid);
    std::vector<RenderSpan> viaConvert = ConvertSpansForRender(text, ats, kMyUid);

    REQUIRE(viaBuild.size() == viaConvert.size());
    for (std::size_t i = 0; i < viaBuild.size(); ++i)
    {
        INFO("片段序号 = " << i);
        CHECK(viaBuild[i].m_uid == viaConvert[i].m_uid);
        CHECK(viaBuild[i].m_off == viaConvert[i].m_off);
        CHECK(viaBuild[i].m_len == viaConvert[i].m_len);
        CHECK(viaBuild[i].m_isMe == viaConvert[i].m_isMe);
    }
}

TEST_CASE("MENTION35 isMe 按当前登录用户重新判定", "[mention][history]")
{
    // 同一条历史消息，换个账号登录再看，@我 的配色应当随之变化 —— isMe 不是
    // 消息自带的属性，而是「相对当前登录者」的属性。
    const std::string text = std::string("@") + kZhangSan + " " + kNiHao;

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));

    std::vector<RenderSpan> asMe = BuildRenderSpansForMessage(text, ats, false, kMyUid);
    REQUIRE(asMe.size() == 1);
    CHECK(asMe[0].m_isMe);

    std::vector<RenderSpan> asOther = BuildRenderSpansForMessage(text, ats, false, kOtherUid);
    REQUIRE(asOther.size() == 1);
    CHECK_FALSE(asOther[0].m_isMe);
}

TEST_CASE("MENTION36 未登录（myUid 为 0）时片段一律不算 @我", "[mention][history]")
{
    const std::string text = std::string("@") + kZhangSan + " " + kNiHao;

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMyUid, 0, 7));

    std::vector<RenderSpan> render = BuildRenderSpansForMessage(text, ats, false, 0);

    REQUIRE(render.size() == 1);
    CHECK_FALSE(render[0].m_isMe);   // 无从判定时按"不是@我"处理，不误标成醒目色
}

// =====================================================================
// MENTION37-38. 「再 @ 一次 TA」为什么必须补前导空格
//
//   气泡里 @某人 → 右键 →「再 @ 一次 TA」是在输入框光标处直接插入 `@显示名 `。
//   若光标前一个字符不是空白（典型：用户已经打了"你好"），插出来的就是
//   "你好@张三 " —— 重扫只认「位于文本开头、或紧跟空白之后」的 '@'，这一条
//   扫不出来，发出去只是一段普通文字，@ 不生效、对方收不到提醒。
//   这两条用例把这个前提钉住：实现里 InsertMentionAtCaret 据此在必要时先补一个空格。
// =====================================================================

TEST_CASE("MENTION37 紧跟非空白的 @ 扫不出来", "[mention][insert]")
{
    // "你好@张三"：'@' 前面是"好"，不是空白也不是文本开头。
    const std::string text = std::string(kNiHao) + "@" + kZhangSan;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    std::vector<MentionSpan> spans = RescanMentions(text, cands);
    CHECK(spans.empty());
}

TEST_CASE("MENTION38 补一个空格之后就能扫出来", "[mention][insert]")
{
    // "你好 @张三"：与上一条只差中间那个空格。
    const std::string text = std::string(kNiHao) + " @" + kZhangSan;

    std::vector<MentionCandidate> cands;
    cands.push_back(MakeCandidate(kMyUid, kZhangSan));

    std::vector<MentionSpan> spans = RescanMentions(text, cands);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].m_uid == kMyUid);
    // 偏移落在 '@' 上：你好(6 字节) + 空格(1 字节) = 7。
    CHECK(spans[0].m_off == 7);
    CHECK(spans[0].m_len == 7);   // '@' + 张三 = 1 + 6 字节
}

// =====================================================================
// MENTION39-43. 发送时的 @所有人 标志推导
//
//   协议里「本条是不是 @所有人」有两处表述：content.at.all 标志位，以及片段数组里
//   那一段 uid==0。对端判"是否@到我"两处都看（见 IsMentioningMe），所以发送时两者
//   必须一致。此前发送路径把标志位写死成 false，正文里就算有 @所有人 也传不出去；
//   现在改成由片段推导（HasMentionAll），只有一个真相。
// =====================================================================

TEST_CASE("MENTION39 没有片段时不算 @所有人", "[mention][atall]")
{
    CHECK_FALSE(HasMentionAll(std::vector<MentionSpan>()));
}

TEST_CASE("MENTION40 只 @ 了具体的人不算 @所有人", "[mention][atall]")
{
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMyUid, 0, 7));
    spans.push_back(MakeSpan(kOtherUid, 8, 7));

    CHECK_FALSE(HasMentionAll(spans));
}

TEST_CASE("MENTION41 含 uid 为 0 的片段即算 @所有人", "[mention][atall]")
{
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMentionUidAll, 0, 10));

    CHECK(HasMentionAll(spans));
}

TEST_CASE("MENTION42 @所有人 与 @某人 混排也算", "[mention][atall]")
{
    // "@所有人 @张三 辛苦了" 这类句子：只要有那一段就该置标志位。
    std::vector<MentionSpan> spans;
    spans.push_back(MakeSpan(kMentionUidAll, 0, 10));
    spans.push_back(MakeSpan(kOtherUid, 11, 7));

    CHECK(HasMentionAll(spans));
}

TEST_CASE("MENTION43 推导出的标志位经协议往返后对端能判出 @到我", "[mention][atall]")
{
    // 端到端把这条链路钉住：正文里有 @所有人 → HasMentionAll 推出 true →
    // 写进 content.at.all → 对端解析出 m_atAll → IsMentioningMe 命中。
    // 发送路径若又退回写死 false，这条会红。
    const std::string text = std::string("@") + kMentionAllNameUtf8 + " ok";

    std::vector<MentionSpan> ats;
    ats.push_back(MakeSpan(kMentionUidAll, 0, 10));   // '@' + 所有人 = 1 + 9 字节

    net::MessageNotify out;
    REQUIRE(RoundTripGroupText(text, ats, HasMentionAll(ats), out));

    CHECK(out.m_atAll);
    REQUIRE(out.m_ats.size() == 1);
    CHECK(out.m_ats[0].m_uid == kMentionUidAll);
    // 群里任何人收到这条都算被 @ 到。
    CHECK(IsMentioningMe(out.m_ats, out.m_atAll, kMyUid));
    CHECK(IsMentioningMe(out.m_ats, out.m_atAll, kOtherUid));
}
