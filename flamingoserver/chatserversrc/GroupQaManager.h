/**
 *  群问答存储管理器，GroupQaManager.h（docs/群问答方案.md）。
 *
 *  负责三张表：t_group_qa（本群问答）、t_enterprise_qa（企业问答）、t_enterprise_qa_group（企业问答应用到的群）。
 *  会话层的校验（角色、长度、敏感词）在 ChatSession / HttpSession，本类只管存取。「群属于哪个企业」按群主所属
 *  企业认定（t_group 没有企业列），由调用方算好传进来。
 *
 *  每次操作单独建立一条数据库连接（与 ContactTagManager 同一风格），不在内存缓存。
 *
 *  典型用法：
 *      GroupQaManager& mgr = Singleton<GroupQaManager>::Instance();
 *      std::vector<GroupQaRecord> groupItems, entItems;
 *      mgr.listGroupQa(groupid, groupItems);
 *      mgr.listEnterpriseQaForGroup(enterpriseId, groupid, entItems);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "GroupQaProtocol.h"   // GroupQaRecord / EnterpriseQaRecord

//群问答存储管理器；全局单例，经 Singleton<GroupQaManager> 访问。无需 init()。
class GroupQaManager final
{
public:
    GroupQaManager();
    ~GroupQaManager();

    GroupQaManager(const GroupQaManager& rhs) = delete;
    GroupQaManager& operator=(const GroupQaManager& rhs) = delete;

    // ---------------- 本群问答 ----------------

    /**
     *  取本群全部问答，按最后修改时间倒序（同一时间按 ID 倒序）。editorname 不在这里填。
     *  @return 数据库连接失败返回 false
     */
    bool listGroupQa(int64_t groupid, std::vector<GroupQaRecord>& out);

    /**
     *  本群问答的条数（新增前判断上限）。
     *  @return 数据库访问失败返回 -1
     */
    int countGroupQa(int64_t groupid);

    /**
     *  按 ID 取一条本群问答。
     *  @return 找到返回 true
     */
    bool getGroupQa(int64_t id, GroupQaRecord& out);

    /**
     *  新增一条本群问答。
     *  @param outId [出参] 新问答的 ID
     *  @return 写入成功返回 true
     */
    bool addGroupQa(int64_t groupid, const std::string& question, const std::string& answer,
                    int64_t editor, int64_t nowMs, int64_t& outId);

    /**
     *  修改一条本群问答（只改这个群的那一条）。
     *  @return 写入成功返回 true；调用方须先用 getGroupQa 确认它属于本群
     */
    bool updateGroupQa(int64_t groupid, int64_t id, const std::string& question, const std::string& answer,
                       int64_t editor, int64_t nowMs);

    /**
     *  删除一条本群问答（只删这个群的那一条）。
     *  @return 写入成功返回 true
     */
    bool deleteGroupQa(int64_t groupid, int64_t id);

    // ---------------- 企业问答 ----------------

    /**
     *  取应用到某个群的企业问答：本企业应用到全部群的，加上所选的群里有这个群的。按最后修改时间倒序。
     *  editorname 填管理员姓名。
     *  @param enterpriseId 群所属企业（群主所属企业）；0 时直接返回空
     */
    bool listEnterpriseQaForGroup(int64_t enterpriseId, int64_t groupid, std::vector<GroupQaRecord>& out);

    /**
     *  取本企业全部企业问答（管理接口用），每条带上应用到的群。按最后修改时间倒序。
     */
    bool listEnterpriseQa(int64_t enterpriseId, std::vector<EnterpriseQaRecord>& out);

    /**
     *  本企业企业问答的条数。
     *  @return 数据库访问失败返回 -1
     */
    int countEnterpriseQa(int64_t enterpriseId);

    /**
     *  按 ID 取一条企业问答（带应用到的群）。
     *  @return 找到返回 true
     */
    bool getEnterpriseQa(int64_t id, EnterpriseQaRecord& out);

    /**
     *  新增一条企业问答（含应用到的群），一个事务。
     *  @param rec   要写入的内容；id 与时间以外的字段都要填好
     *  @param outId [出参] 新问答的 ID
     */
    bool addEnterpriseQa(const EnterpriseQaRecord& rec, int64_t nowMs, int64_t& outId);

    /**
     *  修改一条企业问答（整份覆盖内容与应用到的群），一个事务。
     *  @param rec rec.id 与 rec.enterpriseid 指定是哪一条；调用方须先确认它属于该企业
     */
    bool updateEnterpriseQa(const EnterpriseQaRecord& rec, int64_t nowMs);

    /**
     *  删除一条企业问答（连同应用到的群），一个事务。
     */
    bool deleteEnterpriseQa(int64_t enterpriseId, int64_t id);

    /**
     *  列出全部群的 (群ID, 群名, 群主) —— 管理接口按群主所属企业筛出本企业的群。按群ID升序。
     */
    bool listAllGroups(std::vector<std::pair<int64_t, std::pair<std::string, int64_t> > >& out);
};
