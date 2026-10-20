/**
 *  SearchDropdown.h
 *
 *  主面板搜索框下方的"搜索结果下拉"。本控件<u>不</u>自带输入框 —— 用户在主面板
 *  既有的搜索框里输入，本下拉只负责把命中结果紧贴搜索框下方展示。
 *
 *  窗口形态：顶层无边框浮窗（WS_POPUP），窗口类带 CS_DROPSHADOW 系统投影，使它
 *  像下拉/菜单那样明显浮于内容之上（白底卡片在浅色内容上靠投影区分层次）。建窗
 *  时加 WS_EX_NOACTIVATE，故点击下拉<u>不抢</u>主面板的激活与键盘焦点：用户在
 *  主面板搜索框里能连续输入，下拉只是浮层。宿主能力复用 balloonui DuiHost（以
 *  SubclassWindow 方式接管自建的 HWND）。
 *
 *  内部结构：顶部分类筛选标签栏（全部 / 联系人 / 群组 / 消息 / 文件）+ 结果滚动区
 *  （SearchResultScrollView，滚动条为聊天窗样式的细滑块）
 *  （命中的联系人 / 群组行，命中昵称/群名时黄底高亮）。匹配
 *  复用 contactsearch 的匹配规则（子串 + 拼音首字母 + 全拼，大小写不敏感）。各候选
 *  与查询词无关的预处理结果（显示名、小写形式、首字母串、音节序列）保存在
 *  SearchCandidateCache 里，每次按键只做比对与匹配，不再逐个现算。
 *
 *  通知路由：本窗口不是 WS_CHILD 宿主，DuiHost::SendNotify 把控件通知发回本窗口（开发约定
 *  文档 UI 约定第 11 条），结果行 / 标签点击在本类 OnChildNotify 内自行处理（命中行 →
 *  回调对端 / 群 id 并隐藏；标签 → 切筛选刷新）。
 *
 *  宿主经 SetLocateCallback 提供「定位」回调后，联系人结果行会在鼠标悬停时于行右侧
 *  显示一枚定位图标（MainPanelListItem::SetLocateButtonVisible）；点它发出的是
 *  DUIN_LISTITEM_LOCATE 而非 DUIN_CLICK，本类据此回调宿主去通讯录树里定位这个人，
 *  而不是打开会话。群组结果行不提供该按钮。
 *
 *  「消息」标签：宿主经 SetMessageCallbacks 提供本地聊天记录的查询回调后启用。查询词
 *  变化后不立即查库，停止输入 kDelayedSearchMs 毫秒后才提交一次查询；查询在宿主持有的
 *  后台线程（balloonhistory::CV2MsgSearchWorker）里执行，结果经 kMsgLocalSearchDone 投回，
 *  只采纳最近一次提交的那个序号（本地库消息多时整表扫描要几百毫秒，放在界面线程上会卡住）。
 *  结果按会话汇总，每个会话一行：只命中一条时副行显示那句话并标黄
 *  关键字，命中多条时显示「N 条相关聊天记录」（文案见 GlobalSearchText.h）。点一行经回调
 *  交给宿主打开该会话的消息记录。只查本机已保存的记录，不向服务端发请求。
 *
 *  「文件」标签：宿主经 SetFileCallbacks 提供回调后启用，分两段。「聊天文件」一段查本地库
 *  里的文件、文件夹传输记录与文件共享卡片，与聊天记录在同一次延迟查询里查；「群文件」一段
 *  在同一时刻经回调请宿主向服务端搜索本人所在全部群的群文件，应答由宿主经
 *  OnGroupFileSearchResult 转交，回显的关键字与最近一次请求不同就丢弃（GroupFileSearchState）。
 *  没连上服务器时群文件一段显示「群文件需联网搜索」，聊天文件照常显示。行首是本机按类型取的
 *  系统图标（ShellTypeIcon）。本机找得到文件的聊天文件行、文件夹传输记录行在鼠标悬停时右侧
 *  出现「打开文件」「打开所在文件夹」小按钮（规则见 GlobalSearchText.h 的 PickFileHitActions），
 *  点了直接在本机打开，不去消息记录窗。
 *
 *  「部门」：宿主经 SetDepartments 提供部门列表与定位回调后，「全部」「联系人」两个标签在联系人
 *  之后列出名字匹配的部门（匹配规则同联系人，含拼音），副行是上级路径；点一行请宿主在通讯录树里
 *  展开并定位到该部门。
 *
 *  「备忘」：宿主经 SetMemos 提供备忘簿与打开回调后，「全部」标签在群组之后列出标题或备注含关键字的
 *  备忘（最多 kMemoHitLimit 条），点一行请宿主打开那条备忘。
 *
 *  「搜索服务器上的聊天记录」：「消息」标签在本机结果之后放一个按钮，点了经回调请宿主向服务端发
 *  cmd_global_search（本机只保存了部分聊天记录）。应答由宿主经 OnServerSearchResult 转交，回显的
 *  关键字与当前不同就丢弃（状态复用 GroupFileSearchState）。结果分「服务器上的聊天记录」（按会话
 *  汇总，点一行打开该会话的消息记录并跳到命中的那天）与「群公告」（点一行打开群聊窗的公告页）两段。
 *
 *  「最近搜索」：搜索框获得焦点、内容为空时，宿主调 ShowRecent 列出最近打开过结果的关键字
 *  （隐藏分类筛选标签栏），点一个关键字经回调请宿主填回搜索框，点「清空」经回调请宿主清空。
 *  打开任一搜索结果时经 RecordRecentCallback 把当时的查询词交给宿主记录。关键字的保存
 *  （按账号写进用户配置）与去重规则（balloonsearch::RecentSearchList）都在宿主一侧。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "SearchCandidateCache.h"   // balloonsearch::SearchCandidateCache
#include "GroupFileSearchState.h"   // balloonsearch::GroupFileSearchState（丢弃过期的群文件应答）
#include "SearchKeyboard.h"         // balloonsearch::SearchSelection（上下方向键的选中状态）
#include "../BalloonHistory/V2MsgQuery.h"   // balloonhistory::ConversationHit / HistoryRecord
#include "../../net/GroupFileProtocolClient.h"   // net::SearchGroupFilesResult / SearchedGroupFileItem
#include "../../net/GlobalSearchProtocolClient.h" // net::GlobalSearchResult（服务器上的聊天记录与群公告）

#include <stdint.h>
#include <string>
#include <vector>
#include <map>

namespace net { struct ContactItem; struct UserGroupItem; struct MemoItem; }
namespace balloonwjui { class DuiControl; class DuiScrollView; class DuiVBox; }
namespace balloonsearch { class SearchFilterTab; }
namespace balloonmain { class MainPanelListItem; }

/**
 *  CSearchDropdown —— 主面板搜索结果下拉（WS_CHILD 子窗口）。
 */
