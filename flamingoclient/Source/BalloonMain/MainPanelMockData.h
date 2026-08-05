/**
 *  MainPanelMockData.h
 *
 *  主面板的 mock 数据 —— 当前登录用户、好友、服务号、群等。数据取自
 *  design 的 data.js。本阶段(P1b)用 mock 数据驱动主面板视觉，真实数据
 *  接入(从 CFlamingoClient 取真实好友/群/消息)留作后续阶段。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方必须已引入 ATL/WTL
 *  （需要 CString / COLORREF）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <vector>

namespace balloonmain {

/**
 *  在线状态。逐值对应 design data.js 里联系人的 status 字段。
 */
enum MockStatus
{
    kMockOnline    = 0,   // 在线
    kMockAway      = 1,   // 离开
    kMockBusy      = 2,   // 忙碌
    kMockInvisible = 3,   // 隐身
    kMockOffline   = 4,   // 离线
};

/**
 *  一个联系人 / 群成员 / 服务号的 mock 信息。
 */
struct MockContact
{
    CString  id;           // 唯一标识（如 "me" / "c1" / "s1"）
    CString  name;         // 昵称
    CString  remark;       // 备注名（空串则界面显示 name）
    CString  account;      // 账号
    CString  avatarText;   // 头像内显示的字 / emoji（如 "L" / "🔔"）
    COLORREF avatarColor;  // 头像底色
    CString  signature;    // 个性签名 / 副标题
    CString  title;        // 职位
    int      status;       // 在线状态，取值见 MockStatus
    bool     isService;    // 是否服务号
};

/**
 *  一个群的 mock 信息。
 */
struct MockGroup
{
    CString  id;           // 群标识（如 "g1"）
    CString  name;         // 群名
    CString  avatarText;   // 头像内显示的字
    COLORREF avatarColor;  // 头像底色
    int      memberCount;  // 群成员数
};

/**
 *  一条会话（最近联系）的 mock 信息 —— 显示字段已预先解析好（无需再去
 *  联系人 / 群表里查 id）。
 */
struct MockConversation
{
    CString  title;        // 会话显示名（联系人备注名/名 或 群名）
    CString  avatarText;   // 头像内显示的字
    COLORREF avatarColor;  // 头像底色
    CString  lastMsg;      // 末条消息预览
    CString  time;         // 时间文字（如 "14:32" / "昨天"）
    int      unread;       // 未读数
    bool     isGroup;      // 是否群会话
    int      status;       // 单聊对方在线状态（群会话忽略），见 MockStatus
};

/**
 *  当前登录用户（mock）—— design data.js 里的 ME（林知夏）。
 */
const MockContact& MockMe();

/**
 *  未读消息总数（mock）—— 对应 Banner 上"你有 N 条新消息"。
 */
int MockUnreadTotal();

/**
 *  "我的好友"分组里的联系人（mock，对应 design MY_FAVORITES）。
 */
const std::vector<MockContact>& MockFavorites();

/**
 *  "服务号"分组里的账号（mock，对应 design SERVICE_ACCOUNTS）。
 */
const std::vector<MockContact>& MockServiceAccounts();

/**
 *  "我的群聊"列表（mock，对应 design GROUPS）。
 */
const std::vector<MockGroup>& MockGroups();

/**
 *  会话列表（mock，对应 design CONVERSATIONS，按最近时间排序）。
 */
const std::vector<MockConversation>& MockConversations();

/**
 *  通讯录总人数（mock）—— 对应联系人页"通讯录 · N 人"。
 */
int MockContactsTotalCount();

/**
 *  取在线状态对应的状态点颜色。
 *    status：取值见 MockStatus。
 */
COLORREF MockStatusColor(int status);

} // namespace balloonmain
