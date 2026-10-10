/**
 *  GroupLinkFlow.h
 *
 *  加群链接（docs/邀请入群方案.md §12）在 PC 客户端的纯逻辑：点开链接后按查询结果决定下一步、各种提示与确认文字、
 *  「生成加群链接」窗口的有效期选项与到期时间文字、该窗口「我生成的链接」列表的数据与作废结果、1341 / 1342 应答的去向、
 *  从启动命令行里找出链接、注册 flamingo:// 协议用的命令串。
 *  不依赖窗口与网络，单测见 Source/Tests/GroupLinkTests.cpp。
 *
 *  典型用法（主面板收到 1343 应答后）：
 *      const int action = DecideLinkInfoAction(r.m_code, r.m_member, r.m_joinmode);
 *      if (action == kLinkActionConfirmJoin && Confirm(JoinLinkConfirmText(name, r.m_membercount, r.m_joinmode)))
 *          CIUSocket::GetInstance().SendJoinGroupByLink(code);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net { struct GroupLinksResult; }

namespace balloonmain {

// 「生成加群链接」窗口有效期下拉的选项个数：1 天、7 天、永久
const int kGroupLinkValidityChoiceCount = 3;
// 有效期下拉默认选中的下标：7 天
const int kGroupLinkValidityDefaultIndex = 1;

// WM_COPYDATA 的 dwData：第二个客户端进程把启动命令行里的加群链接交给已运行的客户端（ASCII 'FJGL'）
const unsigned long kCopyDataJoinGroupLink = 0x464A474C;

// 点开加群链接、查到群之后的下一步
enum LinkInfoAction
{
    // 本人已在群里：直接打开群聊窗
    kLinkActionOpenGroup = 0,
    // 可以加入（自由加入或需审批）：弹确认框，确认后发 1344
    kLinkActionConfirmJoin = 1,
    // 群设置为禁止加入：提示后结束
    kLinkActionForbidden = 2,
    // 查询失败（链接无效、不是同一企业等）：按错误码提示后结束
    kLinkActionError = 3
};

/**
 *  按 1343 应答决定下一步。
 *    code：    应答结果码；member：本人是否已在群里；joinMode：群的加入方式（0 自由 1 审批 2 禁止）。
 *  @return 取值见 LinkInfoAction
 */
int DecideLinkInfoAction(int code, bool member, int joinMode);

/**
 *  确认加入的提示文字：自由加入「确定加入群聊「{群名}」（{N} 人）吗？」，
 *  需审批时说明要等群主或管理员同意。
 *    groupName：群名；memberCount：成员数；joinMode：0 自由、1 审批。
 */
std::wstring JoinLinkConfirmText(const std::wstring& groupName, int memberCount, int joinMode);

/**
 *  链接相关操作失败时的提示（已翻译）：链接失效、不是同一企业、群禁止加入、不是群成员等；
 *  认不出的错误码带上错误码。
 */
std::wstring GroupLinkErrorText(int code);

/**
 *  凭链接加群成功但需要审批时的提示：「已提交加群申请，等待群主或管理员审批」。
 */
std::wstring JoinLinkPendingText();

/**
 *  有效期下拉第 index 项的天数：0 → 1 天，1 → 7 天，2 → 永久（0）；越界按默认的 7 天。
 */
int GroupLinkDaysByChoice(int index);

/**
 *  有效期下拉第 index 项的文字：「1 天」「7 天」「永久有效」；越界返回空串。
 */
std::wstring GroupLinkChoiceText(int index);

/**
 *  链接到期时间的说明：0 为「永久有效」，否则「有效期至 MM-DD HH:mm」（本地时间）。
 *    expireTimeMs：到期时间，Unix 毫秒。
 */
std::wstring GroupLinkExpireText(int64_t expireTimeMs);

// ---- 「生成加群链接」窗口的「我生成的链接」列表（2026-10-07 补：普通成员进不了群管理窗，在这里查看并作废自己生成的链接）----

// 「我生成的链接」最多显示的条数；窗口按它给每行的「复制」「作废」按钮分配 ctrlId 段
const int kMyGroupLinkRowMax = 100;

// 「我生成的链接」列表的加载状态
enum MyGroupLinksLoadState
{
    // 已发出 1341、还没有收到过成功的应答：列表处显示「正在加载…」
    kMyLinksLoading = 0,
    // 至少成功加载过一次：显示链接，没有链接时显示「还没有生成过」
    kMyLinksLoaded = 1,
    // 从没加载成功过、最近一次应答失败：列表处显示「没有加载出来」
    kMyLinksFailed = 2
};

// 「我生成的链接」里的一行
struct MyGroupLinkRow
{
    int64_t      m_id;         // 链接ID，作废时按它定位
    std::wstring m_url;        // 完整链接文字，「复制」写进剪贴板的就是它
    std::wstring m_metaText;   // 第二行的说明：「10-07 14:32 生成 · 有效期至 10-14 14:32」或「… · 永久有效」

    MyGroupLinkRow()
        : m_id(0)
    {
    }
};

