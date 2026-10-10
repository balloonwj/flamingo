/**
 *  HistoryRowMenu.h
 *
 *  「消息记录」窗口列表行右键菜单的<u>纯模型</u>部分：给定这一行是什么类型的消息、
 *  它的本地文件在不在、是不是正在下载、有没有被 Pin，算出菜单该有哪几行。
 *  不碰 HWND、不建真菜单、不依赖 balloonui，便于被 FlamingoUnit 测试工程直接调用断言
 *  （见 flamingoclient/Source/Tests/HistoryRowMenuTests.cpp，Suite HRM）。
 *
 *  真正弹菜单、把选中项分发出去在 CBalloonHistoryWindow 那边做；本文件只回答
 *  「这个菜单长什么样」。写法对齐 BalloonChatWnd/ChatMessageMenu —— 菜单模型一律做成
 *  纯函数并配测试，避免菜单逻辑散落在窗口代码里没法验证。
 *
 *  典型用法：
 *      balloonhistory::HistoryRowMenuContext ctx;
 *      ctx.m_msgType      = ::message_type_file;   //取值见 net/Protocol.h
 *      ctx.m_hasLocalFile = true;
 *      ctx.m_msgId        = record.m_msgid;
 *      const std::vector<balloonhistory::HistoryRowMenuItem> model =
 *          balloonhistory::BuildHistoryRowMenuModel(ctx);
 *      //再按 model 逐行灌进 DuiMenu；菜单落点必须走
 *      //balloonmain::ClampPopupMenuToWorkArea（开发约定文档 UI 约定第 3 条），
 *      //否则靠近窗口底边的行弹出来会跑到桌面外。
 *
 *  本头文件<u>不</u>引 net/Protocol.h，与 BalloonHistory 其它头文件一致，只在 .cpp 里引；
 *  故 m_msgType 声明成 int，取值在旁边写成字段注释。实现文件 HistoryRowMenu.cpp 引了
 *  Protocol.h，可直接与 ::message_type_* 比对。（此前还有一条理由是 Protocol.h 与旧版
 *  Msg.h 枚举名冲突、两者不得同处一个编译单元；v1 协议头已于 2026-08-14 删除。）
 *
 *  行为约定（方案见 docs/消息记录内容交互方案.md §3.2、§5 的 F3/F4/F5，已与用户逐条确认）：
 *    · 文本 / 链接消息       → 复制、定位到聊天窗、Pin。
 *    · 图片消息 + 本地有文件 → 复制图片、图片另存为、打开所在文件夹、定位、Pin。
 *    · 图片消息 + 本地无文件 → 下载、定位、Pin。
 *    · 文件消息 + 本地有文件 → 打开文件、打开所在文件夹、另存为、定位、Pin。
 *    · 文件消息 + 本地无文件 → 下载、定位、Pin。
 *    · 任意类型 + 正在下载   → 取消下载、定位、Pin（下载中既不给「下载」也不给「打开」）。
 *    · 语音及其它尚未在本窗单独支持的类型 → 只有定位、Pin。
 *    · 服务器还没给这条消息分配 msgid（m_msgId 为 0）时「Pin」整项<u>不出现</u>，
 *      而不是置灰 —— 与气泡菜单「撤回」超时即整项消失同一种处理，菜单里不留点不动的项。
 *    · 「取消 Pin」的权限<u>不在客户端预判</u>：本端不一定知道一条历史消息是谁钉的
 *      （operator 只随推送来、历史行未必带），预判会出现"该显示的没显示"。故这一项
 *      一律给出，无权时由服务端以 error_code_pin_not_allowed 拒绝并提示。
 *    · 「转发」出现条件见 CanForwardHistoryRow。转发不依赖本地文件：服务端按原消息重新落
 *      一条，图片 / 文件本地没下载、甚至正在下载时照样给出（方案见 docs/聊天消息转发方案.md
 *      §9 第 9 步）。
 *
 *  菜单形态（2026-10-03 与用户确认的分组规则，与主面板、聊天窗几个右键菜单相同）：
 *      <内容组>                 ← 随消息类型与本地文件 / 下载状态变化，见上文；可能为空
 *      ──────────
 *      定位到聊天窗             ← 恒有
 *      查看上下文               ← 有筛选条件时
 *      ──────────
 *      转发                     ← 能转发时
 *      多选                     ← msgid 已分配时
 *      Pin / 取消 Pin           ← msgid 已分配、且不是共享卡片时
 *      ──────────
 *      删除                     ← msgid 已分配时
 *      ──────────
 *      清空聊天记录             ← m_canClearConversation 为真时
 *  某一组整组为空时连同它前面的分隔线一起不出现，菜单首尾不会出现分隔线。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <string>
#include <vector>
#include <stdint.h>

namespace balloonhistory {

//msgid 的"未分配"哨兵值。服务器给每条落库消息分配的 msgid 从 1 起，0 表示本端
//还没拿到服务端回执（刚发出的那一瞬），此时不能对它做 Pin —— 发过去必被判
//message_not_exist，故整项不显示。
constexpr int64_t kHistoryRowMsgIdUnassigned = 0;

/**
 *  菜单项标识。分发时按它 switch，不要按行下标 —— 行数会随消息类型、本地文件
 *  状态与下载状态变化，同一个下标在不同类型的行上根本不是同一项。
 */
