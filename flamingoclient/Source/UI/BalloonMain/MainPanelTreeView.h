/**
 *  MainPanelTreeView.h
 *
 *  主面板三个 Tab 内容面板里用的 DuiTreeView 子类。
 *
 *  本子类只做绘制层面的扩展(右侧列文字 / 会话行装饰),滚轮不必自己管:
 *  单列 DuiTreeView::OnMouseWheel 会如实返回 false(见 DuiTreeView.cpp 里
 *  !IsMultiCol 的短路),DuiHost::DispatchMouseWheel 随即沿父链上冒,滚轮
 *  自然落到外层 MainPanelScrollView 上。2026-08-14 之前库里的滚轮不冒泡,
 *  本类曾覆写 OnMouseWheel 手工转发,库补上默认上冒后那段代码已删除。
 *
 *  本文件遵循 balloonui 头文件约定:include 它之前调用方须已引入
 *  ATL/WTL(Flamingo 侧由 stdafx.h 保证,FlamingoUnit 侧由其自定义 prolog
 *  保证)。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#pragma once

#include <map>
#include <set>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"

namespace balloonmain {

// 会话行版式的一行在一次绘制里的排布结果（第二行摘要与各标记的位置）。只在 MainPanelTreeView::OnPaint
// 期间使用，定义在 MainPanelTreeView.cpp。
struct ConvRowPaint;

/**
 *  MainPanelTreeView —— 主面板三个 Tab 树用的 DuiTreeView 子类，在基类之上
 *  做四件事：
 *
 *    1) 接管行右侧列的绘制 —— 基类把右侧文字强制按整行垂直居中，两行节点里它会落在
 *       主标签与副标签之间的中缝上，做不出"右侧文字和主标签同高"的效果。本子类不修改
 *       balloonui，而是子类化扩展：调用方用 SetItemRightTopText 交进来的文字，本类画在
 *       上半行（与主标签同一垂直带，用基类同款默认字体 / 灰色，自动对齐）。为不让左侧
 *       文字顶进右侧列，本类用等宽空格占位塞进基类 rightText（空格不可见，仅让基类据此
 *       裁剪左侧文字）。目前两处在用：会话树放"时间"，群树放"群成员数"。所有行的右侧列
 *       都贴同一条右边线，时间与未读数在各行之间上下对齐。
 *    2) 会话行专有的装饰 —— 未读红点画在下半行（与预览同高，用 SetItemRightStacked
 *       连同时间一起交进来），以及置顶（整行覆淡暗色 + 左侧强调竖条）/ 免打扰（红点转灰
 *       并在其左侧加静音图标）标记。这些只有会话树会用；群树只走上排文字那一条路。
 *    3) 行标记 —— 特别关注星标、收藏书签、字幕徽章、「通话中」胶囊、「仅通知」标签。通讯录与群列表的行
 *       画在名字之后；会话行版式（SetItemSummary）的行画在第二行右侧，摘要按剩余宽度截断
 *       （docs/消息提醒模式与消息盒子方案.md 第 13 节）。
 *    4) 头像右下角的在线状态角标（SetItemPresence）—— 离开、忙碌两种状态在头像右下角画一个带描边的
 *       小圆角标，不占文字宽度。2026-10-07 起取代基类的行右端状态徽标（SetItemStatusIcon），本类管理的
 *       行不要再设基类的状态徽标：本类画的右侧列不再给它让位，设了会与时间、未读数重叠。
 *
 *  联系人树不用第 1、2 项，相关映射恒为空，OnPaint 那一段走短路分支；其右侧文字仍由基类正常绘制。
 *
 *  除上述外的所有行为与裸 DuiTreeView 完全一致，节点 / 列 / 图标 / 排序 /
 *  编辑等 API 直接继承。
 */
class MainPanelTreeView : public balloonwjui::DuiTreeView
{
public:
    // GetItemPresence 在该行没有登记在线状态时的返回值。
    static const int kNoPresence = -1;

    MainPanelTreeView();

