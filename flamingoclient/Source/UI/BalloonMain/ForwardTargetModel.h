/**
 *  转发目标选择窗（CForwardTargetDlg）的纯逻辑模型：已选目标集合、候选过滤、
 *  「最近会话」候选的整理。不碰任何窗口与绘制 API，可被 FlamingoUnit 直接测试。
 *
 *  目标用（会话类型，会话 id）唯一标识：同一个人在「最近会话」与「联系人」两个页签里
 *  都会出现，勾选状态必须共用一份，否则会向同一个人转发两遍。
 *
 *  典型用法：
 *      ForwardTargetSelection sel(selfUserId);
 *      if (sel.Toggle(target) == kForwardToggleFull)
 *          提示「最多选择 9 个」;
 *      std::vector<size_t> shown = FilterForwardTargets(candidates, query);
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace balloonmain {

//一次转发最多选择的目标数。每个目标一次请求，逐条转发时每条消息在服务端各落一次库；
//上限同时让右侧已选列表不必滚动。与用户确认的取值，见 docs/聊天消息转发方案.md §1
const size_t kForwardMaxTargets = 9;

/**
 *  一个转发目标，也是目标选择窗里的一个候选。
 */
struct ForwardTarget
{
    int          m_ctype;     //会话类型：1 单聊、2 群聊
    int64_t      m_cid;       //会话 id：单聊为对方 userid，群聊为群 id
    std::wstring m_name;      //显示名：单聊为联系人显示名，群聊为群名
    std::wstring m_account;   //账号，参与搜索；群聊为空

    //头像位图，只借用：归主面板的头像缓存所有，缓存在主面板登出 / 销毁时统一释放，而
    //目标选择窗先于它关闭，故选择窗存续期间位图一直有效。为 NULL 时画首字头像
    HBITMAP      m_avatar;

    ForwardTarget()
        : m_ctype(0)
        , m_cid(0)
        , m_avatar(NULL)
    {
    }
};

/**
 *  ForwardTargetSelection::Toggle 的结果。
 */
enum ForwardToggleResult
{
    kForwardToggleAdded   = 0,   // 原先未选，现已加入已选集合
    kForwardToggleRemoved = 1,   // 原先已选，现已从已选集合移除
    kForwardToggleFull    = 2,   // 原先未选，但已选满 kForwardMaxTargets 个，未加入
    kForwardToggleInvalid = 3    // 不能作为转发目标（自己、id 非正、会话类型不是单聊或群聊），未加入
};

/**
 *  已选转发目标的集合，按勾选的先后顺序保存。目标选择窗的三个页签共用同一个实例。
 */
class ForwardTargetSelection
{
public:
    /**
     *  @param selfUserId 本人 userid；以本人为对方的单聊不能作为转发目标（服务端会拒绝）
     */
    explicit ForwardTargetSelection(int64_t selfUserId);

    /**
     *  切换一个目标的选中状态：未选则加入，已选则移除。
     *  @param target 目标；按（m_ctype，m_cid）判断是否同一个
     *  @return 本次操作的结果，见 ForwardToggleResult
     */
    ForwardToggleResult Toggle(const ForwardTarget& target);

    /**
     *  移除一个已选目标（右侧已选列表里点移除）。
     *  @return 原先已选、现已移除返回 true；原先未选返回 false
     */
    bool Remove(int ctype, int64_t cid);

    /**
     *  判断一个目标当前是否已选。
     */
    bool IsSelected(int ctype, int64_t cid) const;

    /**
     *  是否已选满 kForwardMaxTargets 个。
     */
    bool IsFull() const;

    /**
     *  已选目标，按勾选的先后顺序排列。引用在下一次修改集合之前有效。
     */
    const std::vector<ForwardTarget>& Selected() const;

    /**
     *  清空已选集合。
     */
    void Clear();

private:
    //判断一个目标能否被选：单聊或群聊、id 为正、不是本人
    bool IsSelectable(const ForwardTarget& target) const;

    //在已选集合里查找目标，返回下标；找不到返回 -1
    int IndexOf(int ctype, int64_t cid) const;

    int64_t                    m_selfUserId;   //本人 userid，构造时传入，生命周期内不变
    std::vector<ForwardTarget> m_selected;     //已选目标，按勾选先后排列，最多 kForwardMaxTargets 个
};

/**
 *  按查询词过滤候选。
 *  @param candidates 候选列表（「最近会话」或「群组」页的全部行）
 *  @param query      查询词；去掉首尾空白后为空时不过滤
 *  @return 命中候选在 candidates 里的下标，保持原顺序。匹配规则同主面板搜索
 *          （contactsearch::Match）：名字子串、账号子串、名字的拼音首字母与全拼
 */
std::vector<size_t> FilterForwardTargets(const std::vector<ForwardTarget>& candidates,
                                         const std::wstring& query);

/**
 *  从会话列表整理「最近会话」页的候选。
 *  只保留单聊与群聊（服务号会话不能转发），去掉以本人为对方的单聊与 id 非正的项；
 *  同一会话出现多次时只保留第一次出现的那一项。其余保持会话列表原有顺序（置顶在前、
 *  按时间倒序）。
 *  @param conversations 会话列表，已由调用方解析好显示名
 *  @param selfUserId    本人 userid
 *  @return 候选列表
 */
std::vector<ForwardTarget> BuildRecentForwardTargets(const std::vector<ForwardTarget>& conversations,
                                                     int64_t selfUserId);

} // namespace balloonmain
