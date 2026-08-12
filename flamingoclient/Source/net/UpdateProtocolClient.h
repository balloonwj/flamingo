/**
 *  v2 §10.12 检查客户端更新报文（客户端侧），UpdateProtocolClient.h
 *
 *  只负责 cmd_check_update(1905) 的请求组包与应答解析，外加点分版本号的高低比较。
 *  与 net/ 下其它 XxxProtocolClient 同构：无状态静态方法，只依赖 jsoncpp，不碰
 *  socket 与界面，便于单元测试（见 Tests/UpdateProtocolClientTests.cpp）。
 *
 *  两点与其它命令不同，值得留意：
 *    1. 本命令<u>允许未登录时发送</u>。版本过低的客户端会被 cmd_login 以
 *       error_code_version_too_old 拒绝，若检查更新也要求先登录，这类客户端就再无
 *       自救途径，故服务端把它放进了登录前白名单。
 *    2. 应答里<u>直接带下载令牌</u>，不必再走 cmd_request_download(1802) 申请 ——
 *       升级包对所有人是同一份公开内容，服务端在应答时就把令牌签好了。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_check_update(1905) 应答的解析结果。
//m_code 为 0 时其余字段才有效；m_hasUpdate 为 false 时只有 m_latestVersion 可能有值。
struct CheckUpdateResult
{
    int         m_code;           //业务错误码，0 表示成功；取值见 Protocol.h 的 enum error_code
    std::string m_msg;            //面向开发者的诊断信息，不直接展示给用户
    bool        m_hasUpdate;      //是否有可用的新版本
    std::string m_latestVersion;  //服务器当前发布的最新版本号；无更新时可能为空（服务器一个版本都没发过）
    std::string m_minVersion;     //允许登录的最低版本号
    bool        m_forced;         //是否强制升级（本机版本低于 m_minVersion）；true 时界面不给"稍后"的出口
    std::string m_releaseNote;    //更新说明，UTF-8 文本，展示在升级窗里
    std::string m_md5;            //升级包内容MD5（小写十六进制32位），下载后据此校验
    int64_t     m_size;           //升级包字节数
    std::string m_packageName;    //升级包原始文件名，仅用于界面显示与本地落地命名
    std::string m_downloadToken;  //下载令牌，直连 fileserver 取包时携带
    std::string m_host;           //升级包所在 fileserver 的主机名或 IP
    int         m_port;           //该 fileserver 的端口
    bool        m_tls;            //该 fileserver 是否启用 TLS

    CheckUpdateResult()
        : m_code(0), m_hasUpdate(false), m_forced(false), m_size(0), m_port(0), m_tls(false)
    {
    }

    //是否拿到了一个可以真正去下载的新版本：业务成功 + 有更新 + 令牌与校验值齐备。
    //调用方据此决定要不要进下载流程，不必自己逐字段判空。
    bool HasUsableUpdate() const
    {
        return m_code == 0 && m_hasUpdate && !m_downloadToken.empty()
            && !m_md5.empty() && m_size > 0;
    }
};

//检查更新报文编解码；全部为静态方法，不可实例化。
class UpdateProtocolClient
{
public:
    /**
     *  构造 cmd_check_update 请求 JSON 包体（紧凑 UTF-8）。
     *
     *    platform： 客户端平台，取值见 Protocol.h 的 enum platform_type；PC 端填 platform_pc
     *    version：  本机当前版本号，取自 ClientVersion.h 的 FLAMINGO_CLIENT_VERSION
     *
     *  @return 请求包体
     */
    static std::string buildCheckUpdateRequest(int platform, const std::string& version);

    /**
     *  解析 cmd_check_update 应答 JSON 包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）
     *    out：      [出参] 解析结果；<u>进函数先重置</u>
     *
     *  @return JSON 合法且是对象返回 true（业务是否成功看 out.m_code）；
     *          报文本身不可解析返回 false。
     *
     *  包大小按 64 位读：升级包超过 2GiB 时 32 位会溢出成负数。
     */
    static bool parseCheckUpdateResponse(const std::string& jsonbody, CheckUpdateResult& out);

    /**
     *  比较点分版本号：ver 是否不低于 minver。
     *
     *    ver：    待判定的版本号，如 "3.2.1"
     *    minver： 参照版本号，如 "3.0.0"
     *
     *  @return ver >= minver 返回 true
     *
     *  逐段按<u>数值</u>比较（不是字典序，否则 "3.10.0" 会被判成小于 "3.9.0"），
     *  段数不足的一方按 0 补齐（"3.2" 等价于 "3.2.0"）。与服务端
     *  LoginProtocol::isVersionAtLeast 的规则逐条对齐 —— 两端判断不一致会出现
     *  "服务端说要强制升级、客户端却认为自己够新"这种谁也说不清的状态。
     */
    static bool isVersionAtLeast(const std::string& ver, const std::string& minver);

private:
    UpdateProtocolClient() = delete;
    ~UpdateProtocolClient() = delete;
    UpdateProtocolClient(const UpdateProtocolClient&) = delete;
    UpdateProtocolClient& operator=(const UpdateProtocolClient&) = delete;
};

} // namespace net