    /**
     *  设置某节点"与主标签同高"的右侧文字。本类接管该节点右侧列的绘制，把这段文字
     *  画在行的上半区、右对齐，字体与颜色沿用基类右侧文字那一套（默认字体 + 灰色），
     *  从而与左侧主标签处在同一条水平线上。
     *
     *  之所以不直接用基类的 SetItemRightText：基类把右侧文字按<u>整行</u>垂直居中
     *  （见 DuiTreeView.cpp 单列分支），两行节点里它会落在主标签与副标签之间的中缝，
     *  看上去是"飘着"的。用例：群列表第一行右侧的成员数、会话列表第一行右侧的时间。
     *
     *    id    节点 id（AddRoot / AddChild 的返回值）。
     *    text  右侧文字；NULL 或空串表示不画，并释放该节点已占的右侧预留宽。
     *  内部会按文字宽度给基类 rightText 塞等宽空格占位以预留右侧列宽（使左侧主标签
     *  自动省略号截断、不顶到本文字底下），再 Invalidate 触发重绘。
     */
    void SetItemRightTopText(int id, LPCTSTR text);

    /**
     *  设置某会话节点右侧的"时间（上排）+ 未读红点（下排）"。是会话行专用的便捷方法：
     *  时间等价于 SetItemRightTopText（画在与姓名同高的上排），红点画在与预览同高的下排。
     *    id         节点 id（AddRoot / AddChild 的返回值）。
     *    timeText   右上排显示的时间文字（如 "14:32" / "昨天"）；空串表示不画时间。
     *    badgeCount 未读消息数；<= 0 表示该节点不画红点。
     *  内部会按时间 / 红点的宽度，给基类 rightText 塞等宽空格占位以预留右侧列宽，
     *  再 Invalidate 触发重绘。
     */
    void SetItemRightStacked(int id, LPCTSTR timeText, int badgeCount);

    /**
     *  设置某会话节点的"置顶 / 消息提醒模式"标记。两个标记的呈现方式：
     *    · 置顶 —— 整行底色轻微加深（在基类画完行内容之后覆一层极低透明度的暗色，
     *              不影响文字可读性），与未置顶行区分开；
     *    · 提醒模式 —— 见 SetItemNotifyMode。
     *  两者可同时成立。提醒模式会改变右下角占位宽度，故本方法内部会重算右侧列预留宽。
     *    id          节点 id（AddRoot / AddChild 的返回值）。
     *    pinned      该会话是否置顶。
     *    notifyMode  该会话的消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
     *                （2026-10-03 之前这个参数是「是否免打扰」的布尔值，true 对应现在的
     *                notify_mode_silent）。
     */
    void SetItemConversationFlags(int id, bool pinned, int notifyMode);

    /**
     *  设置某节点的消息提醒模式徽章（docs/消息提醒模式与消息盒子方案.md §4.3）。会话行、通讯录的
     *  联系人行、群列表的群行共用，画在行右下角（未读胶囊左边）：
     *    · 接收消息但不提醒 —— 铃铛加斜线（kIconBellSlash），未读胶囊转灰；
     *    · 收进消息盒子     —— 抽屉柜（kIconDrawer），未读胶囊转灰；
     *    · 屏蔽消息         —— 禁止符（kIconBlock），未读胶囊转灰（调用方一般已不传未读数）；
     *    · 允许提醒 / 特别关注 —— 不画（特别关注由 SetItemStarred 画星标）。
     *  徽章宽度计入右侧占位，故本方法内部会重算右侧列预留宽。状态没变时直接返回。
     *    id          节点 id。
     *    notifyMode  消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode；
     *                不认识的值按允许提醒处理（不画）。
     */
    void SetItemNotifyMode(int id, int notifyMode);

    // 某行当前登记的消息提醒模式（只记会画徽章的三种；其余返回 notify_mode_allow）。
    // 只读查询，供单元测试核对行标记。
    //   id：节点 id。返回：enum conversation_notify_mode 的取值。
    int GetItemNotifyMode(int id) const;

    /**
     *  设置某行的未读胶囊是否用灰底、但不画提醒模式徽章。会话列表里「消息盒子」那一行用：
     *  它汇总的是不提醒的会话，未读胶囊要灰，而头像已经是抽屉柜，再在右下角画一个抽屉柜就
     *  重复了。与 SetItemNotifyMode 互不影响（两者都设时以徽章那一套为准，胶囊同样是灰的）。
     *  不改变右侧占位宽度。状态没变时直接返回。
     *    id     节点 id。
     *    quiet  true 胶囊用灰底；false 恢复按提醒模式决定。
     */
    void SetItemQuietUnread(int id, bool quiet);

