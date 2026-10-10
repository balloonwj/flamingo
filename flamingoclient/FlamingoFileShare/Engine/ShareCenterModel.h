/**
 *  「文件共享」窗口的纯逻辑，ShareCenterModel.h
 *
 *  docs/文件共享方案.md 7.4「共享窗口」有两个页签：
 *    - 「我的共享」每行显示名称、显示路径、所在会话、成员数、状态，位于另一台电脑或本机缺少记录的加标注；
 *      操作有在本机打开、调整成员、取消共享、定位到会话；
 *    - 「共享给我的」每行显示名称、分享者、所在会话、我的权限、状态；操作有打开、定位到会话。
 *  选中一行时右侧显示成员与权限、正在访问的人与正在传输的内容、最近操作记录。本文件给出这些文字与每行可做的操作，
 *  不碰窗口，可被 FlamingoUnit 直接测试（标签 [sharecenter]）。
 *
 *  典型用法：
 *      std::wstring sub = ShareCenterModel::MineSubtitle(entry, conversationName, service.GetLocalStatus(entry.m_shareId));
 *      int actions = ShareCenterModel::MineActions(entry, localStatus);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ShareProtocolCodec.h"   //ShareListEntry
#include "ShareLocalStore.h"      //ShareActivityRecord
#include "ShareService.h"         //ShareActiveVisitor

namespace fileshare {

//「文件共享」窗口里一行可做的操作，按位组合
enum share_center_action
{
    share_center_action_open_local = 1,    //在本机打开共享的文件夹（我的共享，且本机有它的记录）
    share_center_action_edit       = 2,    //调整成员（我的共享）
    share_center_action_cancel     = 4,    //取消共享（我的共享）
    share_center_action_open       = 8,    //打开浏览窗口（共享给我的）
    share_center_action_locate     = 16,   //定位到共享所在的会话
};

/**
 *  ShareCenterModel —— 「文件共享」窗口的纯逻辑；全部为静态方法，不可实例化。
 */
class ShareCenterModel
{
public:
    //共享类型的文字：磁盘、文件夹、文件
    static std::wstring KindText(int kind);

    /**
     *  最近操作里动作的文字：下载、上传、新建文件夹、重命名、删除、移动。
     *  @param action 动作，share_audit_action；不认识的按「下载」
     *  @return 文字
     */
    static std::wstring ActionVerb(int action);

    /**
     *  权限的文字（2026-10-03 起带上成员默认就有的下载权限），如「下载、上传、重命名」；没有写权限时为「浏览、下载」，
     *  单文件共享为「只能下载」。
     *  @param perms 写权限位
     *  @param kind  共享类型
     */
    static std::wstring PermsText(int perms, int kind);

    /**
     *  本机提供状态的标注（我的共享）：位于另一台电脑、本机缺少记录；其余为空串。
     *  @param localStatus CShareService::GetLocalStatus 的结果（share_local_status，0 表示还不知道）
     */
    static std::wstring LocalStatusText(int localStatus);

    /**
     *  「我的共享」一行的第二行：显示路径 · 所在会话 · N 名成员 · 状态（位于另一台电脑等标注加在最后）。
     *  @param e            共享
     *  @param conversation 所在会话的名字（群名或对方的名字）；为空时省略这一段
     *  @param localStatus  本机提供状态
     */
    static std::wstring MineSubtitle(const ShareListEntry& e, const std::wstring& conversation, int localStatus);

    /**
     *  「共享给我的」一行的第二行：某某的共享 · 所在会话 · 我的权限：… · 状态。
     *  @param e            共享
     *  @param owner        分享者的显示名
     *  @param conversation 所在会话的名字；为空时省略这一段
     */
    static std::wstring SharedSubtitle(const ShareListEntry& e, const std::wstring& owner, const std::wstring& conversation);

    /**
     *  「我的共享」一行可做的操作：已失效的只能定位到会话；本机有记录（上线比对报告过）才能在本机打开。
     *  @param e           共享
     *  @param localStatus 本机提供状态
     */
    static int MineActions(const ShareListEntry& e, int localStatus);

