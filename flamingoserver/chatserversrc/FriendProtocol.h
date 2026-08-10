/**
 *  v2 联系人相关报文（cmd_search / cmd_set_friend_remark / cmd_set_blacklist）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_friendprotocol.cpp）。查找、备注落库、拉黑落库等均在会话层。
 *
 *  文件名里的 "Friend" 是历史遗留：企业版已于 2026-08-05 删除好友关系子系统
 *  （加好友 / 删好友 / 好友标签 / 消息盒子 1203~1208、1210、1214 全部移除），
 *  留下的三个协议本质上是"联系人"而非"好友"操作，只是文件名与协议名沿用旧称，
 *  以免动到已发布的协议号与客户端。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//cmd_search 单条结果：用户与群共用本结构，按 type 取用相应字段
struct SearchResultItem
{
    int64_t     id;           //用户ID 或 群ID
    std::string username;     //账号（用户）
    std::string name;         //昵称（用户）或群名称（群）
    int         facetype;     //头像类型
    std::string customface;   //自定义头像文件标识
    int         gender;       //性别（用户）
    std::string signature;    //个性签名（用户）
    std::string address;      //所在地（用户）
    int         usertype;     //用户类型（用户）：0 普通 1 服务号
    int         membercount;  //成员数（群）
    int64_t     ownerid;      //群主ID（群）

    SearchResultItem()
        : id(0), facetype(0), gender(0), usertype(0)
        , membercount(0), ownerid(0)
    {
    }
};

//cmd_set_friend_remark 请求
struct SetFriendRemarkRequestData
{
    int64_t     userid;  //目标联系人用户ID
    std::string remark;  //备注名；空串表示清除备注

    SetFriendRemarkRequestData()
        : userid(0)
    {
    }
};

//cmd_set_blacklist 请求
struct SetBlacklistRequestData
{
    int64_t userid;  //目标用户ID
    int     action;  //操作：1 加入黑名单 0 移出黑名单

    SetBlacklistRequestData()
        : userid(0), action(0)
    {
    }
};

//联系人报文编解码工具类；全部为静态方法，不可实例化
class FriendProtocol
{
public:
    /**
     *  解析 cmd_search 请求。
     *  @param json    请求包体（UTF-8 JSON）
     *  @param keyword [出参] 查找关键字
     *  @param type    [出参] 查找类型：1 用户 2 群 3 企业；缺失时为 1
     *  @return JSON 合法且 keyword 非空返回 true
     */
    static bool parseSearchRequest(const std::string& json, std::string& keyword, int& type);

    /**
     *  构造 cmd_search 应答 JSON：{ code, msg, type, results:[...] }。
     *  results 各元素按 type 取字段：type=1 为用户卡片，type=2 为群卡片。
     */
    static std::string buildSearchResponse(int code, const std::string& msg,
                                           int type, const std::vector<SearchResultItem>& items);

    /**
     *  解析 cmd_set_friend_remark 请求；userid 缺失或非正返回 false。
     *  remark 可缺省，缺省或为空串表示清除备注。
     */
    static bool parseSetFriendRemarkRequest(const std::string& json, SetFriendRemarkRequestData& req);

    /**
     *  解析 cmd_set_blacklist 请求；userid 缺失/非正或 action 缺失返回 false。
     */
    static bool parseSetBlacklistRequest(const std::string& json, SetBlacklistRequestData& req);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  构造 cmd_set_friend_remark 应答 { code, msg, userid }。
     *
     *  比通用应答多回带一个 userid —— 客户端改备注是"先把界面改掉、再发请求"的
     *  乐观更新，失败时要把改动退回去。用户连着改几个人的备注时会有多条请求在途，
     *  应答里不带 userid 就分不清是哪一条失败了，退错人比不退更糟。口径与
     *  cmd_set_favorite 原样回带 ctype/cid/favorite 一致。
     *
     *    code：   结果码，0 成功；其余见 Protocol.h 的 enum error_code。
     *    msg：    结果描述。
     *    userid： 本次设置备注的目标联系人ID，原样回带；以字符串编码，避免大整数
     *             在 JSON 里丢精度（与本协议其余 64 位 ID 同一口径）。
     */
    static std::string buildSetFriendRemarkResponse(int code, const std::string& msg,
                                                    int64_t userid);

private:
    FriendProtocol() = delete;
    ~FriendProtocol() = delete;
    FriendProtocol(const FriendProtocol&) = delete;
    FriendProtocol& operator=(const FriendProtocol&) = delete;
};
