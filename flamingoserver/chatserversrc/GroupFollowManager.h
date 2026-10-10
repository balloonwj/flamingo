/**
 *  群内关注名单存储管理器，GroupFollowManager.h
 *
 *  负责 t_group_follow 表的落库与查询。一行记录的含义是「f_userid 在群 f_groupid 里关注了
 *  成员 f_target_userid 的发言」。名单按「人 + 群」各存一份，只影响本人这一侧：群的提醒模式
 *  为「只提醒 @我和关注的人」（notify_mode_mention_follow）时，这些成员发言会提醒本人；
 *  其它模式下客户端只用它给会话摘要加「[关注] 」前缀（docs/消息提醒模式与消息盒子方案.md §12）。
 *
 *  写入语义是<u>整份覆盖</u>：调用方给出本人在某个群的完整名单，本类先删掉该群名下的全部行、
 *  再逐条插入，空列表即清空。删与插包在同一个事务里，不会出现「删完了只插进去一半」。
 *  本人须为群成员、名单上限、不能关注自己、只能关注本群成员这几项校验由 checkTargets 完成，
 *  会话层（ChatSession::onSetGroupFollow）先调它、通过后再调 setFollows。
 *
 *  本人退群、被移出、群解散时，名单由 GroupManager::removeMembers / dismissGroup 在各自的
 *  事务里一并删除，本类不提供单独的清理方法。
 *
 *  每次操作单独建立一条数据库连接（与 ContactTagManager 同一风格），不在内存缓存。
 *
 *  典型用法：
 *      ChatSession::onSetGroupFollow 校验后调 setFollows，再调 getFollows 读回推 1331；
 *      ChatSession::onSyncConversations 调 getFollowsAll 一次取全，随 1501 下发。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "GroupProtocol.h"   //GroupFollowList

//群内关注名单存储管理器；全局单例，经 Singleton<GroupFollowManager> 访问。
//本类无需 init()：不持有任何启动期加载的状态，每次调用现开连接现查。
class GroupFollowManager final
{
public:
    GroupFollowManager();
    ~GroupFollowManager();

    GroupFollowManager(const GroupFollowManager& rhs) = delete;
    GroupFollowManager& operator=(const GroupFollowManager& rhs) = delete;

    /**
     *  校验一份待写入的名单（cmd_set_group_follow 的业务规则，docs/消息提醒模式与消息盒子方案.md §12.4）。
     *  依次判断：本人是该群成员；名单不超过 kGroupFollowMax 人；名单里没有本人；名单里每个人都是该群成员。
     *  @param userid  本人 userid
     *  @param groupid 群ID
     *  @param targets 待写入的名单（已去重）
     *  @return error_code_ok 表示可以写入；本人不是群成员返回 error_code_not_group_member；
     *          其余不合规的情况返回 error_code_bad_request
     */
    int checkTargets(int32_t userid, int64_t groupid, const std::vector<int32_t>& targets);

    /**
     *  取本人在全部群里的关注名单（登录时随 cmd_sync_conversations 下发）。
     *  @param userid 本人 userid
     *  @param out    [出参] 每个有名单的群一项，按群ID升序；名单内按成员 userid 升序。
     *                进函数先清空；没有任何名单时为空
     *  @return 数据库连接失败返回 false；查询结果为空照常返回 true
     */
    bool getFollowsAll(int32_t userid, std::vector<GroupFollowList>& out);

    /**
     *  取本人在某个群里的关注名单。
     *  @param userid  本人 userid
     *  @param groupid 群ID
     *  @param out     [出参] 关注的成员 userid，升序；进函数先清空
     *  @return 数据库连接失败返回 false
     */
    bool getFollows(int32_t userid, int64_t groupid, std::vector<int32_t>& out);

    /**
     *  整份覆盖本人在某个群里的关注名单。
     *  @param userid  本人 userid，须为正
     *  @param groupid 群ID，须为正
     *  @param targets 完整名单（调用方已去重并校验过上限与成员身份）；空表示清空
     *  @param now     写入时间，Unix 毫秒，记入 f_create_time
     *  @return 全部写入成功返回 true；任何一步失败整份回滚并返回 false
     */
    bool setFollows(int32_t userid, int64_t groupid, const std::vector<int32_t>& targets, int64_t now);
};
