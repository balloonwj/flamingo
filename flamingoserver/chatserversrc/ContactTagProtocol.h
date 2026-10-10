/**
 *  v2「联系人标签」报文（cmd_set_contact_tags）的编解码。
 *
 *  标签是给联系人贴的短词（形如"设计""外包"），在联系人资料对话框里成排显示。
 *  与备注名一样是<u>单向</u>的：只影响本人这一侧的显示，对方看不到也不受影响。
 *
 *  请求带的是某个联系人在本人视角下的<u>完整</u>标签列表，服务端整份覆盖，空数组表示
 *  清空 —— 增、改、删共用这一条命令。之所以不设"加一条 / 删一条"的增量命令：标签的
 *  编辑入口是一个可以随意增删改排序的输入区，用户点"保存"时客户端手里本来就是一份
 *  完整列表，增量协议反而要客户端自己算差异、还要处理多端并发编辑时的合并。
 *
 *  应答回带 userid 与服务端归一后的最终列表（失败时回带服务端当前实际存着的列表），
 *  客户端据它把界面刷成与服务端一致的样子，不必自己猜哪些标签被去重或被去空白了。
 *
 *  本模块是纯逻辑 —— 只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_contacttagprotocol.cpp）。归一、长度与条数校验、落库都在
 *  ContactTagManager 与会话层 ChatSession::onSetContactTags。
 *
 *  读取标签不另设命令：标签随 cmd_get_contacts 应答的每个 contacts[] 元素一起下发
 *  （见 ContactsProtocol 的 ContactInfo::tags）。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//cmd_set_contact_tags 请求
struct SetContactTagsRequest
{
    int64_t userid;                   //被贴标签的联系人 userid，恒 > 0
    std::vector<std::string> tags;    //该联系人在本人视角下的完整标签列表，可为空（表示清空）

    SetContactTagsRequest()
        : userid(0)
    {
    }
};

//联系人标签报文编解码工具类；全部为静态方法，不可实例化
class ContactTagProtocol
{
public:
    /**
     *  解析 cmd_set_contact_tags 请求。
     *
     *    json： 请求包体（UTF-8 JSON 文本），形如
     *           {"userid":"5081","tags":["设计","外包"]}
     *           userid 按本协议约定以<u>字符串</u>编码，不是数字。
     *           tags 为字符串数组，<u>必填</u>；空数组表示清空该联系人的全部标签。
     *    req：  出参，解析成功时被填充；失败时内容未定义，调用方不得使用。
     *
     *  @return true 解析成功且各字段合法；false 表示 JSON 非法、缺字段、
     *          userid 不是可转成正整数的字符串、tags 不是数组、或数组里
     *          混有非字符串元素。调用方据 false 直接回 error_code_bad_request。
     *
     *  tags 缺失一律判非法，<u>不</u>按"空数组"处理：两者在语义上差得太远 ——
     *  真按空数组走，一个漏发字段的客户端会把用户贴好的标签整份清空。
     *
     *  本方法只做<u>格式</u>校验，不做归一，也不判单条长度与条数上限：归一后条数才是
     *  判上限的依据，而归一逻辑属于存储层（ContactTagManager::normalizeTags）。
     *  同样不判断"这个人在不在同一企业"——那要查库，属于会话层的事。
     */
    static bool parseSetContactTagsRequest(const std::string& json, SetContactTagsRequest& req);

    /**
     *  构造 cmd_set_contact_tags 应答。
     *
     *    code：   结果码，0 表示成功，其余取值见 Protocol.h 的 enum error_code。
     *    msg：    结果描述文本，可为空串。
     *    userid： 本次操作的联系人 userid，原样回带（以字符串编码进 JSON）。
     *    tags：   服务端归一后的最终标签列表；失败时应传服务端当前实际存着的那一份，
     *             让客户端把界面回滚到与服务端一致的状态。空列表照常写出空数组。
     *
     *  @return 紧凑格式的应答 JSON 文本。
     *
     *  之所以把 userid 原样回带：客户端是"先改界面再发请求"的乐观更新，用户连着
     *  编辑几个人的标签时会有多条请求在途，应答不带它就分不清是哪一条失败、回滚会
     *  退错人。口径与 cmd_set_favorite 回带对象一致。
     */
    static std::string buildSetContactTagsResponse(int code, const std::string& msg,
                                                   int64_t userid,
                                                   const std::vector<std::string>& tags);

private:
    ContactTagProtocol() = delete;
    ~ContactTagProtocol() = delete;
    ContactTagProtocol(const ContactTagProtocol&) = delete;
    ContactTagProtocol& operator=(const ContactTagProtocol&) = delete;
};
