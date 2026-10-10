/**
 *  群互动卡片互动存储管理器，CardManager.h
 *
 *  负责 v2 群互动卡片互动表 t_card_interaction 的落库与查询。每次操作单独
 *  建立一条数据库连接（与 MessageManager / GroupManager 同一风格），不在
 *  内存缓存。卡片本体是一条消息（存 t_chatmsg），本类只管互动。
 *
 *  典型用法：main.cpp 启动时 Singleton<CardManager>::Instance().init(...)；
 *  会话层在 cmd_vote_poll / cmd_join_relay / cmd_update_todo 处理中调用。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "CardProtocol.h"

//群互动卡片互动存储管理器；全局单例，经 Singleton<CardManager> 访问
class CardManager final
{
public:
    CardManager();
    ~CardManager();

    CardManager(const CardManager& rhs) = delete;
    CardManager& operator=(const CardManager& rhs) = delete;

    /**
     *  初始化（互动不预加载到内存）。
     *  @return 恒返回 true
     */
    bool init();

    /**
     *  写入 / 更新一条卡片互动（投票、接龙、待办勾选共用）。
     *  按 (f_msgid,f_userid) 联合唯一键 UPSERT —— 每人每卡片至多一行，重复互动只覆盖互动数据，
     *  首次互动的时间保持不变，因此接龙修改本人那条后名次不变。
     *  @param msgid  卡片所在消息id
     *  @param userid 互动用户userid
     *  @param data   互动数据（投票为选项下标JSON数组 / 接龙为文本 / 待办为勾选标记）
     *  @param time   互动时间，Unix毫秒；仅在首次写入时落库，覆盖已有行时忽略
     *  @return 数据库访问成功返回 true
     */
    bool upsertInteraction(int64_t msgid, int32_t userid,
                           const std::string& data, int64_t time);

    /**
     *  删除某用户对某卡片的互动（待办取消勾选用）。记录不存在也视为成功。
     *  @param msgid  卡片所在消息id
     *  @param userid 互动用户userid
     *  @return 数据库访问成功返回 true
     */
    bool removeInteraction(int64_t msgid, int32_t userid);

    /**
     *  取某用户对某卡片的那一条互动。撤回本人那条接龙前据此判断本人是否参与过，
     *  参与接龙时据此区分"首次参与"与"修改"。
     *  @param msgid  卡片所在消息id
     *  @param userid 互动用户userid
     *  @param out    [出参] 找到时写入该条互动记录；没找到时不改动
     *  @param found  [出参] 找到为 true，没有这条互动为 false
     *  @return 数据库访问成功返回 true（无论是否找到）；访问失败返回 false，此时 found 为 false
     */
    bool getInteraction(int64_t msgid, int32_t userid, CardInteractionRecord& out, bool& found);

    /**
     *  把一张卡片标记为已提前结束（写入 t_card_state）。用 INSERT IGNORE 按主键 f_msgid
     *  去重：两个人几乎同时结束同一张卡片时，只有先写入的那一次生效，后到的不覆盖操作者与时间。
     *  @param msgid    卡片所在消息id
     *  @param closer   结束操作者userid
     *  @param time     结束时间，Unix毫秒
     *  @param inserted [出参] 本次确实写入了结束记录为 true；卡片此前已被结束为 false
     *  @return 数据库访问成功返回 true；失败返回 false，此时 inserted 为 false
     */
    bool closeCard(int64_t msgid, int32_t closer, int64_t time, bool& inserted);

    /**
     *  取一张卡片的结束记录。
     *  @param msgid 卡片所在消息id
     *  @param out   [出参] 找到时写入结束记录（closed 为 true）；没找到时不改动
     *  @param found [出参] 卡片已被提前结束为 true，没有结束记录为 false
     *  @return 数据库访问成功返回 true（无论是否找到）；失败返回 false，此时 found 为 false
     */
    bool getCardState(int64_t msgid, CardStateRecord& out, bool& found);

    /**
     *  取某卡片的全部互动记录，按首次互动时间升序；时间相同时按写入先后（自增 f_id）排列，
     *  保证同一毫秒内参与的两人每次读回的先后一致。
     *  @param msgid 卡片所在消息id
     *  @param out   [出参] 互动记录列表
     *  @return 数据库访问成功返回 true
     */
    bool getCardInteractions(int64_t msgid, std::vector<CardInteractionRecord>& out);

private:
};
