/**
 *  「特别关注」上下线提示窗的<u>行模型</u>，StarredPopupModel.h
 *
 *  把"攒下来的若干条上下线事件，该在弹窗里列成哪几行文字"从窗口代码里抽出成纯函数，
 *  便于被 FlamingoUnit 测试工程直接调用断言（真正的建窗、计时、点击留在
 *  CStarredPopupWindow）。写法与同级的 MainPanelContactMenu / GroupRowText 一致。
 *
 *  为什么要合并成一份模型：多人同时上下线时，弹一串窗口会变成弹窗轰炸，故合并成
 *  <u>一个</u>弹窗、逐行列出。合并就带出三条要判的规则，都不适合散在窗口代码里：
 *
 *    一、同一个人反复变化只留最后一次。张三上线又下线，用户要看的是"他现在不在"，
 *        列成两行只会让人以为是两个人。
 *    二、行数封顶。攒了十几个人时窗口会长到顶天，故超过 maxRows 时把多出来的收敛成
 *        一行「等 N 人」。
 *    三、保持事件到达的先后顺序。谁先变化谁在上面，与用户的感知顺序一致。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <stdint.h>
#include <atlstr.h>       // CString
#include <vector>

namespace balloonmain {

//弹窗最多列几行（含「等 N 人」那一行在内）。再多窗口就长得离谱了，
//而用户真正关心的也就是最近变化的那几个人。
const int kStarredPopupMaxRows = 5;

/**
 *  一条待展示的上下线事件。由主面板在收到 presence 推送、判定过"是被关注的人 +
 *  确实是上线或下线跳变 + 未被抑制"之后攒进来。
 */
struct StarredPresenceEvent
{
    int64_t m_userid;   // 谁发生了变化；<= 0 的会被行模型丢掉
    CString m_name;     // 显示名（备注优先，与通讯录树同一口径）；空串时回落成「用户<id>」
    bool    m_online;   // true 上线、false 下线

    StarredPresenceEvent()
        : m_userid(0)
        , m_online(false)
    {
    }
};

/**
 *  弹窗里的一行。
 */
struct StarredPopupRow
{
    CString m_text;      // 显示文字，形如「张三 已上线」；汇总行形如「等 3 人」
    int64_t m_userid;    // 点这一行打开与谁的单聊；汇总行为 0（不可点）
    bool    m_isSummary; // 是不是「等 N 人」那条汇总行

    StarredPopupRow()
        : m_userid(0)
        , m_isSummary(false)
    {
    }
};

/**
 *  把攒下来的事件排成弹窗里的行。纯函数、无副作用、不创建窗口。
 *
 *    events： 按到达先后排好的事件序列（调用方直接 push_back 即可，不必自己去重）。
 *    maxRows：最多列几行，含汇总行；一般传 kStarredPopupMaxRows。
 *             <= 0 时返回空 vector（视为"一行都不显示"）。
 *
 *  @return 行模型（值语义，调用方拥有）。
 *
 *  规则（三条都有对应用例）：
 *    · 同一个 userid 出现多次时<u>只留最后一次</u>的状态，位置留在它<u>第一次</u>出现的
 *      地方 —— 位置跟着最后一次跑的话，用户眼看着行在列表里跳来跳去。
 *    · 去重后的人数超过 maxRows 时，前 maxRows-1 行照常列，末行收敛成「等 N 人」，
 *      N 为被折叠掉的人数。
 *    · userid <= 0 的事件直接丢弃（脏数据不该占一行）。
 */
std::vector<StarredPopupRow> BuildStarredPopupRows(
    const std::vector<StarredPresenceEvent>& events, int maxRows);

/**
 *  取单条事件的显示文字，形如「张三 已上线」。供上面的行模型使用，也供日志与测试
 *  直接调用，免得两处各拼一份措辞。
 *
 *    name：  显示名；空串时回落成「用户<userid>」，不能留一个孤零零的「已上线」。
 *    userid：显示名为空时用来拼回落文案。
 *    online：true 上线、false 下线。
 *
 *  @return 该行的完整文字。
 */
CString FormatStarredPresenceText(const CString& name, int64_t userid, bool online);

} // namespace balloonmain
