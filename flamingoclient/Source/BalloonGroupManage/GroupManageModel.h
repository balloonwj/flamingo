/**
 *  GroupManageModel.h
 *
 *  群管理对话框的数据模型 —— 群基本信息、成员列表、加群申请、禁言成员、
 *  群权限设置等。本轮（先做 UI）由 GroupManageMockData 用假数据填充，
 *  后续接入 v2 群管理协议时只需替换数据来源、界面层不动。
 *
 *  对照 design/群管理1.png ~ 群管理7.png。本轮只覆盖群主全功能视图，
 *  管理员受限视图（图 7 的群公告页与按钮门控）留待下一轮。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <string>   //群头像 customface 原值按 UTF-8 原样保存，不转宽字符
#include <vector>

namespace balloongroupmanage {

// =====================================================================
// 群成员角色
// =====================================================================
//
// 决定成员在「成员管理」页里被归入哪个分组、显示哪种角色徽章，也决定
// 当前登录者能看到哪些页面与操作（角色门控留待下一轮）。
enum GroupRole
{
    kRoleOwner  = 0,    // 群主 —— 全部权限，每群唯一一人
    kRoleAdmin  = 1,    // 管理员 —— 由群主指派，权限受限
    kRoleMember = 2     // 普通成员 —— 无管理权限
};

// =====================================================================
// 在线状态（头像右下角状态点）
// =====================================================================
enum MemberOnline
{
    kOnlineNone    = 0,    // 不显示状态点
    kOnlineOnline  = 1,    // 在线（绿点）
    kOnlineOffline = 2     // 离线（灰点）
};

// =====================================================================
// 加群申请来源
// =====================================================================
//
// 显示在申请卡片的「来源：xxx」一行，对照 design/群管理2.png。
enum JoinSource
{
    kJoinFromFriend = 0,    // 好友邀请（可附带推荐人）
    kJoinFromSearch = 1,    // 群搜索
    kJoinFromQrCode = 2     // 扫码加群
};

// =====================================================================
// 数据结构
// =====================================================================

// 一名群成员。对照 design/群管理1.png 的成员卡片。
struct GroupMember
{
    CString   name;          // 昵称（卡片主标题）。有群名片时用群名片，否则用账号昵称
    CString   title;         // 职务 / 备注（卡片副标题，如「资深设计师」）
    CString   avatarText;    // 头像内显示的字（无图时的回退缩写）
    COLORREF  avatarColor;   // 头像底色
    GroupRole role;          // 角色（决定分组与角色徽章）
    int       online;        // 在线状态，取值见 MemberOnline

    // 成员 userid。右键菜单里的移出 / 设管理员 / 禁言都按它定位目标，
    // 也用于判断"这一行是不是我自己"。0 表示假数据（调试入口填充的）。
    int64_t   userid;

    // 是否处于禁言中。true 时成员卡显示禁言标记、右键菜单显示"解除禁言"。
    bool      muted;

    // 禁言到期时间，Unix毫秒。muted 为 true 且本字段为 0 表示永久禁言；
    // 大于 0 时界面显示"剩余 N 分钟"。muted 为 false 时无意义。
    int64_t   muteUntil;

    CString   muteReason;    // 禁言原因；未禁言时为空

    GroupMember()
        : avatarColor(RGB(0xFF, 0x7A, 0x45))
        , role(kRoleMember)
        , online(kOnlineNone)
        , userid(0)
        , muted(false)
        , muteUntil(0)
    {
    }
};

// 一条加群申请。对照 design/群管理2.png 的申请卡片。
struct JoinRequest
{
    CString   name;          // 申请人昵称
    CString   account;       // 申请人账号（如「23451234」）
    CString   avatarText;    // 头像字
    COLORREF  avatarColor;   // 头像底色
    CString   message;       // 验证留言（带引号显示）
    int       source;        // 来源，取值见 JoinSource
    CString   recommender;   // 推荐人昵称（仅好友邀请时可能非空）
    CString   timeText;      // 申请时间的展示文本（如「10 分钟前」）

    // 申请ID。同意 / 拒绝时按它定位，0 表示假数据。
    int64_t   requestid;

    // 申请人 userid，用于同意后在成员列表里找到他。
    int64_t   userid;

    JoinRequest()
        : avatarColor(RGB(0x6C, 0x7A, 0xE0))
        , source(kJoinFromSearch)
        , requestid(0)
        , userid(0)
    {
    }
};

// 一名被禁言成员。对照 design/群管理3.png 的禁言卡片。
struct MutedMember
{
    CString  name;           // 成员昵称
    CString  avatarText;     // 头像字
    COLORREF avatarColor;    // 头像底色
    CString  reason;         // 禁言原因（如「频繁刷屏」）
    CString  remainingText;  // 剩余时长展示文本（如「剩余 18 分钟」）
    bool     permanent;      // true = 永久禁言（只显示「解除」，不显示「延长」）

    int64_t  userid;         // 被禁言成员的 userid，解除禁言时按它定位；0 表示假数据

    MutedMember()
        : avatarColor(RGB(0x8A, 0x8A, 0x8A))
        , permanent(false)
        , userid(0)
    {
    }
};

// =====================================================================
// 群公告
// =====================================================================

// 公告优先级。只影响公告卡上那枚徽章的文案与配色，不改变任何收发行为
// （与服务端 group_announcement_severity 同序）。
enum AnnounceSeverity
{
    kSeverityNormal    = 0,    // 通知（默认档，灰底徽章）
    kSeverityImportant = 1,    // 重要（橙底徽章）
    kSeverityUrgent    = 2     // 紧急（红底徽章）
};

// 群管理窗「历史公告」列表每页显示几条。
//
// 取 8 而不是把全部铺开：公告攒到几百条时，一次性建几百张卡片进控件树，
// 每次发布 / 删除又要整页重建，界面会明显卡；而 8 条正好填满那一页的可视高度，
// 用户一眼扫得完。翻页交互与群文件面板同一套。
const int kAnnouncePageSize = 8;

// 一条群公告。对照 design/群管理7.png 的历史公告条目与 design/群公告.png 的公告卡。
//
// 没有"状态"字段是刻意的：公告只有"在"与"不在"两种情形 —— 删除是真删，删掉的
// 那一条从库里、从所有人的列表里一起消失，不存在"已撤回但还留着"的中间态。
struct GroupAnnouncement
{
    int64_t  id;             // 公告ID，删除 / 重新发布时按它定位；0 表示假数据
    CString  title;          // 公告标题（卡片主标题，粗体）
    CString  content;        // 公告正文
    int      severity;       // 优先级，取值见 AnnounceSeverity
    int64_t  publisher;      // 发布人 userid
    CString  publisherName;  // 发布人昵称。服务端下发 —— 发布人可能已退群，本地查不到
    CString  timeText;       // 发布时间的展示文本（如「今天 14:32」「5/28 09:00」）

    GroupAnnouncement()
        : id(0)
        , severity(kSeverityNormal)
        , publisher(0)
    {
    }
};

// 一条群敏感词。对照"管理词库"二级界面里的词条行。
struct SensitiveWord
{
    int64_t id;        // 词条ID，删除时按它定位；0 表示假数据
    CString word;      // 敏感词原文
    CString timeText;  // 添加时间的展示文本（如「3 天前」）

    SensitiveWord()
        : id(0)
    {
    }
};

// 群权限设置项的当前取值。对照 design/群管理5.png。
struct GroupPermissions
{
    int  joinMethodIndex;       // 加群方式下拉选中项（0=需要验证消息，1=允许任何人，2=禁止任何人）
    int  atAllScopeIndex;       // 「谁能 @ 全体成员」下拉选中项（0=仅群主和管理员，1=所有人）
    bool newMemberSeeHistory;   // 新成员查看历史消息
    bool memberInviteFriend;    // 成员邀请好友加群（无需审批）
    bool welcomeMessage;        // 入群欢迎语
    bool sensitiveWordFilter;   // 消息敏感词过滤

    GroupPermissions()
        : joinMethodIndex(0)
        , atAllScopeIndex(0)
        , newMemberSeeHistory(true)
        , memberInviteFriend(false)
        , welcomeMessage(true)
        , sensitiveWordFilter(true)
    {
    }
};

// =====================================================================
// 群管理数据模型
// =====================================================================
//
// 聚合一个群的全部管理数据，供 GroupManageLayout 构建各页界面、供
// 窗口在交互后读写。本轮全部由 GroupManageMockData 填充。
class GroupManageModel
{
public:
    GroupManageModel();

    // ---- 群基本信息（顶部 Header + 基本信息页）----

    // 群名称（如「设计组·周会」）。Header 标题与基本信息页同源。
    CString  m_groupName;        // 群名称
    CString  m_groupAvatarText;  // 群头像字（如「设」）；真图未就绪时也用它占位
    COLORREF m_groupAvatarColor; // 群头像底色

    // 群头像种类，取值见 balloonmain::AvatarKind（0 自动 / 1 字形 / 2 自定义图）。
    // 由 ApplyGroupInfo 从群资料的 (facetype, customface) 解码而来，脏数据一律
    // 落回 0，保证渲染点永远有字形 + 底色可画。
    int      m_groupAvatarKind;
    // 自定义图头像的文件ID（m_groupAvatarKind == 2 时有效，其余为空）。
    // 本层<u>只</u>存这个 id：真图的本地缓存路径要查文件、没命中还要发下载请求，
    // 都不是纯逻辑，故留给窗口层用它去 ImageTransfer 里解析。
    CString  m_groupAvatarFileId;

    // 群头像的<u>协议原值</u>，与上面几项解码结果并存。打开「修改群头像」对话框时
    // 要拿它去预选当前头像 —— 解码结果丢掉了色号等信息，反推不回来。
    // customface 保持 UTF-8 不转码：它原样回传给服务端，多一道转换只多一次出错机会。
    int          m_groupFacetype;
    std::string  m_groupCustomfaceUtf8;

    // 自定义图头像已下到本地时的完整路径；空=画字形 + 底色。
    // 由窗口层（CBalloonGroupManageWindow::ResolveGroupAvatarImage）填 —— 查缓存、
    // 没命中就发下载请求，都要碰文件与网络，不属于本模型的纯逻辑映射范围。
    CString  m_groupAvatarImagePath;

    CString  m_groupNumber;      // 群号（如「881234567」），只读
    CString  m_announcement;     // 群公告正文
    CString  m_intro;            // 群简介（可选）
    GroupRole m_myRole;          // 当前登录者在本群的角色，决定各页可见性与可操作项

    // 入群欢迎语正文。它与 m_perms.welcomeMessage（开关）配套：开关关掉时
    // 正文仍保留在库里，下次打开还是这句，不必重打。
    CString  m_welcomeText;

    // ---- 各列表数据 ----

    std::vector<GroupMember> m_members;   // 全部成员（按角色分组展示）
    std::vector<JoinRequest> m_requests;  // 待处理加群申请
    std::vector<MutedMember> m_muted;     // 被禁言成员

    // 本群敏感词库（"管理词库"二级界面用）。只有进入该界面时才会去拉，
    // 平时为空 —— 主界面各页都不显示它。
    std::vector<SensitiveWord> m_sensitiveWords;

    // 敏感词库是否已经拉取过。用来区分"还没拉"与"拉过但是空的"：前者显示
    // "加载中"，后者显示"还没有敏感词"。
    bool m_sensitiveWordsLoaded;

    // 本群公告的<u>当前这一页</u>，按发布时间倒序，最新的在前。
    //
    // 公告可能有几百上千条，一次全拉回来既慢、又要把几百张卡片全建进控件树。
    // 故这里只存一页（kAnnouncePageSize 条），翻页时重新向服务端要。
    std::vector<GroupAnnouncement> m_announcements;

    // 本群公告总条数（服务端给的 total，不受分页限制）。
    // 底部「共 N 条 · 第 x/y 页」的 N 与页数、以及左侧导航那个计数徽章都用它 ——
    // 不能拿 m_announcements.size() 顶替，那只是本页的条数。
    int m_announcementTotal;

    // 当前停在第几页（0 起）。翻页按钮增减，列表刷新后会夹回合法范围。
    int m_announcementPage;

    // 公告是否已经拉取过。同 m_sensitiveWordsLoaded 的用途：区分"还没拉"
    // （显示加载中）与"拉过但是一条都没有"（显示空状态提示）。
    bool m_announcementsLoaded;

    // 「发布新公告」表单上当前选中的优先级，取值见 AnnounceSeverity。
    //
    // 它必须存在模型里而不是只留在控件上：优先级是用三个按钮做的三选一，选中态
    // 靠"哪个按钮画成主色"表达，而每次点击都要整页重建才能换掉配色 —— 重建后
    // 控件是新造的，选中态只能从模型里取回来。标题与正文没有同样存进模型：
    // GroupManageLayout.cpp 的 BuildPage 只给新造的输入框设占位文字、不回填
    // 已输入内容，所以整页重建后这两项会回到空白。
    int m_draftSeverity;

    // ---- 权限 ----

    GroupPermissions m_perms;    // 群权限设置当前取值
    bool             m_muteAll;  // 全员禁言开关（禁言管理页右上角）

    // ---- 派生统计 ----

    // 成员总数（= m_members.size()）。供 Header 副标题「N 名成员」与左侧
    // 「成员管理」计数徽章使用。
    int MemberCount() const;

    // 待处理加群申请数（= m_requests.size()）。供左侧「加群审批」计数徽章。
    int PendingRequestCount() const;

    // 被禁言成员数（= m_muted.size()）。供左侧「禁言管理」计数徽章。
    int MutedCount() const;

    // 公告总页数（按 kAnnouncePageSize 分页）。供底部「第 x/y 页」的分母。
    // 一条都没有时返回 1 —— 显示"第 1/0 页"读不通。
    int AnnouncementPageCount() const;

    // 按角色统计成员数量。供「成员管理」页各分组标题「群主·1」等。
    //   role：要统计的角色。
    //   返回：该角色的成员数量。
    int CountByRole(GroupRole role) const;
};

// =====================================================================
// 假数据
// =====================================================================

// 用 design/群管理1.png ~ 6.png 中的示例内容填充一份群主视图的模型。
//   model：[出参] 待填充的模型，调用前其内容会被清空覆盖。
void FillMockData(GroupManageModel& model);

} // namespace balloongroupmanage