// 「我生成的链接」列表：生成窗口持有，每次打开窗口、生成或作废之后按 1341 应答刷新
struct MyGroupLinkList
{
    int                         m_state;   // 加载状态，取值见 MyGroupLinksLoadState
    std::vector<MyGroupLinkRow> m_rows;    // 本人生成、仍然有效的链接，顺序与服务端一致（生成时间倒序），最多 kMyGroupLinkRowMax 条

    MyGroupLinkList()
        : m_state(kMyLinksLoading)
    {
    }
};

/**
 *  把 1341 应答并入「我生成的链接」。
 *    result：    1341 应答解析结果；
 *    groupId：   生成窗口当前显示的群ID，应答的群号与它不一致时不改列表（失败的应答可能不带群号，
 *               群号为 0 的失败应答按当前这个群处理）；
 *    selfUserId：本人 userid。群主、管理员收到的是本群全部链接，这里只留生成者是本人的；
 *    list：      要更新的列表（出入参）。
 *  成功时换成本人生成的链接（超过 kMyGroupLinkRowMax 条的截掉）并置为已加载；失败时，已加载过的保留原内容，
 *  从没加载成功过的置为加载失败。
 *  @return 应答属于这个群、列表已按它处理时为 true；群号不一致时为 false
 */
bool ApplyMyGroupLinks(const net::GroupLinksResult& result, int64_t groupId, int64_t selfUserId, MyGroupLinkList& list);

/**
 *  列表区域没有链接可列时显示的文字：正在加载、没有加载出来、还没有生成过。列表里有链接时返回空串。
 */
std::wstring MyGroupLinksPlaceholderText(const MyGroupLinkList& list);

/**
 *  作废结果（1342 应答）在生成窗口状态行显示的文字（已翻译）。
 *    code：应答结果码。0 为已作废；6 为不是本人生成的（只能作废自己生成的）；316 为链接已经失效；
 *          其余按 GroupLinkErrorText 说明原因。
 */
std::wstring RevokeResultStatusText(int code);

// 1341 应答的去向：生成窗口与群管理窗各自按群号匹配，两边看的是同一个群时都给
struct GroupLinksDelivery
{
    bool m_toDialog;         // 交给「生成加群链接」窗口
    bool m_toManageWindow;   // 交给群管理窗

    GroupLinksDelivery()
        : m_toDialog(false)
        , m_toManageWindow(false)
    {
    }
};

/**
 *  决定一条 1341 应答交给谁。
 *    resultGroupId：      应答里的群ID；
 *    dialogGroupId：      生成窗口当前显示的群ID，窗口没打开时为 0；
 *    manageWindowGroupId：开着的群管理窗管的群ID，没打开时为 0。
 */
GroupLinksDelivery DecideGroupLinksDelivery(int64_t resultGroupId, int64_t dialogGroupId, int64_t manageWindowGroupId);

/**
 *  GroupLinkRevokeTracker —— 记录生成窗口发出、还没有收到应答的作废请求。
 *
 *  1342 的应答体里没有链接ID，也没有群号，主面板只能靠「生成窗口是不是正等着作废结果」区分这条应答该交给
 *  生成窗口还是群管理窗（与修改头像对话框等 1311 应答的做法相同）。同一时刻只登记一次作废；两个窗口恰好同时
 *  作废时，先到的那条应答归生成窗口，后到的归群管理窗，两边收到应答后都会重拉列表，显示不会出错。
 *
 *  典型用法：
 *      m_revokeTracker.Begin(linkId);
 *      CIUSocket::GetInstance().SendRevokeGroupLink(groupId, linkId);
 *      //主面板收到 1342 应答：
 *      if (m_groupLinkDlg.TakeRevokeResult(code)) return;   // 内部调用 TakeResult()
 */
class GroupLinkRevokeTracker
{
public:
    GroupLinkRevokeTracker();

    /**
     *  发出 1342 之前登记。已在等另一条作废结果时覆盖为这一条。
     *    linkId：要作废的链接ID，须大于 0。
     */
    void Begin(int64_t linkId);

    /**
     *  撤销登记（生成窗口换成另一个群时调用）：之后到达的 1342 应答照旧交给群管理窗。
     *  窗口只是隐藏时不撤销：之后到达的应答仍归生成窗口，不会被误当成群管理窗的作废结果。
     */
    void Cancel();

    /**
     *  是否正在等作废结果。
     */
    bool IsWaiting() const;

    /**
     *  正在等作废结果时的链接ID；没在等时为 0。
     */
    int64_t LinkId() const;

    /**
     *  收到一条 1342 应答时调用。
     *  @return 正在等作废结果时清掉登记并返回 true（这条应答归生成窗口）；没在等时返回 false，登记不变
     */
    bool TakeResult();

private:
    int64_t m_linkId;   // 正在等结果的作废请求所针对的链接ID；0 表示没有在等
};

/**
 *  从启动命令行里找出加群链接（flamingo://joingroup?...），带不带引号都可以；没有时返回空串。
 *  系统按注册的命令「"exe" "%1"」启动程序时，链接作为一个带引号的参数出现。
 */
std::wstring FindJoinUrlInCommandLine(const std::wstring& commandLine);

/**
 *  注册 flamingo:// 协议时 shell\open\command 的值：「"<exe 路径>" "%1"」。
 */
std::wstring BuildProtocolOpenCommand(const std::wstring& exePath);

} // namespace balloonmain
