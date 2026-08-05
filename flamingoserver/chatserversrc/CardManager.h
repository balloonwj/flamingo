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
     *  按 (f_msgid,f_userid) 联合唯一键 UPSERT —— 每人每卡片至多一行，重复互动即覆盖。
     *  @param msgid  卡片所在消息id
     *  @param userid 互动用户userid
     *  @param data   互动数据（投票为选项下标JSON数组 / 接龙为文本 / 待办为勾选标记）
     *  @param time   互动时间，Unix毫秒
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
     *  取某卡片的全部互动记录，按互动时间升序。
     *  @param msgid 卡片所在消息id
     *  @param out   [出参] 互动记录列表
     *  @return 数据库访问成功返回 true
     */
    bool getCardInteractions(int64_t msgid, std::vector<CardInteractionRecord>& out);

private:
};
