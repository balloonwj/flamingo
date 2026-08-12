/**
 *  v2「特别关注」报文（cmd_set_starred）的编解码。
 *
 *  特别关注的对象<u>只能是联系人</u>，所以本模块通篇只有一个 target（对端 userid），
 *  不像收藏那样带 (ctype, cid) 一对值 —— 群没有"上下线"，标记它没有意义，多一个
 *  ctype 只会让人误以为群也能标。
 *
 *  被特别关注的联系人上下线时，关注方的客户端会在桌面右下角弹一个置顶小窗提示。
 *  <u>服务端不因关注关系改变任何推送</u>：上下线仍按企业全员广播（见
 *  ChatServer::broadcastToEnterprise），服务端只保管这份名单，弹不弹由客户端
 *  拿自己的名单判断。详见 docs/特别关注接入方案.md。
 *
 *  本模块是纯逻辑 —— 只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_starredprotocol.cpp）。落库、权限校验、人数上限判定都在
 *  StarredManager 与会话层 ChatSession::onSetStarred。
 *
 *  与「收藏」（FavoriteProtocol）是两件事：收藏是常用入口的书签、可以是群；
 *  特别关注只能是人，用来盯上下线。一个人可以只收藏不关注、也可以只关注不收藏。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>

//单个用户最多能特别关注多少人。特别关注的意义就是"少数几个人"，标满几百人等于
//没关注；设上限同时也避免有人恶意刷表。超出时以 error_code_starred_limit 拒绝。
const int kMaxStarredCount = 50;

//cmd_set_starred 请求
struct SetStarredRequest
{
    int64_t target;   //被关注的联系人 userid，恒 > 0
    int     starred;  //目标状态：1 标记为特别关注、0 取消

    SetStarredRequest()
        : target(0)
        , starred(0)
    {
    }
};

//特别关注报文编解码工具类；全部为静态方法，不可实例化
class StarredProtocol
{
public:
    /**
     *  解析 cmd_set_starred 请求。
     *
     *    json： 请求包体（UTF-8 JSON 文本），形如
     *           {"target":"5081","starred":1}
     *           target 按本协议约定以<u>字符串</u>编码，不是数字。
     *    req：  出参，解析成功时被填充；失败时内容未定义，调用方不得使用。
     *
     *  @return true 解析成功且各字段合法；false 表示 JSON 非法、缺字段、
     *          target 不是可转成正整数的字符串、或 starred 不是 0 或 1。
     *          调用方据 false 直接回 error_code_bad_request，不必再逐项校验。
     *
     *  本方法只做<u>格式与取值域</u>校验，不判断"对方在不在同一企业""是不是自己"——
     *  那些要查库或要知道请求发起人是谁，属于会话层的事。
     */
    static bool parseSetStarredRequest(const std::string& json, SetStarredRequest& req);

    /**
     *  构造 cmd_set_starred 应答。
     *
     *    code：    结果码，0 表示成功，其余取值见 Protocol.h 的 enum error_code。
     *    msg：     结果描述文本，可为空串。
     *    target：  本次操作的对象 userid，原样回带（以字符串编码进 JSON）。
     *    starred： 本次操作的目标状态，原样回带。
     *
     *  @return 紧凑格式的应答 JSON 文本。
     *
     *  之所以把 target / starred 原样回带：客户端是"先改界面再发请求"的乐观更新，
     *  用户连着标记几个人时会有多条请求在途，应答不带这两项就分不清是哪一条失败、
     *  回滚会退错人。口径与 cmd_set_favorite 回带 ctype/cid/favorite 一致。
     */
    static std::string buildSetStarredResponse(int code, const std::string& msg,
                                               int64_t target, int starred);

private:
    StarredProtocol() = delete;
    ~StarredProtocol() = delete;
    StarredProtocol(const StarredProtocol&) = delete;
    StarredProtocol& operator=(const StarredProtocol&) = delete;
};
