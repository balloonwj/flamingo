/**
 *  「特别关注」存储管理器，StarredManager.h
 *
 *  负责 t_starred 表的落库与查询。特别关注的对象<u>只能是联系人</u>，故本类通篇
 *  只有 (userid, target) 一对 userid，不像收藏那样带 ctype。每次操作单独建立一条
 *  数据库连接（与 FavoriteManager / GroupManager 同一风格），不在内存做缓存 ——
 *  特别关注是低频写、按用户一次性读，缓存不划算还要处理失效。
 *
 *  典型用法：
 *      会话层 ChatSession::onSetStarred 收到 cmd_set_starred 后调
 *      Singleton<StarredManager>::Instance().setStarred(...)；
 *      ChatSession::onGetContacts 组装通讯录时调 getStarred 填 ContactsData::starred。
 *
 *  <u>本类与推送无关</u>：上下线仍按企业全员广播（ChatServer::broadcastToEnterprise），
 *  服务端不需要知道谁关注了谁；这份名单只是下发给客户端，由客户端决定弹不弹提示。
 *
 *  与「收藏」（FavoriteManager）互不相干：一个人可以只收藏不关注、也可以只关注
 *  不收藏，两张表各存各的。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "StarredProtocol.h"   //kMaxStarredCount

//「特别关注」存储管理器；全局单例，经 Singleton<StarredManager> 访问
class StarredManager final
{
public:
    StarredManager();
    ~StarredManager();

    StarredManager(const StarredManager& rhs) = delete;
    StarredManager& operator=(const StarredManager& rhs) = delete;

    /**
     *  取某用户特别关注的全部联系人 userid，按标记时间<u>倒序</u>（最近标记的排最前）。
     *
     *    userid： 名单归属用户的 userid。
     *    out：    出参，成功时被追加填充；本方法<u>不会</u>先清空它，调用方自行准备。
     *
     *  @return true 查询成功（一个都没关注也算成功）；false 表示数据库不可用。
     *          调用方在 false 时应按"没有关注任何人"降级处理，不要因此让整个通讯录
     *          应答失败 —— 名单拉不到只是少一层提醒，不该连部门树也看不成。
     */
    bool getStarred(int64_t userid, std::vector<int64_t>& out);

    /**
     *  标记或取消一个特别关注对象。
     *
     *    userid：       名单归属用户的 userid。
     *    target：       被关注的联系人 userid。
     *    starred：      1 标记、0 取消。
     *    now：          当前时刻，Unix 毫秒；仅在 starred==1 时写入 f_create_time。
     *                   由调用方传入而非本类自取，便于测试构造确定的时间序。
     *    outErrorCode： 出参，失败原因（取值见 Protocol.h 的 enum error_code）；
     *                   成功时置为 error_code_ok。
     *
     *  @return true 操作成功；false 时看 outErrorCode。
     *
     *  两个方向都是<u>幂等</u>的：重复标记同一个人不会产生第二行、也不算失败；
     *  取消一个本来就没标记的人同样返回成功。这样客户端连点两下不会收到莫名的报错。
     *
     *  人数上限 kMaxStarredCount 在这里校验，超出时返回 error_code_starred_limit。
     *  <u>取消不受上限约束</u> —— 否则存量超限的用户会连"取消几个"都做不到。
     *
     *  不能关注自己（target == userid）：返回 error_code_bad_request。自己的上下线
     *  自己最清楚，给自己弹提示没有意义。
     *
     *  本方法<u>不校验</u>"对方在不在同一企业"，那要查部门表，属于会话层的业务判定。
     */
    bool setStarred(int64_t userid, int64_t target, int starred,
                    int64_t now, int& outErrorCode);

    /**
     *  统计某用户当前关注了多少人。供上限判定与日志使用。
     *
     *    userid： 名单归属用户的 userid。
     *
     *  @return 关注人数；数据库不可用时返回 -1（调用方须与 0 区分开，
     *          不能把"查不到"当成"一个都没关注"就放行写入）。
     */
    int countStarred(int64_t userid);

    /**
     *  删除<u>所有人</u>对某个联系人的关注。
     *
     *    target： 被关注的联系人 userid。
     *
     *  @return true 执行成功（一条都没删到也算成功）；false 表示数据库不可用。
     *
     *  用在账号被停用、成员被移出企业这类"这个人不该再出现在别人名单里"的场合，
     *  免得各人的名单里长期挂着一个永远不会上线的人。
     */
    bool removeStarredForAll(int64_t target);
};
