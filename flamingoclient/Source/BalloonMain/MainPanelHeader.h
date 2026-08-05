/**
 *  MainPanelHeader.h
 *
 *  主面板顶部的粉紫渐变 Banner 控件 —— 对照 design 05/06 截图与
 *  main-panel.jsx 的 Banner 区。绘制内容：橙→粉→紫三段渐变背景、
 *  当前用户头像 / 昵称 / 在线状态胶囊 / 个性签名，以及底部一行
 *  "🔔 你有 N 条新消息"通知。数据来自 MainPanelMockData。
 *
 *  P1b 阶段为纯绘制控件（不处理点击）；头像 / 状态 / 通知的交互留待
 *  后续阶段。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  MainPanelHeader —— 主面板渐变 Banner。继承 DuiControl，只覆写 OnPaint。
 *
 *  数据来源：默认从 MainPanelMockData 取（设计期截图核对 / 单元测试用）；
 *  生产场景由调用方通过 SetUserData 注入真实当前用户信息后再 Invalidate
 *  让本控件重绘。一旦 SetUserData 被调用,本控件即切到"已注入"模式,
 *  此后 OnPaint 全程用注入数据,不再回退到 Mock。
 */
class MainPanelHeader : public balloonwjui::DuiControl
{
public:
    // 头部"签名区"被点击时发给父 host 的通知码。区别于点头像/昵称发的 DUIN_CLICK
    // （那个打开个人信息窗）—— 本码触发"个性签名就地编辑"。取 DUIN_CUSTOM 之后的
    // 值，避免与 balloonui 内置通知码冲突。
    enum { DUIN_HEADER_SIGN_CLICK = balloonwjui::DUIN_CUSTOM + 1 };

    // 头部"在线状态胶囊"被点击时发给父 host 的通知码 —— 触发"弹出在线状态选择菜单"
    // （CBalloonMainPanel::ShowStatusMenu）。同样取 DUIN_CUSTOM 之后的值避免冲突。
    enum { DUIN_HEADER_STATUS_CLICK = balloonwjui::DUIN_CUSTOM + 2 };

    // 头部"头像格"被点击时发给父 host 的通知码 —— 触发"打开修改头像对话框"
    // （CBalloonMainPanel::OpenModifyAvatarDlg）。与点昵称发的 DUIN_CLICK（打开个人
    // 信息窗）区分开：头像→改头像，昵称→看资料。同取 DUIN_CUSTOM 之后的值避免冲突。
    enum { DUIN_HEADER_AVATAR_CLICK = balloonwjui::DUIN_CUSTOM + 3 };

    // 头部底部"消息盒子"行（铃铛盒 + 文字 + 角标）被点击时发给父 host 的通知码 ——
    // 触发"打开消息盒子窗口"（平台公告的收件箱，见 docs/消息盒子接入方案.md）。
    // 同取 DUIN_CUSTOM 之后的值避免冲突。
    enum { DUIN_HEADER_BELL_CLICK = balloonwjui::DUIN_CUSTOM + 4 };

    MainPanelHeader();
    ~MainPanelHeader();

    /**
     *  注入真实当前用户数据,切到"已注入"模式。
     *    nickname     当前用户昵称(空串则用 account 兜底显示)
     *    account      当前用户账号(用于昵称为空时兜底显示)
     *    signature    个性签名(空串显示一行占位文字)
     *    avatarText   头像格内显示的字(通常取昵称首字符)
     *    avatarColor  头像格底色(RGB)
     *    unreadTotal  通知盒右上角未读数(0 时不绘制角标)
     *    presence     当前在线状态(v2 presence_type，见 net/Protocol.h)；决定状态胶囊
     *                 的文字与圆点颜色。
     */
    void SetUserData(const CString& nickname,
                     const CString& account,
                     const CString& signature,
                     const CString& avatarText,
                     COLORREF       avatarColor,
                     int            unreadTotal,
                     int            presence);

    /**
     *  设置 / 清除"真实头像位图"（本地上传的真图）。设了非空位图后 OnPaint 改画该
     *  圆角真图（而非底色方块 + 字形）；传 NULL 则回到字形渲染。
     *  <b>本控件接管位图所有权</b>：再次设置或析构时 DeleteObject 旧位图。位图应是
     *  与头像同边长、32bpp 预乘 alpha、自带圆角（角外透明）的 DIB（见
     *  AvatarImageUtil::MakeRoundedAvatarFromFile）。
     *    hbm：头像位图；NULL 表示清除、回到字形渲染。
     */
    void SetAvatarBitmap(HBITMAP hbm);

