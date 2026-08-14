/**
 *  远程桌面中转令牌（RelayToken）。
 *
 *  远程桌面会话经被控端授权后，由 chatserver 给控制端、被控端各签发一枚自校验
 *  令牌；双方连上 imgserver 的中转通道后仅凭令牌即可本地鉴权与配对，imgserver
 *  无需握手登录、无需回连 chatserver、无需查库。同一会话两端的令牌携带相同的
 *  sessionid、相反的 role，imgserver 据 sessionid 把两条连接两两配对、按字节
 *  原样双向转发屏幕帧与控制命令。
 *
 *  令牌结构与 FileToken 一致：payload + "." + HMAC-MD5(key, payload) 的十六进制
 *  串。payload 为竖线分隔的明文字段：sessionid|role|uid|peeruid|expire。各字段
 *  均为数字或受限字符（sessionid 由 chatserver 生成，约定只含十六进制与连字符，
 *  <u>不含</u> "." 与 "|"，以免破坏切分），故以首个 "." 切分 payload 与签名、再
 *  以 "|" 切分字段均无歧义。明文 payload 不含机密信息，防伪靠 HMAC 签名。
 *
 *  本模块为纯逻辑，只依赖 utils/MD5，不触及网络与数据库，便于单元测试（见
 *  tests/test_relaytoken.cpp）。chatserver 与 imgserver 共同链接本模块，HMAC
 *  密钥由两者配置文件中的同一项提供。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>

//令牌承载的载荷字段；签发时由调用方填充，校验成功后回填给调用方
struct RelayTokenPayload
{
    std::string  sessionid;   //中转会话 id，远程桌面 / 音视频 / 文件夹传输共用一套格式
                              //（同一会话两端相同；只含十六进制与连字符）
    int          role;        //本端角色：取值见 RelayToken::role_*
                              //（控制端 / 被控端 / 主叫 / 被叫 / 文件夹发送端 / 文件夹接收端）
    int64_t      uid;         //本令牌持有者的用户 userid
    int64_t      peeruid;     //会话对端的用户 userid（供 imgserver 校验配对关系）
    int64_t      expire;      //过期时间，Unix 毫秒；校验时 now >= expire 即判定过期

    RelayTokenPayload()
        : role(0), uid(0), peeruid(0), expire(0)
    {
    }
};

//远程桌面中转令牌签发 / 校验工具类；全部为静态方法，不可实例化
class RelayToken
{
public:
    //角色：控制端，连上中转后"收屏幕帧、发控制命令"（发起远程桌面的一方）
    static const int role_controller = 1;
    //角色：被控端，连上中转后“推屏幕帧、收控制命令”（被控制的一方）
    static const int role_controlled = 2;
    //角色：主叫，音视频通话的发起方；连上中转后与被叫双向收发音视频帧
    //（音视频通话与远程桌面共用本中转令牌与 RelayHub 配对，故角色编号续接 3/4，四种角色全局不重复）
    static const int role_caller = 3;
    //角色：被叫，音视频通话的被邀请方；连上中转后与主叫双向收发音视频帧
    static const int role_callee = 4;
    //角色：文件夹传输的发送端，连上中转后"发文件字节、收回执"（选中目录并发起邀请的一方）。
    //单聊文件夹传输走 fileserver 中转（不走 imgserver），但令牌格式与签发 / 校验流程完全一致，
    //故角色编号续接 5/6，六种角色全局不重复。
    static const int role_folder_sender = 5;
    //角色：文件夹传输的接收端，连上中转后"收文件字节、回回执并写盘"（同意接收的一方）。
    //同一次传输的两端令牌：sessionid 相同、role 相反（5 对 6）、uid 与 peeruid 对调。
    //与前四种角色的差别在于一枚令牌要开多条连接——发送端与接收端各用自己那一枚建
    //1 条控制通道 + N 条数据通道，通道序号 lane 由各连接在中转握手帧
    //（cmd_folder_relay_hello）里自报，<u>不</u>写进令牌，故不必按通道逐条签发。
    static const int role_folder_receiver = 6;

    /**
     *  按 payload 与密钥签发一枚令牌。
     *  @param payload 载荷字段；sessionid 须非空且不含 "." / "|"，role 取 role_*，
     *                 uid/peeruid/expire 由调用方填好
     *  @param key     HMAC 密钥，chatserver 与 imgserver 共享，来自各自配置文件的
     *                 relaytokenkey 项
     *  @return 令牌字符串（payload + "." + 签名）
     */
    static std::string sign(const RelayTokenPayload& payload, const std::string& key);

    /**
     *  校验并解析一枚令牌：验签 + 验过期。
     *  @param token 待校验的令牌字符串
     *  @param key   HMAC 密钥，须与签发时一致
     *  @param nowMs 当前时间，Unix 毫秒；用于过期判定
     *  @param out   [出参] 校验通过时回填解析出的载荷字段
     *  @return 令牌合法且未过期返回 true；格式非法 / 签名不符 / 已过期 / 字段非法
     *          返回 false
     */
    static bool verify(const std::string& token, const std::string& key,
                       int64_t nowMs, RelayTokenPayload& out);

private:
    RelayToken() = delete;
    ~RelayToken() = delete;
    RelayToken(const RelayToken&) = delete;
    RelayToken& operator=(const RelayToken&) = delete;
};