    // 某行的未读胶囊是否被 SetItemQuietUnread 设成了灰底。只读查询，供单元测试核对。
    bool IsItemQuietUnread(int id) const;

    /**
     *  设置某节点的「特别关注」标记 —— 标记为真时画一个金色实心五角星，表示这个人上下线时会在
     *  桌面右下角弹提示。通讯录行、群行画在<u>主标签文字之后</u>；会话行版式的行（SetItemSummary）
     *  画在第二行右侧徽章区的最左边。
     *
     *  名字之后的星标横坐标由基类的 GetItemLabelTextRect 给出（文字实际画到哪儿结束，取决于
     *  缩进层级、图标尺寸、右侧占位与省略号截断，只有基类算得出）。调用方不必预留
     *  宽度：本类会把星的宽度计入该行的右侧占位，名字过长时基类先把名字省略号截断，
     *  星跟在截断处之后，不会压到右侧的时间 / 成员数。
     *
     *  注意：打了标记的行由本类接管基类 rightText（写入等宽空格占位），调用方不要再
     *  对这些行直接调用 SetItemRightText。
     *    id       节点 id（AddRoot / AddChild 的返回值）。
     *    starred  该联系人是否被特别关注。
     */
    void SetItemStarred(int id, bool starred);

    /**
     *  设置某节点是否画「我的收藏」徽章：实心书签（见 MainPanelIcons.h kIconBookmark），
     *  同时被特别关注时排在星标之后。部门树的联系人行、群列表的群行画在名字之后，会话行版式的行画在
     *  第二行；「我的收藏」分区里的行全是收藏，调用方不必再打这个标记。
     *
     *  名字之后的位置规则与星标相同：横坐标取自 GetItemLabelTextRect，徽章宽度计入右侧占位，
     *  名字过长时先截断名字，徽章不会压到右侧内容。使用约束也与星标相同（本类接管
     *  该行的基类 rightText）。
     *    id        节点 id（AddRoot / AddChild 的返回值）。
     *    favorite  该联系人 / 群是否已收藏。
     */
    void SetItemFavorite(int id, bool favorite);

    // 某行当前是否画收藏徽章（SetItemFavorite 设过且为 true）。只读查询，供核对行标记
    // 是否与收藏模型一致（主面板基准测试用）。
    //   id：节点 id。返回：带徽章返回 true。
    bool IsItemFavorite(int id) const { return m_favoriteRows.count(id) != 0; }

    /**
     *  设置某节点是否画「字幕模式」徽章（docs/聊天字幕模式方案.md 第 1 节第 13 条）：这个会话（单聊的联系人、
     *  群）的桌面字幕开着时，在星标、收藏徽章之后、「通话中」胶囊之前画一个与聊天窗头部「字幕模式」
     *  按钮相同的小图标，蓝色；选中行上改画白色。会话树（画在第二行）、联系人树（含「我的收藏」分区）、群树共用。
     *
     *  名字之后的位置规则与星标、收藏徽章相同：横坐标取自 GetItemLabelTextRect，徽章宽度计入右侧占位，名字过长时
     *  先截断名字，徽章不会压到右侧内容；使用约束也相同（本类接管该行的基类 rightText）。
     *    id        节点 id（AddRoot / AddChild 的返回值）。
     *    subtitle  这个会话的字幕是否开着。
     */
    void SetItemSubtitle(int id, bool subtitle);

    // 某行当前是否画字幕徽章（SetItemSubtitle 设过且为 true）。只读查询，供单元测试与主面板核对。
    //   id：节点 id。返回：带徽章返回 true。
    bool IsItemSubtitle(int id) const { return m_subtitleRows.count(id) != 0; }

    /**
     *  设置某节点是否画「通话中」徽章（docs/音视频通话优化方案.md 第 3 项）：本人正在与这个联系人一对一通话、或本人在这个
     *  群的群通话里时，在星标、收藏徽章、字幕徽章之后画一个绿色小胶囊，里面是电话或摄像机图标加「通话中」。
     *  会话树（画在第二行，宽度不够时也不省略）、联系人树（含「我的收藏」分区）、群树共用。
     *
     *  名字之后的位置规则与星标、收藏徽章相同：横坐标取自 GetItemLabelTextRect，胶囊宽度计入右侧占位，名字过长时
     *  先截断名字，胶囊不会压到右侧内容；使用约束也相同（本类接管该行的基类 rightText）。
     *    id     节点 id（AddRoot / AddChild 的返回值）。
     *    media  0 不在通话中（去掉徽章），1 语音通话（画电话），2 视频通话（画摄像机）。
     */
    void SetItemInCall(int id, int media);

