/**
 *  StarredProtocolClient.h
 *
 *  「特别关注」报文（cmd_set_starred=1216）的客户端侧编解码：构造"加入 / 取消特别
 *  关注"请求，解析其应答。与服务端 chatserversrc/StarredProtocol.{h,cpp} 一一对应。
 *
 *  特别关注的对象<u>只能是联系人</u>，故通篇只有一个 target（对端 userid），不像收藏
 *  那样带 (ctype, cid) 一对值 —— 群没有"上下线"，标记它没有意义。
 *
 *  关注<u>名单</u>不走本文件：它随通讯录一起下发（cmd_get_contacts 应答里的
 *  starred 数组），解析在 ContactsProtocolClient。本文件只管"改一条"。
 *
 *  与「收藏」（FavoriteProtocolClient）是两件事：收藏是常用入口的书签、可以是群；
 *  特别关注只能是人，用来在对方上下线时弹右下角提示。详见 docs/特别关注接入方案.md。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不碰窗口、不碰 socket，可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_set_starred 应答解析结果。
struct SetStarredResult
{
    int         m_code;     //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
                            //（关注人数超限为 error_code_starred_limit=210；
                            //  对方不是同企业同事为 error_code_not_friend）
    std::string m_msg;      //结果描述（UTF-8），可为空串
    int64_t     m_target;   //本次操作的对象 userid，服务端原样回带
    int         m_starred;  //本次操作的目标状态，服务端原样回带：1 标记、0 取消

    SetStarredResult()
        : m_code(-1)        //-1：尚未解析出有效结果码
        , m_target(0)
        , m_starred(0)
    {
    }
};

//「特别关注」报文编解码工具类；全部为静态方法，不可实例化。
class StarredProtocolClient
{
public:
    /**
     *  构造"加入 / 取消特别关注"请求 JSON 包体（cmd_set_starred=1216）。
     *
     *    target：  被关注的联系人 userid（须 > 0，且不能是自己）。
     *    starred： true 标记为特别关注、false 取消。
     *
     *  @return 紧凑格式的请求 JSON 文本，形如 {"target":"5081","starred":1}。
     *          target 以字符串编码，避免大整数在 JSON 里丢精度。
     *
     *  本方法<u>不校验</u>入参合法性（那是服务端的事），传了非法值也会照样生成 JSON，
     *  服务端会以 error_code_bad_request 拒绝。
     */
    static std::string buildSetStarredRequest(int64_t target, bool starred);

    /**
     *  解析 cmd_set_starred 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法且含整型 code 时返回 true；否则 false（调用方应丢弃该应答）。
     *
     *  应答会把 target / starred 原样回带，调用方据此判断这是哪一条请求的结果 ——
     *  用户连点几个人时会有多条请求在途，不带这两项就对不上号、乐观更新也没法正确回滚。
     */
    static bool parseSetStarredResponse(const std::string& jsonbody, SetStarredResult& out);

private:
    StarredProtocolClient() = delete;
    ~StarredProtocolClient() = delete;
    StarredProtocolClient(const StarredProtocolClient&) = delete;
    StarredProtocolClient& operator=(const StarredProtocolClient&) = delete;
};

} // namespace net
