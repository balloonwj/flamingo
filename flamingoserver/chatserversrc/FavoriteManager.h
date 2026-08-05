/**
 *  「我的收藏」存储管理器，FavoriteManager.h
 *
 *  负责 t_favorite 表的落库与查询。收藏对象既可以是联系人也可以是群，两者用
 *  (ctype, cid) 一对值区分，取值见 Protocol.h 的 enum conversation_type。
 *  每次操作单独建立一条数据库连接（与 GroupManager / MessageManager 同一风格），
 *  不在内存做缓存 —— 收藏是低频写、按用户一次性读，缓存不划算还要处理失效。
 *
 *  典型用法：
 *      会话层 ChatSession::onSetFavorite 收到 cmd_set_favorite 后调
 *      Singleton<FavoriteManager>::Instance().setFavorite(...)；
 *      ChatSession::onGetContacts 组装通讯录时调 getFavorites 填 ContactsData::favorites。
 *
 *  注意与 1901~1903 的"收藏消息"是两个功能，本类只管收藏联系人与群。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "FavoriteProtocol.h"   //FavoriteItem / kMaxFavoriteCount

//「我的收藏」存储管理器；全局单例，经 Singleton<FavoriteManager> 访问
class FavoriteManager final
{
public:
    FavoriteManager();
    ~FavoriteManager();

    FavoriteManager(const FavoriteManager& rhs) = delete;
    FavoriteManager& operator=(const FavoriteManager& rhs) = delete;

    /**
     *  取某用户的全部收藏，按收藏时间<u>倒序</u>（最近收藏的排最前）。
     *
     *    userid： 收藏归属用户的userid。
     *    out：    出参，成功时被追加填充；本方法<u>不会</u>先清空它，调用方自行准备。
     *
     *  @return true 查询成功（收藏为空也算成功）；false 表示数据库不可用。
     *          调用方在 false 时应按"没有收藏"降级处理，不要因此让整个通讯录应答失败 ——
     *          收藏拉不到只是少一个分区，不该连部门树也看不成。
     */
    bool getFavorites(int64_t userid, std::vector<FavoriteItem>& out);

    /**
     *  添加或取消一条收藏。
     *
     *    userid：   收藏归属用户的userid。
     *    ctype：    收藏对象类型：1 联系人、2 群组（取值见 conversation_type）。
     *    cid：      收藏对象ID：联系人为对端userid，群组为群id。
     *    favorite： 1 加入收藏、0 移出收藏。
     *    now：      当前时刻，Unix 毫秒；仅在 favorite==1 时写入 f_create_time。
     *               由调用方传入而非本类自取，便于测试构造确定的时间序。
     *    outErrorCode： 出参，失败原因（取值见 Protocol.h 的 enum error_code）；
     *                   成功时置为 error_code_ok。
     *
     *  @return true 操作成功；false 时看 outErrorCode。
     *
     *  两个方向都是<u>幂等</u>的：重复收藏同一对象不会产生第二行、也不算失败；
     *  取消一条本来就不存在的收藏同样返回成功。这样客户端连点两下不会收到莫名的报错。
     *
     *  数量上限 kMaxFavoriteCount 在这里校验，超出时返回 error_code_favorite_limit。
     *  <u>取消收藏不受上限约束</u> —— 否则存量数据超限的用户会连"删掉几个"都做不到。
     *
     *  本方法<u>不校验</u>"对方是不是好友""自己在不在群里"，那属于会话层的业务判定。
     */
    bool setFavorite(int64_t userid, int ctype, int64_t cid, int favorite,
                     int64_t now, int& outErrorCode);

    /**
     *  统计某用户当前的收藏条数。供上限判定与日志使用。
     *
     *    userid： 收藏归属用户的userid。
     *
     *  @return 收藏条数；数据库不可用时返回 -1（调用方须与 0 区分开，
     *          不能把"查不到"当成"没有收藏"就放行写入）。
     */
    int countFavorites(int64_t userid);

    /**
     *  删除<u>所有人</u>对某个对象的收藏。
     *
     *    ctype： 对象类型：1 联系人、2 群组。
     *    cid：   对象ID。
     *
     *  @return true 执行成功（一条都没删到也算成功）；false 表示数据库不可用。
     *
     *  用在群被解散、账号被停用这类"这个对象不复存在"的场合，避免各人的收藏区
     *  长期挂着一条点不开的死行。删好友只影响双方，走 setFavorite 逐个取消即可，
     *  不要用本方法。
     */
    bool removeFavoriteForAll(int ctype, int64_t cid);
};