    // 某行「通话中」徽章的媒体类型（SetItemInCall 设过的值）；没有徽章时为 0。只读查询，供单元测试与主面板核对。
    //   id：节点 id。
    int GetItemInCallMedia(int id) const;

    /**
     *  设置某个群的节点是否画「仅通知」标签（docs/群管理接入方案.md §14）：群开启了仅通知模式（原称全员禁言，只有群主和
     *  管理员能发言）时，在所有标记的最后画一个灰底小标签，里面是文字「仅通知」。会话树（画在第二行）、群树共用。
     *  名字之后的位置规则与「通话中」胶囊相同：横坐标取自 GetItemLabelTextRect，标签宽度计入右侧占位，名字过长时先截断名字。
     *    id  节点 id（AddRoot / AddChild 的返回值）。
     *    on  这个群是否开启了仅通知模式。
     */
    void SetItemNotifyOnly(int id, bool on);

    // 某行当前是否画「仅通知」标签。只读查询，供单元测试与主面板核对。
    //   id：节点 id。返回：带标签返回 true。
    bool IsItemNotifyOnly(int id) const { return m_notifyOnlyRows.count(id) != 0; }

    /**
     *  设置某会话节点是否画「未读小红点」（会话被手动标为未读，docs/会话标为未读方案.md）：
     *  一个不带数字的小圆点，画在未读胶囊的位置。行上有未读数（SetItemRightStacked 传了正数）时以数字胶囊为准，
     *  不再画红点；提醒模式让胶囊变灰的行（SetItemNotifyMode / SetItemQuietUnread）红点同样变灰。
     *  红点宽度计入右侧占位。状态没变时直接返回。
     *    id  节点 id。
     *    on  是否画红点。
     */
    void SetItemUnreadDot(int id, bool on);

    // 某行当前是否登记了「未读小红点」。只读查询，供单元测试核对。
    //   id：节点 id。返回：登记了返回 true（行上同时有未读数时实际只画数字胶囊）。
    bool IsItemUnreadDot(int id) const { return m_unreadDotRows.count(id) != 0; }

    /**
     *  把某节点设为「会话行版式」并设置它第二行的摘要（docs/消息提醒模式与消息盒子方案.md 第 13 节）。
     *  最近会话列表（以及复用同一套填行代码的消息盒子窗口）的每一行都用它代替基类的 SetItemSubLabel。
     *  会话行版式与通讯录、群列表的版式有三点不同：
     *    · 第一行只有名字与时间：名字只按时间的宽度截断，名字后面不画任何标记；
     *    · 星标、收藏书签、字幕徽章、「通话中」胶囊、「仅通知」标签都画在第二行右侧，排在提醒模式图标与
     *      未读数的左边，排布规则见 ConversationRowLayout.h 的 LayoutConvRowSecondLine；
     *    · 摘要按第二行剩余的宽度截断并补省略号，并保留一个最小宽度（宽度不够时先省略低优先级的徽章）。
     *  摘要由本类在每次绘制前按当时的宽度截断，再写进基类的副标签，由基类照常绘制（字体、颜色、选中态
     *  与其它行一致）。因此这类行用基类 GetItemSubLabel 取到的是截断后的文字，要取原文请用 GetItemSummary。
     *  摘要为空时基类副标签写一个空格，行仍按两行排版（名字在上半行，与时间同高）。
     *    id    节点 id（AddRoot / AddChild 的返回值）。
     *    text  摘要原文；NULL 按空串处理。
     */
    void SetItemSummary(int id, LPCTSTR text);

    // 某行由 SetItemSummary 设下的摘要原文；不是会话行版式的行返回空串。只读查询。
    //   id：节点 id。
    CString GetItemSummary(int id) const;

    // 某行是否为会话行版式（调用过 SetItemSummary）。只读查询，供单元测试与主面板核对。
    //   id：节点 id。返回：是会话行版式返回 true。
    bool IsItemConversationLayout(int id) const { return m_summaryRows.count(id) != 0; }