enum HistoryRowMenuId
{
    kHistoryRowMenuCopy           = 1,   // 复制：仅文本 / 链接行出现。右键点在正文选区上时复制选中部分，否则复制整条正文；按聊天内容格式写剪贴板（纯文字里表情写成“[表情]”，粘回聊天输入框时还原成表情）
    kHistoryRowMenuCopyImage      = 2,   // 复制图片：仅图片行且本地已有原图时出现，把图片本身放进剪贴板（CF_BITMAP）
    kHistoryRowMenuSaveImageAs    = 3,   // 图片另存为：仅图片行且本地已有原图时出现，把原图另存到用户选定位置
    kHistoryRowMenuOpenFile       = 4,   // 打开文件：仅文件行且本地文件在时出现，用系统默认程序打开
    kHistoryRowMenuOpenFolder     = 5,   // 打开所在文件夹：图片 / 文件行且本地文件在时出现，资源管理器定位到该文件
    kHistoryRowMenuSaveFileAs     = 6,   // 另存为：仅文件行且本地文件在时出现，把本地文件另存到用户选定位置
    kHistoryRowMenuDownload       = 7,   // 下载：图片 / 文件行且本地没有文件、也没在下载时出现，主动拉取
    kHistoryRowMenuCancelDownload = 8,   // 取消下载：本行正在下载时出现，停掉这次传输（此时不给「下载」与「打开」）
    kHistoryRowMenuLocate         = 9,   // 定位到聊天窗：所有类型的行都有，是本窗口的主操作 —— 跳到聊天窗并高亮这条
    kHistoryRowMenuPin            = 10,  // Pin / 取消 Pin：所有类型的行都有，文案随当前是否已钉住取反；msgid 未分配时整项不出现
    kHistoryRowMenuOpenFolderRoot = 11,  // 打开文件夹：仅文件夹传输记录行、且本机登记过这次传输的根目录且目录还在时出现，在资源管理器里打开<u>那个目录本身</u>。与 kHistoryRowMenuOpenFolder（打开文件所在的父目录并选中文件）不是一回事，故另开一个 id
    kHistoryRowMenuDelete         = 12,  // 删除：所有类型的行都有，单独一组排在「清空聊天记录」之前；msgid 未分配时整项不出现。本人视角的删除（cmd_delete_message），本机与本人其他设备都不再显示，对方不受影响；确认后发出，界面等删除推送到达再更新（见 docs/清理聊天记录方案.md §9.1）
    kHistoryRowMenuViewContext    = 13,  // 查看上下文：当前有筛选条件（关键字、类型、时间、发送人）时出现，紧跟「定位到聊天窗」；清掉筛选、在本窗口里翻到这条所在的页并高亮它（判定见 HistoryModel.h 的 ShouldOfferViewContext）
    kHistoryRowMenuForward        = 14,  // 转发：本条能转发时出现（判定见 CanForwardHistoryRow），与「多选」「Pin」同在「定位到聊天窗」那组之后的一组，排在该组头一位；点它打开主面板的目标选择窗，按逐条转发把这一条发给选定的会话
    kHistoryRowMenuMultiSelect    = 15,  // 多选：msgid 已分配的行都有，紧跟「转发」；点它进入多选模式并预先选中这一条，之后可合并转发、逐条转发或批量删除（选择集合见 HistorySelection.h）
    kHistoryRowMenuClearConversation = 16,  // 清空聊天记录：清空整个会话（本人视角），与这一行是哪条消息无关；m_canClearConversation 为真时单独一组出现在菜单最末（「删除」之后，中间隔一条线）。与底部「清空聊天记录」按钮同一条路径（2026-10-03 起从聊天窗头部移到消息记录窗）
};