    //「共享给我的」一行可做的操作：未失效的可以打开；都可以定位到会话
    static int SharedActions(const ShareListEntry& e);

    /**
     *  共享所在会话在本人视角下的会话 id。2205 应答里的 cid 是分享者视角：单聊共享里那是对方，也就是本人自己，
     *  本人要定位的会话是与分享者的单聊；群聊两种视角相同。
     *  @param e      共享
     *  @param selfId 本人 userid
     *  @return 本人视角的会话 id
     */
    static int64_t ViewerCid(const ShareListEntry& e, int64_t selfId);

    /**
     *  「正在访问的人」的一行，如「张三 正在浏览」「张三 正在下载 1 项、上传 1 项（35%）」。
     *  @param v    访问者及其传输
     *  @param name 访问者的显示名（为空时用 v 里服务端给的名字）
     */
    static std::wstring VisitorText(const ShareActiveVisitor& v, const std::wstring& name);

    /**
     *  访问者正在做什么（2026-10-03 起，详情里访问者行的第二行），如「正在浏览」「正在下载 1 项、上传 1 项（35%）」。
     *  VisitorText 就是显示名加一个空格再加它。
     *  @param v 访问者及其传输
     */
    static std::wstring VisitorStatusText(const ShareActiveVisitor& v);

    /**
     *  详情里成员权限的小胶囊（2026-10-03 美化起）：第一个是成员默认就有的「下载」，后面按界面列的顺序逐项列出写权限
     *  （上传、新建文件夹…），没有写权限时只有「下载」；单个文件的共享一个「只能下载」。
     *  @param perms 写权限位（share_perm 的组合）
     *  @param kind  共享类型（share_kind）
     *  @return 各胶囊的文字，至少一个
     */
    static std::vector<std::wstring> PermChipTexts(int perms, int kind);

    /**
     *  列表行右端状态胶囊的文字（2026-10-03 美化起）：可用、暂不可用、已失效；原因不写进胶囊，放在详情里。
     *  @param state 对本人的状态（share_state）
     *  @return 胶囊文字；不认识的状态返回空串（不画胶囊）
     */
    static std::wstring StateChipText(int state);

    /**
     *  「我的共享」列表行的第二行（2026-10-03 美化起）：所在会话 · N 名成员；会话名为空时只有成员数。路径与状态不再放在这一行
     *  （路径在详情里，状态是行右端的胶囊）。
     *  @param e            共享
     *  @param conversation 所在会话的名字，可为空
     */
    static std::wstring MineRowSubtitle(const ShareListEntry& e, const std::wstring& conversation);

    /**
     *  「共享给我的」列表行的第二行（2026-10-03 美化起）：分享者 · 我的权限；分享者名为空时只有权限。
     *  @param e     共享
     *  @param owner 分享者的显示名，可为空
     */
    static std::wstring SharedRowSubtitle(const ShareListEntry& e, const std::wstring& owner);

    /**
     *  搜索框的过滤（2026-10-03 起）：共享名称、显示路径、所在会话名、分享者名中任何一项包含关键字即算匹配，不区分大小写；
     *  关键字为空（或只有空白）时都匹配。
     *  @param e            共享
     *  @param conversation 所在会话的名字，可为空
     *  @param owner        分享者的显示名，可为空
     *  @param keyword      搜索框里的文字
     *  @return 匹配返回 true
     */
    static bool MatchesFilter(const ShareListEntry& e, const std::wstring& conversation, const std::wstring& owner,
                              const std::wstring& keyword);

    /**
     *  最近操作记录的一行，如「10-03 14:22  张三 下载 docs · 成功」。
     *  @param rec      记录
     *  @param name     访问者的显示名
     *  @param timeText 时间文字（调用方按本机时区格式化，测试传固定值）
     */
    static std::wstring ActivityText(const ShareActivityRecord& rec, const std::wstring& name, const std::wstring& timeText);

private:
    ShareCenterModel() = delete;
};

} // namespace fileshare
