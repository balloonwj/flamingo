/**
 *  BalloonContactWindow.cpp
 *
 *  联系人资料对话框的实现。标签增删 + 备注名行内编辑 + 三个开关（消息免打扰 /
 *  添加到收藏 / 特别关注）+ 分组下拉 + 快捷操作行（语音通话 / 视频通话 /
 *  历史消息 / 组织架构）+ 底部按钮。
 *
 *  三个开关与四个快捷操作都不在本窗口执行，只把用户的意图回投给主面板，理由见
 *  BalloonContactWindow.h 的文件说明。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"
#include "BalloonContactWindow.h"

#include <memory>
#include <windowsx.h>                 // GET_X_LPARAM / GET_Y_LPARAM（鼠标消息取坐标）

#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Session/UserSessionData.h"       // FMG_MSG_OPEN_CHAT（"发消息"路由）/ FMG_MSG_SET_CONTACT_REMARK（备注提交回投）/ FMG_MSG_SET_CONTACT_TAGS（标签整份提交回投）/ FMG_MSG_SET_CONTACT_MUTE 等三条开关消息 / FMG_MSG_LOCATE_CONTACT / FMG_MSG_CALL_START_INVITE 等
#include <string>                     // 备注 / 标签回投的 UTF-8 堆负载，通话邀请的 std::wstring 显示名
#include <vector>                     // 标签整份提交的列表
#include <stdint.h>                   // int64_t（历史消息记录窗回投的会话 id）
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_ERROR / LOG_WARNING
#include "../BalloonMain/OnlineCapability.h"   // 快捷操作行的离线门禁（IsActionAllowedInState）
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标）
#include "../../resource.h"                   // IDR_MAINFRAME（任务栏 / Alt-Tab 图标）
#include "ContactLayout.h"            // BuildContactBody / ContactCtrlId 等
#include "ContactTagModel.h"          // 标签的归一与增 / 改判定（纯函数）
#include "TagChip.h"                  // TagChip::DUIN_TAGCHIP_DELETE / DUIN_TAGCHIP_EDIT
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace ballooncontact;

// ---- 文件作用域常量 -----------------------------------------------------

namespace {

// 联系人资料对话框的客户区宽度（逻辑像素）。与布局侧共用同一份常量，
// 免得两处各写一个 420 日后走岔。
// 高度不是固定值：部门路径整宽区块的行数决定窗口要高多少，由
// ballooncontact::ContactBodyHeight(行数) 算出，见 ApplyBodyHeight。
const int kContactWindowWidth = ballooncontact::kContactBodyWidth;

// 标题栏高度（逻辑像素，照设计稿 Modal header 44）。
const int kContactTitleBarHeight = 44;

// 标题栏左侧程序图标的边长（逻辑像素）。balloonui 最终按 16x16 绘制，取同值可免去缩放。
const int kTitleIconSize = 16;

// 标题栏底色。本窗口没有把标题栏设成透明，balloonui 画的就是这个颜色
// （见 DuiFrameWindow.cpp 的 Title strip background 一段）。图标位图必须合成到
// 同色底上，否则图标四周会出现一块与标题栏不同色的方块，缘由详见 TitleBarIcon.h。
const COLORREF kTitleBarBgColor = RGB(245, 246, 250);

// ---- 动画的推进方式 ----------------------------------------------------
// 复制成功的轻量提示（DuiToast）走渐入 / 渐出动画、开关（DuiSwitch）鼠标翻转走
// 150ms 滑动动画，两者都由 DuiAnimMgr 推进。本窗口<u>不</u>需要为此挂定时器：
// DuiAnimMgr 自 2026-08-14 起自带脉冲定时器（见 balloonui/DuiAnimation.cpp 的
// StartPulse / PulseProc），有动画在跑时它自己按 16ms 发 WM_TIMER，跑完即停。
// 该定时器是线程定时器（SetTimer 的 hwnd 传 NULL），任何转发未过滤消息的消息循环
// 都能驱动它，本窗口用的正是这种循环。

// ---- 标签相关的提示文案 ------------------------------------------------
// 标签操作被判定拦下时弹给用户看的一句话。含具体数字的两条（"最长 N 个字符"、
// "最多 N 个"）在用到的地方按上限常量拼出来，改上限时不必回来改文案，故这里
// 只放不含数字的这一条。

const TCHAR* const kTipTagDuplicate = _T("该标签已存在");

// "新增标签"时用来表示"这次不是在改某一枚已有标签"的下标值。
const int kTagIndexNone = -1;

// ---- 打开历史消息记录窗时 wParam 的打包（约定见 UserSessionData.h 上
//      FMG_MSG_OPEN_HISTORY 的说明：低字节 = 会话类型，次低字节 = 预选的类型胶囊）----

// 会话类型：1 = 单聊（v2 协议的 conversation_single）。联系人资料对话框只可能打开
// 单聊的历史记录，故取固定值。
const int kHistoryConvTypeSingle = 1;

// 打开后预选哪一个类型胶囊：0 = 全部（balloonhistory::kHistoryTypeAll）。本入口是
// "看看和这个人聊过什么"，不预先筛掉任何类型。
const int kHistoryTypeFilterAll = 0;

// 类型胶囊在 wParam 里的位偏移 —— 它占的是次低字节，故左移一个字节的位数。
const int kHistoryTypeFilterShift = 8;

// CString（宽字符）转 UTF-8 字节。备注名要跨窗口回投给主面板、并最终进 JSON 报文，
// 两处都以 UTF-8 为准。空串原样返回空串。
std::string CStrToUtf8(const CString& text)
{
    if (text.IsEmpty())
    {
        return std::string();
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
    {
        return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, -1, &out[0], len, NULL, NULL);
    // WideCharToMultiByte 带上了结尾的 '\0'，std::string 自己管长度，去掉它。
    if (!out.empty() && out[out.size() - 1] == '\0')
    {
        out.resize(out.size() - 1);
    }
    return out;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonContactWindow::CBalloonContactWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_hostPanel(NULL)
    , m_peerUserId(0)
    , m_remarkEditing(false)
    , m_deptLineCount(0)
    , m_tagRowCount(1)
    , m_editingTagIndex(kTagIndexNone)
    , m_titleIconBmp(NULL)
    , m_hTaskIconBig(NULL)
    , m_hTaskIconSmall(NULL)
    , m_connUiState((int)balloonmain::kConnUiOnline)
{
    // 标签输入弹层的提交回调只需注册一次，弹层本身随窗口存活。
    m_tagPopup.SetCommitCallback(&CBalloonContactWindow::OnTagPopupCommitThunk, this);
}

void CBalloonContactWindow::OpenForContact(HWND hostPanel, UINT peerUserId,
                                           const ballooncontact::ContactModel& model,
                                           bool beginRemarkEdit)
{
    m_hostPanel  = hostPanel;
    m_peerUserId = peerUserId;
    m_model      = model;

    // 已打开：用新数据重建客户区并前置；未打开：走创建流程。
    if (IsWindow())
    {
        InitUi();
        // 换了联系人，部门路径的行数可能跟着变，窗口高度要重算 —— 漏掉这一步的话，
        // 从部门路径只有一行的联系人切到三行的联系人，底部按钮会被挤出窗口。
        ApplyBodyHeight();
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ::SetForegroundWindow(m_hWnd);
    }
    else
    {
        CreateAndShow(NULL);
    }

    // 右键菜单「修改备注」的入口：窗口就绪后立刻进编辑态。必须放在窗口创建 / 重建
    // 客户区<u>之后</u> —— SetRemarkEditing 要在控件树里找到备注名文本框并给它
    // 设焦点，而那个控件是 InitUi 里才建出来的。
    if (beginRemarkEdit && IsWindow())
    {
        SetRemarkEditing(true);
    }
}

void CBalloonContactWindow::RefreshContactData(const ballooncontact::ContactModel& model)
{
    // 未打开就不刷（下次打开会用最新缓存重建）。
    if (!IsWindow())
    {
        return;
    }
    m_model = model;
    InitUi();        // 用新数据重建客户区
    // 推送来的新资料可能换了部门，部门路径行数变了窗口高度也要跟着变。
    ApplyBodyHeight();
    // 不调 SetForegroundWindow —— 后台静默刷新,不抢用户当前焦点。
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CBalloonContactWindow::ApplyConnectionState(int state)
{
    // 状态没变就不必再记一条日志 —— 主面板是每次连接态变化逐窗推的，重复值很常见。
    if (m_connUiState == state)
    {
        return;
    }
    m_connUiState = state;

    // 本窗口界面上没有连接态提示条，连接态只影响快捷操作行的门禁，故这里不重绘。
    LOG_INFO("contact dialog: connection state applied, peer=%u, state=%d "
             "(0=online 1=connecting 2=offline).", m_peerUserId, state);
}

CBalloonContactWindow::~CBalloonContactWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。

    // 标题栏图标位图与任务栏图标由本类持有：DuiFrameWindow::SetIcon 只借用不释放，
    // WM_SETICON 交给系统的图标同样不由系统释放，都要在这里还回去。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_hTaskIconBig != NULL)
    {
        ::DestroyIcon(m_hTaskIconBig);
        m_hTaskIconBig = NULL;
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::DestroyIcon(m_hTaskIconSmall);
        m_hTaskIconSmall = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonContactWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("联系人资料对话框 Create 失败。"));
        return;
    }

    // 先按当前模型摆一个标题：InitUi 末尾还会再刷一次，但 InitUi 有可能在客户区
    // 构建失败时提前返回，那种情况下标题栏也不该是空的。
    UpdateWindowTitle();
    SetTitleBarHeight(kContactTitleBarHeight);
    //最小化 + 关闭。本窗口是无属主的独立顶层窗口（上面 Create 的第一个参数为 NULL、
    //也没有 WS_EX_TOOLWINDOW），最小化后在任务栏上有自己的按钮，点回来即可还原。
    //不给最大化：窗口尺寸由内容算出、SetResizable(false) 也不允许缩放。
    SetButtons(true, false, true);
    SetResizable(false);              // 固定尺寸对话框

    //标题栏左侧的程序图标。位图只建一次，跨"关闭再打开"复用；本类持有，析构时释放。
    if (m_titleIconBmp == NULL)
    {
        m_titleIconBmp = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBgColor);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    //任务栏 / Alt-Tab 图标。balloonui 的窗口类没有关联类图标，不显式设置的话，
    //窗口最小化后在任务栏上是一块空白 —— 本窗口现在有最小化按钮，这一步不能省。
    if (m_hTaskIconBig == NULL)
    {
        m_hTaskIconBig = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                          ::GetSystemMetrics(SM_CXICON),
                                          ::GetSystemMetrics(SM_CYICON));
    }
    if (m_hTaskIconSmall == NULL)
    {
        m_hTaskIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                            ::GetSystemMetrics(SM_CXSMICON),
                                            ::GetSystemMetrics(SM_CYSMICON));
    }
    if (m_hTaskIconBig != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_hTaskIconBig);
    }
    if (m_hTaskIconSmall != NULL)
    {
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hTaskIconSmall);
    }

    // 先把客户区调整到对话框真实的宽度，再构建客户区内容。窗口是以
    // CWindow::rcDefault 建出来的，此刻客户区宽度还是系统给的默认值（实测一千四百
    // 多像素）；而 InitUi 里同步标签区时要按滚动视口实际布局出来的宽度数标签占几行，
    // 量到那个默认宽度就会把十来个标签算成一行。高度这时还不知道（取决于部门路径与
    // 标签各占几行），先按"没有部门、标签一行"的基准高度占位，InitUi 之后由
    // ApplyBodyHeight 改写成真实高度。
    ResizeClient(kContactWindowWidth, ballooncontact::ContactBodyHeight(0, 1));

    // 窗口高度取决于部门路径折了几行，只有 InitUi 把资料灌进控件之后才知道，
    // 故 SetMinSize / 真实高度的 ResizeClient 都放在 InitUi 之后由 ApplyBodyHeight 统一做。
    InitUi();
    ApplyBodyHeight();
    CenterWindow();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    LOG_INFO(_T("联系人资料对话框已创建并显示。"));
}

// =======================================================================
// 客户区初始化
// =======================================================================

void CBalloonContactWindow::InitUi()
{
    std::unique_ptr<DuiControl> root = BuildContactBody();
    if (!root)
    {
        LOG_ERROR(_T("BuildContactBody 返回空 —— 联系人资料对话框客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 备注名行内编辑用的文本框：画出边框并限制长度。
    DuiEdit* remarkEdit = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdRemarkEdit));
    if (remarkEdit != NULL)
    {
        remarkEdit->SetShowBorder(true);
        remarkEdit->SetMaxLength(32);   // 备注名上限 32 字符
        // 默认处于非编辑态 —— 文本框先隐藏，让出位置给当前值 + 铅笔按钮。
        remarkEdit->SetVisible(false);
    }

    // 把模型数据灌进各控件。
    FillHeader();
    RebuildTags();
    FillGroupCombo();
    FillSwitches();

    // 「备注名」行右侧当前值显示。
    {
        const CString& remark = m_model.Remark();
        SetContactText(content, kIdRemarkValue,
                       remark.IsEmpty() ? _T("未设置") : (LPCTSTR)remark);
    }

    // 标题随联系人走。放在这里而不是只放在 CreateAndShow 里：窗口已经开着时切换到
    // 另一个联系人（OpenForContact）或收到资料变更推送（RefreshContactData），走的
    // 都是重新调 InitUi 这条路，标题必须跟着一起换。
    UpdateWindowTitle();

    LOG_INFO(_T("联系人资料对话框客户区 UI 初始化完成。"));
}

void CBalloonContactWindow::UpdateWindowTitle()
{
    if (!IsWindow())
    {
        return;
    }

    const CString& name = m_model.Name();
    if (name.IsEmpty())
    {
        SetTitle(_T("联系人资料"));
        return;
    }

    CString title = name;
    title += _T("的资料");
    SetTitle(title);
}

void CBalloonContactWindow::FillHeader()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 头像 —— XML 里已设了占位字母 / 形状 / 状态 / 底色，这里按模型再确认
    // 占位字母与状态（模型未来接真实数据时此处即为唯一灌入点）。
    DuiAvatar* avatar = dynamic_cast<DuiAvatar*>(
        content->FindCtrlById(kIdAvatar));
    if (avatar != NULL)
    {
        avatar->SetName(m_model.AvatarLetter());
        // 模型状态映射到 DuiAvatar 的状态点。
        switch (m_model.Status())
        {
        case kContactStatusOnline:
            avatar->SetStatus(DuiAvatar::StatusOnline);
            break;
        case kContactStatusAway:
            avatar->SetStatus(DuiAvatar::StatusAway);
            break;
        case kContactStatusBusy:
            avatar->SetStatus(DuiAvatar::StatusBusy);
            break;
        case kContactStatusOffline:
            avatar->SetStatus(DuiAvatar::StatusOffline);
            break;
        default:
            avatar->SetStatus(DuiAvatar::StatusNone);
            break;
        }
    }

    // 头部右侧多行资料块 —— 各行文本由模型拼出，交自由函数按 id 配置。

    // 主名是真名，备注跟在后面的括号里 —— 与通讯录树 / 会话列表的「真名（备注）」
    // 同一形态。2026-08-13 之前是反过来的（备注作主名、真名进括号），改动的理由是
    // 企业场景下认人靠真实姓名，备注只是个人给自己加的记号，不该把真名挤掉。
    CString primaryName = m_model.Name();

    // 括号内容：备注名。只有空白字符的备注按没有处理，免得画出一对空括号。
    CString remark = m_model.Remark();
    remark.Trim();

    CString parenText;
    // 备注与真名一模一样时不再重复一遍，否则会显示成「张三（张三）」。
    if (!remark.IsEmpty() && remark != primaryName)
    {
        // 括号用全角：主名两侧多是中文，半角括号夹在中文之间两边贴得太紧。
        parenText = _T("（");
        parenText += remark;
        parenText += _T("）");
    }

    // 第 2 行只放职务。2026-08-14 之前这一行是「职务 · 部门路径」，且要求职务与
    // 部门<u>都</u>非空才显示 —— 于是有部门却没填职务的人，部门一个字也看不到。
    // 现在部门路径独立成整宽区块（下面的 ConfigureContactDeptPath），两者各自
    // 判空：职务非空就显示这一行，部门非空就显示那一块，互不牵连。
    CString title = m_model.Title();

    // 第 3 行「账号 XXX · 地区」。
    CString accountRegion;
    accountRegion = _T("账号 ");
    accountRegion += m_model.Account();
    if (!m_model.Region().IsEmpty())
    {
        accountRegion += _T(" · ");
        accountRegion += m_model.Region();
    }

    ConfigureContactHeader(content, primaryName, parenText, title,
                           accountRegion, m_model.Signature(), m_model.Account());

    // 部门路径整宽区块。行数决定这一块占多高，也决定整个窗口要多高，记下来给
    // ApplyBodyHeight 用。
    m_deptLineCount = ConfigureContactDeptPath(content, m_model.Dept());
    LOG_INFO("contact dialog: dept path wrapped into %d line(s).", m_deptLineCount);
}

void CBalloonContactWindow::ApplyBodyHeight()
{
    if (!IsWindow())
    {
        return;
    }

    const int height = ballooncontact::ContactBodyHeight(m_deptLineCount, m_tagRowCount);
    SetMinSize(kContactWindowWidth, height);
    ResizeClient(kContactWindowWidth, height);

    // 高度变了要让控件树按新矩形重排一次，否则新腾出来的空间没人认领。
    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        RelayoutContactBody(content);
    }
}

void CBalloonContactWindow::RebuildTags()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 标签流式容器 —— 增删 / 遍历子控件用 DuiControl 基类接口即可，
    // 无需 ContactTagFlow 的完整类型（它是 Layout 文件局部类型）。
    DuiControl* flow = content->FindCtrlById(kIdTagFlow);
    if (flow == NULL)
    {
        return;
    }

    // 清空原有胶囊 + 「+ 添加标签」按钮，整体重建。
    while (!flow->Children().empty())
    {
        flow->RemoveChild(flow->Children().front().get());
    }

    // 按模型标签列表逐个建胶囊；ctrlId = kIdTagChipBase + 下标。超出 ctrlId 预留
    // 区间的部分不建 —— 服务端理应保证不超上限，真收到超量数据时宁可少画几枚，
    // 也不能让胶囊的 ctrlId 撞进后面别的控件的编号里去。
    const int tagCount = m_model.TagCount();
    const int chipCount = (tagCount > kContactTagMaxCount) ? kContactTagMaxCount : tagCount;
    for (int i = 0; i < chipCount; ++i)
    {
        std::unique_ptr<DuiControl> chip =
            CreateTagChip(m_model.Tag(i), (UINT)(kIdTagChipBase + i));
        flow->AddChild(std::move(chip));
    }

    // 末尾追加「+ 添加标签」按钮；标签数已达上限时改为一行说明文字 —— 按钮还在
    // 却一按就弹"加不了了"是很差的体验，不如直接把原因写在原位置上。
    if (tagCount >= kContactTagMaxCount)
    {
        flow->AddChild(CreateTagLimitHint());
    }
    else
    {
        flow->AddChild(CreateAddTagButton());
    }

    // 重算标签占几行，同步滚动范围与标签区高度提示，再按新行数调整窗口高度。
    // ApplyBodyHeight 内部会做一次整树重排。
    m_tagRowCount = SyncContactTagArea(content);
    ApplyBodyHeight();

    // 整树重排是经各容器的 SetRect 一层层下传的，而 SetRect 有 EqualRect 判等
    // 短路：矩形没变就不再调 Layout。标签区有两种"矩形没变但内容变了"的情形，
    // 都要靠 ForceLayout 无条件重跑 Layout 补上：
    //
    //   · 标签行数已经封顶（第 4 行往后只增内容不增高度），标签区 vbox 的矩形
    //     一模一样，滚动视口便收不到 Layout，SyncContactTagArea 刚设进去的内容
    //     总高度也就没机会落到内容控件的矩形上 —— 表现为多出来的那几行既显示
    //     不出来、又滚不到。
    //   · 行数没变、只是换了几枚胶囊，流式容器自身矩形不变，新建的胶囊拿不到
    //     矩形，OnPaint 在宽高为 0 处直接返回，整片标签区都不显示。
    //
    // 顺序不能反：先让滚动视口按新的内容总高度摆好内容矩形，再让流式容器在这个
    // 矩形里重排各枚胶囊。
    DuiControl* scrollToLayout = content->FindCtrlById(kIdTagScroll);
    if (scrollToLayout != NULL)
    {
        scrollToLayout->ForceLayout(scrollToLayout->GetRect());
    }
    DuiControl* flowToLayout = content->FindCtrlById(kIdTagFlow);
    if (flowToLayout != NULL)
    {
        flowToLayout->ForceLayout(flowToLayout->GetRect());
        flowToLayout->Invalidate();
    }

    // 行数是按测量出来的胶囊宽度数出来的，出问题时光看界面很难判断是"数错了"
    // 还是"排错了"，故把行数与流式容器实际拿到的矩形一起记下来。
    const RECT rcFlow = (flowToLayout != NULL) ? flowToLayout->GetRect() : RECT();
    LOG_INFO("contact dialog: tag area rebuilt, %d tag(s) in %d row(s), flow rect %d,%d-%d,%d.",
             tagCount, m_tagRowCount,
             (int)rcFlow.left, (int)rcFlow.top, (int)rcFlow.right, (int)rcFlow.bottom);
}

void CBalloonContactWindow::FillGroupCombo()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(
        content->FindCtrlById(kIdGroupCombo));
    if (combo == NULL)
    {
        return;
    }

    combo->ResetContent();
    const int groupCount = m_model.GroupCount();
    for (int i = 0; i < groupCount; ++i)
    {
        combo->AddString(m_model.Group(i));
    }
    // 选中模型当前分组 —— notify=false，避免初始化时假触发 VALUECHANGED。
    combo->SetCurSel(m_model.SelectedGroup(), false);
}

void CBalloonContactWindow::FillSwitches()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 各开关的 ctrlId 与对应模型开关类别。两张表按行一一对应，顺序即界面上
    // 自上而下的顺序（消息免打扰 / 添加到收藏 / 特别关注）。
    const UINT switchIds[kContactSwitchCount] =
    {
        kIdSwitchMute, kIdSwitchFavorite, kIdSwitchStarred
    };
    const int switchKinds[kContactSwitchCount] =
    {
        kContactSwitchMute, kContactSwitchFavorite, kContactSwitchStarred
    };

    for (int i = 0; i < kContactSwitchCount; ++i)
    {
        DuiSwitch* sw = dynamic_cast<DuiSwitch*>(
            content->FindCtrlById(switchIds[i]));
        if (sw != NULL)
        {
            // animated=false、notify=false —— 只是把模型值同步进控件。
            sw->SetChecked(m_model.Switch(switchKinds[i]), false, false);
        }
    }
}

// =======================================================================
// 备注名行内编辑
// =======================================================================

void CBalloonContactWindow::SetRemarkEditing(bool editing)
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    m_remarkEditing = editing;

    // 当前值文本与铅笔按钮只需切换显隐 —— 用 DuiControl 基类接口即可，
    // 无需其文件局部具体类型。文本框需要 DuiEdit 的接口。
    DuiControl* remarkValue = content->FindCtrlById(kIdRemarkValue);
    DuiControl* remarkBtn   = content->FindCtrlById(kIdRemarkEditBtn);
    DuiEdit* remarkEdit = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdRemarkEdit));

    if (editing)
    {
        // 进入编辑态：隐藏当前值 + 铅笔，显示文本框并填入当前备注。
        if (remarkValue != NULL)
        {
            remarkValue->SetVisible(false);
        }
        if (remarkBtn != NULL)
        {
            remarkBtn->SetVisible(false);
        }
        if (remarkEdit != NULL)
        {
            remarkEdit->SetText(m_model.Remark());
            remarkEdit->SetVisible(true);
        }
    }
    else
    {
        // 退出编辑态：隐藏文本框，恢复当前值 + 铅笔。
        if (remarkEdit != NULL)
        {
            remarkEdit->SetVisible(false);
        }
        if (remarkValue != NULL)
        {
            remarkValue->SetVisible(true);
        }
        if (remarkBtn != NULL)
        {
            remarkBtn->SetVisible(true);
        }
    }

    // 显隐变化影响行内布局 —— 强制整树重排。
    RelayoutContactBody(content);

    // 把焦点交给文本框并全选，方便用户直接覆盖输入。文本框改成无窗口实现之后
    // 一次 SetFocus 就够了：它既把 DUI 焦点指到本控件，又让宿主窗口把 Win32 键盘
    // 焦点拿到手（无窗口控件没有自己的窗口，按键消息要先投递到宿主才能转下来）。
    if (editing && remarkEdit != NULL)
    {
        remarkEdit->SetFocus();
        remarkEdit->SelectAll();
    }
}

void CBalloonContactWindow::CommitRemarkEdit()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    DuiEdit* remarkEdit = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdRemarkEdit));
    if (remarkEdit == NULL)
    {
        SetRemarkEditing(false);
        return;
    }
    const CString edited = remarkEdit->GetText();

    // 退出编辑态。
    SetRemarkEditing(false);

    // 有宿主主面板（正常登录态）时，"该不该发、发不出去怎么办、失败怎么退回来"
    // 全部交给主面板判定与执行 —— 它才同时握着通讯录缓存（原值）、连接态与 socket。
    // 本窗口只负责把用户敲进去的原始文本报上去，不在这里再做一份判定，免得两处
    // 规则日后走岔。主面板处理完会回头调 RefreshContactData 把本窗刷成最终状态。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel) && m_peerUserId != 0)
    {
        // 跨窗口传 CString 不安全（生命周期与线程），堆上传一份 UTF-8 字节，
        // 由主面板侧 delete —— 与本工程其余"窗口→主面板"的回投一致。
        std::string* pRemark = new std::string(CStrToUtf8(edited));
        ::PostMessage(m_hostPanel, FMG_MSG_SET_CONTACT_REMARK,
                      (WPARAM)m_peerUserId, (LPARAM)pRemark);
        // 备注名可能含用户隐私，日志只记长度不记内容。
        LOG_INFO(_T("联系人资料对话框提交备注: userid=%u, 长度=%d。"),
                 m_peerUserId, edited.GetLength());
        return;
    }

    // 调试入口（--contact）没有宿主主面板，也没有真实连接：就地改内存并刷新，
    // 好让这条交互在调试入口下仍然能走通、能目检。
    CString trimmed = edited;
    trimmed.Trim();
    m_model.SetRemark(trimmed);
    RefreshRemarkDisplay();
    LOG_INFO(_T("联系人资料对话框（无宿主主面板）：备注仅就地更新，未发协议。"));
}

void CBalloonContactWindow::RefreshRemarkDisplay()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 备注名那一行的当前值；空则显示"未设置"占位，别留一片空白让人以为控件坏了。
    const CString& remark = m_model.Remark();
    SetContactText(content, kIdRemarkValue,
                   remark.IsEmpty() ? _T("未设置") : (LPCTSTR)remark);

    // 头部也要刷：备注名会影响第 1 行的主名与括号里的真名。
    FillHeader();
}

// =======================================================================
// 标签增删
// =======================================================================

bool CBalloonContactWindow::GetTagCtrlScreenRect(UINT ctrlId, RECT& outRectScreen)
{
    ::SetRectEmpty(&outRectScreen);

    DuiControl* content = GetClientContent();
    if (content == NULL || !IsWindow())
    {
        return false;
    }
    DuiControl* ctrl = content->FindCtrlById(ctrlId);
    if (ctrl == NULL)
    {
        return false;
    }

    // DUI 控件的矩形是宿主客户区坐标，换算成屏幕坐标才能交给 WS_POPUP 弹层。
    RECT rc = ctrl->GetRect();
    if (rc.right <= rc.left || rc.bottom <= rc.top)
    {
        return false;   // 尚未布局过，位置无意义
    }
    POINT topLeft     = { rc.left,  rc.top };
    POINT bottomRight = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &topLeft);
    ::ClientToScreen(m_hWnd, &bottomRight);

    outRectScreen.left   = topLeft.x;
    outRectScreen.top    = topLeft.y;
    outRectScreen.right  = bottomRight.x;
    outRectScreen.bottom = bottomRight.y;
    return true;
}

void CBalloonContactWindow::OnTagDeleteClicked(UINT chipCtrlId)
{
    // 胶囊 ctrlId = kIdTagChipBase + 标签下标 —— 反推下标。
    if (chipCtrlId < (UINT)kIdTagChipBase)
    {
        return;
    }
    const int index = (int)(chipCtrlId - (UINT)kIdTagChipBase);
    if (index < 0 || index >= m_model.TagCount())
    {
        return;
    }

    // 删除同样走"整份提交"：把去掉这一枚之后的完整列表报上去。协议本身就是整份
    // 覆盖语义，增、改、删三种操作共用同一条路径。
    std::vector<CString> tags = m_model.Tags();
    tags.erase(tags.begin() + index);
    CommitTagList(tags);

    LOG_INFO("contact dialog: delete tag at index %d.", index);
}

void CBalloonContactWindow::OnTagEditClicked(UINT chipCtrlId)
{
    if (chipCtrlId < (UINT)kIdTagChipBase)
    {
        return;
    }
    const int index = (int)(chipCtrlId - (UINT)kIdTagChipBase);
    if (index < 0 || index >= m_model.TagCount())
    {
        return;
    }

    RECT rcScreen;
    if (!GetTagCtrlScreenRect(chipCtrlId, rcScreen))
    {
        return;
    }

    m_editingTagIndex = index;
    m_tagPopup.OpenAt(rcScreen, m_hWnd, m_model.Tag(index));
    LOG_INFO("contact dialog: open tag editor at index %d.", index);
}

void CBalloonContactWindow::OnAddTagClicked()
{
    RECT rcScreen;
    if (!GetTagCtrlScreenRect((UINT)kIdAddTagButton, rcScreen))
    {
        return;
    }

    m_editingTagIndex = kTagIndexNone;
    m_tagPopup.OpenAt(rcScreen, m_hWnd, _T(""));
    LOG_INFO("contact dialog: open tag editor for a new tag.");
}

void CBalloonContactWindow::OnTagPopupCommitThunk(void* userdata, const CString& text)
{
    CBalloonContactWindow* self = (CBalloonContactWindow*)userdata;
    if (self != NULL)
    {
        self->OnTagPopupCommit(text);
    }
}

void CBalloonContactWindow::OnTagPopupCommit(const CString& text)
{
    DuiControl* content = GetClientContent();
    std::vector<CString> tags = m_model.Tags();

    // 本窗口不判定连接态 —— 该不该发、发不出去怎么办全由主面板决定（见
    // CommitTagList 的说明），故这里恒按"可以发"判定。判定函数留着 online 这个
    // 参数，是为了让主面板侧将来能复用同一份判定。
    const bool kAssumeOnline = true;

    if (m_editingTagIndex == kTagIndexNone)
    {
        // ---- 新增标签 ----
        const TagAddDecision decision = DecideTagAdd(tags, text, kAssumeOnline,
                                                     kContactTagMaxLen,
                                                     kContactTagMaxCount);
        switch (decision.m_action)
        {
        //归一后是个新标签：追加到末尾并提交整份列表。
        case kTagAddAppend:
            tags.push_back(decision.m_normalized);
            CommitTagList(tags);
            break;

        //与已有标签重复：不追加，给一句轻提示，免得用户以为点击没生效。
        case kTagAddDuplicate:
            ShowContactTipToast(content, kTipTagDuplicate);
            break;

        //超长：输入框已 SetMaxLength 限死长度，正常敲不出来，粘贴等路径才会走到。
        case kTagAddTooLong:
            {
                CString tip;
                tip.Format(_T("标签最长 %d 个字符"), kContactTagMaxLen);
                ShowContactTipToast(content, tip);
            }
            break;

        //已达条数上限：满额时界面本来就不显示「+ 添加标签」按钮，属防御性分支。
        case kTagAddFull:
            {
                CString tip;
                tip.Format(_T("标签最多 %d 个"), kContactTagMaxCount);
                ShowContactTipToast(content, tip);
            }
            break;

        //用户什么都没输入、或本窗口判定为不可发起联网操作：都不提示、不发包。
        default:
            break;
        }
        return;
    }

    // ---- 修改已有标签 ----
    const TagEditDecision decision = DecideTagEdit(tags, m_editingTagIndex, text,
                                                   kAssumeOnline, kContactTagMaxLen);
    switch (decision.m_action)
    {
    //改成了一个新值：就地替换该下标的文本并提交。
    case kTagEditUpdate:
        tags[m_editingTagIndex] = decision.m_normalized;
        CommitTagList(tags);
        break;

    //内容被全删光：等价于删除这一枚标签。
    case kTagEditRemove:
        tags.erase(tags.begin() + m_editingTagIndex);
        CommitTagList(tags);
        break;

    //改成了与另一枚等价的文本：两枚合并成一枚。先把留下来的那一枚改成用户这次
    //输入的写法，再删掉被编辑的这一枚 —— 顺序反过来的话下标会先失效。
    case kTagEditMerge:
        if (decision.m_mergeIndex >= 0 && decision.m_mergeIndex < (int)tags.size())
        {
            tags[decision.m_mergeIndex] = decision.m_normalized;
            tags.erase(tags.begin() + m_editingTagIndex);
            CommitTagList(tags);
        }
        break;

    //超长：与新增同样处理。
    case kTagEditTooLong:
        {
            CString tip;
            tip.Format(_T("标签最长 %d 个字符"), kContactTagMaxLen);
            ShowContactTipToast(content, tip);
        }
        break;

    //一个字没动、下标越界、或本窗口判定为不可发起联网操作：都不提示、不发包。
    default:
        break;
    }
}

void CBalloonContactWindow::CommitTagList(const std::vector<CString>& tags)
{
    // 有宿主主面板（正常登录态）时，"该不该发、发不出去怎么办、失败怎么退回来"
    // 全部交给主面板判定与执行 —— 它才同时握着通讯录缓存（原值）、连接态与 socket。
    // 本窗口只负责把改好的完整列表报上去。主面板处理完会回头调 RefreshContactData
    // 把本窗刷成最终状态。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel) && m_peerUserId != 0)
    {
        // 跨窗口传 CString 不安全（生命周期与线程），堆上传一份 UTF-8 字节，
        // 由主面板侧 delete —— 与备注名回投一致。
        std::vector<std::string>* pTags = new std::vector<std::string>();
        pTags->reserve(tags.size());
        for (size_t i = 0; i < tags.size(); ++i)
        {
            pTags->push_back(CStrToUtf8(tags[i]));
        }
        ::PostMessage(m_hostPanel, FMG_MSG_SET_CONTACT_TAGS,
                      (WPARAM)m_peerUserId, (LPARAM)pTags);
        // 标签内容可能含用户隐私，日志只记条数不记内容。
        LOG_INFO("contact dialog: submit tags, userid=%u, count=%d.",
                 m_peerUserId, (int)tags.size());
        return;
    }

    // 调试入口（--contact）没有宿主主面板，也没有真实连接：就地改内存并重建标签区，
    // 好让这条交互在调试入口下仍然能走通、能人工核对界面。
    m_model.SetTags(tags);
    RebuildTags();
    LOG_INFO("contact dialog: no host panel, tags updated in memory only, count=%d.",
             (int)tags.size());
}

// =======================================================================
// 开关 / 按钮
// =======================================================================

void CBalloonContactWindow::OnSwitchToggled(UINT switchCtrlId, bool on)
{
    // 开关类别（写模型用）与要回投给主面板的消息号一起定下来。
    int  kind  = -1;
    UINT msgId = 0;
    switch (switchCtrlId)
    {
    //消息免打扰：主面板据此改与该联系人这条会话的免打扰属性
    case kIdSwitchMute:
        kind  = kContactSwitchMute;
        msgId = FMG_MSG_SET_CONTACT_MUTE;
        break;

    //添加到收藏：主面板据此把该联系人加入 / 移出通讯录顶部的「我的收藏」分区
    case kIdSwitchFavorite:
        kind  = kContactSwitchFavorite;
        msgId = FMG_MSG_SET_CONTACT_FAVORITE;
        break;

    //特别关注：主面板据此登记 / 取消该联系人上下线时的桌面提示
    case kIdSwitchStarred:
        kind  = kContactSwitchStarred;
        msgId = FMG_MSG_SET_CONTACT_STARRED;
        break;

    //其余控件不是本对话框的开关，不处理
    default:
        return;
    }

    // 先把新状态写进模型：界面上开关已经拨过去了，模型不跟上的话，下一次重建
    // 客户区（FillSwitches）又会按旧值把它拨回来。
    m_model.SetSwitch(kind, on);

    // 有宿主主面板（正常登录态）时，"该不该发、发不出去怎么办、失败怎么退回来"
    // 全部交给主面板判定与执行 —— 收藏 / 特别关注的缓存、会话免打扰的开关与 socket
    // 都在它手上。做法与备注名（CommitRemarkEdit）、标签（CommitTagList）完全同构。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel) && m_peerUserId != 0)
    {
        // 传的是<u>目标状态</u>而不是"取反"：用户连点两下、而第一条应答还在路上时，
        // 按取反算会让主面板对着同一个旧状态取反两次，结果与用户看到的开关相反。
        ::PostMessage(m_hostPanel, msgId, (WPARAM)m_peerUserId, (LPARAM)(on ? 1 : 0));
        LOG_INFO("contact dialog: submit switch, userid=%u, ctrlId=%u, target=%d.",
                 m_peerUserId, switchCtrlId, on ? 1 : 0);
        return;
    }

    // 调试入口（--contact）没有宿主主面板，也没有真实连接：只改内存，好让这条交互
    // 在调试入口下仍然能走通、能人工核对界面。
    LOG_INFO("contact dialog: no host panel, switch updated in memory only, "
             "ctrlId=%u, target=%d.", switchCtrlId, on ? 1 : 0);
}

void CBalloonContactWindow::OnQuickActionClicked(UINT ctrlId)
{
    // 调试入口（--contact）没有宿主主面板，四个动作没有一个发得出去。这里只记一条
    // 日志、不弹提示框 —— 调试入口是拿来看界面的，弹框正好挡住要看的东西。
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel) || m_peerUserId == 0)
    {
        LOG_INFO("contact dialog: no host panel, quick action ignored, ctrlId=%u.",
                 ctrlId);
        return;
    }

    // 通话邀请要带上给对方"呼叫中"对话框看的显示名：备注优先，没有备注才用真名 ——
    // 与 DoSendMessage 的取名口径一致。
    const CString& remark = m_model.Remark();
    const CString peerName = remark.IsEmpty() ? m_model.Name() : remark;

    switch (ctrlId)
    {
    //语音通话：转投主面板的通话协调器，由它统一发 cmd_call_invite 并弹"呼叫中"对话框。
    //做法与单聊窗头部那颗按钮一致（见 CBalloonChatWindow::OnDuiNotify）。
    case kIdBtnVoice:
        {
            // 通话要走信令通道，离线时发起必然失败，在入口就拦住。
            if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
            {
                return;
            }
            // 跨窗口传 CString 不安全（生命周期与线程），显示名走堆 std::wstring，
            // 由收件方处理后 delete。
            std::wstring* pPeerName = new std::wstring(peerName.GetString());
            if (!::PostMessage(m_hostPanel, FMG_MSG_CALL_START_INVITE,
                               (WPARAM)m_peerUserId, (LPARAM)pPeerName))
            {
                delete pPeerName;   // 投递失败（主面板已销毁）自行回收，杜绝泄漏
            }
            LOG_INFO("contact dialog: request voice call, userid=%u.", m_peerUserId);
        }
        break;

    //视频通话：与语音通话同一条路由，只是消息号不同（主面板据此发 media=video）。
    case kIdBtnVideo:
        {
            if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
            {
                return;
            }
            std::wstring* pPeerName = new std::wstring(peerName.GetString());
            if (!::PostMessage(m_hostPanel, FMG_MSG_CALL_START_VIDEO_INVITE,
                               (WPARAM)m_peerUserId, (LPARAM)pPeerName))
            {
                delete pPeerName;
            }
            LOG_INFO("contact dialog: request video call, userid=%u.", m_peerUserId);
        }
        break;

    //历史消息：请主面板打开与该联系人这条单聊会话的历史消息记录窗。历史记录读的是
    //本地库，不需要联网，故这一项不过离线门禁。
    case kIdBtnHistory:
        {
            // 会话类型与"预选哪个类型胶囊"一起塞在 wParam 里；对端 userid 经堆
            // int64_t 回投，由主面板处理后 delete（约定见 UserSessionData.h）。
            int64_t* pCid = new int64_t((int64_t)m_peerUserId);
            const WPARAM wp = (WPARAM)(kHistoryConvTypeSingle
                | (kHistoryTypeFilterAll << kHistoryTypeFilterShift));
            if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_HISTORY, wp, (LPARAM)pCid))
            {
                delete pCid;   // 投递失败（主面板已销毁）自行回收，杜绝泄漏
            }
            LOG_INFO("contact dialog: request history window, userid=%u.", m_peerUserId);
        }
        break;

    //组织架构：请主面板切到通讯录页、展开各级部门、滚动到该联系人那一行并选中。
    //纯本地操作，同样不过离线门禁。
    case kIdBtnLocate:
        ::PostMessage(m_hostPanel, FMG_MSG_LOCATE_CONTACT, (WPARAM)m_peerUserId, 0);
        LOG_INFO("contact dialog: request locate in address book, userid=%u.",
                 m_peerUserId);
        // 本窗口是居中的独立顶层窗口，正好盖住主面板；不关掉的话用户看不见定位结果。
        // 与「发消息」一样投完即关。
        if (IsWindow())
        {
            DestroyWindow();
        }
        break;

    //其余控件不是快捷操作行的按钮，不处理
    default:
        break;
    }
}

bool CBalloonContactWindow::CheckOnlineFor(int action)
{
    // 门禁只看本窗记下的连接态：它由主面板推下来，与托盘图标、状态胶囊同源。
    // 判定本身交给 OnlineCapability，不在本窗口再写一份（规则只该有一处）。
    if (balloonmain::IsActionAllowedInState(m_connUiState, action))
    {
        return true;
    }

    LOG_INFO("ContactWindow::CheckOnlineFor: blocked. peer=%u, action=%d, "
             "connUiState=%d (0=online 1=connecting 2=offline).",
             m_peerUserId, action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonContactWindow::DoSendMessage()
{
    // 真实入口（m_hostPanel 有效）：请主面板按 userid 打开单聊，并关闭本资料窗。
    if (m_hostPanel != NULL && m_peerUserId != 0)
    {
        ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CHAT, (WPARAM)m_peerUserId, 0);
        LOG_INFO("联系人资料对话框「发消息」: 请求打开单聊, userid=%u。", m_peerUserId);
        if (IsWindow())
        {
            DestroyWindow();
        }
        return;
    }

    // 调试入口（无主面板绑定）：保留 mock 占位提示。
    CString msg;
    const CString& remark = m_model.Remark();
    const CString primaryName = remark.IsEmpty() ? m_model.Name() : remark;
    msg.Format(_T("向「%s」发消息\n\n（调试入口，当前为 mock 演示）"),
               (LPCTSTR)primaryName);
    ::MessageBox(m_hWnd, msg, _T("联系人资料"), MB_OK | MB_ICONINFORMATION);
    LOG_INFO(_T("联系人资料对话框「发消息」被点击（mock）。"));
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonContactWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口（--contact）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 请求进程退出。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonContactWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // ---- 标签胶囊的两个自定义通知 ----
    // 判定条件里必须<u>同时</u>写上 code 与 ctrlId，且 ctrlId 的上下界都要判：
    // balloonui 的自定义通知码按控件各自从 DUIN_CUSTOM 起编号，不同控件的同一档
    // 数值必然相同，只比 code、或者只判 ctrlId 下界，都会把别的控件发来的同值
    // 通知一并收下（症状是"点了没反应"，不报错也不进日志）。
    const UINT chipIdLow  = (UINT)kIdTagChipBase;
    const UINT chipIdHigh = (UINT)(kIdTagChipBase + kContactTagMaxCount);

    if (pNotify->code == (UINT)TagChip::DUIN_TAGCHIP_DELETE
        && pNotify->ctrlId >= chipIdLow && pNotify->ctrlId < chipIdHigh)
    {
        OnTagDeleteClicked(pNotify->ctrlId);
        return 0;
    }

    if (pNotify->code == (UINT)TagChip::DUIN_TAGCHIP_EDIT
        && pNotify->ctrlId >= chipIdLow && pNotify->ctrlId < chipIdHigh)
    {
        OnTagEditClicked(pNotify->ctrlId);
        return 0;
    }

    // ---- 点击类通知 ----
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //点「+ 添加标签」—— 在按钮位置弹出输入框，输入完提交整份标签列表。
        //标签数已达上限时这个按钮压根不存在（换成了说明文字），故不会走到这里。
        case kIdAddTagButton:
            OnAddTagClicked();
            return 0;
        //点铅笔按钮 —— 进入备注名行内编辑态。
        case kIdRemarkEditBtn:
            SetRemarkEditing(true);
            return 0;
        //点底部「关闭」按钮 —— 等价于标题栏的关闭按钮。
        case kIdBtnClose:
            PostMessage(WM_CLOSE, 0, 0);
            return 0;
        //点底部「发消息」按钮 —— 请主面板按 userid 打开单聊。
        case kIdBtnSend:
            DoSendMessage();
            return 0;
        //点快捷操作行的四个按钮（语音通话 / 视频通话 / 历史消息 / 组织架构）——
        //四者都只是把动作回投给主面板，统一交给 OnQuickActionClicked 按 ctrlId 分派。
        case kIdBtnVoice:
        case kIdBtnVideo:
        case kIdBtnHistory:
        case kIdBtnLocate:
            OnQuickActionClicked(pNotify->ctrlId);
            return 0;
        //其余控件的点击本分支不处理，落到函数后面的分支继续判定。
        default:
            break;
        }
    }

    // ---- 状态变化类通知 ----
    if (pNotify->code == DUIN_VALUECHANGED)
    {
        // 开关翻转 —— extra 为新状态（1 开 / 0 关）。
        if (pNotify->ctrlId == kIdSwitchMute
            || pNotify->ctrlId == kIdSwitchFavorite
            || pNotify->ctrlId == kIdSwitchStarred)
        {
            OnSwitchToggled(pNotify->ctrlId, pNotify->extra != 0);
            return 0;
        }

        // 分组下拉选择变化 —— extra >= 0 为新选中下标。
        if (pNotify->ctrlId == kIdGroupCombo)
        {
            if (pNotify->extra >= 0)
            {
                m_model.SetSelectedGroup((int)pNotify->extra);
                LOG_INFO(_T("联系人资料对话框分组已切换。"));
            }
            return 0;
        }
    }

    // ---- 备注名文本框失焦 —— 去抖后视为提交编辑 ----
    // 不能就地提交：用户在编辑框内点一下，EDIT 会短暂失焦再得焦，立即提交就成了
    // "一点就退回"。延后一拍投消息，由 OnRemarkCommitDeferred 查焦点再定夺。
    if (pNotify->code == DUIN_KILLFOCUS && pNotify->ctrlId == kIdRemarkEdit)
    {
        if (m_remarkEditing && IsWindow())
        {
            ::PostMessage(m_hWnd, WM_CONTACT_REMARK_COMMIT, 0, 0);
        }
        return 0;
    }

    // 其它通知交回 DuiFrameWindow 链。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonContactWindow::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    // 备注名编辑态下点在文本框之外 —— 把 DUI 焦点从文本框上摘掉，由此触发
    // DUIN_KILLFOCUS → 去抖提交，达成"点别处自动退出编辑并保存"。
    //
    // 这条路径换过一次触发方式。文本框还是内嵌真 Win32 EDIT 子窗口的时候，靠的是
    // 「把键盘焦点收回宿主窗口 → 子窗口失焦 → DUIN_KILLFOCUS」；那时点在子窗口上的
    // 鼠标消息由系统直送该子窗口，压根不会到达宿主，所以宿主收到本消息就等于"点在
    // 文本框之外"，不必判落点。文本框改成无窗口实现之后这两条前提都不成立了：键盘
    // 焦点本来就在宿主窗口上，再调一次 ::SetFocus(m_hWnd) 不产生任何焦点变化（故那
    // 一句已删除）；而点在文本框上的鼠标消息同样会到达宿主。于是改为在这里自己判
    // 落点，只有确实点在文本框之外时才摘焦点。
    if (m_remarkEditing)
    {
        DuiControl* content = GetClientContent();
        DuiEdit* remarkEdit = (content != NULL)
            ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdRemarkEdit))
            : NULL;
        // 只在文本框当前确实持有 DUI 焦点时才动焦点：焦点已经不在它身上时，
        // DUIN_KILLFOCUS 早已发过、提交也已经投递过，这里再摘一次只会给别的控件
        // 发出一条无意义的失焦通知。
        if (remarkEdit != NULL && remarkEdit->IsFocused())
        {
            // 控件矩形与本消息的鼠标坐标同为宿主窗口客户区坐标，可直接比对。
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            RECT rcEdit = remarkEdit->GetRect();
            if (!::PtInRect(&rcEdit, pt))
            {
                // 摘掉 DUI 焦点即走原有的那条提交链：文本框收到失焦 → 发出
                // DUIN_KILLFOCUS → 本窗口投递 WM_CONTACT_REMARK_COMMIT → 去抖后提交。
                // 这里刻意不直接调 CommitRemarkEdit：提交会隐藏文本框并重排整棵树，
                // 而本次点击随后还要交给 DuiHost 做命中派发，就地提交会让它按提交后的
                // 新布局去命中控件，点到本不该点的东西上。
                SetDuiFocus(NULL);
            }
        }
    }
    bHandled = FALSE;   // 继续交给 DuiFrameWindow / DuiHost 正常处理这次点击
    return 0;
}

LRESULT CBalloonContactWindow::OnRemarkCommitDeferred(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_remarkEditing)
    {
        return 0;   // 已不在编辑态（例如已被别处提交），无须处理
    }

    // 焦点又回到了备注名文本框（用户在文本框内重新点击导致的短暂失焦 → 得焦）：
    // 说明人还在编辑，跳过这次提交。焦点真正离开了才提交。
    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        DuiEdit* remarkEdit = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdRemarkEdit));
        if (remarkEdit != NULL && remarkEdit->IsFocused())
        {
            return 0;
        }
    }

    CommitRemarkEdit();
    return 0;
}
