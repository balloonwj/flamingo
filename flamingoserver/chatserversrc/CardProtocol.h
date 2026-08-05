/**
 *  v2 群互动卡片相关报文（cmd_vote_poll / cmd_poll_changed_notify /
 *  cmd_join_relay / cmd_relay_changed_notify / cmd_update_todo /
 *  cmd_todo_changed_notify）的编解码。
 *
 *  卡片本体（投票 / 接龙 / 待办）是一条 msgtype=10/11/12 的消息；本模块处理
 *  对卡片的交互。本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，
 *  便于单元测试（见 tests/test_cardprotocol.cpp）。互动落库与推送在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//一条卡片互动记录，对应数据库 t_card_interaction 的一行
struct CardInteractionRecord
{
    int64_t     userid;      //互动用户userid
    std::string data;        //互动数据：投票为选项下标JSON数组 / 接龙为接龙文本 / 待办为勾选标记
    int64_t     createtime;  //互动时间，Unix毫秒

    CardInteractionRecord()
        : userid(0), createtime(0)
    {
    }
};

//cmd_vote_poll 请求
struct VotePollRequest
{
    int              ctype;    //会话类型：1 单聊 2 群聊
    int64_t          cid;      //会话ID
    int64_t          msgid;    //投票卡片所在消息id
    std::vector<int> options;  //所投选项的下标列表（多选时可多个）

    VotePollRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//cmd_join_relay 请求
struct JoinRelayRequest
{
    int         ctype;  //会话类型
    int64_t     cid;    //会话ID
    int64_t     msgid;  //接龙卡片所在消息id
    std::string text;   //本人的接龙文本

    JoinRelayRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//cmd_update_todo 请求
struct UpdateTodoRequest
{
    int     ctype;  //会话类型
    int64_t cid;    //会话ID
    int64_t msgid;  //待办卡片所在消息id
    int     done;   //勾选状态：1 勾选完成 0 取消勾选

    UpdateTodoRequest()
        : ctype(1), cid(0), msgid(0), done(0)
    {
    }
};

//群互动卡片报文编解码工具类；全部为静态方法，不可实例化
class CardProtocol
{
public:
    /**
     *  解析 cmd_vote_poll 请求；cid/msgid 缺失非正、或 options 缺失/为空返回 false。
     */
    static bool parseVotePollRequest(const std::string& json, VotePollRequest& req);

    /**
     *  解析 cmd_join_relay 请求；cid/msgid 缺失非正、或 text 缺失/为空返回 false。
     */
    static bool parseJoinRelayRequest(const std::string& json, JoinRelayRequest& req);

    /**
     *  解析 cmd_update_todo 请求；cid/msgid 缺失非正、或 done 缺失返回 false。
     */
    static bool parseUpdateTodoRequest(const std::string& json, UpdateTodoRequest& req);

    /**
     *  把投票选项下标列表序列化为紧凑JSON数组文本，作为 t_card_interaction.f_data。
     */
    static std::string buildVoteData(const std::vector<int>& options);

    /**
     *  从投票卡片的 content（定义）中取出元信息。
     *  @param content     投票卡片消息的 content JSON 文本
     *  @param optioncount [出参] 选项个数
     *  @param multi       [出参] 是否多选：0 单选 1 多选
     *  @param expiretime  [出参] 截止时间，Unix毫秒；0 表示不过期
     *  @return content 为合法投票卡片定义返回 true
     */
    static bool getPollMeta(const std::string& content, int& optioncount,
                            int& multi, int64_t& expiretime);

    /**
     *  判断某用户是否在待办卡片的 assignees（负责人）列表中。
     *  @param content 待办卡片消息的 content JSON 文本
     *  @param userid  待判定用户userid
     */
    static bool isTodoAssignee(const std::string& content, int64_t userid);

    /**
     *  构造 cmd_poll_changed_notify 推送 JSON：{ msgid, votes:{下标:票数}, totalcount }。
     *  仅含聚合票数，不暴露投票人（匿名投票安全）。
     */
    static std::string buildPollChangedNotify(int64_t msgid,
                                              const std::vector<CardInteractionRecord>& rows);

    /**
     *  构造 cmd_relay_changed_notify 推送 JSON：{ msgid, items:[{userid,text}] }。
     */
    static std::string buildRelayChangedNotify(int64_t msgid,
                                               const std::vector<CardInteractionRecord>& rows);

    /**
     *  构造 cmd_todo_changed_notify 推送 JSON：{ msgid, doneusers:[userid] }。
     */
    static std::string buildTodoChangedNotify(int64_t msgid,
                                              const std::vector<CardInteractionRecord>& rows);

    /**
     *  把卡片当前互动状态合并进卡片消息的 content。
     *  投票并入 votes/totalcount，接龙并入 items，待办并入 doneusers；供
     *  cmd_sync_messages 下发卡片消息时使用，使客户端打开会话即见最新状态。
     *  @param msgtype 卡片消息类型（10 投票 / 11 接龙 / 12 待办；其它原样返回）
     *  @param content 卡片消息原始 content JSON 文本
     *  @param rows    该卡片的全部互动记录
     *  @return 合并后的 content JSON 文本；content 非法时原样返回
     */
    static std::string augmentCardContent(int msgtype, const std::string& content,
                                          const std::vector<CardInteractionRecord>& rows);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    CardProtocol() = delete;
    ~CardProtocol() = delete;
    CardProtocol(const CardProtocol&) = delete;
    CardProtocol& operator=(const CardProtocol&) = delete;
};
