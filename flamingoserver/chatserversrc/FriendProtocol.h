/**
 *  v2 好友关系相关报文（cmd_search / add_friend_request / friend_request_notify /
 *  handle_friend_request / friend_request_result_notify /
 *  friend_relation_changed_notify / get_notifications）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_friendprotocol.cpp）。查找、申请落库、关系建立等均在会话层。
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
    bool        isfriend;     //是否已是当前用户的好友（用户）
    int         membercount;  //成员数（群）
    int64_t     ownerid;      //群主ID（群）

    SearchResultItem()
        : id(0), facetype(0), gender(0), usertype(0)
        , isfriend(false), membercount(0), ownerid(0)
    {
    }
};

//cmd_add_friend_request 请求
struct AddFriendRequestData
{
    int64_t     userid;     //被申请加为好友的目标用户ID
    std::string source;     //申请来源：search/group/card 等
    std::string verifymsg;  //验证留言
};

//cmd_handle_friend_request 请求
struct HandleFriendRequestData
{
    int64_t requestid;  //被处理的申请ID
    int     action;     //处理动作：0 拒绝 1 同意
};

//一条加好友申请：供 cmd_friend_request_notify 与 cmd_get_notifications 共用
struct FriendRequestItem
{
    int64_t     requestid;       //申请ID
    int64_t     fromuserid;      //申请发起方ID
    std::string fromusername;    //发起方账号
    std::string fromnickname;    //发起方昵称
    int         fromfacetype;    //发起方头像类型
    std::string fromcustomface;  //发起方自定义头像
    std::string verifymsg;       //验证留言
    int         status;          //状态：0 待处理 1 已同意 2 已拒绝
    int64_t     timestamp;       //申请时间，Unix 毫秒

    FriendRequestItem()
        : requestid(0), fromuserid(0), fromfacetype(0), status(0), timestamp(0)
    {
    }
};

//cmd_delete_friend 请求
struct DeleteFriendRequestData
{
    int64_t userid;  //被删除的好友用户ID

    DeleteFriendRequestData()
        : userid(0)
    {
    }
};

//cmd_set_friend_remark 请求
struct SetFriendRemarkRequestData
{
    int64_t     userid;  //目标好友用户ID
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

//cmd_set_friend_tags 请求
struct SetFriendTagsRequest
{
    int64_t     userid;    //目标好友用户ID
    std::string tagsJson;  //标签列表已序列化为紧凑JSON数组文本（如 ["同事","项目组"]）

    SetFriendTagsRequest()
        : userid(0)
    {
    }
};

//好友关系报文编解码工具类；全部为静态方法，不可实例化
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
     *  解析 cmd_add_friend_request 请求；userid 缺失或非正返回 false。
     */
    static bool parseAddFriendRequest(const std::string& json, AddFriendRequestData& req);

    /**
     *  解析 cmd_handle_friend_request 请求；requestid 缺失或非正返回 false。
     */
    static bool parseHandleFriendRequest(const std::string& json, HandleFriendRequestData& req);

    /**
     *  构造 cmd_search 应答 JSON：{ code, msg, type, results:[...] }。
     *  results 各元素按 type 取字段：type=1 为用户卡片，type=2 为群卡片。
     */
    static std::string buildSearchResponse(int code, const std::string& msg,
                                           int type, const std::vector<SearchResultItem>& items);

    /**
     *  构造 cmd_add_friend_request 应答 JSON：{ code, msg, requestid }。
     */
    static std::string buildAddFriendResponse(int code, const std::string& msg, int64_t requestid);

    /**
     *  构造 cmd_friend_request_notify 推送 JSON（推给被申请方）。
     */
    static std::string buildFriendRequestNotify(const FriendRequestItem& item);

    /**
     *  构造 cmd_friend_request_result_notify 推送 JSON（推给原申请方）。
     *  @param action 申请方申请的处理结果：0 被拒绝 1 被同意
     */
    static std::string buildFriendRequestResultNotify(int64_t requestid, int64_t fromuserid, int action);

    /**
     *  构造 cmd_friend_relation_changed_notify 推送 JSON。
     *  @param userid     发生关系变更的对端用户ID
     *  @param changetype 变更类型：1 成为好友 2 好友被删除
     */
    static std::string buildFriendRelationChangedNotify(int64_t userid, int changetype);

    /**
     *  构造 cmd_get_notifications 应答 JSON：{ code, msg, notifications:[...] }。
     */
    static std::string buildGetNotificationsResponse(int code, const std::string& msg,
                                                     const std::vector<FriendRequestItem>& items);

    /**
     *  解析 cmd_delete_friend 请求；userid 缺失或非正返回 false。
     */
    static bool parseDeleteFriendRequest(const std::string& json, DeleteFriendRequestData& req);

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
     *  解析 cmd_set_friend_tags 请求（请求体为 { "userid":"...", "tags":[...] }）。
     *  tags 缺省或为空数组表示清除全部标签；非字符串元素被忽略。
     *  解析后 req.tagsJson 为标签数组的紧凑JSON文本。
     *  @return userid 缺失或非正返回 false
     */
    static bool parseSetFriendTagsRequest(const std::string& json, SetFriendTagsRequest& req);

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
     *    userid： 本次设置备注的目标好友ID，原样回带；以字符串编码，避免大整数
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