class CSearchDropdown : public balloonwjui::DuiHost
{
public:
    // 选中某联系人结果时的回调；peerUserId 为对端 userid。
    typedef void (*PickContactCallback)(void* userdata, UINT peerUserId);
    // 选中某群组结果时的回调；groupId 为群 id。
    typedef void (*PickGroupCallback)(void* userdata, int64_t groupId);
    // 选中某联系人结果行右侧的「定位」按钮时的回调；peerUserId 为对端 userid。
    typedef void (*LocateContactCallback)(void* userdata, UINT peerUserId);
    // 提交一次本地库后台查询的回调（宿主转交给它持有的 CV2MsgSearchWorker）。查询在后台线程
    // 执行，结果经 PostMessage(target, msg, 序号, V2SearchResponse*) 投回本窗口。宿主负责确保
    // 本地库已打开。kind 取值见 balloonhistory::V2SearchKind（本下拉只用会话汇总与文件消息两种）；
    // keywordUtf8 已去掉首尾空白、非空；limit 为最多返回几条。
    // 返回请求序号；本地库打不开、后台线程没在运行时返回 0，下拉按"没有命中"显示。
    typedef uint32_t (*SubmitLocalSearchCallback)(void* userdata, int kind, const std::string& keywordUtf8,
                                                  int limit, HWND target, UINT msg);
    // 选中「消息」标签某一行时的回调：ctype 为会话类型（1 单聊、2 群聊），cid 为会话 id
    // （单聊为对端 userid，群聊为群 id），keywordUtf8 为当时的查询词。
    typedef void (*PickMessageCallback)(void* userdata, int ctype, int64_t cid,
                                        const std::string& keywordUtf8);
    // 请宿主向服务端搜索群文件的回调。宿主没连上服务器、请求发不出去时返回 false；返回 true
    // 表示请求已发出，应答之后经 OnGroupFileSearchResult 转交回来。
    typedef bool (*RequestGroupFilesCallback)(void* userdata, const std::string& keywordUtf8, int limit);
    // 选中「聊天文件」一段某一行时的回调：该消息所在的会话（ctype、cid）、消息的 msgid 与
    // 消息类型 msgtype（文件、文件夹传输记录或文件共享卡片，取值见 net/Protocol.h 的
    // message_type），以及当时的查询词。
    typedef void (*PickFileMessageCallback)(void* userdata, int ctype, int64_t cid, int64_t msgid,
                                            int msgtype, const std::string& keywordUtf8);
    // 选中「群文件」一段某一行时的回调：群 id 与群文件条目 id（entryid，字符串编码的 int64）。
    typedef void (*PickGroupFileCallback)(void* userdata, int64_t groupId, const std::string& entryId);
    // 用户打开了一条搜索结果（点击或回车）时的回调，宿主据此把当时的查询词记进「最近搜索」。
    // keyword 为当时的查询词，已去掉首尾空白、非空。
    typedef void (*RecordRecentCallback)(void* userdata, const std::wstring& keyword);
    // 在「最近搜索」列表里点了某个关键字时的回调：宿主把它填进搜索框（填入后搜索框的内容
    // 变化通知会照常驱动 UpdateQuery 查一次）。keyword 为被点的关键字。
    typedef void (*PickRecentCallback)(void* userdata, const std::wstring& keyword);
    // 在「最近搜索」列表里点了「清空」时的回调：宿主清空该账号保存的最近搜索。
    typedef void (*ClearRecentCallback)(void* userdata);
    // 请宿主向服务端发全局搜索（cmd_global_search）的回调：关键字、最多几个会话、最多几条群公告。
    // 宿主没连上服务器、请求发不出去时返回 false；返回 true 表示已发出，应答之后经
    // OnServerSearchResult 转交回来。
    typedef bool (*RequestServerSearchCallback)(void* userdata, const std::string& keywordUtf8,
                                                int limit, int announcementLimit);
    // 点「服务器上的聊天记录」一行时的回调：会话（ctype、cid）、最近一条命中的时间（Unix 毫秒）
    // 与当时的查询词。宿主打开该会话的消息记录，按关键字筛选并跳到那天。
    typedef void (*PickServerMessageCallback)(void* userdata, int ctype, int64_t cid, int64_t timeMs,
                                              const std::string& keywordUtf8);
    // 点「部门」一段某一行时的回调：部门 id。宿主在通讯录树里展开并定位到该部门。
    typedef void (*LocateDepartmentCallback)(void* userdata, int64_t deptid);
    // 点「备忘」一段某一行时的回调：备忘 id。宿主打开那条备忘。
    typedef void (*PickMemoCallback)(void* userdata, int64_t memoid);
    // 点「群公告」一行时的回调：群 id 与公告 id。宿主打开群聊窗并切到公告页。
    typedef void (*PickAnnouncementCallback)(void* userdata, int64_t groupId, int64_t announcementId);
    // 点「消息」标签底部「在聊天记录管理器中查看全部」时的回调：宿主打开聊天记录管理器并按
    // keyword 跨会话搜索。keyword 为当时的查询词，已去掉首尾空白、非空。
    typedef void (*OpenMessageManagerCallback)(void* userdata, const std::wstring& keyword);

