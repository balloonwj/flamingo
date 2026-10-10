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
    //角色：屏幕共享的发起人，连上 imgserver 房间中转后推送屏幕画面（也收发声音、摄像头与标注）。
    //与前三组两端配对不同，屏幕共享是一个房间挂多名成员：令牌的 sessionid 为房间号，peeruid 字段
    //承载本人在房间里的座位号（发起人固定为 0），imgserver 据此把连接挂进房间、校验帧头的发送者编号。
    static const int role_screenshare_host = 7;
    //角色：屏幕共享的观看者，连上房间中转后接收屏幕画面、收发声音、摄像头与标注；peeruid 为座位号（1~254）。
    static const int role_screenshare_member = 8;
    //角色：文件共享的分享者一端，连上 fileserver 中转后应答访问者的列目录、读写文件等请求
    //（共享的文件位于这一端的电脑上）。取 9 而不是续接 7：7 / 8 已归屏幕共享（见上），
    //两个功能谁先落地都不会撞号，见 docs/文件共享方案.md 5.2。
    //中转 sessionid 一律带 "share-" 前缀，fileserver 据此与本角色做一致性校验。
    static const int role_share_owner = 9;
    //角色：文件共享的访问者一端，连上中转后发出列目录、下载、上传等请求（打开共享的成员）。
    //同一次访问的两端令牌：sessionid 相同、role 相反（9 对 10）、uid 与 peeruid 对调。
    //与文件夹传输相同，lane 由各连接在 cmd_folder_relay_hello 里自报、不写进令牌。
    static const int role_share_visitor = 10;
    //角色：P2P 链路的发起方在回落中转时使用。打洞超时后双方改连 imgserver 的一对一中转
    //（cmd_p2p_relay_hello），令牌的 sessionid 即 P2P 会话号（带 "p2p-" 前缀），两端令牌
    //role 相反（11 对 12）、uid 与 peeruid 对调。中转上承载的是两端之间端到端加密的报文，
    //imgserver 只做配对与转发（docs/P2P通信方案.md 6.4）。
    static const int role_p2p_controlling = 11;
    //角色：P2P 链路的应答方在回落中转时使用，与 role_p2p_controlling 配对。
    static const int role_p2p_controlled = 12;
    //角色：群通话房间的成员（docs/群音视频通话方案.md）。与屏幕共享一样走 imgserver 的房间中转（1859 / 1860），
    //但群通话里没有发起人与观看者之分，所有已加入的人都用这一个角色：sessionid 为房间号（gc- 开头），peeruid 字段
    //承载座位号（1~254）。imgserver 不允许同一房间里混入屏幕共享的 7 / 8，也不接受它做一对一报到。
    static const int role_groupcall_member = 13;

    //屏幕共享房间里「只推流」的连接（第 22 期多人共享）：主持人以外的共享者另连一条中转连接推自己的屏幕，peeruid 承载这块画面的
    //座位号（1~254，与成员座位号不重复）。imgserver 只把发给这个座位的控制帧（关键帧请求、键鼠输入）转给它，房间里的其它画面、
    //声音与标注一律不转给它
    static const int role_screenshare_publisher = 14;

    //角色：云端录制的控制连接（屏幕共享与录制方案 12.6.2，第 26 期）。不是客户端的角色：chatserver 主动连 imgserver 的中转端口，
    //用 cmd_cloudrec_ctl_hello（1863）报到后经这条连接通知开始 / 停止录制、删除文件，imgserver 经它回报录制结束与上传完成。
    //sessionid 为 "srvctl-<毫秒>"，uid 与 peeruid 为 0，有效期 60 秒，每次重连重签。imgserver 的一对一报到与房间报到都拒绝本角色
    static const int role_server_control = 15;

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
