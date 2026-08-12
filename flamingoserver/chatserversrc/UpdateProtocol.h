/**
 *  v2 §10.12 检查客户端更新报文的编解码：cmd_check_update(1905) 的请求解析与应答构造。
 *
 *  与其它协议模块有一点不同：本命令允许在<u>未登录</u>时调用（见 ChatSession 的登录前
 *  白名单）。原因是版本过低的客户端会被 cmd_login 以 error_code_version_too_old 拒绝，
 *  若检查更新也要求先登录，这类客户端就再无自救途径。因此应答里的下载令牌由 chatserver
 *  直接签发（场景取 file_scene_update，未登录时令牌载荷的 uid 为 0），客户端拿到即可直连
 *  fileserver 下载升级包，不必再走一次 cmd_request_download(1802)。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试（见
 *  tests/test_updateprotocol.cpp）。版本号的高低比较复用 LoginProtocol::isVersionAtLeast，
 *  不在本模块另起一份；查版本表由 ClientVersionManager 负责，签令牌与选服务器在会话层。
 *
 *  典型用法（会话层 ChatSession::onCheckUpdate）：
 *      CheckUpdateRequest req;
 *      if (!UpdateProtocol::parseCheckUpdateRequest(body, req)) → 回 buildSimpleResponse(bad_request)
 *      查表拿到 ClientVersionInfo → 比版本 → 有新版则签令牌后 buildHasUpdateResponse，
 *      否则 buildNoUpdateResponse。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <stdint.h>
#include <string>

//cmd_check_update 请求报文解析后的结构
struct CheckUpdateRequest
{
    int32_t     platform;   //客户端平台，取值见 Protocol.h 的 enum platform_type；
                            //请求未带该字段时取 platform_pc（PC 客户端是唯一有自动更新的端，
                            //老客户端不带该字段时按 PC 处理才不会漏掉更新）
    std::string version;    //客户端当前版本号，点分十进制如 "3.2.1"；必填且不得为空串

    CheckUpdateRequest()
        : platform(0)
    {
    }
};

//一个可下发的客户端版本，字段与 t_client_version 表一一对应。
//由 ClientVersionManager 从库里读出，交给本模块组装进应答。
struct ClientVersionInfo
{
    std::string version;      //该版本的版本号，点分十进制如 "3.3.0"
    std::string minVersion;   //允许登录的最低版本号；客户端低于它即为强制升级
    std::string releaseNote;  //更新说明，UTF-8 文本，展示在升级窗里
    std::string packageMd5;   //升级包内容MD5（小写十六进制32位），同时是它在 filecache 里的文件名
    int64_t     packageSize;  //升级包字节数
    std::string packageName;  //升级包原始文件名，仅用于界面显示与本地落地命名

    //发布时勾选的"强制升级"开关：1 强制、0 不强制。与 minVersion 相互独立，
    //两者任一命中即按强制处理 —— 前者是发布人的主观意愿（这个版本必须装），
    //后者是版本门槛（低于它连登录都不放行）。
    int32_t     forced;

    ClientVersionInfo()
        : packageSize(0), forced(0)
    {
    }
};

//检查更新报文编解码工具类；全部为静态方法，不可实例化
class UpdateProtocol
{
public:
    /**
     *  解析 cmd_check_update 请求 JSON 包体。
     *
     *    jsonbody： 请求包体（UTF-8 JSON 文本）
     *    req：      [出参] 解析结果；仅当返回 true 时各字段有效
     *
     *  必填字段只有 version：缺失、非字符串、空串，或 JSON 本身非法 / 不是对象，
     *  一律返回 false。
     *
     *  platform 为选填：不带该字段时回落为 platform_pc；带了则必须是整数且落在
     *  enum platform_type 的取值范围内（1~5），给了 0、负数或超范围的值直接拒绝
     *  ——一个连自己是什么端都报错的请求，与其猜它想要什么，不如让它显式失败。
     *
     *  @return 解析并校验通过返回 true，否则返回 false
     */
    static bool parseCheckUpdateRequest(const std::string& jsonbody, CheckUpdateRequest& req);

    /**
     *  构造"有新版本"的应答 JSON（code=0，hasupdate=true）。
     *
     *    info：          待下发的版本信息（版本号 / 更新说明 / 包MD5 / 包大小 / 包名）
     *    forced：        是否强制升级。调用方按"客户端当前版本 < info.minVersion"判定后传入；
     *                    true 时客户端不提供"稍后再说"的出口
     *    downloadToken： chatserver 现签的下载令牌，客户端凭它直连 fileserver 取包
     *    host：          升级包所在 fileserver 的主机名或 IP
     *    port：          该 fileserver 的端口
     *    tls：           该 fileserver 是否启用 TLS
     *
     *  @return 紧凑格式的应答 JSON 文本
     *
     *  包大小以 JSON 数字下发，内部按 64 位整数编码——升级包超过 2GiB 时 32 位会溢出。
     */
    static std::string buildHasUpdateResponse(const ClientVersionInfo& info,
                                              bool forced,
                                              const std::string& downloadToken,
                                              const std::string& host,
                                              int port,
                                              bool tls);

    /**
     *  构造"已是最新版本"的应答 JSON（code=0，hasupdate=false）。
     *
     *    latestVersion： 服务器当前发布的最新版本号，供客户端在关于页显示成
     *                    "当前已是最新版本 3.3.0"。<u>空串表示服务器一个版本都没发布过</u>，
     *                    此时不下发该字段，客户端只显示"当前已是最新版本"。
     *
     *  @return 紧凑格式的应答 JSON 文本
     *
     *  应答里<u>不含</u> downloadtoken / md5 / size 等字段——没有更新就不该出现这些东西，
     *  空着下发只会让客户端多一条"字段有值但没用"的判断路径。
     */
    static std::string buildNoUpdateResponse(const std::string& latestVersion);

    /**
     *  构造通用 { code, msg } 失败应答（请求报文非法、服务器内部错误等）。
     *
     *    code： 错误码（非 0），取值见 Protocol.h 的 enum error_code
     *    msg：  面向开发者的诊断信息，不直接展示给用户
     *
     *  @return 紧凑格式的应答 JSON 文本
     */
    static std::string buildSimpleResponse(int32_t code, const std::string& msg);

private:
    UpdateProtocol() = delete;
    ~UpdateProtocol() = delete;
    UpdateProtocol(const UpdateProtocol&) = delete;
    UpdateProtocol& operator=(const UpdateProtocol&) = delete;
};
