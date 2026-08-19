/**
 *  BalloonModifyAvatarDlg.h
 *
 *  "修改头像"对话框 —— 对照 design/修改头像1/2/3.png。基于 balloonui
 *  DuiFrameWindow。顶部预览大头像 + 说明；中部三段切换（系统头像 / 首字母 /
 *  本地上传）；底部取消 / 使用此头像。
 *
 *  第一期范围（字形头像端到端，零服务端改动）：
 *    · 系统头像页：一组预置「字形 + 底色」磁贴（AvatarPickerTile），点选其一。
 *    · 首字母页：输入 1~2 字符 + 12 色底色选择器，实时刷新预览。
 *    · 本地上传页：仅按设计稿画占位（拖拽框 + 文案），实际上传留第二期。
 *  点"使用此头像"把当前「字形 + 底色」经 AvatarSpec 编码成 (facetype,customface)，
 *  走 v2 cmd_change_avatar 发往服务端；应答经主面板转发到 OnChangeAvatarResult。
 *
 *  注：系统头像磁贴 / 首字母预览都用 MainPanelAvatar 的 MakeMonogramSquareAvatar
 *  生成「圆角方形 + 字形」位图。GDI+ DrawString 不做彩色 emoji 渲染，故第一期
 *  系统头像采用单 CJK 字 / 字母这类能被 monogram 字体清晰渲染的字形；设计稿里的
 *  彩色 emoji 需后续彩色字形渲染能力，留作后续（见类内 kSystemPresets 注释）。
 *
 *  与主面板的关系：主面板头像区被点击时创建并显示本对话框；改像应答由
 *  由主面板转发到本对话框的 OnChangeAvatarResult。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

#include <string>
#include <vector>

class CFlamingoClient;

namespace net { struct UpdateProfileResponse; struct AvatarUploadResult; }
namespace balloonwjui
{
    class DuiVBox;
    class DuiHBox;
    class DuiButton;
    class DuiEdit;
    class DuiControl;
}
namespace balloonmain { class AvatarPickerTile; class CUploadDropZone; }

/**
 *  CBalloonModifyAvatarDlg —— 修改头像对话框（非模态浮窗，owner = 主面板）。
 *
 *  本对话框<u>目标无关</u>：既能改当前登录用户自己的头像，也能改某个群的群头像 ——
 *  两者的选图界面（系统头像 / 首字母 / 本地上传三页）逐像素相同，差别只在"改完
 *  发哪条请求"，故收成一个类、由 OpenForCurrentUser / OpenForGroup 两个入口区分。
 */
class CBalloonModifyAvatarDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  本次要改的是谁的头像。决定"使用此头像"发哪条请求、标题栏写什么，
     *  以及自动头像按谁的 id 取底色。
     */
    enum TargetKind
    {
        target_user  = 0,   //当前登录用户自己的头像，走 cmd_change_avatar
        target_group = 1,   //某个群的群头像，走 cmd_update_group_info 的头像两项
    };

    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；发请求走
     *                CIUSocket 单例，pFMGClient 暂留作后续扩展。
     */
    explicit CBalloonModifyAvatarDlg(CFlamingoClient* pFMGClient);
    ~CBalloonModifyAvatarDlg();

    /**
     *  创建并显示对话框，按当前用户已存头像预选。已显示则前置并复位预选。
     *    owner：owner 窗口句柄（主面板）；内部 Create 传 NULL 让子控件通知自路由
     *           回本对话框（与改密 / 新建群对话框同处理）。
     *    facetype：当前用户已存的头像类型（见 balloonmain::AvatarKind）。
     *    customfaceUtf8：当前用户已存的 customface（UTF-8）。
     *    nickname：当前用户昵称（自动头像取首字、首字母页默认值、预览回退用）。
     *    userid：当前用户 id（自动头像底色取色用，同时也是上传缓存的账号分桶）。
     */
    void OpenForCurrentUser(HWND owner, int facetype, const std::string& customfaceUtf8,
                            LPCTSTR nickname, __int64 userid);

    /**
     *  创建并显示对话框，改的是某个群的群头像。已显示则前置并按新目标复位预选。
     *
     *  与 OpenForCurrentUser 的差别只有三处：标题写"修改群头像"、自动头像按
     *  groupid 取底色与群名首字、"使用此头像"发的是 cmd_update_group_info。
     *  选图交互（三页、预选、上传校验）完全一致。
     *
     *    owner：owner 窗口句柄（群管理窗口）；同上，内部 Create 仍传 NULL。
     *    groupId：目标群 id（自动头像取色、以及发请求时定位群）。
     *    groupName：群名称（自动头像取首字、预览回退用；可为 NULL）。
     *    facetype：该群已存的头像类型（见 balloonmain::AvatarKind）。
     *    customfaceUtf8：该群已存的 customface（UTF-8）。
     *    selfUserId：当前登录用户 id —— <u>不是</u> groupId。上传头像图片时按它
     *                分桶落地本地缓存（Users\<selfId>\Avatar\），传错会让图落到
     *                别的账号目录下。
     */
    void OpenForGroup(HWND owner, __int64 groupId, LPCTSTR groupName,
                      int facetype, const std::string& customfaceUtf8,
                      __int64 selfUserId);

    /**
     *  收到改像应答（通用 {code,msg}）：0 → 提示成功并关闭；108（头像过大，第二期
     *  才会触发）/其它 → 就地提示。由主面板的 OnChangeAvatarResult 转发。
     *    resp：应答解析结果。
     */
    void OnChangeAvatarResult(const net::UpdateProfileResponse& resp);

    /**
     *  收到头像图片上传结果（本地上传页"使用此头像"触发上传后）：成功则接着发
     *  cmd_change_avatar(facetype=2, customface=fileid) 落库；失败就地提示。由主面板的
     *  OnAvatarUploadResult 转发。
     *    result：上传结果（含 fileid / 本地缓存路径或失败码）。
     */
    void OnAvatarUploadResult(const net::AvatarUploadResult& result);

    // 取上一次"使用此头像"实际发出的头像类型（供主面板成功后写回本端缓存）。
    int GetAppliedFacetype() const { return m_appliedFacetype; }
    // 取上一次"使用此头像"实际发出的 customface（UTF-8；供主面板写回本端缓存）。
    const std::string& GetAppliedCustomface() const { return m_appliedCustomface; }
    // 取本次改的是谁的头像（取值见 TargetKind）：主面板据此决定成功后要不要
    // 写回自己的头像缓存 —— 改的是群头像时不能动登录用户自己的那份。
    int GetTargetKind() const { return m_targetKind; }
    // 取本次目标的 id：目标是群时为 groupid，是用户时为该用户 userid。
    __int64 GetTargetId() const { return m_targetId; }

    /**
     *  本对话框是不是正等着一条"改群头像"的应答。
     *
     *  改群头像与群管理页的「保存修改」共用 cmd_update_group_info，应答体里认不出
     *  是哪一个发的；主面板据本方法把两者分开，为真时才把应答转进来。
     *
     *  @return 三者同时成立才为真：窗口还开着、本次目标是群、已提交且未收到应答。
     */
    bool IsWaitingGroupAvatarResult() const
    {
        return IsWindow() && m_targetKind == target_group && m_submitting;
    }

    BEGIN_MSG_MAP(CBalloonModifyAvatarDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（预览 + 三段切换 + 三页内容 + 底部按钮）。
    void BuildUiOnce();
    void BuildSystemPage(balloonwjui::DuiVBox* host);   // 系统头像页：预置磁贴网格
    void BuildLetterPage(balloonwjui::DuiVBox* host);   // 首字母页：输入框 + 12 色点
    void BuildUploadPage(balloonwjui::DuiVBox* host);   // 本地上传页：占位（第二期接通）

    /**
     *  两个 Open 入口的共用实现：建窗（或前置）、按目标已存头像解码出预选、
     *  刷新预览并显示。
     *    targetKind：目标种类，取值见 TargetKind。
     *    targetId：目标 id（用户 userid 或群 groupid），用于自动头像取底色。
     *    targetName：目标显示名（昵称 / 群名），用于自动头像取首字；可为 NULL。
     *    facetype / customfaceUtf8：目标已存的头像，决定初始页与预选磁贴。
     *    selfUserId：当前登录用户 id，上传缓存分桶用（目标是用户时与 targetId 相同）。
     */
    void OpenForTarget(int targetKind, __int64 targetId, LPCTSTR targetName,
                       int facetype, const std::string& customfaceUtf8,
                       __int64 selfUserId);

    // 切到第 kind 页（0 系统 / 1 首字母 / 2 上传）：切可见页 + 刷新分段按钮选中态。
    void ShowPage(int kind);
    // 按 m_page 刷新三个分段按钮的主/次配色（选中=Primary 橙）。
    void ApplyTabVisual();
    // 用 m_glyph / m_colorIndex 重生成预览位图并刷新。
    void RefreshPreview();
    // 选中第 idx 枚系统磁贴 → 同步 m_glyph / m_colorIndex / 选中态 + 刷新预览。
    void SelectSystemPreset(int idx);
    // 选中「默认」磁贴 → 置 m_defaultSelected、清掉预置磁贴选中态 + 刷新预览。
    // 提交时发 facetype=0 + 空 customface，让服务端把头像退回自动渲染。
    void SelectDefaultTile();
    // 按当前目标（名字首字 + id 取色）重画「默认」磁贴的位图。目标变了要重画，
    // 否则改完自己的头像再去改群头像时，这枚磁贴还画着上一个目标的样子。
    void RefreshDefaultTile();
    // 选中第 idx 个底色点 → 同步 m_colorIndex / 选中态 + 刷新预览。
    void SelectColorDot(int idx);
    // 首字母输入框文字变化 → 同步 m_glyph + 刷新预览。
    void OnLetterChanged();
    // 本地上传页点击拖拽框：弹文件对话框选图 → 校验 → 处理 → 刷新预览。
    void OnPickLocalImage();
    // "使用此头像"：按当前页分流（字形页发 cmd_change_avatar；上传页先传图）。
    void DoUse();
    // 生成一张 sizePx 圆角方形「字形 + 底色」位图（磁贴 / 预览共用）。
    HBITMAP MakeGlyphBitmap(LPCTSTR glyph, int colorIndex, int sizePx);
    // 生成一张"当前目标的自动头像"位图（名字首字 + 按目标 id 取色），供「默认」
    // 磁贴与选中它时的预览共用。底色不来自调色板槽位，故不能用 MakeGlyphBitmap。
    HBITMAP MakeAutoAvatarBitmap(int sizePx);
    // 同步底色点选中态到 m_colorIndex（系统磁贴 / 首字母切换时调）。
    void SyncColorDotsToCurrent();

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），引用不持有

    bool m_built;          // 骨架是否已构建
    bool m_submitting;     // 是否已发出改像请求、等待应答（防重复提交）

    int     m_page;          // 当前页：0 系统 / 1 首字母 / 2 上传
    CString m_glyph;         // 待应用字形（1~2 字符）
    int     m_colorIndex;    // 待应用底色槽下标 [0,kAvatarPickerSize)

    int     m_targetKind;    // 本次改谁的头像，取值见 TargetKind
    __int64 m_targetId;      // 目标 id：用户 userid 或群 groupid（自动底色取色用）
    CString m_targetName;    // 目标显示名：昵称或群名（回退字形 / 首字母页默认值用）
    // 当前登录用户 id。<u>与 m_targetId 不是一回事</u>：改群头像时前者是登录者、
    // 后者是群。上传头像图片按登录者分桶落地本地缓存，只能用这一个。
    __int64 m_selfUserId;

    int     m_selectedPreset;// 当前选中的系统磁贴下标（-1 无）
    // 是否选中了「默认」磁贴。为真时"使用此头像"发 facetype=0 + 空 customface，
    // 把头像退回按名字首字自动渲染，而不是发当前预览里的字形。
    bool    m_defaultSelected;

    int         m_appliedFacetype;   // 上次"使用此头像"发出的 facetype（成功后供主面板写回）
    std::string m_appliedCustomface; // 上次"使用此头像"发出的 customface（UTF-8）

    // 控件指针（借用，控件树持有）。
    balloonmain::AvatarPickerTile* m_preview;        // 顶部预览大头像（自持位图）
    balloonwjui::DuiButton*        m_tabBtns[3];     // 三个分段按钮
    balloonwjui::DuiControl*       m_pages[3];       // 三页内容容器（show/hide 切换）
    balloonwjui::DuiControl*       m_pageHost;       // 页容器（切页后 ForceLayout 用）
    balloonwjui::DuiEdit*      m_letterEdit;     // 首字母输入框
    balloonmain::CUploadDropZone*  m_uploadZone;    // 本地上传页的拖拽框（虚线框；未选图显示图标+文案，选图后框内显示真图）

    CString m_pendingAvatarFile;   // 本地上传：已处理好、待上传的本地图片完整路径（空=未选）
    bool    m_hasPendingUpload;    // 本地上传：是否有待上传图片
    // 系统头像页第一枚「默认」磁贴（借用，控件树持有）。它<u>不</u>进
    // m_systemTiles —— 那个向量的下标要与 kSystemPresets 一一对应。
    balloonmain::AvatarPickerTile* m_defaultTile;
    std::vector<balloonmain::AvatarPickerTile*> m_systemTiles;  // 系统头像磁贴
    std::vector<balloonmain::AvatarPickerTile*> m_colorDots;    // 首字母底色点

    // 标题栏图标位图（程序图标合成到标题栏底色上的 HBITMAP）；本类持有、析构释放。
    HBITMAP m_titleIconBmp;
};