    /**
     *  设置「消息盒子」未读<u>公告</u>条数，决定 Banner 底部那一行的文字与角标：
     *  大于 0 时显示「你有 N 条新公告」并画红色角标，等于 0 时只显示「消息盒子」四个字。
     *  本方法只改内存状态，调用方负责随后 Invalidate / 局部刷新。
     *
     *  <u>注意别与 SetUserData 的 unreadTotal 参数混为一谈</u>：那个是<u>会话</u>未读总数
     *  （现阶段恒为 0，见 MainPanelUserData.h），本项是<u>平台公告</u>的未读数，来自本地
     *  公告库 CAnnounceStore::GetUnreadCount()。两者语义不同，不要互相挪用。
     *
     *    count  未读公告条数；负数按 0 处理。
     */
    void SetAnnounceUnread(int count) { m_announceUnread = (count > 0) ? count : 0; }

    // 当前显示的未读公告条数。供单元测试断言注入是否生效。
    int  GetAnnounceUnread() const { return m_announceUnread; }

    /**
     *  仅更新在线状态并触发胶囊重绘所需的状态（不改其它注入字段）。供状态菜单选中后
     *  "乐观更新"胶囊、以及应答失败时"回滚"胶囊使用。本方法只改内存状态，调用方负责
     *  随后 Invalidate / 局部刷新。
     *    presence     新的在线状态(v2 presence_type)。
     */
    void SetPresence(int presence) { m_presence = presence; }

    // 当前在线状态(v2 presence_type)。供状态菜单"回滚"时取回先前值。
    int  GetPresence() const { return m_presence; }

    /**
     *  设置界面连接态，据此决定状态胶囊显示什么文字、头像要不要置灰。由
     *  CBalloonMainPanel::ApplyConnectionState 在连接态变化时推入，调用方负责随后
     *  Invalidate / 局部刷新。
     *
     *  <u>本方法不改 m_presence</u>：掉线只说明连不上，用户自己选的在线状态没变。
     *  两者分开存是必须的 —— 状态菜单与托盘右键菜单标记的"当前项"读的是 GetPresence()，
     *  若把"正在连接"塞进 m_presence，菜单里就会冒出一个用户从没选过的项。
     *
     *    state： 界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState。
     */
    void SetConnectionState(int state) { m_connUiState = state; }

    // 当前界面连接态(ConnectionUiState)。供单元测试断言连接态是否被正确注入。
    int  GetConnectionState() const { return m_connUiState; }

    // 5 个只读 getter,供单元测试验证 SetUserData 是否被正确调用(纯
    // 内存状态查询,不影响 OnPaint 行为)。生产代码<u>不应</u>依赖它们读
    // Header 状态 —— 上层数据源始终是 m_UserMgr.m_UserInfo。
    bool           GetHasUserData() const { return m_hasUserData; }
    const CString& GetNickname()    const { return m_nickname; }
    const CString& GetAccount()     const { return m_account; }
    const CString& GetSignature()   const { return m_signature; }
    const CString& GetAvatarText()  const { return m_avatarText; }
    COLORREF       GetAvatarColor() const { return m_avatarColor; }
    int            GetUnreadTotal() const { return m_unreadTotal; }

    // 个性签名文字的命中矩形（host 客户区坐标，由 OnPaint 按实际绘制位置缓存）。
    // 供主面板把内联编辑弹层精确盖到签名上。空矩形(全 0)表示当前不可点。
    const RECT&    GetSignHitRect() const { return m_rcSignHit; }

    // 在线状态胶囊的命中矩形（host 客户区坐标，由 OnPaint 按实际绘制位置缓存）。
    // 供主面板把状态选择菜单锚到胶囊正下方。空矩形(全 0)表示当前尚未绘制。
    const RECT&    GetStatusHitRect() const { return m_rcStatusHit; }

