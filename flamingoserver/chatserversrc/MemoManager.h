/**
 *  备忘存储管理器，MemoManager.h
 *
 *  负责「备忘与提醒」的两张表 t_memo（备忘本体）与 t_memo_target（群备忘指定提醒的成员）的
 *  读写。每次操作单独建立一条数据库连接（与 CardManager / GroupManager 同一风格），不在内存缓存。
 *  本类只管存储，权限与可见范围的判断在 MemoProtocol 与 ChatSession。
 *
 *  典型用法：main.cpp 启动时 Singleton<MemoManager>::Instance().init()；
 *  会话层在 cmd_memo_list / cmd_memo_save / cmd_memo_delete 的处理以及退群、踢人、解散群时调用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "MemoProtocol.h"

//备忘存储管理器；全局单例，经 Singleton<MemoManager> 访问
class MemoManager final
{
public:
    MemoManager();
    ~MemoManager();

    MemoManager(const MemoManager& rhs) = delete;
    MemoManager& operator=(const MemoManager& rhs) = delete;

    /**
     *  初始化（备忘不预加载到内存）。
     *  @return 恒返回 true
     */
    bool init();

    /**
     *  新建一条备忘，连同它的提醒对象在一个事务里写入。
     *  @param memo [入参/出参] 待写入的备忘，须已经过 MemoProtocol::normalizeMemo；
     *              成功后 memoid 被填成数据库分配的 ID
     *  @return 全部写入并提交时返回 true；失败时库里不留任何一行
     */
    bool insertMemo(MemoRecord& memo);

    /**
     *  修改一条备忘：覆盖除 f_ctype / f_owner / f_creator / f_create_time 以外的各列，
     *  并整体替换它的提醒对象，在一个事务里完成。
     *  @param memo 修改后的备忘，memoid 须为已存在的那一条
     *  @return 全部写入并提交时返回 true；失败时库里的数据保持原样
     */
    bool updateMemo(const MemoRecord& memo);

    /**
     *  删除一条备忘及其提醒对象（真删）。备忘本就不存在也视为成功。
     *  @param memoid 备忘 ID
     *  @return 数据库访问成功并提交时返回 true
     */
    bool deleteMemo(int64_t memoid);

    /**
     *  按 ID 取一条备忘，含提醒对象。
     *  @param memoid 备忘 ID
     *  @param out    [出参] 找到时写入该备忘；没找到时不改动
     *  @param found  [出参] 找到为 true
     *  @return 数据库访问成功返回 true（无论是否找到）；失败返回 false，此时 found 为 false
     */
    bool getMemo(int64_t memoid, MemoRecord& out, bool& found);

    /**
     *  数某个所属下的备忘条数，新建前判断是否已达上限用。
     *  @param ctype    所属：1 个人、2 群
     *  @param owner    个人备忘为本人 userid，群备忘为群 id
     *  @param outCount [出参] 条数
     *  @return 数据库访问成功返回 true
     */
    bool countMemos(int ctype, int64_t owner, int& outCount);

    /**
     *  取某人的全部个人备忘，按开始时间升序。
     *  @param userid 用户 userid
     *  @param out    [出参] 先清空再填入
     *  @return 数据库访问成功返回 true
     */
    bool listPersonalMemos(int64_t userid, std::vector<MemoRecord>& out);

    /**
     *  取某人所在各群的全部群备忘（不按可见范围过滤），同时给出他在每条备忘所属群里的角色，
     *  由调用方用 MemoProtocol::isVisibleTo 过滤。按群 id、开始时间升序。
     *  @param userid   用户 userid
     *  @param groupid  只取这个群时传群 id；传 0 取他所在的全部群
     *  @param out      [出参] 先清空再填入，含提醒对象
     *  @param outRoles [出参] 先清空再填入，与 out 一一对应，取值见 group_member_role
     *  @return 数据库访问成功返回 true
     */
    bool listGroupMemosOfMember(int64_t userid, int64_t groupid,
                                std::vector<MemoRecord>& out, std::vector<int>& outRoles);

    /**
     *  取某群的全部群备忘，含提醒对象，按开始时间升序。退群、改角色、解散群时据此推送。
     *  @param groupid 群 id
     *  @param out     [出参] 先清空再填入
     *  @return 数据库访问成功返回 true
     */
    bool listGroupMemos(int64_t groupid, std::vector<MemoRecord>& out);

    /**
     *  把一批成员从某群各条群备忘的提醒对象里移除（退群、被踢时调用）。
     *  @param groupid 群 id
     *  @param userids 被移除的成员；为空时直接返回 true
     *  @return 数据库访问成功返回 true
     */
    bool removeTargetsInGroup(int64_t groupid, const std::vector<int64_t>& userids);

    /**
     *  删除某群的全部群备忘及其提醒对象（解散群时调用），在一个事务里完成。
     *  @param groupid 群 id
     *  @return 全部删除并提交时返回 true
     */
    bool deleteGroupMemos(int64_t groupid);
};