/**
 *  菜单里的一行。
 *
 *  m_isSeparator 为 true 时这一行是分隔线，其余字段无意义（窗口侧照着插一条
 *  分隔线即可，不必看 id 与文案）。分隔线的表示方式与 ChatMessageMenuEntry 一致。
 */
struct HistoryRowMenuItem
{
    HistoryRowMenuId m_id;           //本行的标识，分发时用；分隔线行该字段无意义
    std::wstring     m_label;        //显示文字；分隔线行为空串。用 std::wstring 而不是
                                     //裸指针，免去调用方追问字面量生命周期
    bool             m_isSeparator;  //true 表示这一行是分隔线

    HistoryRowMenuItem()
        : m_id(kHistoryRowMenuCopy)
        , m_isSeparator(false)
    {
    }
};

/**
 *  算菜单模型所需的全部入参。
 *
 *  一律由调用方（消息记录窗）在弹菜单前从该行的 HistoryRecord 与本地文件登记表填好；
 *  本模块不查库、不碰磁盘，"本地文件在不在"由调用方用现成的
 *  balloonchatwnd::ResolveLocalFilePath + 一次磁盘存在性检查得出。
 */
struct HistoryRowMenuContext
{
    //消息内容类型。取值见 net/Protocol.h 的 enum message_type（<u>全局</u>命名空间，
    //如 ::message_type_text / ::message_type_image / ::message_type_file）。这里用 int
    //而不是枚举，是为了不把 Protocol.h 拖进本头文件（理由见文件头注释）。
    //默认值 0 不是合法取值，构造后必须由调用方填。
    int     m_msgType;

    //本行对应的图片 / 文件在本地存不存在。true 才给「打开 / 另存为 / 复制图片 /
    //打开所在文件夹」这类需要本地文件的项；false 则给「下载」。
    //对文本等不涉及文件的类型本字段被忽略。
    bool    m_hasLocalFile;

    //本行的图片 / 文件是不是正在下载。为 true 时内容组<u>只</u>给「取消下载」——
    //这时文件还没落地，「下载」「打开」「另存为」都无从谈起。
    //本字段优先级高于 m_hasLocalFile。
    bool    m_downloading;

    //本条消息当前是否已被钉住（Pin）。决定那一项的文案是「Pin」还是「取消 Pin」，
    //不影响它出不出现。
    bool    m_pinned;

    //服务器给本条消息分配的 msgid。等于 kHistoryRowMsgIdUnassigned（0）时表示
    //服务端还没分配，此时「Pin」整项不出现；负数不是合法 msgid，一并按未分配处理。
    int64_t m_msgId;

    //是否给「查看上下文」。由调用方按 ShouldOfferViewContext（HistoryModel.h）算好填入，
    //本模块不看筛选条件
    bool    m_canViewContext;

    //是否给「清空聊天记录」。清空要经主面板发请求，消息记录窗有主面板时填 true；调试入口为 false
    bool    m_canClearConversation;