    CSearchDropdown();
    ~CSearchDropdown();

    /**
     *  创建本下拉浮窗（幂等，仅首次真正创建）。须在调用方 HWND 就绪后调。
     *    owner：归属窗口（主面板）HWND，仅用于记录 / 屏幕换算基准；窗口本身以
     *           无 owner 的顶层 WS_POPUP 创建（历史原因，见 .cpp）。
     *  @return 创建成功（或已创建）返回 true；失败返回 false。
     */
    bool EnsureCreated(HWND owner);

    /**
     *  设置搜索数据源与当前登录用户（均借用，不持有；调用方须保证生命周期覆盖
     *  本下拉使用期。一般直接传主面板自己持有的 m_contacts / m_myGroups）。
     *    contacts：联系人资料池指针。
     *    groups：本人加入的群列表指针。
     *    selfUserId：当前登录用户 userid。<u>注意它有两个用途</u>：一是按
     *                includeSelf 决定要不要把自己从联系人结果里滤掉，二是画头像
     *                时要用（ApplyRowAvatar 需要知道哪一行是自己）。所以想让
     *                结果包含自己时，<u>不能</u>把本参数传 0，要走 includeSelf。
     *    includeSelf：联系人结果里要不要包含自己。通讯录树已经会列出自己（好让
     *                用户看清自己在组织架构中的位置），搜索跟着放开才不别扭；
     *                命中自己的那一行显示名会加"（我）"后缀。
     */
    void SetData(const std::vector<net::ContactItem>* contacts,
                 const std::vector<net::UserGroupItem>* groups,
                 UINT selfUserId,
                 bool includeSelf);

    /**
     *  设置选中结果回调。
     *    contactCb：点联系人结果时触发。
     *    groupCb：点群组结果时触发。
     *    userdata：回调透传的用户数据（一般是主面板 this）。
     */
    void SetPickCallbacks(PickContactCallback contactCb,
                          PickGroupCallback groupCb, void* userdata);

    /**
     *  设置「定位」回调。本方法不另收 userdata —— 触发时透传的是 SetPickCallbacks
     *  存下的那一份，两者调用的先后顺序不限。
     *    locateCb：点联系人结果行右侧定位按钮时触发，参数为该行对应的对端 userid；
     *              传 nullptr 表示不提供该能力 —— 此时结果行不显示定位按钮，行为
     *              与改造前完全一致。
     *  副作用：本设置在下一次 RefreshResults 重建结果行时才生效（结果行是每次刷新
     *  整批重建的），已经在屏幕上的那一批行不会即时长出按钮。
     */
    void SetLocateCallback(LocateContactCallback locateCb);

    /**
     *  设置「消息」标签用的两个回调。透传的用户数据同样是 SetPickCallbacks 存下的那一份。
     *    submitCb：提交本地聊天记录的后台查询；传 nullptr 表示宿主不提供该能力，此时「消息」
     *              标签保持置灰、不可点。
     *    pickCb：点「消息」标签的结果行时触发；可传 nullptr（点了只收起下拉）。
     *  副作用：立即刷新「消息」标签的可用状态；已有的查询结果作废，下次输入时重新查询。
     */
    void SetMessageCallbacks(SubmitLocalSearchCallback submitCb, PickMessageCallback pickCb);

    /**
     *  设置「消息」标签底部「在聊天记录管理器中查看全部」的回调。透传的用户数据同样是
     *  SetPickCallbacks 存下的那一份。
     *    managerCb：点该按钮时触发；传 nullptr 表示宿主不提供该能力，此时不显示该按钮。
     *  副作用：同 SetLocateCallback，下一次 RefreshResults 时才生效。
     */
    void SetMessageManagerCallback(OpenMessageManagerCallback managerCb);

    /**
     *  设置「搜索服务器上的聊天记录」用的三个回调。透传的用户数据同样是 SetPickCallbacks 存下的那一份。
     *    requestCb：点「搜索服务器上的聊天记录」时触发；传 nullptr 表示宿主不提供该能力，此时不显示该按钮。
     *    pickMsgCb：点「服务器上的聊天记录」一行时触发；可传 nullptr（点了只收起下拉）。
     *    pickAnnCb：点「群公告」一行时触发；可传 nullptr。
     *  副作用：同 SetLocateCallback，下一次 RefreshResults 时才生效。
     */
    void SetServerSearchCallbacks(RequestServerSearchCallback requestCb, PickServerMessageCallback pickMsgCb,
                                  PickAnnouncementCallback pickAnnCb);

    /**
     *  宿主收到全局搜索应答（cmd_global_search）后转交给本下拉。回显的关键字与最近一次请求不同
     *  （用户已经接着输入了）、或下拉已收起时丢弃；否则保存结果，下拉可见时刷新。
     *    result：应答的解析结果，只读，本方法不持有它。
     */
    void OnServerSearchResult(const net::GlobalSearchResult& result);

    /**
     *  设置「文件」标签用的四个回调。透传的用户数据同样是 SetPickCallbacks 存下的那一份。
     *    submitCb：提交本地库文件类消息的后台查询；传 nullptr 时「聊天文件」一段不查。
     *    groupCb：请宿主搜索群文件；传 nullptr 时「群文件」一段不查。
     *             submitCb 与 groupCb 都为 nullptr 时「文件」标签置灰、不可点。
     *    pickFileCb：点「聊天文件」一段的行时触发；可传 nullptr（点了只收起下拉）。
     *    pickGroupFileCb：点「群文件」一段的行时触发；可传 nullptr。
     *  副作用：立即刷新「文件」标签的可用状态；已有的查询结果作废。
     */
    void SetFileCallbacks(SubmitLocalSearchCallback submitCb, RequestGroupFilesCallback groupCb,
                          PickFileMessageCallback pickFileCb, PickGroupFileCallback pickGroupFileCb);