    /**
     *  设置某节点头像右下角的在线状态角标（2026-10-07 起取代行右端的状态徽标）。
     *  只有「离开」「忙碌」两种状态画角标：一个带描边的小圆，字形与配色与 PresenceBadge 相同（离开是橙色
     *  圆底加白色时钟指针，忙碌是红色圆底加白色横杠）；在线不画；离线由头像灰度表示（SetItemIconGrayed），
     *  也不画。角标画在头像上，不占文字宽度，不影响右侧时间与未读数的位置；行上没有头像（SetItemIcon）时不画。
     *    id        节点 id。
     *    presence  在线状态，取值见 net/Protocol.h 的 presence_type；传负数（如 kNoPresence）表示不知道，
     *              去掉该行的记录。状态没变时直接返回。
     */
    void SetItemPresence(int id, int presence);

    // 某行登记的在线状态（presence_type 的取值）；没有登记时返回 kNoPresence。只读查询，供单元测试与主面板核对。
    //   id：节点 id。
    int GetItemPresence(int id) const;

    /**
     *  清空所有节点的行装饰接管记录（上排右侧文字 / 未读红点 / 置顶 / 免打扰 / 特别关注 /
     *  收藏 / 字幕 / 通话中 / 摘要 / 在线状态 / 已写入的占位空格个数）。
     *  任何一棵树整表重建（Clear 后重填）之前都要调用 —— 基类 Clear 只清节点，
     *  不认识本子类的这几个映射。基类 Clear 不重置节点 id 计数（DuiTreeView.cpp 的
     *  AddRoot / AddChild 一律 m_nextId++），新节点不会继承旧节点的记录，但旧条目会
     *  一直留在映射里，且绘制时要逐条遍历。会话树与群树都要调。
     */
    void ClearAllRowDecorations();

    /**
     *  DuiControl 覆写:绘制。顺序如下：
     *    1) 会话行版式的行先按当前宽度算出第二行排布，把截断后的摘要写进基类副标签；
     *    2) 基类把整行其余内容（行底色 / 头像 / 标题 / 预览）画完（被本类接管的行，基类右侧文字是占位空格、不可见）；
     *    3) 头像右下角的在线状态角标；
     *    4) 行标记（名字之后，或会话行的第二行）；
     *    5) 置顶行底色与左侧竖条、上排时间（与姓名同高）、下排提醒模式图标与未读胶囊（与预览同高；
     *       不提醒的模式下胶囊由红转灰）。
     *
     *  置顶底色之所以画在基类<u>之后</u>而非之前：基类对每一行都会先用行背景色
     *  FillSolidRect 铺一遍底（见 DuiTreeView::OnPaint 单列分支），画在前面会被
     *  它整片盖掉；故改为事后覆一层极低透明度的暗色。透明度取得很低，压在文字与
     *  头像上肉眼看不出发灰，只让整行读起来比旁边的行"重"一点。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标）；用于跳过不在脏区内的行，省去无谓绘制。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    /**
     *  所在窗口的 DPI 变化时由宿主调用。各行写进基类 rightText 的占位空格个数，是按
     *  旧 DPI 下的默认字体量出来的；字号变了以后要按新字体重新算一遍，否则右侧时间、
     *  角标的预留宽度与实际绘制宽度对不上。
     *    dpi  新的 DPI（重算时经 GetDefaultFont 间接使用，不直接读取）。
     */
    void OnDpiChanged(int dpi) override;

private:
    /**
     *  算出与脏区相交的可见行号范围 [outFirst, outLast]（行矩形与 DuiTreeView 单列绘制
     *  同口径）。没有相交的行时 outLast < outFirst。OnPaint 与 PaintNameMarks 只遍历这一段，
     *  几千行的列表每次绘制只看十几行。
     *    rcDirty：本次重画的区域（客户区坐标）。
     */
    void DirtyRowRange_(const RECT& rcDirty, int& outFirst, int& outLast) const;

    /**
     *  为脏区里会话行版式的行做绘制前的准备：按当前宽度算出第二行的排布（LayoutConvRowSecondLine），
     *  把摘要原文截断到摘要矩形的宽度后写进基类副标签（与现有内容相同时不写），并把排布结果交给调用方，
     *  供随后画第二行的标记。必须在基类绘制之前调用，基类才会按截断后的文字画副标签。
     *    hdc      目标设备上下文，只用来选字体测量摘要宽度，不在上面绘制。
     *    rcDirty  脏矩形（客户区坐标）。
     *    outRows  输出：脏区里每个会话行版式的行一项（先清空再填）。
     */
    void PrepareConvRows_(HDC hdc, const RECT& rcDirty, std::vector<ConvRowPaint>& outRows);

