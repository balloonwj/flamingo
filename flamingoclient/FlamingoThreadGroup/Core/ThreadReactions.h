/**
 *  点赞与表情回应的汇总运算（纯函数，docs/帖子群方案.md 4.4）：
 *    - 点了立即更新（乐观更新），请求失败时恢复点之前的样子；
 *    - 2711 应答带回的汇总以本人视角为准，直接替换；
 *    - 2720 推送带回的汇总里 mine 一律为 false：本人加过哪些沿用本地记录，只有动作发起人是本人时按 on 改那一项；
 *    - 显示顺序：「赞」在最前，其余按编号；人数为 0 的项去掉；
 *    - 名单文字：最多列 20 个名字，其余写「等 N 人」（由界面按译文拼）。
 *
 *  典型用法：
 *      std::vector<ThreadReaction> before = post.m_reactions;
 *      threadgroup::ToggleReaction(post.m_reactions, kReactionLike, true);
 *      ...请求失败：post.m_reactions = before;
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "ThreadTypes.h"

namespace threadgroup {

/**
 *  本人点了一种互动后立即更新汇总（不等应答）。
 *  @param list     要更新的汇总
 *  @param reaction 互动编号
 *  @param on       true 为加上，false 为取消；状态已经是目标状态时不变
 */
void ToggleReaction(std::vector<ThreadReaction>& list, int reaction, bool on);

/**
 *  本人是否加过某种互动。
 */
bool HasMyReaction(const std::vector<ThreadReaction>& list, int reaction);

/**
 *  某种互动的人数；没有这一项时为 0。
 */
int ReactionCount(const std::vector<ThreadReaction>& list, int reaction);

/**
 *  用 2720 推送的汇总更新本地汇总。
 *  @param list         本地汇总（被替换为推送的汇总，mine 按规则补上）
 *  @param summary      推送里目标最新的汇总（mine 一律为 false）
 *  @param reaction     本次变化的互动编号
 *  @param on           本次是加上还是取消
 *  @param operatorIsMe 动作发起人是不是本人（本人在别的设备上点的也算）
 */
void ApplyReactionNotify(std::vector<ThreadReaction>& list, const std::vector<ThreadReaction>& summary, int reaction,
                         bool on, bool operatorIsMe);

/**
 *  整理显示顺序：去掉人数为 0 的项，「赞」在最前，其余按互动编号升序。
 */
void SortReactions(std::vector<ThreadReaction>& list);

/**
 *  名单文字要列出多少个名字、还剩多少人写成「等 N 人」。
 *  @param listed   拉到的用户数
 *  @param total    加了这种互动的总人数
 *  @param maxNames 最多列出的名字数（kReactorListLimit）
 *  @param shown    [出参] 要列出的名字数
 *  @param rest     [出参] 「等 N 人」里的 N 应取的总人数；不需要「等」时为 0
 */
void ReactorListSplit(int listed, int total, int maxNames, int& shown, int& rest);

} // namespace threadgroup