    /**
     *  宿主收到群文件搜索应答（cmd_search_group_files）后转交给本下拉。回显的关键字与最近
     *  一次请求不同（用户已经接着输入了）、或下拉已收起时丢弃；否则保存结果，下拉可见时刷新。
     *    result：应答的解析结果，只读，本方法不持有它，调用方之后可以释放。
     */
    void OnGroupFileSearchResult(const net::SearchGroupFilesResult& result);

    /**
     *  设置「最近搜索」用的三个回调。透传的用户数据同样是 SetPickCallbacks 存下的那一份。
     *  关键字列表的读写都在宿主一侧（按账号保存在用户配置里），本下拉只负责显示与点击。
     *    recordCb：打开一条搜索结果时触发，传入当时的查询词；可传 nullptr（不记录）。
     *    pickCb：在最近搜索列表里点了某个关键字时触发；可传 nullptr（点了只收起下拉）。
     *    clearCb：点了「清空」时触发；可传 nullptr。
     */
    void SetRecentCallbacks(RecordRecentCallback recordCb, PickRecentCallback pickCb,
                            ClearRecentCallback clearCb);

    /**
     *  在搜索框下方展示「最近搜索」列表（搜索框获得焦点、内容为空时由宿主调用）。这时不显示
     *  分类筛选标签栏，只列一行标题（右侧带「清空」）和各个关键字。
     *    anchorScreenRect / panelWidth：口径同 UpdateQuery。
     *    keywords：要列出的关键字，最近使用的在前；为空时隐藏下拉。只读，本方法复制一份。
     *  副作用：停掉在途的延迟查询、作废已有的查询结果（同 HideDropdown）。
     */
    void ShowRecent(const RECT& anchorScreenRect, int panelWidth, const std::vector<std::wstring>& keywords);

    /**
     *  下拉当前是否在显示「最近搜索」列表（而不是搜索结果）。
     */
    bool IsShowingRecent() const;

    /**
     *  处理搜索框里的导航按键（主面板在搜索框有焦点、下拉在显示时转交过来）：
     *    上 / 下方向键：在结果行之间移动选中（选中行画悬停底色并滚动到可见）；
     *    回车：打开选中行，没有选中时打开第一行，效果与鼠标点击该行相同；
     *    Ctrl+Tab / Ctrl+Shift+Tab：循环切换筛选标签，跳过置灰的标签。
     *    vk：虚拟键码。ctrl / shift：按键时 Ctrl、Shift 是否按下。
     *  @return 按键被处理（调用方不必再交给搜索框）返回 true；不是这几个键、下拉没在显示、
     *          或回车时一行结果都没有，返回 false。
     *  副作用：回车打开结果时会收起下拉并调用相应的选中回调。
     */
    bool HandleNavigationKey(UINT vk, bool ctrl, bool shift);

    /**
     *  设置「userid → 部门路径」查询表，用于联系人结果行的副行展示。<u>借用不持有</u>，
     *  调用方须保证其生命周期覆盖本下拉的使用期；传 nullptr 表示没有部门信息，副行
     *  退回展示「账号 xxx · 地区」。
     *    deptPaths：userid → 部门路径（形如「技术中心 / 研发部 / 前端组」）。表内查不到
     *               的人、或路径为空串的人，同样退回展示账号与地区。
     *  副作用：同 SetLocateCallback，下一次 RefreshResults 时才生效。
     */
    void SetContactDeptPaths(const std::map<UINT, CString>* deptPaths);

    /**
     *  设置「部门」一段的数据源与点击回调。<u>借用不持有</u>，调用方须保证部门列表的生命周期覆盖
     *  本下拉的使用期（一般直接传主面板的 m_departments）。透传的用户数据同样是 SetPickCallbacks
     *  存下的那一份。
     *    departments：部门列表；传 nullptr 表示不搜部门。
     *    locateCb：点部门行时触发；传 nullptr 时同样不搜部门（点了也无处可去）。
     *  副作用：同 SetLocateCallback，下一次 RefreshResults 时才生效。
     */
    void SetDepartments(const std::vector<net::DepartmentItem>* departments, LocateDepartmentCallback locateCb);

    /**
     *  设置「备忘」一段的数据源与点击回调。<u>借用不持有</u>，调用方须保证列表的生命周期覆盖本下拉
     *  的使用期（一般传备忘中枢的备忘簿列表）。透传的用户数据同样是 SetPickCallbacks 存下的那一份。
     *    memos：备忘列表；传 nullptr 表示不搜备忘。
     *    pickCb：点备忘行时触发；传 nullptr 时同样不搜备忘。
     *  副作用：同 SetLocateCallback，下一次 RefreshResults 时才生效。
     */
    void SetMemos(const std::vector<net::MemoItem>* memos, PickMemoCallback pickCb);

    /**
     *  预热搜索候选缓存：按给定的联系人资料池与群列表执行一次 Sync，把本来要在第一次
     *  按键时才做的整批预处理提前完成。之后数据再变化，按键时的 Sync 照样会补上，因此
     *  本方法只影响第一次按键的快慢，不影响搜索结果。
     *    contacts：联系人资料池，须与随后经 SetData 绑定的是同一份数据（主面板的 m_contacts）。
     *    groups：  "我的群"列表，同上（主面板的 m_myGroups）。两者都只读取，不持有。
     *  不要求下拉窗口已创建。耗时与数据量成正比，数据量大时可能需要数百毫秒，
     *  调用方应安排在不影响界面绘制的时机（主面板在列表绘制完成后调用）。
     */
    void WarmUpCandidates(const std::vector<net::ContactItem>& contacts,
                          const std::vector<net::UserGroupItem>& groups);

    /**
     *  清空搜索候选缓存（登出时调用），释放上一个账号的联系人与群名字。下一次 Sync
     *  或预热会整批重建。
     */
    void ClearCandidates();

    /**
     *  设新查询词并据它在搜索框下方展示 / 刷新结果。查询词去首尾空白后为空则
     *  隐藏下拉（"空内容不显示"）。
     *    anchorScreenRect：搜索框矩形（<u>屏幕坐标</u>，调用方对 searchBox->GetRect()
     *                      做 ClientToScreen 后传入）。下拉左上角落在其左下角。
     *    panelWidth：主面板客户区宽（下拉与主面板同宽，逻辑像素）。
     *    query：当前查询词（UTF-16）。
     */
    void UpdateQuery(const RECT& anchorScreenRect, int panelWidth, LPCTSTR query);