    /**
     *  画头像右下角的在线状态角标（离开、忙碌两种）。头像的位置按基类的排布推出：头像右边界 = 名字起点 -
     *  基类的头像与文字间距，头像在行内垂直居中、边长为 GetIconSize()。由 OnPaint 在基类画完之后调用；
     *  一行都没有登记在线状态时立即返回。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标）；不与之相交的行跳过。
     */
    void PaintPresenceCorners_(HDC hdc, const RECT& rcDirty);

    /**
     *  画行标记：特别关注星标、「我的收藏」书签、「字幕模式」徽章、「通话中」胶囊、「仅通知」标签。
     *  通讯录与群列表的行画在名字之后（依次排开）；会话行版式的行画在第二行，位置取自 convRows 里的排布结果。
     *  由 OnPaint 在基类画完之后调用；五个集合都为空时立即返回，不产生任何开销。
     *
     *  名字之后的横坐标取自基类的 GetItemLabelTextRect —— 名字画到哪儿结束只有基类算得出
     *  （缩进层级、图标尺寸、右侧占位、省略号截断都影响它）。取不到（节点不可见、
     *  标签为空、可用宽度被挤没）时该行不画，不做任何近似猜测。
     *    hdc       目标设备上下文。
     *    rcDirty   脏矩形（客户区坐标）；不与之相交的行跳过。
     *    convRows  PrepareConvRows_ 算出的会话行排布结果；只读。
     */
    void PaintNameMarks(HDC hdc, const RECT& rcDirty, const std::vector<ConvRowPaint>& convRows);

    /**
     *  某节点名字之后的标记（星标、收藏徽章、字幕徽章、通话中胶囊、仅通知标签）连同间距一共占多宽，单位像素；
     *  没有标记时为 0。会话行版式的行标记不在名字之后，恒为 0。
     *    id  节点 id。
     */
    int NameMarksWidth_(int id) const;

    /**
     *  某节点下排未读数的宽度，单位像素：有未读数时为数字胶囊宽，没有未读数但手动标为未读时为小红点直径，否则为 0。
     *    id  节点 id。
     */
    int UnreadWidth_(int id) const;

    /**
     *  「通话中」胶囊的宽度，单位像素：左右内边距 + 图标 + 图标与文字的间距 + 「通话中」按当前界面语言译文的文字宽。
     *  文字宽用基类默认字体量（与绘制同一字体），DPI 变化后由 OnDpiChanged 间接重算。
     */
    int InCallCapsuleWidth_() const;

    /**
     *  「仅通知」标签的宽度，单位像素：左右内边距 + 「仅通知」按当前界面语言译文缩小后的文字宽。
     */
    int NotifyOnlyCapsuleWidth_() const;

    /**
     *  某节点左侧文字列（名字与摘要）的右边界，客户区坐标：基类按"行右端 - 右内边距 - 右侧占位宽 - 间距"
     *  收窄左侧文字，与 DuiTreeView 单列绘制同口径。占位宽按本类写进基类 rightText 的空格个数测量，
     *  因此只对由 UpdateRightReserve_ 写过占位的行准确。
     *    id  节点 id。
     */
    int BaseTextRight_(int id) const;

    /**
     *  重算某节点右侧列的预留宽度并写回基类 rightText（等宽空格占位）。
     *    · 会话行版式的行：只按上排时间的宽度预留，名字只为时间让位；第二行的摘要由本类自己截断。
     *    · 其它行：取"上排文字宽"与"下排（提醒模式图标 + 未读胶囊）宽"的较大者，再加上名字之后的标记宽度，
     *      让基类把左侧标题 / 预览裁剪在右侧列左边、并给标记留出位置。
     *  时间 / 未读数 / 提醒模式 / 各标记 / 摘要任一变化后都要调。
     *    id  节点 id。
     */
    void UpdateRightReserve_(int id);

    // 节点 id → 上排右侧文字（会话树放时间，群树放成员数）。联系人树恒空。整表重建前
    // 由 ClearAllRowDecorations 清空。键存在即由本类接管该行右侧绘制（基类 rightText
    // 里是等宽空格占位）。文字为空的节点不入表，好让不用这个能力的树保持短路。
    std::map<int, CString> m_rightTopText;

