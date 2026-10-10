/**
 *  群问答的记录结构、纯逻辑与报文编解码，GroupQaProtocol.h（docs/群问答方案.md）。
 *
 *  群问答分两种：本群问答（t_group_qa，群主或管理员在群聊窗里维护）与企业问答（t_enterprise_qa，企业管理员在
 *  flamingoAdmin 里维护，应用到本企业的全部群或所选的群）。客户端拉某个群的问答时两种合在一起返回：本群问答在前、
 *  企业问答在后，企业问答与本群问答的问题相同（去掉首尾空白、ASCII 不分大小写后相同）时只留本群的。
 *
 *  本模块只依赖 jsoncpp，不碰网络与数据库，见 tests/test_groupqa.cpp。存取在 GroupQaManager，会话层在
 *  ChatSession（1336~1339）与 HttpSession（/admin/qa/*.do）。
 *
 *  典型用法：
 *      std::vector<GroupQaRecord> merged = GroupQaProtocol::mergeForGroup(groupItems, enterpriseItems);
 *      send(cmd_get_group_qa, seq, GroupQaProtocol::buildGetResponse(0, "ok", groupid, merged));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//问题最多多少个字符（按 UTF-8 字符数算，与 t_group_qa.f_question 的列宽 varchar(128) 一致）
static const int kGroupQaQuestionMaxChars = 128;
//答案最多多少个字符（与 f_answer 的列宽 varchar(2048) 一致）
static const int kGroupQaAnswerMaxChars = 2048;
//每个群最多多少条本群问答。问答是给人翻的，几百条就没法找了；上限同时防止刷表
static const int kGroupQaPerGroupMax = 200;
//每个企业最多多少条企业问答
static const int kEnterpriseQaPerEnterpriseMax = 1000;

//问答来源：拉某个群的问答时用来区分两种
enum group_qa_source
{
    group_qa_source_group      = 0,   //本群问答：群主或管理员维护，可修改、删除
    group_qa_source_enterprise = 1,   //企业问答：企业管理员在 flamingoAdmin 里维护，群里只读
};

//企业问答的应用范围（t_enterprise_qa.f_scope）
enum enterprise_qa_scope
{
    enterprise_qa_scope_all      = 0,   //本企业的全部群
    enterprise_qa_scope_selected = 1,   //t_enterprise_qa_group 里登记的群
};

//保存一条问答前的内容检查结果
enum group_qa_check
{
    group_qa_check_ok             = 0,   //可以保存
    group_qa_check_empty          = 1,   //问题或答案去掉首尾空白后为空
    group_qa_check_question_long  = 2,   //问题超过 kGroupQaQuestionMaxChars 个字符
    group_qa_check_answer_long    = 3,   //答案超过 kGroupQaAnswerMaxChars 个字符
};

//一条问答（本群问答或企业问答，拉某个群的问答时两种都转成它）
struct GroupQaRecord
{
    int64_t     id;          //问答ID（本群问答为 t_group_qa.f_id，企业问答为 t_enterprise_qa.f_id，两边各自编号）
    int64_t     groupid;     //所属群ID；企业问答为请求的那个群
    std::string question;    //问题
    std::string answer;      //答案
    int         source;      //来源，取值见 group_qa_source
    int64_t     editor;      //最后修改人：本群问答为 userid，企业问答为管理员ID
    std::string editorname;  //最后修改人的显示名（本群问答由会话层补昵称，企业问答为管理员姓名）
    int64_t     createtime;  //创建时间，Unix毫秒
    int64_t     updatetime;  //最后修改时间，Unix毫秒

    GroupQaRecord()
        : id(0), groupid(0), source(group_qa_source_group), editor(0), createtime(0), updatetime(0)
    {
    }
};

//一条企业问答（管理接口用）
struct EnterpriseQaRecord
{
    int64_t              id;            //问答ID
    int64_t              enterpriseid;  //所属企业ID
    std::string          question;      //问题
    std::string          answer;        //答案
    int                  scope;         //应用范围，取值见 enterprise_qa_scope
    std::vector<int64_t> groupids;      //scope 为 enterprise_qa_scope_selected 时应用到的群
    int64_t              creatorid;     //最后修改的管理员ID
    std::string          creatorname;   //最后修改的管理员姓名
    int64_t              createtime;    //创建时间，Unix毫秒
    int64_t              updatetime;    //最后修改时间，Unix毫秒

    EnterpriseQaRecord()
        : id(0), enterpriseid(0), scope(enterprise_qa_scope_all), creatorid(0), createtime(0), updatetime(0)
    {
    }
};

//cmd_save_group_qa 请求
struct SaveGroupQaRequest
{
    int64_t     groupid;   //群ID
    int64_t     id;        //问答ID；0 表示新增
    std::string question;  //问题（已去掉首尾空白）
    std::string answer;    //答案（已去掉首尾空白）

    SaveGroupQaRequest()
        : groupid(0), id(0)
    {
    }
};

//群问答的纯逻辑与报文编解码工具类；全部为静态方法，不可实例化
class GroupQaProtocol
{
public:
    /**
     *  去掉首尾空白（空格、制表符、回车、换行）。
     */
    static std::string trim(const std::string& s);

    /**
     *  问题的比较口径：去掉首尾空白，ASCII 字母转小写。两个问题的口径相同即视为同一个问题。
     */
    static std::string normalizeQuestion(const std::string& question);

    /**
     *  检查一条问答能不能保存（长度按 UTF-8 字符数算）。
     *  @param question 问题（调用方已去掉首尾空白）
     *  @param answer   答案（调用方已去掉首尾空白）
     *  @return 取值见 group_qa_check
     */
    static int checkContent(const std::string& question, const std::string& answer);

    /**
     *  合并某个群看到的问答：本群问答在前（保持传入顺序），企业问答在后（保持传入顺序）；企业问答里与本群问答问题相同的、
     *  以及企业问答之间问题重复的（留第一条）去掉。
     *  @param groupItems      本群问答
     *  @param enterpriseItems 应用到本群的企业问答（source 由本函数置为 group_qa_source_enterprise）
     *  @return 合并结果
     */
    static std::vector<GroupQaRecord> mergeForGroup(const std::vector<GroupQaRecord>& groupItems,
                                                    const std::vector<GroupQaRecord>& enterpriseItems);

    /**
     *  解析 cmd_save_group_qa 请求 { groupid:"群ID", id:"问答ID", question, answer }。问题与答案去掉首尾空白，
     *  内容是否合规不在这里判断（见 checkContent）。
     *  @return groupid 为正、id 不为负、question 与 answer 都是字符串时返回 true
     */
    static bool parseSaveRequest(const std::string& json, SaveGroupQaRequest& req);

    /**
     *  解析 cmd_delete_group_qa 请求 { groupid:"群ID", id:"问答ID" }。
     *  @return groupid 与 id 都为正时返回 true
     */
    static bool parseDeleteRequest(const std::string& json, int64_t& groupid, int64_t& id);

    /**
     *  构造 cmd_get_group_qa 应答：{ code, msg, groupid, items:[{id, question, answer, source, editorname, updatetime}] }，
     *  source 写成字符串 "group" 或 "enterprise"，id 一律写成字符串。
     */
    static std::string buildGetResponse(int code, const std::string& msg, int64_t groupid,
                                        const std::vector<GroupQaRecord>& items);

    /**
     *  构造 cmd_save_group_qa 应答：{ code, msg, groupid, id }，id 为保存后的问答ID（失败时为 0）。
     */
    static std::string buildSaveResponse(int code, const std::string& msg, int64_t groupid, int64_t id);

    /**
     *  构造 cmd_group_qa_changed_notify 推送：{ groupid }。
     */
    static std::string buildChangedNotify(int64_t groupid);

private:
    GroupQaProtocol() = delete;
    ~GroupQaProtocol() = delete;
    GroupQaProtocol(const GroupQaProtocol&) = delete;
    GroupQaProtocol& operator=(const GroupQaProtocol&) = delete;
};
