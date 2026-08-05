/**
 *  FavoriteProtocolClient.h
 *
 *  「我的收藏」报文（cmd_set_favorite=1215）的客户端侧编解码：构造"加入 / 移出收藏"
 *  请求，解析其应答。与服务端 chatserversrc/FavoriteProtocol.{h,cpp} 一一对应。
 *
 *  收藏对象既可以是联系人也可以是群，两者用 (ctype, cid) 一对值区分，取值见
 *  Protocol.h 的 enum conversation_type —— 与会话、消息里 ctype 的口径完全一致。
 *
 *  收藏<u>列表</u>不走本文件：它随通讯录一起下发（cmd_get_contacts 应答里的
 *  favorites 数组），解析在 ContactsProtocolClient。本文件只管"改一条"。
 *
 *  注意与 1901~1903 的"收藏消息"（把某条聊天记录存起来）是两个功能，别混用。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不碰窗口、不碰 socket，可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_set_favorite 应答解析结果。
struct SetFavoriteResult
{
    int     m_code;      //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
                         //（收藏数量超限为 error_code_favorite_limit=209）
    std::string m_msg;   //结果描述（UTF-8），可为空串
    int     m_ctype;     //本次操作的收藏对象类型，服务端原样回带：1 联系人、2 群组
    int64_t m_cid;       //本次操作的收藏对象ID，服务端原样回带
    int     m_favorite;  //本次操作的目标状态，服务端原样回带：1 加入收藏、0 移出收藏

    SetFavoriteResult()
        : m_code(-1)     //-1：尚未解析出有效结果码
        , m_ctype(0)
        , m_cid(0)
        , m_favorite(0)
    {
    }
};

//「我的收藏」报文编解码工具类；全部为静态方法，不可实例化。
class FavoriteProtocolClient
{
public:
    /**
     *  构造"加入 / 移出收藏"请求 JSON 包体（cmd_set_favorite=1215）。
     *
     *    ctype：    收藏对象类型（见 conversation_type，1 联系人 2 群组）。
     *    cid：      收藏对象ID（联系人=对端userid，群组=群id；须 > 0）。
     *    favorite： true 加入收藏、false 移出收藏。
     *
     *  @return 紧凑格式的请求 JSON 文本，形如 {"ctype":1,"cid":"5081","favorite":1}。
     *          cid 以字符串编码，避免大整数在 JSON 里丢精度。
     *
     *  本方法<u>不校验</u>入参合法性（那是服务端的事），传了非法值也会照样生成 JSON，
     *  服务端会以 error_code_bad_request 拒绝。
     */
    static std::string buildSetFavoriteRequest(int ctype, int64_t cid, bool favorite);

    /**
     *  解析 cmd_set_favorite 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。
     *
     *  @return JSON 合法且含整型 code 时返回 true；否则 false（调用方应丢弃该应答）。
     *
     *  应答会把 ctype / cid / favorite 原样回带，调用方据此判断这是哪一条请求的结果 ——
     *  用户连点几个人时会有多条请求在途，不带这三项就对不上号、乐观更新也没法正确回滚。
     */
    static bool parseSetFavoriteResponse(const std::string& jsonbody, SetFavoriteResult& out);

private:
    FavoriteProtocolClient() = delete;
    ~FavoriteProtocolClient() = delete;
    FavoriteProtocolClient(const FavoriteProtocolClient&) = delete;
    FavoriteProtocolClient& operator=(const FavoriteProtocolClient&) = delete;
};

} // namespace net
