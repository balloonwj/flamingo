/**
 *  FavoritesModel.cpp
 *
 *  「我的收藏」内存模型的实现。全部是内存运算，不碰窗口、不碰网络，故可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文案）。两个工程均已开启 /utf-8 编译选项，源码
 *  按 UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "FavoritesModel.h"

#include <map>

#include "../net/Protocol.h"   // conversation_single / conversation_group

namespace balloonmain {

namespace {

//群行第二行显示成员数时的文案格式："12 人"。人数未知（<= 0）时不显示第二行，
//宁可留空也不显示骗人的 "0 人"（与 GroupRowText 的口径一致）。
const wchar_t* const kGroupMemberFormat = L"%d 人";

//UTF-8 → CString。空串安全。
//
//<u>必须走 CP_UTF8 显式转换</u>，不能直接 `CString(utf8.c_str())`：那个构造函数按
//系统 ANSI 代码页（本地是 936/GBK）解释窄字节，UTF-8 的中文会变成乱码。ASCII 内容
//看不出区别，所以这个坑很容易漏到运行期才发现（本模块的单测 FAVM6 就是为此而设）。
//
//注：仓库里已有约 10 份同名同实现的本地副本（MemberPickerTreeModel.cpp、
//BalloonMainPanel.cpp 等），本处沿用同一写法保持一致；统一抽成共用模块留作后续清理。
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.GetBuffer(wlen), wlen);
    out.ReleaseBuffer();
    return out;
}

//判断一个 (ctype, cid) 是否是合法的收藏对象。收藏只认联系人与群两种；
//conversation_service(3) 不在其列 —— 服务号作为联系人被收藏时走 ctype=1。
bool IsValidTarget(int ctype, int64_t cid)
{
    if (ctype != conversation_single && ctype != conversation_group)
        return false;
    if (cid <= 0)
        return false;
    return true;
}

} // anonymous namespace

FavoritesModel::FavoritesModel()
{
}

void FavoritesModel::Reset(const std::vector<net::FavoriteItem>& items)
{
    m_items.clear();

    for (size_t i = 0; i < items.size(); ++i)
    {
        const net::FavoriteItem& it = items[i];
        if (!IsValidTarget(it.m_ctype, it.m_cid))
            continue;

        //去重：服务端主键已经保证不会有重复行，这里再挡一道是防着解析层
        //或将来别的注入路径出岔子，重复项会让界面上冒出两行一模一样的。
        if (FindIndex(it.m_ctype, it.m_cid) >= 0)
            continue;

        m_items.push_back(it);
    }
}

int FavoritesModel::FindIndex(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_ctype == ctype && m_items[i].m_cid == cid)
            return static_cast<int>(i);
    }
    return -1;
}

bool FavoritesModel::IsFavorite(int ctype, int64_t cid) const
{
    if (!IsValidTarget(ctype, cid))
        return false;
    return FindIndex(ctype, cid) >= 0;
}

bool FavoritesModel::SetFavorite(int ctype, int64_t cid, bool favorite)
{
    if (!IsValidTarget(ctype, cid))
        return false;

    const int index = FindIndex(ctype, cid);
    const bool wasFavorite = (index >= 0);

    if (favorite)
    {
        //已经收藏过就什么都不做 —— 特别是不要把它挪到队首。用户对着同一个人
        //连点两下"添加到收藏"，位置不该跟着跳。
        if (!wasFavorite)
        {
            net::FavoriteItem item;
            item.m_ctype = ctype;
            item.m_cid   = cid;
            //插到最前：与服务端"按收藏时间倒序"一致，乐观更新后的位置和下次
            //拉通讯录后的位置是同一个。
            m_items.insert(m_items.begin(), item);
        }
    }
    else
    {
        if (wasFavorite)
        {
            m_items.erase(m_items.begin() + index);
        }
    }

    return wasFavorite;
}

int FavoritesModel::Count() const
{
    return static_cast<int>(m_items.size());
}

bool FavoritesModel::IsEmpty() const
{
    return m_items.empty();
}

bool FavoritesModel::HasGroupFavorite() const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_ctype == conversation_group)
            return true;
    }
    return false;
}

void FavoritesModel::ExportItems(std::vector<net::FavoriteItem>& out) const
{
    out = m_items;
}

const wchar_t* FavoritesModel::GetMenuLabel(bool isFavorite)
{
    if (isFavorite)
        return L"取消收藏";
    return L"添加到收藏";
}

void FavoritesModel::BuildRows(const std::vector<net::ContactItem>& contacts,
                               const std::vector<net::UserGroupItem>& groups,
                               std::vector<FavoriteRow>& out) const
{
    out.clear();

    //先建两张索引，避免对每条收藏都把资料池整个扫一遍。
    std::map<int64_t, const net::ContactItem*> contactById;
    for (size_t i = 0; i < contacts.size(); ++i)
    {
        contactById[contacts[i].m_userid] = &contacts[i];
    }

    std::map<int64_t, const net::UserGroupItem*> groupById;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        groupById[groups[i].m_groupid] = &groups[i];
    }

    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const net::FavoriteItem& fav = m_items[i];

        if (fav.m_ctype == conversation_single)
        {
            std::map<int64_t, const net::ContactItem*>::const_iterator it =
                contactById.find(fav.m_cid);
            //查不到资料就跳过这一行。好友刚被删、账号刚被停用时会走到这里，
            //宁可少一行，也不要画出"用户12345"这种点不开的死行。
            if (it == contactById.end())
                continue;

            const net::ContactItem* c = it->second;

            FavoriteRow row;
            row.m_ctype         = fav.m_ctype;
            row.m_cid           = fav.m_cid;
            //昵称缺失时退回账号，两者都空就跳过 —— 一行没有名字是没法用的。
            row.m_displayName   = Utf8ToCStr(c->m_nickname);
            if (row.m_displayName.IsEmpty())
                row.m_displayName = Utf8ToCStr(c->m_username);
            if (row.m_displayName.IsEmpty())
                continue;

            row.m_subLabel      = Utf8ToCStr(c->m_signature);
            row.m_showStatusDot = true;
            row.m_presence      = c->m_presence;
            row.m_facetype      = c->m_facetype;
            row.m_customface    = Utf8ToCStr(c->m_customface);
            out.push_back(row);
        }
        else
        {
            std::map<int64_t, const net::UserGroupItem*>::const_iterator it =
                groupById.find(fav.m_cid);
            //群已解散或自己已退群，本地群列表里就没有它了，同样跳过。
            if (it == groupById.end())
                continue;

            const net::UserGroupItem* g = it->second;

            FavoriteRow row;
            row.m_ctype       = fav.m_ctype;
            row.m_cid         = fav.m_cid;
            row.m_displayName = Utf8ToCStr(g->m_name);
            if (row.m_displayName.IsEmpty())
                continue;

            //群不画在线状态点 —— 群没有"在线"这个概念，画一个灰点只会让人以为它离线了。
            row.m_showStatusDot = false;
            row.m_presence      = 0;

            if (g->m_membercount > 0)
            {
                row.m_subLabel.Format(kGroupMemberFormat, g->m_membercount);
            }

            row.m_facetype   = g->m_facetype;
            row.m_customface = Utf8ToCStr(g->m_customface);
            out.push_back(row);
        }
    }
}

} // namespace balloonmain