    /**
     *  跟随宿主窗口移动 / 改变大小 —— 只重新摆放浮窗的位置与宽度，<u>不</u>重算搜索
     *  结果。本下拉是独立顶层浮窗、不是主面板的子窗口，宿主移动时系统不会带着它走，
     *  故主面板收到 WM_MOVE / WM_SIZE 时须调本方法把它拉回搜索框下方，否则拖动主面板
     *  会出现下拉留在原地、与主面板分离的现象。
     *  之所以不走 UpdateQuery：拖动过程中 WM_MOVE 高频到达，每次重算命中列表既无必要
     *  又会卡顿；结果没变，高度也不必变，沿用当前窗口高度即可。
     *    anchorScreenRect：搜索框矩形（<u>屏幕坐标</u>），口径同 UpdateQuery。
     *    panelWidth：主面板客户区宽（逻辑像素），下拉宽 = 它。
     *  下拉当前未创建或不可见时只记下新锚点，等下次展示时自然生效。
     */
    void FollowAnchor(const RECT& anchorScreenRect, int panelWidth);

    /**
     *  隐藏下拉（不销毁；下次 UpdateQuery 再显示）。
     */
    void HideDropdown();

    /**
     *  下拉当前是否可见。
     */
    bool IsShown() const;

    /**
     *  取下拉浮窗的 HWND（未创建返回 NULL）。供调用方做"光标是否落在下拉上"之类
     *  的命中判断（如失焦收起时排除点在下拉上的情形）。
     */
    HWND GetHwnd() const { return m_hWnd; }

    // 窗口私有消息：点「全部」标签段末的「查看更多」后，延后切换到对应标签。wParam 为目标
    // 筛选（FilterKind），lParam 不用。偏移登记见开发约定文档「UI 约定」第 6 条。
    static const UINT kMsgDeferredSwitchFilter = WM_APP + 0x6B;
    // 窗口私有消息：本地库后台查询（CV2MsgSearchWorker）的结果投回本窗口。wParam 为请求序号，
    // lParam 为堆上的 balloonhistory::V2SearchResponse*，处理完由本窗口 delete。
    static const UINT kMsgLocalSearchDone = WM_APP + 0x6C;
    // 窗口私有消息：在「最近搜索」列表里点了关键字或「清空」，延后处理（理由同
    // kMsgDeferredSwitchFilter：处理时会重建列表、销毁被点的按钮）。wParam 为关键字在
    // m_recentKeywords 里的下标，取 kRecentClearIndex 表示「清空」；lParam 不用。
    static const UINT kMsgDeferredRecentAction = WM_APP + 0x6D;
    // 窗口私有消息：点了「搜索服务器上的聊天记录」，延后发请求（理由同 kMsgDeferredSwitchFilter：
    // 发出后会重建列表、销毁被点的按钮）。wParam、lParam 不用。
    static const UINT kMsgDeferredServerSearch = WM_APP + 0x6E;

