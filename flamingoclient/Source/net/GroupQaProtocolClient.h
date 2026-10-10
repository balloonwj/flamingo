/**
 *  群问答的报文编解码（客户端侧）：cmd_get_group_qa(1336) / cmd_save_group_qa(1337) /
 *  cmd_delete_group_qa(1338) 的请求构造与应答解析、cmd_group_qa_changed_notify(1339) 推送解析。
 *
 *  与服务端 chatserversrc/GroupQaProtocol 互为镜像。纯逻辑，只依赖 jsoncpp，不触及网络与 UI，
 *  单测见 Source/Tests/GroupQaTests.cpp。方案见 docs/群问答方案.md。
 *
 *  典型用法：
 *      std::string body = net::GroupQaProtocolClient::buildSaveRequest(groupid, 0, q, a);
 *      net::GroupQaResult r;
 *      net::GroupQaProtocolClient::parseGetResponse(json, r);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//一条群问答的来源
enum GroupQaSource
{
    //群主或管理员在本群维护的问答，群主 / 管理员可以修改与删除
    kGroupQaSourceGroup = 0,
    //管理控制台配置、应用到本群的企业问答，群里只读
    kGroupQaSourceEnterprise = 1
};

//一条群问答（cmd_get_group_qa 应答 items 数组的一项）
struct GroupQaItem
{
    int64_t     m_id;          //问答ID：本群问答与企业问答各自编号，需与 m_source 一起才能确定是哪一条
    std::string m_question;    //问题（UTF-8）
    std::string m_answer;      //答案（UTF-8，纯文本，可含换行与网址）
    int         m_source;      //来源，取值见 GroupQaSource
    std::string m_editorname;  //最后修改人的昵称（UTF-8）；企业问答为空
    int64_t     m_updatetime;  //最后修改时间，Unix 毫秒

    GroupQaItem()
        : m_id(0), m_source(kGroupQaSourceGroup), m_updatetime(0)
    {
    }
};

//拉取群问答（cmd_get_group_qa=1336）的应答解析结果
struct GroupQaResult
{
    int                      m_code;     //结果码，0 成功；302 不是群成员
    std::string              m_msg;      //结果描述
    int64_t                  m_groupid;  //群ID
    std::vector<GroupQaItem> m_items;    //合并去重后的问答：本群问答在前（按修改时间倒序），企业问答在后

    GroupQaResult()
        : m_code(-1), m_groupid(0)
    {
    }
};

//群问答报文的编解码，全部为静态函数
class GroupQaProtocolClient
{
public:
    /**
     *  构造拉取请求（cmd_get_group_qa=1336）：{ "groupid":"..." }。
     */
    static std::string buildGetRequest(int64_t groupid);

    /**
     *  构造新增或修改请求（cmd_save_group_qa=1337）。
     *  @param groupid  群ID
     *  @param id       要修改的本群问答ID；0 表示新增
     *  @param question 问题（UTF-8）
     *  @param answer   答案（UTF-8）
     *  @return 紧凑 JSON：{ groupid, id, question, answer }，ID 写成字符串
     */
    static std::string buildSaveRequest(int64_t groupid, int64_t id, const std::string& question,
                                        const std::string& answer);

    /**
     *  构造删除请求（cmd_delete_group_qa=1338）：{ groupid, id }。
     */
    static std::string buildDeleteRequest(int64_t groupid, int64_t id);

    /**
     *  解析拉取应答（cmd_get_group_qa=1336）。
     *  @param jsonbody 应答包体
     *  @param out      [出参] 解析结果；问题或答案不是字符串、ID 不为正的条目跳过
     *  @return JSON 合法且带 code 返回 true；成功应答还要求 groupid 为正
     */
    static bool parseGetResponse(const std::string& jsonbody, GroupQaResult& out);

    /**
     *  解析问答变更推送（cmd_group_qa_changed_notify=1339）：{ "groupid":"..." }。
     *  @param jsonbody 推送包体
     *  @param groupid  [出参] 群ID
     *  @return groupid 为正返回 true
     */
    static bool parseChangedNotify(const std::string& jsonbody, int64_t& groupid);

private:
    GroupQaProtocolClient();
};

} // namespace net
