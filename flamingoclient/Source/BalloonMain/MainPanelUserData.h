/**
 *  MainPanelUserData.h
 *
 *  把"当前登录用户字段"转成主面板 Banner(MainPanelHeader)需要的 6 个字段;
 *  以及把转换结果应用到主面板控件树上的 helper。
 *
 *  本 helper <u>不依赖 CBuddyInfo</u>,只接收 userid + 三个字符串原始字段。
 *  调用方(CBalloonMainPanel::InitUi)从 m_UserMgr.m_UserInfo 里取出来传入。
 *  解耦原因:FlamingoUnit 测试工程不引 UserSessionData.cpp,让 helper 直接
 *  收原始字段可以纯字段单测,不必拖整套登录态结构。
 *
 *  数据流:
 *    CFlamingoClient::OnLoginResult 把昵称/签名等填进
 *    m_UserMgr.m_UserInfo(CBuddyInfo)
 *      → CBalloonMainPanel::InitUi 末尾取字段
 *      → BuildHeaderUserData 转 HeaderUserDataPack
 *      → ApplyUserDataToMainPanel 调 MainPanelHeader::SetUserData
 *      → MainPanelHeader m_hasUserData 切到注入模式
 *
 *  本文件遵循 balloonui 头文件约定:include 前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  HeaderUserDataPack —— 主面板 Banner 渲染需要的 6 字段集合,与
 *  MainPanelHeader::SetUserData 的 6 个形参一一对应。
 *
 *  作为 BuildHeaderUserData 的输出,目的是把"原始字段 → Header 入参"
 *  的纯逻辑变换隔离出来,便于单元测各种边界(空昵称、空签名、首字符提取、
 *  avatarColor 哈希等)而不必构造 Header 控件。
 */
struct HeaderUserDataPack
{
    CString  nickname;      // 昵称(空串则 Header 自行用 account 兜底渲染)
    CString  account;       // 账号(昵称空时的兜底显示文本)
    CString  signature;     // 个性签名(空串渲染为占位行)
    CString  avatarText;    // 头像格内显示的字(昵称首字符;空昵称取 account 首字符)
    COLORREF avatarColor;   // 头像格底色(按 userid 哈希取调色板某一色)
    int      unreadTotal;   // 通知盒右上角未读总数(P2 占位为 0,后续接入 RecentList)
};

/**
 *  按 userid 哈希分配头像底色(调色板 4 色,与 contacts 列表 mock 数据
 *  的取色风格一致)。同 userid 多次调用稳定返同一色。
 *    userid  用户ID;0 仍按 hash 给一个色。
 *  返回:RGB 颜色。
 *  暴露作为公开 API 是为了让"个人信息"对话框等其它地方拿到与 Banner
 *  Header 完全一致的头像底色。
 */
COLORREF GetUserAvatarColor(UINT userid);

/**
 *  把当前登录用户的 4 个原始字段转成 HeaderUserDataPack。
 *    userid     用户ID,用于哈希分配 avatarColor;0 仍按 hash 给一个色。
 *    account    账号(如 MY0312)。
 *    nickname   昵称(可空)。
 *    signature  个性签名(可空)。
 *    out        输出 pack,函数会先清空所有字段再填入新值。
 *  纯函数,无任何副作用。unreadTotal 现阶段恒为 0。
 */
void BuildHeaderUserData(UINT userid,
                        const CString& account,
                        const CString& nickname,
                        const CString& signature,
                        HeaderUserDataPack& out);

/**
 *  把当前用户信息注入到主面板控件树的 Header 上。
 *    root       BuildMainPanelBody 返回的客户区根控件;null 时返 false。
 *    userid     用户ID。
 *    account    账号。
 *    nickname   昵称(可空)。
 *    signature  个性签名(可空)。
 *    presence   当前在线状态(v2 presence_type,见 net/Protocol.h),决定状态胶囊文字/
 *               圆点色。调用方一般传 CIUSocket::GetRecordOnlineStatus()(期望在线状态);
 *               签名等其它字段更新而状态未变时,也应传入当前 presence,避免胶囊被重置。
 *  返回:成功调用 MainPanelHeader::SetUserData 返 true;root 为 null /
 *  找不到 kIdHeader 控件 / dynamic_cast 失败均返 false。
 *  内部会调 Invalidate 让 Header 重绘(若 root 已挂在 host 上)。
 */
bool ApplyUserDataToMainPanel(balloonwjui::DuiControl* root,
                              UINT userid,
                              const CString& account,
                              const CString& nickname,
                              const CString& signature,
                              int presence);

/**
 *  同上，但额外按当前用户已存的 (facetype, customface) 决定头像字 / 底色：
 *    · facetype 为"字形头像"（见 AvatarSpec）时，用 customface 里编码的字形 + 调色板
 *      底色覆盖（系统头像 / 首字母选定的结果）；
 *    · 否则维持原"昵称首字 + userid 哈希底色"的自动渲染，行为与上面的重载完全一致
 *      （历史账号不变色）。
 *  供主面板注入 / 改头像成功后刷新 Banner 头像用。
 *    facetype       当前用户头像类型（见 balloonmain::AvatarKind）。
 *    customfaceUtf8 当前用户 customface（UTF-8）。
 *  其余参数与返回值同上。
 */
bool ApplyUserDataToMainPanel(balloonwjui::DuiControl* root,
                              UINT userid,
                              const CString& account,
                              const CString& nickname,
                              const CString& signature,
                              int presence,
                              int facetype,
                              const std::string& customfaceUtf8);

} // namespace balloonmain
