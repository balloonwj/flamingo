/**
 *  FavoritesModel.h
 *
 *  「我的收藏」的<u>纯数据模型</u>：持有当前用户的收藏集合，回答"这个对象收藏了没有"，
 *  并把收藏集合与联系人资料池 / 群列表合成"收藏分区该显示哪些行"。
 *
 *  之所以单独成模块：收藏这件事同时被四处用到 —— 联系人右键菜单、群右键菜单、
 *  会话右键菜单（三处都要按当前状态把文案取反成"添加到收藏"或"取消收藏"），以及
 *  「联系人」Tab 树顶的收藏分区（要把 (ctype,cid) 还原成一行行可显示的内容）。
 *  散在 CBalloonMainPanel 里各判一次必然会漏，且没法脱离窗口断言。
 *
 *  本模块不碰窗口、不碰网络，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个
 *  工程逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  收藏列表的来源是 cmd_get_contacts 应答里的 favorites 数组（见
 *  net/ContactsProtocolClient.h 的 FavoriteItem）；改动经 cmd_set_favorite 提交，
 *  见 net/FavoriteProtocolClient.h。本模块只管内存里的这份状态。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <atlstr.h>       // CString
#include <stdint.h>
#include <vector>

#include "../../net/ContactsProtocolClient.h"   // FavoriteItem / ContactItem
#include "../../net/GroupProtocolClient.h"      // UserGroupItem

namespace balloonmain {

/**
 *  收藏分区里的一行 —— 已经把 (ctype, cid) 还原成可直接喂给 DuiTreeView 的内容。
 *  联系人行与群行的区别都体现在字段取值上，绘制层不必再判类型。
 */
struct FavoriteRow
{
    int         m_ctype;          // 1 联系人、2 群组（取值见 Protocol.h 的 conversation_type）
    int64_t     m_cid;            // 联系人为对端userid，群组为群id；恒 > 0
    CString     m_displayName;    // 第一行文字：联系人取昵称，群取群名；恒非空
    CString     m_subLabel;       // 第二行文字：联系人取个性签名，群取"N 人"；可为空串（此时不画第二行）
    bool        m_showStatusDot;  // 是否画在线状态点：联系人 true，群 false
    int         m_presence;       // 在线状态（取值见 presence_type）；m_showStatusDot 为 false 时无意义
    int         m_facetype;       // 头像类型（见 AvatarSpec.h AvatarKind）
    CString     m_customface;     // 头像标识；空表示按 m_displayName 走字形头像

    FavoriteRow()
        : m_ctype(0)
        , m_cid(0)
        , m_showStatusDot(false)
        , m_presence(0)
        , m_facetype(0)
    {
    }
};

/**
 *  FavoritesModel —— 「我的收藏」内存模型。
 *
 *  用法：通讯录到达时 Reset 灌入；右键菜单据 IsFavorite 决定文案；用户点击后先
 *  SetFavorite 做乐观更新（记住它的返回值），再发 cmd_set_favorite，应答失败时
 *  用记住的返回值再 SetFavorite 一次即完成回滚。
 */
class FavoritesModel
{
public:
    FavoritesModel();

    /**
     *  用服务端下发的收藏列表整体替换当前状态（通讯录到达时调用）。
     *    items： cmd_get_contacts 应答里的 favorites 数组，已按收藏时间倒序。
     *            非法项（ctype 不是 1/2、cid <= 0）会被忽略。
     *  本方法会先清空原有状态。
     */
    void Reset(const std::vector<net::FavoriteItem>& items);

    /**
     *  判断某个对象是否已被收藏。
     *    ctype： 1 联系人、2 群组。
     *    cid：   对象ID。
     *  @return true 表示已收藏。ctype / cid 非法时恒返回 false。
     */
    bool IsFavorite(int ctype, int64_t cid) const;

