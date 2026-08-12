/**
 *  StarredModel.h
 *
 *  「特别关注」的<u>纯数据模型</u>：持有当前账号关注的联系人集合，回答"这个人关注了
 *  没有"，供各处右键菜单把文案取反成「特别关注」或「取消特别关注」。
 *
 *  之所以单独成模块：关注状态同时被<u>四处</u>用到 —— 主面板联系人右键菜单、主面板
 *  最近会话右键菜单、群聊窗右侧成员列表右键菜单、群聊窗气泡里的 @片段与发送者头像
 *  右键菜单。散在 CBalloonMainPanel 里各判一次必然会漏，且没法脱离窗口断言。
 *
 *  本模块不碰窗口、不碰网络，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个
 *  工程逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  名单的来源是 cmd_get_contacts 应答里的 starred 数组（见
 *  net/ContactsProtocolClient.h 的 ContactsResult::m_starred）；改动经
 *  cmd_set_starred 提交，见 net/StarredProtocolClient.h。本模块只管内存里的这份状态。
 *
 *  与「我的收藏」（FavoritesModel）是两件事：收藏是常用入口的书签、可以是群；
 *  特别关注只能是人，用来在对方上下线时弹右下角提示。一个人可以只收藏不关注、
 *  也可以只关注不收藏。详见 docs/特别关注接入方案.md。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <stdint.h>
#include <set>
#include <vector>

namespace balloonmain {

/**
 *  StarredModel —— 「特别关注」内存模型。
 *
 *  一个账号一份，随主面板生命周期存在；登录后由通讯录应答灌入，用户在菜单里改动时
 *  <u>先发请求、服务端应答成功后再改本模型</u>（不是乐观更新），避免界面显示"已关注"
 *  而服务端其实没存上。
 */
class StarredModel
{
public:
    StarredModel();

    /**
     *  用通讯录应答里的名单整体替换当前状态。
     *
     *  每次拉通讯录（登录、重连）都调一次；本方法<u>先清空再灌入</u>，
     *  故不会残留上一个账号或上一次同步的数据。
     *    userids：被特别关注的联系人 userid 列表；元素 <= 0 的会被忽略。
     */
    void Reset(const std::vector<int64_t>& userids);

    /**
     *  某个联系人是否被特别关注。
     *    userid：联系人 userid。
     *  @return 已关注返回 true；userid <= 0 一律返回 false。
     */
    bool IsStarred(int64_t userid) const;

    /**
     *  就地标记 / 取消一个联系人（供服务端应答成功后同步本地状态）。
     *
     *  幂等：重复标记同一个人、或取消一个本来就没标记的人，都不会出错。
     *    userid： 联系人 userid；<= 0 时什么都不做。
     *    starred：true 标记、false 取消。
     */
    void SetStarred(int64_t userid, bool starred);

    /**
     *  当前关注了多少人。供上限提示与日志使用（真正的上限校验在服务端）。
     */
    int Count() const;

    /**
     *  把当前名单导出成扁平数组，覆盖写进 out。
     *
     *  用途只有一个：主面板缓存着上一次的通讯录应答，重建界面时会拿缓存再走一遍
     *  OnContactsArrived —— 那里会用缓存里的 starred 做 Reset。不把乐观更新后的
     *  现状写回缓存，刚点下去的标记就会被服务端当初下发的旧值覆盖掉。
     *    out：接收结果的数组，调用前的内容会被清空。
     */
    void ExportItems(std::vector<int64_t>& out) const;

    /**
     *  清空名单。退出登录时调，避免下一个账号看到上一个账号的名单。
     */
    void Clear();

    /**
     *  取右键菜单里那一项该显示的文案。
     *
     *  做成静态的、由本模块统一给出，是因为这句文案要出现在<u>四处</u>菜单里
     *  （主面板联系人 / 主面板会话 / 群成员列表 / 气泡的 @片段与头像），
     *  各写一份迟早措辞不一致。
     *    isStarred：该联系人当前是否已被特别关注（一般由 IsStarred 得到）。
     *  @return 已关注返回 L"取消特别关注"，未关注返回 L"特别关注"。
     *          静态常量串，进程级生命周期，调用方可直接持有。
     */
    static const wchar_t* GetMenuLabel(bool isStarred);

private:
    //被特别关注的联系人 userid 集合。用 set 而不是 vector：判定"这个人关注了没有"
    //是最高频的操作（每弹一次右键菜单、每来一条上下线推送都要问一次），
    //而名单本身最多几十人，有序集合的查找足够快、也省得自己去重。
    std::set<int64_t> m_starred;
};

} // namespace balloonmain
