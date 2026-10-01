/**
 *  AnnouncePopupPolicy.h
 *
 *  「这条群公告推送要不要弹窗」的判定，以及"已弹过哪条"的本地记账。
 *
 *  单独抽成一个模块而不是写在主面板里，是因为这件事的边界条件比它看起来多：
 *  自己发的不该弹给自己、撤回推送不该弹、同一条公告在断线重连后会再推一次也
 *  不该重复弹、而"更旧的那条"补到时同样不该弹（推送乱序时可能发生）。这些格子
 *  靠手工点界面几乎覆盖不全，抽出来才能被 FlamingoUnit 逐条断言。
 *
 *  记账落在用户配置文件（<账号>.ini）的一个专属小节里，按群记"最后弹过的公告ID"。
 *  用 INI 而不是内存：重启客户端后不该把开机前那条公告再弹一遍。
 *
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用同一份源码。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <windows.h>
#include <atlstr.h>
#include <stdint.h>

namespace balloongroupmanage {

// =====================================================================
// 判定输入
// =====================================================================

// 一条群公告推送的判定素材。字段都取自 net::GroupAnnouncementNotify 与当前登录态，
// 由调用方摘出来 —— 这样本模块不必依赖协议结构，可以被单测直接构造。
struct AnnouncePopupInput
{
    int     m_changetype;    // 推送类型：1 新发布 2 已撤回（见 group_announcement_change_type）
    int64_t m_announceId;    // 本条公告ID；<= 0 视为非法推送
    int64_t m_publisherId;   // 发布人userid
    int64_t m_selfUserId;    // 当前登录者userid；0 表示未登录
    int64_t m_lastPoppedId;  // 本群上一次弹过的公告ID；0 表示从没弹过

    AnnouncePopupInput()
        : m_changetype(0)
        , m_announceId(0)
        , m_publisherId(0)
        , m_selfUserId(0)
        , m_lastPoppedId(0)
    {
    }
};

/**
 *  判定这条推送要不要弹窗。
 *
 *  规则（按拒绝的优先级从上到下）：
 *    · 公告ID非正 —— 非法推送，不弹；
 *    · changetype 不是"新发布" —— 撤回不该打扰任何人；
 *    · 发布人就是自己 —— 刚点完"发布"的人不需要再被自己的公告拦一下；
 *    · 公告ID不大于上次弹过的 —— 同一条重复推送（断线重连会再收一次）、
 *      或乱序补到的更旧的一条，都不再弹。
 *
 *    input：判定素材。
 *  返回：该弹返回 true。纯函数，无副作用，便于单元测试。
 */
bool ShouldPopupAnnouncement(const AnnouncePopupInput& input);

// =====================================================================
// 「已弹过哪条」的本地记账
// =====================================================================

/**
 *  读某个群"最后弹过的公告ID"。
 *    configPath：用户配置文件（<账号>.ini）全路径；
 *    groupId：   群ID。
 *  返回：记过的公告ID；从没弹过、或路径为空 / 文件不存在时返回 0。
 */
int64_t LoadLastPoppedAnnounceId(LPCTSTR configPath, int64_t groupId);

/**
 *  记下某个群"最后弹过的公告ID"。
 *    configPath：用户配置文件（<账号>.ini）全路径；路径为空时什么都不做；
 *    groupId：   群ID；
 *    announceId：本次弹过的公告ID。
 */
void SaveLastPoppedAnnounceId(LPCTSTR configPath, int64_t groupId, int64_t announceId);

} // namespace balloongroupmanage
