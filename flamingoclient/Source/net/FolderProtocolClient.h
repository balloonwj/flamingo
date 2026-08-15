/**
 *  v2 单聊文件夹传输信令编解码（客户端侧），FolderProtocolClient.h
 *
 *  构造发往 chatserver 的五条请求 JSON（邀请 1810 / 应答 1812 / 撤回 1813 / 结束 1814 /
 *  重试 1817），解析 chatserver 回来的应答，以及三条服务器主动推送（邀请推送 1811 /
 *  状态推送 1815 / 中转参数下发 1816）。
 *
 *  本模块与服务端 chatserversrc/FolderTransferProtocol 互为镜像——服务端解析请求、构造
 *  应答与推送，客户端构造请求、解析应答与推送，两侧的字段名与编码口径必须逐字对应。写法
 *  与远程桌面 RemoteDesktopProtocolClient、音视频通话 CallProtocolClient 一致：整次传输由
 *  一个 sessionid（字符串）贯穿；64 位 userid 以十进制字符串编码进 JSON，避免按 IEEE754
 *  双精度解析时丢精度；字节数与计数这类数量以 JSON 64 位整数编码。
 *
 *  真正的文件字节不经过本模块，走 fileserver 的中转通道（cmd_folder_relay_hello 1857 /
 *  cmd_folder_relay_data 1858），由 Source/FolderTransfer/FolderRelayClient 与
 *  FolderRelayFraming 负责；中转握手应答是 {code, msg}，与远程桌面同结构，仍由
 *  RemoteDesktopProtocolClient::parseRelayHelloAck 解析，本模块不重复一份。
 *
 *  会话状态取值见 net/Protocol.h 的 enum folder_transfer_state（1~8），失败原因取值见同
 *  文件的 error_code_folder_*（606~620）。本头文件<u>不</u>包含 Protocol.h：原先是不想把
 *  "不得与 v1 的 Msg.h 同处一个编译单元"这条约束传染给每一个使用本模块的 .cpp
 *  （v1 协议头已于 2026-08-14 删除，该约束已消失），现在的理由是使用方多半只需要
 *  本模块的解析函数、用不上协议头的其余定义。取值范围判定都在实现文件里；调用方
 *  要用到枚举值时自行包含 Protocol.h。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp 与标准库，不触及 socket、界面与日志，便于单元测试，
 *  见 flamingoclient/Source/Tests/FolderProtocolClientTests.cpp（Suite FPC）。
 *
 *  典型用法（发送端发起一次传输）：
 *      std::string body = net::FolderProtocolClient::buildInviteRequest(
 *          peerId, folderName, dirs, files, bytes);
 *      // ...经 CIUSocket 发出 cmd_folder_invite(1810)，收到应答后：
 *      net::FolderInviteResponse resp;
 *      if (net::FolderProtocolClient::parseInviteResponse(ackBody, resp) && resp.m_code == 0)
 *          sessionId = resp.m_sessionId;
 *
 *  协议契约见 docs/协议设计-v2.md「文件夹传输协议（单聊）」一章，整体方案见
 *  docs/单聊文件夹传输方案.md。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//文件夹传输中本端扮演的角色，随 cmd_folder_relay_info(1816) 的 role 字段下发。客户端据此
//决定连上 fileserver 中转通道之后是发送文件字节还是接收文件字节。取值与服务端
//utils/RelayToken.h 的 role_folder_sender / role_folder_receiver 一致，并续接远程桌面的
//1 / 2 与音视频通话的 3 / 4，四类角色全局不重复。
enum folder_role
{
    folder_role_sender   = 5,   //发送端：扫描本地目录、发清单与文件字节的一方，即发起邀请者
    folder_role_receiver = 6,   //接收端：建目录、收文件字节并落盘的一方，即被邀请者
};

//发起邀请的应答（cmd_folder_invite=1810 的应答）解析结果。
//三种情形共用本结构：成功时 m_code 为 0，m_sessionId 与 m_state 有效；规模超限时 m_code 为
//error_code_folder_too_many_files(609) 或 error_code_folder_too_large(610)，m_maxFiles 与
//m_maxSize 回带服务端配置的上限，客户端一律用它们做提示，不得硬编码；其余失败只有
//m_code / m_msg 有效（如对方不在线 607、对方已有传输在进行 620、服务器繁忙 619）。
struct FolderInviteResponse
{
    int         m_code;        //结果码，0 成功，非 0 见 net/Protocol.h 的 enum error_code
    std::string m_msg;         //结果描述（成功为 "ok"，失败为英文诊断串，不直接展示给用户）
    std::string m_sessionId;   //本次传输的会话 id（仅 m_code==0 时有效），后续全部信令原样带回
    int         m_state;       //建会话后的初始状态（仅 m_code==0 时有效），按协议恒为 folder_state_ringing
    int64_t     m_maxFiles;    //服务端配置的文件数上限；单位：个。仅 m_code 为 609 / 610 时有效，其余情形为 0
    int64_t     m_maxSize;     //服务端配置的总字节数上限；单位：字节。仅 m_code 为 609 / 610 时有效，其余情形为 0

    FolderInviteResponse()
        : m_code(-1)           //-1：尚未解析出有效结果码，避免被误判为成功
        , m_state(0)
        , m_maxFiles(0)
        , m_maxSize(0)
    {
    }
};

//只带会话 id 的通用应答解析结果，四条请求共用：cmd_folder_response(1812)、
//cmd_folder_cancel(1813)、cmd_folder_close(1814)、cmd_folder_retry(1817)。
//服务端只要拿到了会话 id 就会原样带回（出错应答也带），客户端据此定位是哪一次传输；
//请求本身格式非法时服务端带不出会话 id，此时 m_sessionId 为空。
struct FolderSessionResponse
{
    int         m_code;        //结果码，0 成功，非 0 见 net/Protocol.h 的 enum error_code
    std::string m_msg;         //结果描述（英文诊断串，客户端按 m_code 本地化文案，不直接展示本串）
    std::string m_sessionId;   //请求里那个会话 id；服务端解析不出请求时为空

    FolderSessionResponse()
        : m_code(-1)           //-1：尚未解析出有效结果码，避免被误判为成功
    {
    }
};

//收到的文件夹传输邀请（cmd_folder_invite_notify=1811 推送）解析结果。接收端据此在会话内的
//气泡上显示"谁要发一个多大的文件夹给你"，并给出「接收 / 拒绝」两个按钮，不弹独立授权窗。
struct FolderInviteNotify
{
    std::string m_sessionId;    //本次传输的会话 id；应答 / 结束时须原样带回
    int64_t     m_fromId;       //发送端 userid（JSON 中以十进制字符串编码）
    std::string m_fromName;     //发送端昵称（UTF-8，供气泡文案展示）
    std::string m_folderName;   //根目录名（只有名字、不含路径），接收端据此在选定位置建根目录
    int64_t     m_totalDirs;    //目录总数，含空目录、不含根目录本身；单位：个
    int64_t     m_totalFiles;   //文件总数；单位：个
    int64_t     m_totalSize;    //全部文件字节数合计；单位：字节，64 位（可超过 4 GiB）

    FolderInviteNotify()
        : m_fromId(0)
        , m_totalDirs(0)
        , m_totalFiles(0)
        , m_totalSize(0)
    {
    }
};

//会话状态推送（cmd_folder_state_notify=1815）解析结果。
//按协议只有 folder_state_failed(7) 才带有意义的失败原因，其余状态 m_errCode 恒为 0。
struct FolderStateNotify
{
    std::string m_sessionId;   //本次传输的会话 id
    int         m_state;       //会话状态，取值见 net/Protocol.h 的 enum folder_transfer_state（1~8）
    int         m_errCode;     //失败原因，取值见 error_code_folder_*（606~620）；仅 m_state 为 7 时有意义
    std::string m_errMsg;      //面向开发者的英文诊断串，仅供排查；客户端按 m_errCode 本地化文案，不直接展示本串

    FolderStateNotify()
        : m_state(0)
        , m_errCode(0)
    {
    }
};

//中转参数下发推送（cmd_folder_relay_info=1816）解析结果。接收端同意（1812）或重试成功
//（1817）后，服务端向两端各推一帧：m_sessionId 与 m_lanes 相同，m_token 与 m_role 各不相同。
//本端据此连上 fileserver 的中转端口，共建 m_lanes + 1 条连接（lane 0 控制通道、lane 1..N
//数据通道），每条连接都用同一枚 m_token 报到、各报自己的 lane 序号。
struct FolderRelayInfo
{
    std::string m_sessionId;   //本次传输的会话 id
    int         m_role;        //本端角色，取值见 enum folder_role
    std::string m_relayIp;     //fileserver 的中转地址（点分十进制），就是上传下载用的那个监听地址
    int         m_relayPort;   //fileserver 的监听端口，本功能不新开端口
    std::string m_token;       //中转令牌，本端 N+1 条连接共用同一枚，作为报到帧 body 提交校验
    int         m_lanes;       //数据通道条数 N，不含 lane 0 那条控制通道；由服务端配置 folderlanes 决定

    FolderRelayInfo()
        : m_role(0)
        , m_relayPort(0)
        , m_lanes(0)
    {
    }
};

//文件夹传输记录条（msgtype = message_type_folder = 18）的 content 字段解析结果。
//
//一次文件夹传输走到终态后，服务端在发送方与接收方之间的单聊会话里落一条普通聊天消息，
//from 取文件夹发送方、to 取接收方，content 就是本结构描述的那七个字段。它<u>只记这次
//传输这个动作本身</u>——文件夹名、规模统计与结局——不含文件清单、相对路径与 MD5，故这
//条记录不构成"服务端保存了文件夹内容"，历史里也不提供重新下载。
//
//同一次传输可能留下<u>多条</u>记录：每次尝试各一条（先失败落一条 result=7，保留期内
//重试成功再落一条 result=8），服务端的幂等键是「会话 id + 尝试序号」。因此 m_sessionId
//在同一个会话的多条记录之间是<u>重复</u>的，不能拿它当消息的唯一键用。
struct FolderRecordContent
{
    std::string m_sessionId;    //本次传输的会话 id；客户端据它把这条记录与聊天窗里那条本地气泡对上
    std::string m_folderName;   //根目录名（只有名字、不含路径，UTF-8）
    int64_t     m_totalDirs;    //目录总数，含空目录、不含根目录本身；单位：个
    int64_t     m_totalFiles;   //文件总数；单位：个
    int64_t     m_totalSize;    //全部文件字节数合计；单位：字节，64 位（可超过 4 GiB）
    int         m_result;       //本次传输的结局，取值为 folder_transfer_state 的终态（3~8）
    int         m_errCode;      //失败原因，取值见 error_code_folder_*（606~620）；
                                //仅 m_result 为 folder_state_failed(7) 时有意义，其余结局恒为 0

    FolderRecordContent()
        : m_totalDirs(0)
        , m_totalFiles(0)
        , m_totalSize(0)
        , m_result(0)           //0 不是合法结局（终态从 3 起），避免未解析出的值被误当作某种结局
        , m_errCode(0)
    {
    }
};

//单聊文件夹传输信令编解码工具类；全部为静态方法，不可实例化。
//构造类方法对不合契约的入参一律返回空串——空包体不应当被发出去，调用方据此就地放弃本次
//请求，比发一个服务端必然拒绝的包更早暴露问题。解析类方法对任何输入都只返回 true / false，
//不抛异常：JSON 非法、字段缺失、字段类型不对、取值越界一律返回 false，且此时出参内容未
//定义，调用方不得取用。
class FolderProtocolClient
{
public:
    //==== 客户端 → chatserver 的请求构造 ====

    /**
     *  构造发起文件夹传输邀请的请求 JSON 包体（cmd_folder_invite=1810，紧凑 UTF-8）。
     *  三项规模统计由调用方扫描本地目录树得出，服务端据此与配置的上限比对，超限直接拒绝。
     *  @param peerUserId 接收方 userid（须 >0）；作为 peerid 字段以十进制字符串编码
     *  @param folderName 根目录名（只有名字、不含路径，UTF-8，须非空）
     *  @param totalDirs  目录总数，含空目录、不含根目录本身；单位：个，须 >=0
     *  @param totalFiles 文件总数；单位：个，须 >=0
     *  @param totalSize  全部文件字节数合计；单位：字节，须 >=0（可超过 4 GiB）
     *  @return 紧凑格式的请求 JSON 文本；入参不满足上述取值范围时返回空串（调用方不应发送）。
     *          三项统计同时为 0 是合法的——发送一个空目录
     */
    static std::string buildInviteRequest(int64_t peerUserId, const std::string& folderName,
                                          int64_t totalDirs, int64_t totalFiles, int64_t totalSize);

    /**
     *  构造接收端应答邀请的请求 JSON 包体（cmd_folder_response=1812，紧凑 UTF-8）。
     *  @param sessionId 邀请推送里带来的会话 id（须非空）；原样回带
     *  @param accept    true 接收、false 拒绝；写入 accept 字段（1 / 0）
     *  @return 紧凑格式的请求 JSON 文本；sessionId 为空时返回空串
     */
    static std::string buildResponse(const std::string& sessionId, bool accept);

    /**
     *  构造发送端撤回邀请的请求 JSON 包体（cmd_folder_cancel=1813，紧凑 UTF-8）。
     *  只在接收端尚未应答（会话仍处于响铃中）时可用；传输已经开始之后再取消走 buildClose，
     *  并把失败原因填成 error_code_folder_canceled(617)。
     *  @param sessionId 本次传输的会话 id（须非空）
     *  @return 紧凑格式的请求 JSON 文本；sessionId 为空时返回空串
     */
    static std::string buildCancel(const std::string& sessionId);

    /**
     *  构造结束本次传输的请求 JSON 包体（cmd_folder_close=1814，紧凑 UTF-8）。
     *  成功与失败共用本函数：成功结束时服务端立即销毁会话，失败结束时会话保留
     *  folderretrywindow（默认 10 分钟）供 buildRetry 重试续传，且失败原因会被服务端原样
     *  透传给对端的状态推送，两端因而看到同一条原因。
     *  @param sessionId 本次传输的会话 id（须非空）
     *  @param success   true 成功结束、false 失败结束；写入 result 字段（0 / 1）
     *  @param errCode   失败原因，取值须落在 error_code_folder_*（606~620）之内；success 为
     *                   true 时本参数被忽略，一律写 0（服务端对"成功却带失败原因"判非法请求）
     *  @param errMsg    面向开发者的英文诊断串，可为空；success 为 true 时本参数被忽略，一律写空串
     *  @return 紧凑格式的请求 JSON 文本；sessionId 为空、或 success 为 false 而 errCode 越界时
     *          返回空串——那样的包服务端必然判为非法请求，就地暴露比发出去再被拒更早
     */
    static std::string buildClose(const std::string& sessionId, bool success,
                                  int errCode, const std::string& errMsg);

    /**
     *  构造重试续传的请求 JSON 包体（cmd_folder_retry=1817，紧凑 UTF-8）。
     *  由发送端在失败气泡上点「重试」时发出，只在保留期内、且双方仍在线时能成功；重试不再
     *  征求接收方同意，服务端重签两枚令牌并向双方各再推一帧 cmd_folder_relay_info。
     *  @param sessionId 本次传输的会话 id（须非空）
     *  @return 紧凑格式的请求 JSON 文本；sessionId 为空时返回空串
     */
    static std::string buildRetry(const std::string& sessionId);

    //==== chatserver → 客户端的应答与推送解析 ====

    /**
     *  解析发起邀请的应答 JSON 包体（cmd_folder_invite=1810 的应答）。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code / m_msg 有效；m_code 为 0 时 m_sessionId 与
     *                  m_state 亦有效；m_code 为 609 / 610 时 m_maxFiles 与 m_maxSize 亦有效
     *  @return JSON 合法、含整型 code、且 code 为 0 时会话 id 非空，返回 true；否则 false
     */
    static bool parseInviteResponse(const std::string& jsonbody, FolderInviteResponse& out);

    /**
     *  解析只带会话 id 的通用应答 JSON 包体（cmd_folder_response / cmd_folder_cancel /
     *  cmd_folder_close / cmd_folder_retry 四条命令的应答共用）。
     *  会话 id 是可选的：请求格式非法时服务端解析不出它，应答里就没有这个字段。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时 m_code / m_msg 有效，m_sessionId 可能为空
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseSessionResponse(const std::string& jsonbody, FolderSessionResponse& out);

    /**
     *  解析收到的文件夹传输邀请推送 JSON 包体（cmd_folder_invite_notify=1811）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 会话 id 非空、发送端 userid 为正、根目录名非空、三项规模统计均为非负的 JSON
     *          整数时返回 true；否则 false。缺其中任何一项，接收端的气泡都无从显示与落地
     */
    static bool parseInviteNotify(const std::string& jsonbody, FolderInviteNotify& out);

    /**
     *  解析会话状态推送 JSON 包体（cmd_folder_state_notify=1815）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 会话 id 非空且状态取值落在 folder_transfer_state（1~8）之内返回 true，否则 false。
     *          越界的状态客户端无从映射成气泡样式，按非法推送丢弃
     */
    static bool parseStateNotify(const std::string& jsonbody, FolderStateNotify& out);

    /**
     *  解析中转参数下发推送 JSON 包体（cmd_folder_relay_info=1816）。
     *  地址字段名为 relayip / relayport，与远程桌面 cmd_rd_relay_info、音视频通话的中转参数
     *  推送同名（不是 host / port）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效
     *  @return 会话 id 与令牌非空、中转地址非空、端口落在 1~65535 之内、角色为 folder_role
     *          之一、数据通道条数 >=1 时返回 true；否则 false。这几项缺任何一项都建不起中转
     *          连接。通道条数的硬上限（8）由中转层 foldertransfer::kFolderMaxDataLanes 把关，
     *          本模块不再重复一份，免得同一个上限有两个出处
     */
    static bool parseRelayInfo(const std::string& jsonbody, FolderRelayInfo& out);

    //==== 取值校验（供界面层复用，避免各处各写一份范围判断）====

    /**
     *  判定一个会话状态取值是否合法。
     *  @param state 待判定的状态值
     *  @return 落在 folder_state_ringing(1) ~ folder_state_completed(8) 之内返回 true。
     *          该枚举取值连续，故按上下界判定即可
     */
    static bool isValidState(int state);

    /**
     *  判定一个失败原因错误码是否属于文件夹传输这一组。
     *  @param errCode 待判定的错误码
     *  @return 落在 error_code_folder_rejected(606) ~ error_code_folder_busy(620) 之内返回
     *          true。该组错误码取值连续，故按上下界判定即可
     */
    static bool isValidErrorCode(int errCode);

    /**
     *  判定一个会话状态取值是否为<u>终态</u>——即会话已成定局、可以据此落一条传输记录。
     *  @param state 待判定的状态值
     *  @return 落在 folder_state_rejected(3) ~ folder_state_completed(8) 之内返回 true；
     *          响铃中(1) 与传输中(2) 是过程状态，返回 false
     */
    static bool isTerminalState(int state);

    //==== 传输记录条（msgtype=message_type_folder=18）的 content 编解码 ====
    //
    //本组与服务端 chatserversrc/FolderTransferProtocol 的 buildRecordContent /
    //parseRecordContent 互为镜像，字段名与编码口径必须逐字对应。客户端<u>不发</u>这条
    //消息（记录一律由服务端生成），构造侧存在的理由有两个：一是把收到的记录归一化后
    //写进本地历史消息库（见 balloonhistory::BuildContentJson），二是让"构造—解析"能
    //在单元测试里往返比对，字段名写错当场就能发现。

    /**
     *  构造传输记录条的 content JSON 包体（紧凑 UTF-8）。
     *
     *  「errcode 仅失败态有意义」这条不变式在本函数里归一：rec.m_result 不是
     *  folder_state_failed 时，无论入参给了什么，一律写 0。各调用点因而不必各判一遍。
     *
     *  @param rec 记录内容；m_sessionId 须非空、m_result 须为终态（3~8）、三项规模
     *             统计须非负（同时为 0 是合法的，那是发送了一个空目录）
     *  @return 紧凑格式的 JSON 文本；入参不满足上述取值范围时返回空串
     */
    static std::string buildRecordContent(const FolderRecordContent& rec);

    /**
     *  解析传输记录条的 content JSON 包体。
     *
     *  校验口径与 buildRecordContent 对称：会话 id 必须非空、结局必须是终态、三项规模
     *  统计必须是非负的 JSON 整数（缺字段、写成字符串、写成小数、为负都判非法）。解析
     *  出来的 m_errCode 同样按"仅失败态有意义"归一——非失败结局一律置 0，免得界面把一个
     *  本不该存在的失败原因显示出来。
     *
     *  @param jsonbody content 包体（UTF-8 JSON 文本）
     *  @param out      [出参] 返回 true 时各字段有效；返回 false 时内容未定义，不得取用
     *  @return 解析并校验通过返回 true；JSON 非法、顶层非对象、字段缺失、类型不对或
     *          取值越界一律返回 false（不抛异常）
     */
    static bool parseRecordContent(const std::string& jsonbody, FolderRecordContent& out);

private:
    FolderProtocolClient() = delete;
    ~FolderProtocolClient() = delete;
    FolderProtocolClient(const FolderProtocolClient&) = delete;
    FolderProtocolClient& operator=(const FolderProtocolClient&) = delete;
};

} // namespace net