    HistoryRowMenuContext()
        : m_msgType(0)
        , m_hasLocalFile(false)
        , m_downloading(false)
        , m_pinned(false)
        , m_msgId(kHistoryRowMsgIdUnassigned)
        , m_canViewContext(false)
        , m_canClearConversation(false)
    {
    }
};

/**
 *  算「消息记录」窗口列表行右键菜单的行模型。
 *
 *  菜单最多五组，组与组之间一条分隔线，各组给哪几项见本文件头的「菜单形态」。
 *  内容组可能<u>为空</u>（语音等未支持的类型），msgid 未分配时第三、四组整组为空；
 *  空组前面的分隔线不会出现，保证分隔线既不打头也不结尾、更不会连着两条。
 *
 *  @param ctx 本行的类型与状态，各字段含义见 HistoryRowMenuContext；本函数只读它，
 *             不持有引用。
 *  @return 菜单行模型（值语义，调用方拥有）。恒非空 —— 最少也有「定位到聊天窗」一项。
 *          无副作用：不查库、不碰磁盘、不建窗口。
 */
std::vector<HistoryRowMenuItem> BuildHistoryRowMenuModel(const HistoryRowMenuContext& ctx);

/**
 *  判断某个消息类型在本窗口里算不算「图片行」。
 *
 *  截图（::message_type_screenshot）在协议里是图片的子类型，消息记录窗一直按图片
 *  渲染（见 HistoryView.cpp 的 ParseContentBrief），菜单跟着按图片给项，否则截图行
 *  会出现"有缩略图却没有图片相关菜单项"的割裂。
 *
 *  @param msgType 消息内容类型，取值见 net/Protocol.h 的 enum message_type。
 *  @return 算图片行返回 true。
 */
bool IsHistoryRowImageType(int msgType);

/**
 *  判断某个消息类型在本窗口里算不算「文件行」。
 *
 *  @param msgType 消息内容类型，取值见 net/Protocol.h 的 enum message_type。
 *  @return 算文件行返回 true。
 */
bool IsHistoryRowFileType(int msgType);

/**
 *  判断某个消息类型在本窗口里算不算「文件夹传输记录行」。
 *
 *  文件夹传输记录（::message_type_folder）与文件消息完全是两回事：服务端只记了这次
 *  传输这个动作（文件夹名 / 规模 / 结局），没有文件清单、没有 fileid、没有 MD5，因此
 *  它<u>不支持下载、不支持另存为</u>，唯一可能的操作是"在本机打开当初那个目录"，而这
 *  还得看这台机器上有没有登记过落点。故它不能并进 IsHistoryRowFileType，否则菜单会给
 *  出一堆点下去必然无效的项。
 *
 *  @param msgType 消息内容类型，取值见 net/Protocol.h 的 enum message_type。
 *  @return 算文件夹记录行返回 true。
 */
bool IsHistoryRowFolderType(int msgType);

/**
 *  判断消息记录窗里的一行能不能转发（决定右键菜单里有没有「转发」）。
 *
 *  规则：
 *    · 须有服务器 msgid：服务端按 msgid 取转发源消息，未分配（0）或负数时必被判
 *      message_not_exist；
 *    · 文件夹传输记录（18）、通话记录（16）、系统提示（17）不能转发：三者都是服务端生成的
 *      记录条，转发出去没有对应的产品语义（聊天窗对文件夹记录同样不给「转发」）；
 *    · 其余类型一律可以，与本地文件在不在、是否正在下载无关。
 *  撤回的消息不会出现在消息记录窗的查询结果里，这里不必再判。
 *
 *  @param msgType 消息内容类型，取值见 net/Protocol.h 的 enum message_type。
 *  @param msgId   服务器 msgid；kHistoryRowMsgIdUnassigned（0）表示尚未分配。
 *  @return 能转发返回 true。本函数无副作用。
 */
bool CanForwardHistoryRow(int msgType, int64_t msgId);

} // namespace balloonhistory