    BEGIN_MSG_MAP(CSearchDropdown)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(kMsgDeferredSwitchFilter, OnDeferredSwitchFilter)
        MESSAGE_HANDLER(kMsgLocalSearchDone, OnLocalSearchDone)
        MESSAGE_HANDLER(kMsgDeferredRecentAction, OnDeferredRecentAction)
        MESSAGE_HANDLER(kMsgDeferredServerSearch, OnDeferredServerSearch)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyWindow)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    // 当前分类筛选。
    enum FilterKind
    {
        kFilterAll      = 0,   // 全部（联系人 + 群组 + 聊天记录与文件的前几条）
        kFilterContacts = 1,   // 仅联系人
        kFilterGroups   = 2,   // 仅群组
        kFilterMessages = 3,   // 仅聊天记录（按会话汇总的本地聊天记录命中）
        kFilterFiles    = 4,   // 仅文件（本地的聊天文件 + 服务端搜出的群文件）
    };

    // 本窗口控件发出的 DUI 通知处理：结果行点击 → 回调对端 / 群 id 并隐藏；筛选标签点击 →
    // 切换筛选并刷新；其余通知放行（bHandled=FALSE 交回 DuiHost 链）。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 延迟查询的定时器到点：查本地库、请宿主搜群文件，并刷新结果。其它定时器放行。
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // kMsgDeferredSwitchFilter 的处理：下拉仍在显示时切到 wParam 指定的筛选。
    LRESULT OnDeferredSwitchFilter(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // kMsgLocalSearchDone 的处理：序号是最近一次提交的那一个才采纳（保存结果、下拉可见时刷新），
    // 其余（用户已接着输入、下拉已收起）丢弃。无论采纳与否都释放 lParam 上的结果。
    LRESULT OnLocalSearchDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // kMsgDeferredRecentAction 的处理：下拉仍在显示最近搜索时，按 wParam 回调宿主填入关键字
    // 或清空列表。
    LRESULT OnDeferredRecentAction(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // kMsgDeferredServerSearch 的处理：下拉仍在显示搜索结果时，经回调请宿主发全局搜索，按是否发出
    // 记下状态并刷新。
    LRESULT OnDeferredServerSearch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 追加一行居中的灰色提示文字（矮行高）。
    void AppendHintRow(balloonwjui::DuiVBox& content, LPCTSTR text);

    // 切换「最近搜索」与搜索结果两种显示：最近搜索时隐藏分类筛选标签栏。显隐变化后对根容器
    // 重新排版（标签栏是根容器的直接子控件，见开发约定文档「UI 约定」第 5 条）。
    void SetRecentMode(bool recent);

    // 打开一条搜索结果之前调用：经 RecordRecentCallback 把当前查询词交给宿主记录。
    void RecordCurrentQuery();

    // 「聊天文件」一段某行的悬停按钮被点击：收起下拉，在本机打开第 hitIndex 条结果的文件或
    // 所在文件夹。folderButton 为 true 表示点的是「打开所在文件夹」。本机路径取自
    // m_fileHitLocalPaths，为空时只收起下拉。
    void OpenFileHitLocally(size_t hitIndex, bool folderButton);

    // WM_DESTROY：取出消息队列里还没处理的后台查询结果并释放（窗口销毁后它们无人接收），
    // 然后放行给 DuiHost。
    LRESULT OnDestroyWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次构建控件树（筛选标签栏 + 结果滚动区）。
    void BuildContentOnce();

    // 按当前查询词 + 筛选重建结果列表，并刷新各标签计数；返回渲染出的结果行数。
    int RefreshResults();

    // 按渲染的分组数、结果行数与矮行数算下拉应有高度（逻辑像素，含标签栏与上限封顶）。
    //   sections：分组标题个数；rows：结果行数；smallRows：分组内的提示文字行（如「群文件需联网
    //   搜索」）与「查看更多」按钮行的总数；三者都为 0 时按一行"无匹配结果"提示的高度算。
    int ComputeHeight(int sections, int rows, int smallRows) const;

    // 切换分类筛选：更新各标签的选中态并刷新结果。
    //   filter：目标筛选，取值见 FilterKind。
    void SwitchFilter(int filter);

    // 向结果列表追加一个「查看更多」按钮（文字链样式），返回它的指针（借用，列表持有）。
    //   content：结果列表容器；text：按钮文字。
    balloonwjui::DuiControl* AppendMoreButton(balloonwjui::DuiVBox& content, LPCTSTR text);

    // 宿主是否提供了任何一种需要延迟查询的能力（聊天记录、本地文件、群文件）。
    bool HasDelayedSearch() const;

    // 查询词变化后安排一次延迟查询；查询词为空、没有需要延迟查询的能力、或当前查询词已经
    // 查过时取消在途的延迟查询。
    void ScheduleDelayedSearch();

    // 立即发起一次延迟查询：向后台线程提交聊天记录与文件类消息两个本地查询（结果之后经
    // kMsgLocalSearchDone 投回），再请宿主搜群文件。
    void RunDelayedSearch();

    // 当前查询词的聊天记录结果是否已经回来（回来之前「消息」标签显示"正在搜索"）。
    bool MessageResultsReady() const;

    // 当前查询词的本地文件结果是否已经回来。
    bool FileResultsReady() const;

    // 会话 id → 联系人资料池 / 群列表下标的索引，每次 RefreshResults 建一次，供下面几个
    // 生成结果行的函数查会话名，免得每条结果都把资料池线性扫一遍。
    struct ConversationIndex
    {
        std::map<int64_t, size_t> m_contacts;  // 对端 userid → m_contacts 下标
        std::map<int64_t, size_t> m_groups;    // 群 id → m_groups 下标
    };

    // 按当前的 m_contacts / m_groups 建会话索引。
    ConversationIndex BuildConversationIndex() const;

    // 「消息」标签末尾追加服务器搜索那部分：还没搜时放「搜索服务器上的聊天记录」按钮；在等应答、
    // 没联网、失败时放一行提示；有结果时放「服务器上的聊天记录」与「群公告」两段。
    //   content：结果列表。index：会话查找表（解析名字与头像）。queryUtf8：当前查询词。
    //   sections / rows / smallRows：[入出] 已放的分组标题数、结果行数、矮行数，按本段追加的累加。
    void AppendServerResults(balloonwjui::DuiVBox& content, const ConversationIndex& index,
                             const std::string& queryUtf8, int& sections, int& rows, int& smallRows);

    // 向结果列表追加一个灰色的分组标题（如「联系人」「群文件」）。
    //   content：结果列表容器；text：标题文字。
    void AppendSectionHeader(balloonwjui::DuiVBox& content, LPCTSTR text);

    // 把 m_msgHits 逐个会话做成结果行追加到 rows，hitIndexes 同步追加该行对应的 m_msgHits
    // 下标（两者同下标一一对应）。已退出的群（不在群列表里）跳过，不生成行。
    //   index：BuildConversationIndex 的结果。
    void BuildMessageRows(const ConversationIndex& index,
                          std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                          std::vector<size_t>& hitIndexes);

    // 把 m_fileHits 做成「聊天文件」一段的行，规则同 BuildMessageRows（已退出的群里的记录跳过）。
    void BuildFileRows(const ConversationIndex& index,
                       std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                       std::vector<size_t>& hitIndexes);

    // 把 m_groupFileHits 做成「群文件」一段的行，hitIndexes 同步追加对应的下标。群列表里
    // 查不到的群（群列表尚未刷新）里的文件跳过。
    void BuildGroupFileRows(const ConversationIndex& index,
                            std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                            std::vector<size_t>& hitIndexes);

    // 按会话类型与 id 取会话名与头像参数（与联系人、群结果行同一口径）。
    //   index：BuildConversationIndex 的结果。ctype / cid：会话类型与会话 id。
    //   outName / outFacetype / outCustomface：输出。
    //   返回 false 表示这个会话不该展示（已退出的群、服务号等）。
    bool ResolveConversation(const ConversationIndex& index, int ctype, int64_t cid,
                             CString& outName, int& outFacetype, std::string& outCustomface);

    // 更新「消息」「文件」两个标签的可用状态（宿主没提供相应回调时置灰）；当前选中的
    // 标签变得不可用时退回「全部」。
    void UpdateTabAvailability();

    // 打开某一结果行：按它属于哪一类（联系人、群组、聊天记录、聊天文件、群文件）收起下拉并
    // 调用对应的选中回调。鼠标点击与键盘回车共用。
    //   pCtrl：结果行控件。返回 true 表示它确实是本下拉的结果行并已处理；否则返回 false。
    bool ActivateRow(balloonwjui::DuiControl* pCtrl);

    // 按 m_selection 的当前下标给结果行设键盘选中态（只有一行是选中的），并把选中行滚动到可见。
    void ApplyKeySelection();

    // 把一行滚动到结果区的可见范围内（已经可见时不动）。row：结果行控件（借用）。
    void ScrollRowIntoView(balloonwjui::DuiControl* row);

    // 清空全部延迟查询的结果与在途状态（收起下拉、更换回调时调用）。
    void ResetDelayedResults();

private:
    const std::vector<net::ContactItem>*   m_contacts;   // 联系人数据源，借用
    const std::vector<net::UserGroupItem>* m_groups;     // 群列表数据源，借用
    UINT                m_selfUserId;                     // 当前登录用户 userid（既用于过滤判断，也用于画头像）
    bool                m_includeSelf;                    // 联系人结果里是否包含自己；false 时把自己滤掉
    PickContactCallback m_contactCb;                     // 选中联系人回调
    PickGroupCallback   m_groupCb;                       // 选中群组回调
    void*               m_pickUd;                        // 回调用户数据
    // 点联系人结果行右侧「定位」按钮时的回调。NULL（默认）表示宿主没有提供该能力，
    // 此时结果行不显示定位按钮。透传的用户数据与上面两个回调共用 m_pickUd。
    LocateContactCallback m_locateCb;
    // userid → 部门路径的查询表，<u>借用不持有</u>（宿主主面板持有那份表）。非空且能
    // 查到时，联系人结果行的副行显示部门路径；NULL 或查不到时退回「账号 xxx · 地区」。
    const std::map<UINT, CString>* m_deptPaths;
    // 联系人与群的搜索候选缓存，与 m_contacts / m_groups 按位置一一对应。每次
    // RefreshResults 开头先 Sync 一次（只重算与数据源不一致的条目），生命周期同本对象。
    balloonsearch::SearchCandidateCache m_candidates;

    bool    m_built;          // 控件树是否已构建
    int     m_filter;         // 当前分类筛选（FilterKind）
    CString m_query;          // 当前查询词（已去首尾空白）

    int     m_panelWidth;     // 最近一次展示用的主面板宽（下拉宽 = 它）
    int     m_anchorLeft;     // 最近一次锚点左边 x（屏幕坐标，下拉左上角 x）
    int     m_anchorBottom;   // 最近一次锚点底部 y（屏幕坐标，下拉左上角 y）

    balloonwjui::DuiScrollView*     m_scroll;        // 结果滚动区（借用，树持有）
    balloonsearch::SearchFilterTab* m_tabAll;        // "全部"标签（借用）
    balloonsearch::SearchFilterTab* m_tabContacts;   // "联系人"标签（借用，更新计数）
    balloonsearch::SearchFilterTab* m_tabGroups;     // "群组"标签（借用，更新计数）
    balloonsearch::SearchFilterTab* m_tabMessages;   // "消息"标签（借用，更新计数与可用状态）
    balloonsearch::SearchFilterTab* m_tabFiles;      // "文件"标签（借用，更新计数与可用状态）
    // 筛选标签所在的那一行（借用，树持有）。标签按文字宽度排版，计数变化后要对这一行
    // ForceLayout，新的宽度才会生效。
    balloonwjui::DuiControl*        m_tabRow;
    // 「全部」标签里「聊天记录」「文件」两段末尾的「查看更多」按钮（借用，结果列表持有，
    // 每次 RefreshResults 重建；该段不超过 kAllTabSectionMax 条时没有按钮，为 NULL）。
    // 点它切到「消息」或「文件」标签。
    balloonwjui::DuiControl*        m_moreMessagesBtn;
    balloonwjui::DuiControl*        m_moreFilesBtn;

    // 结果行 → 选中目标。命中联系人写 m_rowContact，命中群组写 m_rowGroup，「消息」标签
    // 的行写 m_rowMessage，「文件」标签两段的行分别写 m_rowFile / m_rowGroupFile；各表键互斥
    // （一行只属于其一）。每次 RefreshResults 重建前清空。
    std::map<balloonwjui::DuiControl*, UINT>    m_rowContact;    // 行 → 对端 userid
    std::map<balloonwjui::DuiControl*, int64_t> m_rowGroup;      // 行 → 群 id
    std::map<balloonwjui::DuiControl*, size_t>  m_rowMessage;    // 行 → m_msgHits 下标
    std::map<balloonwjui::DuiControl*, size_t>  m_rowFile;       // 行 → m_fileHits 下标
    std::map<balloonwjui::DuiControl*, size_t>  m_rowGroupFile;  // 行 → m_groupFileHits 下标

    // 当前显示的全部结果行，按自上而下的显示顺序（借用，结果列表持有；每次 RefreshResults
    // 重建）。上下方向键按这个顺序移动选中。「查看更多」按钮与提示文字不在其中。
    std::vector<balloonmain::MainPanelListItem*> m_selectableRows;
    // 键盘选中状态：选中的是 m_selectableRows 的第几行；每次 RefreshResults 重置为未选中。
    balloonsearch::SearchSelection m_selection;

    // 以下回调的用户数据都用 m_pickUd。
    // 提交本地聊天记录后台查询的回调；NULL 表示宿主没提供，「消息」标签置灰。
    SubmitLocalSearchCallback  m_submitMsgCb;
    // 点「消息」标签结果行时的回调；NULL 时点了只收起下拉。
    PickMessageCallback        m_pickMsgCb;
    // 提交本地库文件类消息后台查询的回调；NULL 时「聊天文件」一段不查。
    SubmitLocalSearchCallback  m_submitFileCb;
    // 请宿主搜索群文件的回调；NULL 时「群文件」一段不查。与 m_submitFileCb 都为 NULL 时
    // 「文件」标签置灰。
    RequestGroupFilesCallback  m_requestGroupFileCb;
    // 点「聊天文件」一段的行时的回调；NULL 时点了只收起下拉。
    PickFileMessageCallback    m_pickFileCb;
    // 点「群文件」一段的行时的回调；NULL 时点了只收起下拉。
    PickGroupFileCallback      m_pickGroupFileCb;

    // 最近一次采纳的本地聊天记录查询结果（按最近命中时间倒序，每项一个会话）。m_msgHitsQuery
    // 与当前查询词相同时是最新结果；不同时是上一个查询词的结果，新结果回来之前先沿用它显示。
    std::vector<balloonhistory::ConversationHit> m_msgHits;
    // m_msgHits 对应的查询词（已去首尾空白），空串表示还没有任何结果。收起下拉时清空，下次
    // 打开一律重新查询，免得显示收起期间到达的新消息之前的结果。
    CString m_msgHitsQuery;
    // 最近一次采纳的本地文件类消息查询结果（按时间倒序），有效期规则同 m_msgHits。
    std::vector<balloonhistory::HistoryRecord>   m_fileHits;
    // 与 m_fileHits 一一对应：每条结果在本机的可用路径（文件或文件夹），本机没有时为空串。
    // 采纳查询结果时一次算好（要查登记库、看磁盘），之后每次重建结果行直接取用。
    std::vector<std::wstring>                    m_fileHitLocalPaths;
    // m_fileHits 对应的查询词，规则同 m_msgHitsQuery。
    CString m_fileHitsQuery;
    // 最近一次提交的聊天记录查询的请求序号与查询词；回来的结果序号不是它就丢弃。0 表示没有
    // 在途的查询（收起下拉、提交失败时清零）。
    uint32_t m_msgSeq;
    CString  m_msgSeqQuery;
    // 最近一次提交的本地文件查询的请求序号与查询词，规则同 m_msgSeq。
    uint32_t m_fileSeq;
    CString  m_fileSeqQuery;
    // 最近一次发起延迟查询时的查询词：与 m_query 相同表示这个词已经发起过，不必再发。
    CString m_lastSearchedQuery;
    // 最近一次被采纳的群文件搜索应答里的结果（按登记时间倒序）。用户接着输入、新请求的
    // 应答到达之前，先沿用它显示；收起下拉、没连上服务器或服务端返回失败时清空。
    std::vector<net::SearchedGroupFileItem> m_groupFileHits;
    // 群文件搜索请求的状态：最近一次请求的关键字与进展，用于丢弃过期的应答、显示提示文字。
    balloonsearch::GroupFileSearchState     m_groupFileState;
    // 延迟查询的定时器是否在计时。收起下拉、查询词清空时停掉。
    bool    m_searchPending;

    // 宿主主面板的窗口（EnsureCreated 传入）。在本机打开文件时作为 ShellExecute 与提示框的
    // 父窗口；本下拉自己是不抢激活的浮窗，不适合当父窗口。
    HWND m_hOwner;

    // 「部门」一段的数据源（借用）与点击回调，见 SetDepartments；任一为 nullptr 时不搜部门。
    const std::vector<net::DepartmentItem>* m_departments;
    LocateDepartmentCallback m_locateDeptCb;
    // 部门结果行（借用，树持有）→ 部门 id；每次刷新重建。
    std::map<balloonwjui::DuiControl*, int64_t> m_rowDept;

    // 「备忘」一段的数据源（借用）与点击回调，见 SetMemos；任一为 nullptr 时不搜备忘。
    const std::vector<net::MemoItem>* m_memos;
    PickMemoCallback m_pickMemoCb;
    // 备忘结果行（借用，树持有）→ 备忘 id；每次刷新重建。
    std::map<balloonwjui::DuiControl*, int64_t> m_rowMemo;

    // 「搜索服务器上的聊天记录」的三个回调，见 SetServerSearchCallbacks；均可为 nullptr。
    RequestServerSearchCallback m_requestServerCb;
    PickServerMessageCallback   m_pickServerMsgCb;
    PickAnnouncementCallback    m_pickAnnCb;
    // 服务器搜索请求的状态：最近一次请求的关键字与进展（与群文件搜索同一套规则）。
    balloonsearch::GroupFileSearchState m_serverState;
    // 最近一次被采纳的全局搜索应答里的结果：会话命中与群公告命中。收起下拉、失败时清空。
    std::vector<net::GlobalSearchConversation> m_serverConvs;
    std::vector<net::GlobalSearchAnnouncement> m_serverAnns;
    // 「搜索服务器上的聊天记录」按钮（借用，树持有）；没有显示时为 NULL。
    balloonwjui::DuiControl* m_serverSearchBtn;
    // 服务器结果行（借用，树持有）→ m_serverConvs / m_serverAnns 下标；每次刷新重建。
    std::map<balloonwjui::DuiControl*, size_t> m_rowServerConv;
    std::map<balloonwjui::DuiControl*, size_t> m_rowServerAnn;

    // 「在聊天记录管理器中查看全部」的回调，见 SetMessageManagerCallback；可为 nullptr。
    OpenMessageManagerCallback m_openMsgManagerCb;
    // 「消息」标签底部「在聊天记录管理器中查看全部」按钮（借用，树持有）；没有显示时为 NULL。
    balloonwjui::DuiControl* m_openMsgManagerBtn;

    // 「最近搜索」的三个回调，见 SetRecentCallbacks；均可为 nullptr。
    RecordRecentCallback m_recordRecentCb;
    PickRecentCallback   m_pickRecentCb;
    ClearRecentCallback  m_clearRecentCb;
    // 是否正在显示最近搜索列表（ShowRecent 置位，UpdateQuery 有查询词时与 HideDropdown 时清除）。
    bool m_recentMode;
    // 最近搜索列表里显示的关键字（ShowRecent 时复制自宿主），下标与 m_rowRecent 的值对应。
    std::vector<std::wstring> m_recentKeywords;
    // 最近搜索列表的关键字行（借用，树持有）→ m_recentKeywords 下标；每次 ShowRecent 重建。
    std::map<balloonwjui::DuiControl*, size_t> m_rowRecent;
    // 最近搜索标题行右侧的「清空」按钮（借用，树持有）；不在最近搜索模式时为 NULL。
    balloonwjui::DuiControl* m_clearRecentBtn;
};
