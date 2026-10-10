/**
 *  代码片段批注的敏感词端到端联调用例（Suite CNLIVE），CodeNotesLiveTests.cpp
 *
 *  背景见 docs/代码片段方案.md 第 10.5 节：服务端的敏感词检查把代码片段 content.notes 里每条批注的说明与正文
 *  一并收集（chatserversrc/MessageProtocol.cpp 的 collectTextsFromContent），命中时与正文命中一样以
 *  error_code_sensitive_word 拒发。服务端的收集函数有 doctest（SW-P7），本文件验证真实的 chatserver 确实
 *  这样处理客户端组出的请求：用客户端自己的加密与协议代码登录、组帧，绕过界面直接发送。
 *
 *    CNLIVE1 登录后发四条代码片段：批注说明含测试敏感词、批注说明含大小写不同的同一个词、正文含测试敏感词
 *            （对照）三条都收到 507 且回执带回 clientmsgid、没有 msgid；随后心跳照常有应答（连接没有被断开）；
 *            批注说明干净的一条照常发出、带回 msgid。
 *    CNLIVE2 追加批注（docs/代码片段方案.md 第 11 节）：MY0001 发一段带批注的代码；MY0002 引用它，带上 MY0001 的批注
 *            （写出作者 userid）与自己新加的一条（标「新」、作者缺省）发回去，MY0001 收到的推送里批注的作者、「新」标记
 *            与引用的原消息都原样到达；追加的批注说明含测试敏感词时拒发（507）。
 *
 *  <u>默认不参与常规测试</u>：用例带 [.live] 标签，Catch2 会跳过它。要跑就显式指定标签：
 *
 *      FlamingoUnit.exe "[codenoteslive]"
 *
 *  跑之前在 WSL 里起一个独立的 chatserver 实例（不碰开发服务器与它的库）：
 *      python3 /mnt/e/flamingo/tmp/notes_live/setup_notes_live.py   （复制开发服务器的 chatserver 与库的在线备份、
 *                                                                    改端口为 kServerPort、给测试账号设口令、
 *                                                                    在副本库里加测试敏感词 kBadWord）
 *      bash /mnt/e/flamingo/tmp/notes_live/server.sh start          （跑完用 stop 停掉）
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string>
#include <vector>

//连接、按帧收发与三帧握手的脚手架（kTestServerPubKey 与开发服务器的身份公钥相同）
#include "LiveTestConn.h"

#include "../ClientVersion.h"              // FLAMINGO_CLIENT_VERSION
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/HandshakeRunner.h"
#include "../net/LoginProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../net/Sha256.h"

using namespace net;
using namespace livetest;

namespace {

// 独立 chatserver 实例的监听端口（tmp/notes_live/setup_notes_live.py 写进它的配置）
const unsigned short kServerPort = 28100;

// 登录用的测试账号与口令（setup 脚本只在副本库里给它设这个口令）
const char* const kAccount  = "MY0001";
const char* const kPassword = "selftest1234";

// CNLIVE2 里追加批注的一方（setup 脚本给它设了同一个口令），userid 即 kPeerUserId
const char* const kPeerAccount = "MY0002";

// 发消息的对端 userid（同一企业的 MY0002）
const int64_t kPeerUserId = 5002;

// setup 脚本只加在副本库里的测试敏感词，与它的大小写变体（服务端按归一化后的形式比对）
const char* const kBadWord      = "notesbadword";
const char* const kBadWordUpper = "NotesBadWord";

// 各帧的序列号。服务端会原样回填到应答里，据此认出是哪一帧的应答
const uint32_t kLoginSeq      = 1;
const uint32_t kNoteBadSeq    = 2;
const uint32_t kNoteUpperSeq  = 3;
const uint32_t kTextBadSeq    = 4;
const uint32_t kHeartbeatSeq  = 5;
const uint32_t kCleanSeq      = 6;
const uint32_t kBaseSeq       = 7;   // CNLIVE2：MY0001 发出原消息
const uint32_t kAnnotateSeq   = 8;   // CNLIVE2：MY0002 发出追加批注的消息
const uint32_t kAnnotateBadSeq = 9;  // CNLIVE2：追加的批注说明含敏感词

// 等某一帧的应答时最多跳过多少条无关的推送帧（登录后服务端会推送在线状态、会话变化等）
const int kMaxFramesToScan = 64;

// 一段干净的代码，三行
const char* const kCode = "int main()\n{\n    return 0;\n}";

// 把明文包体加密、组帧后发出（与 SendLimitLiveTests.cpp 的同名函数相同，各文件的匿名命名空间各留一份）
bool SendEncrypted(TestConn& conn, ClientHandshake& hs, uint16_t cmd, uint32_t seq, const std::string& body)
{
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!encodeFramePayload(cmd, seq, body, &hs.cipher(), 0, payload_compress_auto, wire, flags, originlen))
    {
        return false;
    }
    std::string frame;
    if (!encodeFrame(cmd, seq, flags, wire.data(), (uint32_t)wire.size(), originlen, frame))
    {
        return false;
    }
    return conn.SendAll(frame);
}

// 一直收到 cmd 与 seq 都匹配的那一帧为止，返回其明文包体。中途的推送帧也要按顺序解密（加密是流式的），解开后丢弃
bool RecvReply(TestConn& conn, ClientHandshake& hs, uint16_t cmd, uint32_t seq, std::string& plainOut)
{
    for (int i = 0; i < kMaxFramesToScan; ++i)
    {
        frame_header header;
        std::string  wire;
        if (!conn.RecvFrame(header, wire))
        {
            return false;   // 超时或对端关闭了连接
        }
        std::string plain;
        std::string err;
        if (!decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &hs.cipher(), plain, err))
        {
            return false;
        }
        if (header.cmd == cmd && header.seq == seq)
        {
            plainOut = plain;
            return true;
        }
    }
    return false;
}

// 带上时刻后缀的 clientmsgid：服务端按 clientmsgid 去重，重复运行本用例时不能撞上上一次的
std::string UniqueClientMsgId(const char* prefix)
{
    return std::string(prefix) + "-" + std::to_string((unsigned long long)::GetTickCount64());
}

// 一条批注（第 2～3 行）
std::vector<CodeNoteData> OneNote(const std::string& text)
{
    CodeNoteData note;
    note.m_from = 2;
    note.m_to = 3;
    note.m_text = text;
    return std::vector<CodeNoteData>(1, note);
}

// 发一条代码片段并取回执
bool SendCode(TestConn& conn, ClientHandshake& hs, uint32_t seq, const std::string& clientMsgId,
              const std::string& code, const std::vector<CodeNoteData>& notes, SendMessageResponse& resp)
{
    const std::string body = MessageProtocolClient::buildSendTextRequest(kPeerUserId, clientMsgId, code, TextFont(),
                                                                         ReplyQuote(), text_format_code, "cpp", notes);
    if (!SendEncrypted(conn, hs, cmd_send_message, seq, body))
    {
        return false;
    }
    std::string plain;
    if (!RecvReply(conn, hs, cmd_send_message, seq, plain))
    {
        return false;
    }
    return MessageProtocolClient::parseSendResponse(plain, resp);
}

// 以 account 登录（口令同 kPassword）
bool LoginAs(TestConn& conn, ClientHandshake& hs, const char* account, const char* deviceId, LoginResponse& resp)
{
    LoginRequestParams login;
    login.clientversion = FLAMINGO_CLIENT_VERSION;
    login.account       = account;
    login.password      = Sha256::hexDigest(std::string(kPassword));
    login.clienttype    = platform_pc;
    login.presence      = presence_online;
    login.deviceid      = deviceId;
    login.devicename    = "livetest";
    if (!SendEncrypted(conn, hs, cmd_login, kLoginSeq, LoginProtocolClient::buildLoginRequest(login)))
    {
        return false;
    }
    std::string plain;
    if (!RecvReply(conn, hs, cmd_login, kLoginSeq, plain))
    {
        return false;
    }
    return LoginProtocolClient::parseLoginResponse(plain, resp) && resp.code == 0;
}

// 发一条代码片段给 to，带上引用 quote，并取回执
bool SendCodeTo(TestConn& conn, ClientHandshake& hs, uint32_t seq, int64_t to, const std::string& clientMsgId,
                const std::string& code, const std::vector<CodeNoteData>& notes, const ReplyQuote& quote,
                SendMessageResponse& resp)
{
    const std::string body = MessageProtocolClient::buildSendTextRequest(to, clientMsgId, code, TextFont(), quote,
                                                                         text_format_code, "cpp", notes);
    if (!SendEncrypted(conn, hs, cmd_send_message, seq, body))
    {
        return false;
    }
    std::string plain;
    if (!RecvReply(conn, hs, cmd_send_message, seq, plain))
    {
        return false;
    }
    return MessageProtocolClient::parseSendResponse(plain, resp);
}

// 一直收到 msgid 为 msgid 的新消息推送为止；中途的其它推送帧按顺序解密后丢弃
bool RecvMessagePush(TestConn& conn, ClientHandshake& hs, int64_t msgid, MessageNotify& out)
{
    for (int i = 0; i < kMaxFramesToScan; ++i)
    {
        frame_header header;
        std::string  wire;
        if (!conn.RecvFrame(header, wire))
        {
            return false;
        }
        std::string plain;
        std::string err;
        if (!decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &hs.cipher(), plain, err))
        {
            return false;
        }
        if (header.cmd != cmd_message_notify)
        {
            continue;
        }
        MessageNotify notify;
        if (MessageProtocolClient::parseMessageNotify(plain, notify) && notify.m_msgid == msgid)
        {
            out = notify;
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("CNLIVE1 sensitive words in code notes are rejected by the server", "[.live][codenoteslive]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    INFO("cannot connect to the notes live chatserver on port " << kServerPort
         << "; see the setup steps at the top of this file");
    REQUIRE(conn.Connect(kServerPort));

    ClientHandshake hs;
    std::string reason;
    INFO("handshake failure reason: " << reason);
    REQUIRE(DoHandshake(conn, hs, reason));

    //登录
    LoginRequestParams login;
    login.clientversion = FLAMINGO_CLIENT_VERSION;
    login.account       = kAccount;
    login.password      = Sha256::hexDigest(std::string(kPassword));
    login.clienttype    = platform_pc;
    login.presence      = presence_online;
    login.deviceid      = "livetest-codenotes";
    login.devicename    = "livetest";
    REQUIRE(SendEncrypted(conn, hs, cmd_login, kLoginSeq, LoginProtocolClient::buildLoginRequest(login)));
    std::string loginPlain;
    REQUIRE(RecvReply(conn, hs, cmd_login, kLoginSeq, loginPlain));
    LoginResponse loginResp;
    REQUIRE(LoginProtocolClient::parseLoginResponse(loginPlain, loginResp));
    INFO("login response: " << loginPlain);
    REQUIRE(loginResp.code == 0);

    //正文干净、批注说明含测试敏感词：拒发
    const std::string noteBadId = UniqueClientMsgId("livetest-codenotes-notebad");
    SendMessageResponse noteBad;
    REQUIRE(SendCode(conn, hs, kNoteBadSeq, noteBadId, kCode,
                     OneNote(std::string("this pointer may be ") + kBadWord), noteBad));
    CHECK(noteBad.m_code == error_code_sensitive_word);
    CHECK(noteBad.m_clientmsgid == noteBadId);
    CHECK(noteBad.m_msgid == 0);

    //批注说明里是大小写不同的同一个词：同样拒发
    const std::string noteUpperId = UniqueClientMsgId("livetest-codenotes-noteupper");
    SendMessageResponse noteUpper;
    REQUIRE(SendCode(conn, hs, kNoteUpperSeq, noteUpperId, kCode, OneNote(kBadWordUpper), noteUpper));
    CHECK(noteUpper.m_code == error_code_sensitive_word);
    CHECK(noteUpper.m_msgid == 0);

    //对照：正文含测试敏感词、批注干净，同样拒发
    const std::string textBadId = UniqueClientMsgId("livetest-codenotes-textbad");
    SendMessageResponse textBad;
    REQUIRE(SendCode(conn, hs, kTextBadSeq, textBadId, std::string("// ") + kBadWord + "\n" + kCode,
                     OneNote("fine note"), textBad));
    CHECK(textBad.m_code == error_code_sensitive_word);
    CHECK(textBad.m_msgid == 0);

    //拒收之后连接仍然正常：心跳照常有应答
    REQUIRE(SendEncrypted(conn, hs, cmd_heartbeat, kHeartbeatSeq, std::string()));
    std::string heartbeatPlain;
    CHECK(RecvReply(conn, hs, cmd_heartbeat, kHeartbeatSeq, heartbeatPlain));

    //正文与批注都干净：照常发出
    const std::string cleanId = UniqueClientMsgId("livetest-codenotes-clean");
    SendMessageResponse clean;
    REQUIRE(SendCode(conn, hs, kCleanSeq, cleanId, kCode, OneNote("check the return value"), clean));
    CHECK(clean.m_code == 0);
    CHECK(clean.m_clientmsgid == cleanId);
    CHECK(clean.m_msgid > 0);
}

TEST_CASE("CNLIVE2 notes added to a sent code snippet reach the other side", "[.live][codenoteslive]")
{
    REQUIRE(cryptoInit());

    //代码的发送者 MY0001 与追加批注的 MY0002 各一条连接
    TestConn connA;
    TestConn connB;
    INFO("cannot connect to the notes live chatserver on port " << kServerPort
         << "; see the setup steps at the top of this file");
    REQUIRE(connA.Connect(kServerPort));
    REQUIRE(connB.Connect(kServerPort));
    ClientHandshake hsA;
    ClientHandshake hsB;
    std::string reason;
    REQUIRE(DoHandshake(connA, hsA, reason));
    REQUIRE(DoHandshake(connB, hsB, reason));
    LoginResponse loginA;
    LoginResponse loginB;
    REQUIRE(LoginAs(connA, hsA, kAccount, "livetest-codenotes-a", loginA));
    REQUIRE(LoginAs(connB, hsB, kPeerAccount, "livetest-codenotes-b", loginB));
    REQUIRE(loginB.userid == kPeerUserId);

    //MY0001 发一段带一条批注的代码：原消息
    SendMessageResponse base;
    REQUIRE(SendCodeTo(connA, hsA, kBaseSeq, kPeerUserId, UniqueClientMsgId("livetest-codenotes-base"), kCode,
                       OneNote("check the return value"), ReplyQuote(), base));
    REQUIRE(base.m_code == 0);
    REQUIRE(base.m_msgid > 0);

    //MY0002 追加批注发回给 MY0001：引用原消息；MY0001 的那条写出作者，自己新加的一条标「新」、作者缺省（即 MY0002）
    std::vector<CodeNoteData> notes = OneNote("check the return value");
    notes[0].m_userId = loginA.userid;
    CodeNoteData mine;
    mine.m_from = 1;
    mine.m_to = 1;
    mine.m_text = "main should take argc and argv";
    mine.m_isNew = true;
    notes.push_back(mine);
    ReplyQuote quote;
    quote.m_valid = true;
    quote.m_msgid = base.m_msgid;
    quote.m_from = loginA.userid;
    quote.m_name = kAccount;
    quote.m_msgtype = message_type_text;
    quote.m_digest = "[code] int main()";
    SendMessageResponse annotated;
    REQUIRE(SendCodeTo(connB, hsB, kAnnotateSeq, loginA.userid, UniqueClientMsgId("livetest-codenotes-annotate"), kCode,
                       notes, quote, annotated));
    INFO("annotation send result code " << annotated.m_code);
    REQUIRE(annotated.m_code == 0);
    REQUIRE(annotated.m_msgid > 0);

    //MY0001 收到的推送：批注的作者与「新」原样到达，引用的是原消息
    MessageNotify pushed;
    REQUIRE(RecvMessagePush(connA, hsA, annotated.m_msgid, pushed));
    CHECK(pushed.m_textFormat == text_format_code);
    CHECK(pushed.m_from == kPeerUserId);
    REQUIRE(pushed.m_codeNotes.size() == 2);
    CHECK(pushed.m_codeNotes[0].m_userId == loginA.userid);
    CHECK_FALSE(pushed.m_codeNotes[0].m_isNew);
    CHECK(pushed.m_codeNotes[1].m_userId == 0);
    CHECK(pushed.m_codeNotes[1].m_isNew);
    CHECK(pushed.m_quote.m_valid);
    CHECK(pushed.m_quote.m_msgid == base.m_msgid);

    //追加的批注说明含测试敏感词：与第 10 节的批注一样拒发
    notes[1].m_text = std::string("this is ") + kBadWord;
    SendMessageResponse bad;
    REQUIRE(SendCodeTo(connB, hsB, kAnnotateBadSeq, loginA.userid, UniqueClientMsgId("livetest-codenotes-annotatebad"),
                       kCode, notes, quote, bad));
    CHECK(bad.m_code == error_code_sensitive_word);
    CHECK(bad.m_msgid == 0);
}
