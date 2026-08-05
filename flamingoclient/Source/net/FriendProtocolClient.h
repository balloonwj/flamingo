/**
 *  FriendProtocolClient.h
 *
 *  好友管理报文的客户端侧编解码。当前只覆盖「设置好友备注名」
 *  （cmd_set_friend_remark=1209）：构造请求，解析其应答。与服务端
 *  chatserversrc/FriendProtocol.{h,cpp} 一一对应。
 *
 *  备注名是<u>单向</u>的 —— 只影响"本人看该好友"的显示，不影响对方，也不会
 *  推送给对方。改完的备注<u>不走推送回来</u>，而是随下一次通讯录
 *  （cmd_get_contacts 应答里每个联系人的 remark 字段，见
 *  net/ContactsProtocolClient.h 的 ContactItem::m_remark）读回；本文件只管
 *  "改一条"。
 *
 *  同一段好友管理里的删好友（1207）、黑名单（1213）、好友标签（1210）尚未接入
 *  客户端，日后接时加进本文件即可，不必另起炉灶。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不碰窗口、不碰 socket，可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_set_friend_remark 应答解析结果。
struct SetFriendRemarkResult
{
    int         m_code;    //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
                           //（对方不是好友为 error_code_not_friend）
    std::string m_msg;     //结果描述（UTF-8），可为空串
    int64_t     m_userid;  //本次设置备注的目标好友ID，服务端原样回带

    SetFriendRemarkResult()
        : m_code(-1)       //-1：尚未解析出有效结果码
        , m_userid(0)
    {
    }
};

//好友管理报文编解码工具类；全部为静态方法，不可实例化。
class FriendProtocolClient
{
public:
    /**
     *  构造"设置好友备注名"请求 JSON 包体（cmd_set_friend_remark=1209）。
     *
     *    userid：     目标好友的 userid（须 > 0）。
     *    remarkUtf8： 新备注名（UTF-8）；<u>空串表示清除备注</u>，是合法取值，
     *                 调用方不要把"清空"当成"不提交"。
     *
     *  @return 紧凑格式的请求 JSON 文本，形如 {"userid":"100009","remark":"产品组同事"}。
     *          userid 以字符串编码，避免大整数在 JSON 里丢精度。
     *
     *  本方法<u>不校验</u>入参合法性（那是服务端的事），也<u>不截断</u>过长的备注 ——
     *  长度上限由调用方在提交前把关（库里 f_userX_markname 是 VARCHAR(32)）。
     */
    static std::string buildSetFriendRemarkRequest(int64_t userid,
                                                   const std::string& remarkUtf8);

    /**
     *  解析 cmd_set_friend_remark 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法且含整型 code 时返回 true；否则 false（调用方应丢弃该应答）。
     *
     *  应答会把 userid 原样回带，调用方据此判断这是哪一条请求的结果 —— 用户连着
     *  改几个人的备注时会有多条请求在途，不带它就对不上号，失败时会把别人的备注
     *  退回去。老服务端不回带 userid 时本字段为 0，调用方应据此放弃精确回滚。
     */
    static bool parseSetFriendRemarkResponse(const std::string& jsonbody,
                                             SetFriendRemarkResult& out);

private:
    FriendProtocolClient() = delete;
    ~FriendProtocolClient() = delete;
    FriendProtocolClient(const FriendProtocolClient&) = delete;
    FriendProtocolClient& operator=(const FriendProtocolClient&) = delete;
};

} // namespace net