    // 节点 id → 未读角标数字。值 <= 0 或键不存在表示该行不画红点。仅会话树填充。
    std::map<int, int> m_badges;

    // 置顶的节点 id 集合。命中即给整行覆一层淡暗色并在左侧画橙色竖条。会话树与群树填充。
    std::set<int> m_pinnedRows;

    // 节点 id → 消息提醒模式（只记会画徽章的三种：接收不提醒、收进消息盒子、屏蔽）。命中即在
    // 右下角画对应徽章、且未读胶囊改用灰底。会话树、联系人树、群树共用；整表重建前由
    // ClearAllRowDecorations 清空。
    std::map<int, int> m_notifyModeRows;

    // 未读胶囊用灰底、但不画徽章的节点 id 集合（会话列表里的「消息盒子」那一行）。整表重建前由
    // ClearAllRowDecorations 清空。
    std::set<int> m_quietRows;

    // 被「特别关注」的节点 id 集合。通讯录行与最近会话行共用，为空时 OnPaint 的
    // 星标那一段整段跳过（群树、以及一个人都没关注时零开销）。
    std::set<int> m_starredRows;

    // 已收藏的节点 id 集合。部门树、群树、会话树共用；与 m_starredRows 同时为空时
    // OnPaint 的名字标记那一段整段跳过。整表重建前由 ClearAllRowDecorations 清空。
    std::set<int> m_favoriteRows;

    // 字幕开着的节点 id 集合（画「字幕模式」徽章）。会话树、联系人树、群树共用；为空时绘制这一段整段跳过。
    // 整表重建前由 ClearAllRowDecorations 清空（主面板重建后按字幕集合重新设置）。
    std::set<int> m_subtitleRows;

    // 节点 id → 「通话中」徽章的媒体类型（1 语音、2 视频）；不在通话中的节点不入表。会话树、联系人树、群树共用，
    // 为空时 OnPaint 的胶囊那一段整段跳过。整表重建前由 ClearAllRowDecorations 清空（主面板重建后会重新设置）。
    std::map<int, int> m_inCallRows;

    // 开启了「仅通知模式」（原称全员禁言）的群的节点 id 集合，画灰色小标签「仅通知」（docs/群管理接入方案.md §14）。
    // 会话树、群树共用，为空时绘制这一段整段跳过。整表重建前由 ClearAllRowDecorations 清空（主面板重建后重新设置）。
    std::set<int> m_notifyOnlyRows;

    // 画「未读小红点」的节点 id 集合（会话被手动标为未读，docs/会话标为未读方案.md）。会话树用，为空时绘制这一段
    // 不增加开销。整表重建前由 ClearAllRowDecorations 清空。
    std::set<int> m_unreadDotRows;

    // 节点 id → 会话行版式的摘要原文（SetItemSummary 设下的，可以是空串）。键存在即该行按会话行版式排布与绘制，
    // 基类副标签里是本类截断后的文字。会话树与消息盒子窗口的树填充；整表重建前由 ClearAllRowDecorations 清空。
    std::map<int, CString> m_summaryRows;

    // 节点 id → 在线状态（presence_type 的取值，只记已知的状态）。离开、忙碌两种在头像右下角画角标。会话树的单聊行、
    // 联系人树的联系人行填充，在线状态推送时由 PresenceTreeMarks 就地改写；整表重建前由 ClearAllRowDecorations 清空。
    std::map<int, int> m_presenceRows;

    // 节点 id → 上次经 UpdateRightReserve_ 写进基类 rightText 的占位空格个数。算出来的
    // 个数与上次相同时不再写回：基类 SetItemRightText 要按 id 逐个扫描节点，会话树
    // 每个节点在填充时要算三四次占位，而结果几乎总是不变（2026-10-01 基准实测这部分
    // 约占一次会话列表重建的四成）。
    // 约定：设了上排文字 / 角标 / 标记的行，其基类 rightText 只由 UpdateRightReserve_
    // 写，调用方不要再直接 SetItemRightText，否则这里记的个数与实际内容不一致，下次
    // 个数相同时会跳过写回。整表重建前由 ClearAllRowDecorations 清空。
    std::map<int, int> m_rightSpacerLen;
};

} // namespace balloonmain
