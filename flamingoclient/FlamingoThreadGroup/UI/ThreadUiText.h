/**
 *  帖子群窗口与发帖窗口里拼出来的界面文字（docs/帖子群方案.md 4.6）：时间（「刚刚」「5 分钟前」「今天 14:20」……）、留言数、
 *  互动标签上的文字、操作失败与本地校验不通过时的说明等。都经 TR / TRF 取当前界面语言的译文。
 *
 *  时间的分档在 Core/ThreadTime（纯逻辑，有单元测试），这里只按分档拼文字。
 *
 *  典型用法：
 *      card->SetTime(threadgroup::FormatPostTime(post.m_createTime));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

namespace threadgroup {

/**
 *  把一个时间写成界面上显示的文字：刚刚、N 分钟前、今天 14:20、昨天 14:20、10-06 14:20、2025-10-06。
 *  @param timeMs 时间，Unix 毫秒
 *  @return 显示文字；timeMs 不大于 0 时为空串
 */
std::wstring FormatPostTime(int64_t timeMs);

/**
 *  卡片底部的留言数文字，如「14 条留言」。
 *  @param count 留言与回复总数
 *  @return 显示文字；count 不大于 0 时为空串
 */
std::wstring FormatCommentCount(int count);

/**
 *  「赞」标签上的文字，如「赞 8」。
 *  @param count 点赞人数
 */
std::wstring FormatLikeChip(int count);

/**
 *  一次操作失败时给用户看的说明（浮层提示、发帖窗口与留言框里的出错说明共用）。
 *  @param code 服务端错误码或客户端判定，取值见 ThreadError
 *  @return 说明文字；未单独列出的错误码写成「操作失败（错误码 N）」
 */
std::wstring FormatActionError(int code);

/**
 *  本地校验不通过时的说明。
 *  @param check 校验结果，取值见 ComposeCheck 与 ComposeSubmitCheck
 *  @param kind  内容种类，取值见 ComposeKind（正文上限与「空内容」的说法随种类不同）
 *  @return 说明文字；check 为 compose_ok 时为空串
 */
std::wstring FormatComposeCheck(int check, int kind);

//请求没有发出去（不在线）时的说明
std::wstring NotSentText();

} // namespace threadgroup
