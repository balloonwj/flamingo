/**
 *  v2「我的收藏」报文（cmd_set_favorite）的编解码，以及收藏项的数据结构。
 *
 *  收藏对象既可以是联系人也可以是群组，两者用 (ctype, cid) 这一对值区分 ——
 *  形态与 t_conversation 的主键、以及会话/消息 JSON 里的 ctype 字段完全一致
 *  （取值见 Protocol.h 的 enum conversation_type）。这样"收藏一个人"和"收藏
 *  一个群"共用一套键、一条命令、一张表，不必分成两套。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_favoriteprotocol.cpp）。落库、权限校验、数量上限判定都在
 *  FavoriteManager 与会话层 ChatSession::onSetFavorite。
 *
 *  注意与 1901~1903 的"收藏消息"（把某条聊天记录存起来）是两个功能，
 *  数据互不相干，不要把两者的存储或协议混用。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <stdint.h>
#include <string>

//单个用户最多能收藏多少个对象。收藏是"常用入口"，几百条就失去意义了；
//设上限同时也避免有人恶意刷表。超出时以 error_code_favorite_limit 拒绝。
const int kMaxFavoriteCount = 200;

//一条收藏记录：谁收藏的由调用方上下文决定，本结构只描述"收藏了什么"。
//同时用于 cmd_get_contacts 应答里的 favorites 数组与 FavoriteManager 的回读结果。
struct FavoriteItem
{
    int     ctype;  //收藏对象类型，取值见 Protocol.h 的 conversation_type：1 联系人（含服务号）、2 群组
    int64_t cid;    //收藏对象ID：联系人为对端userid，群组为群id

    FavoriteItem()
        : ctype(0)
        , cid(0)
    {
    }

    FavoriteItem(int t, int64_t id)
        : ctype(t)
        , cid(id)
    {
    }
};

//cmd_set_favorite 请求
struct SetFavoriteRequest
{
    int     ctype;     //收藏对象类型：1 联系人、2 群组
    int64_t cid;       //收藏对象ID
    int     favorite;  //目标状态：1 加入收藏、0 移出收藏

    SetFavoriteRequest()
        : ctype(0)
        , cid(0)
        , favorite(0)
    {
    }
};

//收藏报文编解码工具类；全部为静态方法，不可实例化
class FavoriteProtocol
{
public:
    /**
     *  解析 cmd_set_favorite 请求。
     *
     *    json： 请求包体（UTF-8 JSON 文本），形如
     *           {"ctype":1,"cid":"5081","favorite":1}
     *           cid 按本协议约定以字符串编码，不是数字。
     *    req：  出参，解析成功时被填充；失败时内容未定义，调用方不得使用。
     *
     *  @return true 解析成功且各字段合法；false 表示 JSON 非法、缺字段、
     *          ctype 不是 1 或 2、cid <= 0、或 favorite 不是 0 或 1。
     *          调用方据 false 直接回 error_code_invalid_param，不必再逐项校验。
     *
     *  本方法只做<u>格式与取值域</u>校验，不判断"对方是不是好友""自己在不在群里"——
     *  那些要查库，属于会话层的事。
     */
    static bool parseSetFavoriteRequest(const std::string& json, SetFavoriteRequest& req);

    /**
     *  构造 cmd_set_favorite 应答。
     *
     *    code：     结果码，0 表示成功，其余取值见 Protocol.h 的 enum error_code。
     *    msg：      结果描述文本，可为空串。
     *    ctype：    本次操作的收藏对象类型，原样回带。
     *    cid：      本次操作的收藏对象ID，原样回带（以字符串编码进 JSON）。
     *    favorite： 本次操作的目标状态，原样回带。
     *
     *  @return 紧凑格式的应答 JSON 文本。
     *
     *  之所以把 ctype / cid / favorite 原样回带：客户端可能同时发出多条收藏请求
     *  （用户连点几个人），应答里不带上这三项就对不上是哪一条的结果。
     */
    static std::string buildSetFavoriteResponse(int code, const std::string& msg,
                                                int ctype, int64_t cid, int favorite);

private:
    FavoriteProtocol() = delete;
    ~FavoriteProtocol() = delete;
    FavoriteProtocol(const FavoriteProtocol&) = delete;
    FavoriteProtocol& operator=(const FavoriteProtocol&) = delete;
};