    // 以下三个命中矩形同样由 OnPaint 按实际绘制位置缓存（host 客户区坐标，空矩形
    // 表示尚未绘制）。它们连同上面两个一起，构成 Banner 上"不能被窗口拖动吃掉"的
    // 区域清单 —— 主面板把 Banner 整块作为可拖区，再逐块把它们排除掉，见
    // PanelDragZone.h 与 CBalloonMainPanel::CollectDragZoneRects。
    const RECT&    GetAvatarHitRect() const { return m_rcAvatarHit; }
    const RECT&    GetNameHitRect()   const { return m_rcNameHit; }
    // 底部"消息盒子"一行（通知盒 + 右侧文字 + 未读角标）的命中矩形。本控件当前
    // <u>不</u>响应它的点击（见 OnLButtonUp），先框出来是为了让"拖动窗口"提前给它
    // 让路：日后它变成可点击的，不必再回头改拖动逻辑。
    const RECT&    GetBellHitRect()   const { return m_rcBellHit; }

    // DuiControl 覆写：绘制渐变背景 + 头像 / 昵称 / 状态 / 签名 / 通知行。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    /**
     *  鼠标左键<u>抬起</u>时按命中区域分派通知（与 DuiControl 的"点击在抬起时
     *  触发"语义一致，避免基类 OnLButtonUp 再补发一个通用 DUIN_CLICK）：
     *    · 命中<u>签名</u>区 → 发 DUIN_HEADER_SIGN_CLICK（触发"个性签名就地编辑"）。
     *    · 命中<u>头像 / 昵称</u>区 → 发 DUIN_CLICK（触发"打开个人信息对话框"）。
     *    · 命中<u>状态胶囊</u> → 发 DUIN_HEADER_STATUS_CLICK（弹出在线状态选择菜单）。
     *    · 命中<u>消息盒子行</u>（铃铛盒 + 文字 + 角标）→ 发 DUIN_HEADER_BELL_CLICK
     *      （打开消息盒子窗口）。
     *    · 空白渐变区 → 不触发（留给窗口拖动）。
     *  各块命中矩形在 OnPaint 里按实际绘制位置算好并缓存到
     *  m_rcAvatarHit / m_rcNameHit / m_rcSignHit / m_rcStatusHit / m_rcBellHit。
     *    pt       命中点(host 客户区坐标)。
     *    mkFlags  鼠标按键 / 修饰键状态位。
     *  返回:命中身份区返回 true(已发 notify);否则 false。
     */
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    bool     m_hasUserData;     // 是否已调用过 SetUserData(决定数据走 Mock 还是注入值)
    CString  m_nickname;        // 注入的昵称
    CString  m_account;         // 注入的账号
    CString  m_signature;       // 注入的个性签名
    CString  m_avatarText;      // 注入的头像字
    COLORREF m_avatarColor;     // 注入的头像底色（无真图位图时画底色 + 字形）
    HBITMAP  m_avatarBitmap;    // 真实头像位图（本地上传的真图，圆角方形预乘 alpha）；NULL=走字形；本控件持有、析构释放
    HBITMAP  m_avatarBitmapGray;// m_avatarBitmap 的灰度副本，首次要置灰时按需生成并缓存（灰化整幅位图不便每次重绘都做一遍）；随 m_avatarBitmap 一起作废；本控件持有、析构释放
    int      m_unreadTotal;     // 注入的<u>会话</u>未读总数（SetUserData 传入；现阶段恒为 0，见 MainPanelUserData.h）
    int      m_announceUnread;  // 未读<u>平台公告</u>条数（SetAnnounceUnread 注入，来自本地公告库）；决定底部那行的文字与角标
    int      m_presence;        // 用户<u>自己选的</u>在线状态(v2 presence_type)；只由状态菜单改动，掉线不会改它
    int      m_connUiState;     // 界面连接态(OnlineCapability.h 的 ConnectionUiState)，只影响渲染：决定胶囊文字与头像是否置灰；默认在线

    // 三块"可点击打开个人信息"的命中矩形(host 客户区坐标),由 OnPaint 按
    // 实际绘制位置缓存,供 OnLButtonDown 判定。空矩形(全 0)表示该块当前不可点。
    RECT     m_rcAvatarHit;     // 头像(含外圈)命中矩形
    RECT     m_rcNameHit;       // 昵称文字命中矩形
    RECT     m_rcSignHit;       // 个性签名文字命中矩形
    RECT     m_rcStatusHit;     // 在线状态胶囊命中矩形(供状态菜单锚定)
    RECT     m_rcBellHit;       // 底部"消息盒子"行命中矩形(通知盒 + 文字 + 未读角标)；点了发 DUIN_HEADER_BELL_CLICK，同时据此把窗口拖动区从这一行让开
};

} // namespace balloonmain