    /**
     *  设置某个对象的收藏状态（乐观更新用）。
     *    ctype：    1 联系人、2 群组；非法值本方法不做任何事。
     *    cid：      对象ID；<= 0 时本方法不做任何事。
     *    favorite： true 加入收藏、false 移出收藏。
     *
     *  @return 调用<u>之前</u>该对象的收藏状态。失败回滚时把这个返回值再传回来即可
     *          （`model.SetFavorite(ctype, cid, prev)`），不必自己记录方向。
     *
     *  新加入的收藏排在<u>最前面</u>，与服务端"按收藏时间倒序"的口径一致，这样
     *  乐观更新后界面上的位置和下次拉通讯录后的位置是同一个，不会跳。
     *  重复加入同一对象不会产生第二条，也不会改变它原来的位置。
     */
    bool SetFavorite(int ctype, int64_t cid, bool favorite);

    // 当前收藏条数。
    int Count() const;

    /**
     *  当前收藏里是否含<u>群</u>。
     *  @return true 表示至少有一条 ctype==2 的收藏。
     *
     *  用途：群列表（cmd_get_user_groups）与通讯录（cmd_get_contacts）是两条各自异步
     *  到达的应答，实测群列表<u>晚于</u>通讯录约十几毫秒。收藏分区在通讯录到达时构建，
     *  那时群列表还是空的，群收藏会被 BuildRows 当成"查不到资料"跳过。调用方据本方法
     *  决定群列表到达后要不要把联系人树重建一次。
     */
    bool HasGroupFavorite() const;

    /**
     *  导出当前收藏集合（按当前顺序）。
     *    out： [出参] 导出的收藏项；本方法会先清空它。
     *  用途：重建联系人树要复用缓存的通讯录应答，而那份应答里的 favorites 是服务端
     *  当初下发的旧值。重建前先用本方法把模型的现状写回缓存，重入时的 Reset 才不会
     *  把乐观更新覆盖掉。
     */
    void ExportItems(std::vector<net::FavoriteItem>& out) const;

    // 当前收藏集合是否为空。为空时调用方应<u>整个隐藏</u>收藏分区的根节点，
    // 而不是显示一行"暂无收藏" —— 下面紧跟着部门树，空节点很突兀。
    bool IsEmpty() const;

    /**
     *  取右键菜单里那一项该显示的文案。
     *    isFavorite： 该对象当前是否已收藏（一般由 IsFavorite 得到）。
     *  @return 已收藏返回 L"取消收藏"，未收藏返回 L"添加到收藏"。
     *          静态常量串，进程级生命周期，调用方可直接持有。
     */
    static const wchar_t* GetMenuLabel(bool isFavorite);

    /**
     *  把收藏集合合成可显示的行集合。
     *
     *    contacts： 联系人资料池（cmd_get_contacts 应答的 contacts 数组）。
     *    groups：   我加入的群列表（cmd_get_user_groups 应答的 groups 数组）。
     *    out：      [出参] 行集合，按收藏顺序（时间倒序）排列。本方法会先清空它。
     *
     *  <u>查不到资料的收藏项会被跳过</u>：好友刚被删、群刚被解散时，本地缓存还没
     *  更新，此时宁可少显示一行，也不要画出一条"用户12345"这种点不开的死行。
     *  （服务端那边有 removeFavoriteForAll 做清理，但两边之间有时间差。）
     */
    void BuildRows(const std::vector<net::ContactItem>& contacts,
                   const std::vector<net::UserGroupItem>& groups,
                   std::vector<FavoriteRow>& out) const;

private:
    // 收藏集合，按收藏时间倒序（最近收藏的在 [0]）。条数上限由服务端把关
    // （kMaxFavoriteCount=200），本端不再设限，避免两处上限不一致。
    std::vector<net::FavoriteItem> m_items;

    // 在 m_items 里找某个对象的下标；找不到返回 -1。
    int FindIndex(int ctype, int64_t cid) const;
};

} // namespace balloonmain
