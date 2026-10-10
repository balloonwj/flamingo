/**
 *  密聊信令（cmd_secret_* 2601~2609）的报文编解码，SecretProtocol.h
 *
 *  报文格式见 docs/密聊方案.md 第 5 节与服务端 chatserversrc/SecretChatManager.cpp：
 *  64 位 ID（会话号、userid）一律写成十进制字符串，解析时字符串与整数两种写法都接受。
 *  纯逻辑，便于单元测试（Tests/SecretProtocolTests.cpp，[secretdll]）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

namespace secretchat {

//2603 推送
struct InviteNotify
{
    uint64_t    sessionId;     //会话号
    int64_t     fromId;        //发起方
    std::string pubkey;        //发起方临时公钥（base64）
    uint32_t    expireMs;      //邀请剩余毫秒数
    int         peerProtect;   //发起方的防截图级别

    InviteNotify()
        : sessionId(0)
        , fromId(0)
        , expireMs(0)
        , peerProtect(0)
    {
    }
};

//2605 推送
struct StateNotify
{
    uint64_t    sessionId;     //会话号
    int64_t     peerId;        //对方
    int         state;         //状态，取值见 secret_state
    int         reason;        //结束原因（state 为 4 时）
    std::string pubkey;        //对方公钥（只在发给发起方的「已建立」里有）
    int         peerProtect;   //对方的防截图级别

    StateNotify()
        : sessionId(0)
        , peerId(0)
        , state(0)
        , reason(0)
        , peerProtect(0)
    {
    }
};

//2609 推送
struct MessageNotify
{
    uint64_t    sessionId;    //会话号
    uint32_t    seq;          //对方的帧序号
    std::string nonce;        //nonce（base64）
    std::string data;         //密文（base64）
    int64_t     serverTime;   //服务端转发时刻（Unix 毫秒）

    MessageNotify()
        : sessionId(0)
        , seq(0)
        , serverTime(0)
    {
    }
};

/**
 *  密聊报文编解码，全部为静态方法。
 */
class SecretProtocol
{
public:
    //---- 组包 ----
    static std::string BuildRegister(int version, int protect);
    static std::string BuildInvite(int64_t peerId, const std::string& pubkey);
    static std::string BuildRespond(uint64_t sessionId, bool accept, const std::string& pubkey);
    //2606 / 2607：只带会话号
    static std::string BuildSessionOnly(uint64_t sessionId);
    static std::string BuildMessage(uint64_t sessionId, uint32_t seq, const std::string& nonce, const std::string& data);

    //---- 解析 ----
    //只取 code（各应答都有）；包体非法返回 false
    static bool ParseCode(const std::string& json, int& code);
    //2601 应答
    static bool ParseRegisterResponse(const std::string& json, int& code, bool& enabled);
    //2602 应答：失败时 sessionId 为 0
    static bool ParseInviteResponse(const std::string& json, int& code, uint64_t& sessionId, uint32_t& expireMs);
    //2608 应答：带回被应答的那一帧的 seq
    static bool ParseMessageResponse(const std::string& json, int& code, uint32_t& seq);
    static bool ParseInviteNotify(const std::string& json, InviteNotify& out);
    static bool ParseStateNotify(const std::string& json, StateNotify& out);
    static bool ParseMessageNotify(const std::string& json, MessageNotify& out);

    //会话号 ↔ 十进制文本
    static std::string SessionIdText(uint64_t sessionId);
};

} // namespace secretchat
