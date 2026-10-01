/**
 *  BalloonSystemSettingDlg.h
 *
 *  "系统设置"对话框 —— 对照 design/系统设置1.png ~ 系统设置7.png。基于 balloonui
 *  DuiFrameWindow。布局为左侧圆角胶囊竖向导航（CSettingsNavList，复用
 *  DuiVirtualList）+ 右侧随选中项切换的 7 个分页内容区。同一时刻只显示一页，
 *  靠对各分页容器 SetVisible + ForceLayout 实现（DuiVBox 布局自动跳过隐藏子）。
 *
 *  本期（阶段 1）只搭外壳 + 导航 + 切页 + 接入系统菜单，各页为占位；后续阶段
 *  逐页填入真实设置项（通用 / 消息通知 / 聊天三页先行）。
 *
 *  与主面板的关系：主面板系统菜单"系统设置"项点击时，由 CBalloonMainPanel 持有
 *  并调用本对话框的 OpenSettings（与"修改密码"对话框同套路）。老的 MFC
 *  CSystemSettingDlg 保留但本菜单不再指向它。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "BalloonCacheCleanDlg.h"   // m_cacheCleanDlg 按值持有，需完整类型
#include "BalloonChatCleanDlg.h"    // m_chatCleanDlg 按值持有，需完整类型
#include "../BalloonChatWnd/FontSettingPopup.h"   // m_chatFontPopup 按值持有，需完整类型

#include <vector>

class CFlamingoClient;

namespace balloonwjui { class DuiVBox; class DuiControl; class DuiLabel; }
namespace balloonsettings { class CSettingsNavList; struct SettingsNavEntry; }

/**
 *  CBalloonSystemSettingDlg —— 系统设置对话框（非模态浮窗，owner = 主面板）。
 */
class CBalloonSystemSettingDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢 / 用户配置 m_UserConfig 的来源），
     *                本类只引用、不持有。本期仅作后续接配置用，外壳阶段未使用。
     */
    explicit CBalloonSystemSettingDlg(CFlamingoClient* pFMGClient);
    ~CBalloonSystemSettingDlg();

    /**
     *  创建并显示对话框（已创建则前置显示）。
     *    owner：owner 窗口句柄（主面板）；内部 Create 传 NULL 让子控件通知自路由
     *           回本对话框（与修改密码 / 新建群对话框同处理），owner 仅作语义留存。
     */
    void OpenSettings(HWND owner);

    /**
     *  设置「账号安全」页「自动登录」开关应显示的状态。
     *
     *  自动登录的标志存在本机历史账号列表里，而列表归主面板的会话控制器持有，本对话框
     *  读不到，只能由主面板在打开本对话框之前传进来（见 docs/自动登录方案.md §4）。
     *    on：当前账号是否已开启自动登录。
     *  副作用：窗口尚未创建时只记下该值，建页时据它设置开关初始状态；窗口已存在时
     *          立即把开关拨到该状态（无动画、不发通知，不会反过来触发一次保存）。
     */
    void SetAutoLoginSwitchState(bool on);

    /**
     *  告知当前是否已连接服务器（主面板在连接状态变化时、以及打开本对话框之前调用）。
     *  「清理聊天记录」对话框据此决定「清理」按钮是否可用；该对话框开着时立即转告它。
     *    connected：true 表示已连接且已登录（主面板连接态为 kConnUiOnline）。
     */
    void SetServerConnected(bool connected);

    /**
     *  打开「清理聊天记录」对话框（聊天页「清理…」按钮与调试入口 --chatclean 共用）。
     *  已打开时前置显示。
     */
    void OpenChatCleanDialog();

    /**
     *  收到按时间范围清理（1521）的应答，转交「清理聊天记录」对话框提示结果。
     *  对话框未打开（如用户在清理中就关掉了它）时只记日志。
     *    code：     应答的 code，0 表示成功。
     *    beginTime：实际生效的区间开始（Unix 毫秒），失败时为 0。
     *    endTime：  实际生效的区间结束（Unix 毫秒），失败时为 0。
     */
    void OnChatCleanResult(int code, int64_t beginTime, int64_t endTime);

    BEGIN_MSG_MAP(CBalloonSystemSettingDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（左导航 + 右内容区 7 个分页）。
    void BuildUiOnce();

    // 按导航行模型造一个占位分页（页标题 + 提示）。本期未落地的四页用它。
    //   entry：该页的行模型（提供标题与 implemented 标记）。
    //   返回：分页容器（DuiVBox），所有权移交调用方。
    std::unique_ptr<balloonwjui::DuiVBox> BuildPlaceholderPage(
        const balloonsettings::SettingsNavEntry& entry);

    // 三页真实内容构建（通用 / 消息通知 / 聊天）。初始值从 CUserConfig（与开机
    // 自启动注册表）读取。返回分页容器，所有权移交调用方。
    std::unique_ptr<balloonwjui::DuiVBox> BuildGeneralPage();
    std::unique_ptr<balloonwjui::DuiVBox> BuildNotifyPage();
    std::unique_ptr<balloonwjui::DuiVBox> BuildChatPage();

    // 另四页内容构建（音视频 / 快捷键 / 存储 / 账号安全）。返回分页容器，所有权移交调用方。
    //   音视频：真实枚举麦克风/扬声器设备 + 摄像头占位 + 回声消除 + 麦克风测试按钮（占位）。
    //   快捷键：只读键帽展示（发送/换行随聊天页设置联动，其余为文档默认值）。
    //   存储：真实文件保存路径 + 占用量分段条（按本地数据目录扫描）+ 清理缓存（占位）。
    //   账号安全：修改密码（复用面板改密对话框）+ 自动登录开关（转交主面板）+ 二次验证开关
    //             + 其余按钮占位。
    std::unique_ptr<balloonwjui::DuiVBox> BuildAudioVideoPage();
    std::unique_ptr<balloonwjui::DuiVBox> BuildShortcutPage();
    std::unique_ptr<balloonwjui::DuiVBox> BuildStoragePage();
    std::unique_ptr<balloonwjui::DuiVBox> BuildAccountPage();

    // 造一行的左侧"标题 + 可选副标题"文字块（整体垂直居中）。
    //   title：行标题（必填）。
    //   subtitle：副标题（灰色小字），为 NULL 或空串时不画。
    //   subtitleCtrlId：副标题控件的 id；传 0 表示不需要（默认）。只有事后要换副标题
    //                   文案的行才需要给 id —— 目前只有「自动离开」那行（被「禁止机器
    //                   休眠」置灰时要换成解释文案）。
    //   返回：文字块容器（DuiVBox），所有权移交调用方。
    std::unique_ptr<balloonwjui::DuiVBox> BuildRowTextBlock(LPCTSTR title, LPCTSTR subtitle,
                                                            UINT subtitleCtrlId = 0);

    // 往页里追加一条 1px 行分隔线。
    void AddRowDivider(balloonwjui::DuiVBox* page);

    // 往页里追加一行"开关行"（左标题/副标题，右 DuiSwitch）。
    //   ctrlId：开关控件 id（用于 OnDuiNotify 分发）。
    //   on：开关初始状态。
    //   addDivider：是否在行下追加分隔线（末行传 false）。
    //   subtitleCtrlId：副标题控件 id，见 BuildRowTextBlock 说明；不需要时不传。
    void AddSwitchRow(balloonwjui::DuiVBox* page, LPCTSTR title, LPCTSTR subtitle,
                      UINT ctrlId, bool on, bool addDivider, UINT subtitleCtrlId = 0);

    /**
     *  按「禁止机器休眠」的当前状态，联动更新「自动离开」那两行的可用性与文案。
     *
     *  两者互斥（2026-08-12 与用户确认）：本功能通过周期性合成输入事件推迟锁屏，而
     *  自动离开正是依据"多久没有输入"判定空闲，合成会把该计时器重置为 0，自动离开将
     *  永远不会触发。与其让用户面对一个已开启却不产生效果的开关，不如明确置为禁用。
     *
     *  互斥是<u>单向</u>的：只有「禁止机器休眠」会禁用「自动离开」，反向不作限制 ——
     *  自动离开默认为开启状态，若反向也加限制，用户要使用本功能就得先关闭它。
     *
     *  禁用期间<u>不</u>修改自动离开在配置中的取值，开关也保持在原有位置（仅整行变淡
     *  显示），这样关闭禁止休眠后它原样恢复，不丢失用户此前的选择。
     *
     *    keepAwakeOn：「禁止机器休眠」是否开启。
     *  副作用：修改控件的可用状态与副标题文案（控件内部会自行标脏重绘）。
     *          控件尚未创建时静默返回。
     */
    void ApplyKeepAwakeExclusion(bool keepAwakeOn);

    // 往页里追加一行"下拉行"（左标题/副标题，右 DuiComboBox）。
    //   options/count：下拉选项文本表首元素指针与项数。
    //   selIndex：初始选中索引。
    void AddComboRow(balloonwjui::DuiVBox* page, LPCTSTR title, LPCTSTR subtitle,
                     UINT ctrlId, const wchar_t* const* options, int count, int selIndex,
                     bool addDivider);

    // 往页里追加一行"按钮行"（左标题/副标题，右 DuiButton）。
    //   ctrlId：按钮控件 id；btnText：按钮文字；danger：true 用危险红样式（如"注销"）。
    void AddButtonRow(balloonwjui::DuiVBox* page, LPCTSTR title, LPCTSTR subtitle,
                      UINT ctrlId, LPCTSTR btnText, bool danger, bool addDivider);

    // 往页里追加一行"只读取值行"（左标题/副标题，右灰色取值文本，如文件保存路径）。
    void AddValueRow(balloonwjui::DuiVBox* page, LPCTSTR title, LPCTSTR subtitle,
                     LPCTSTR value, bool addDivider);

    // 往页里追加一行"键帽行"（左快捷键名，右 DuiBadge 键帽 chip，只读）。
    void AddKeyCapRow(balloonwjui::DuiVBox* page, LPCTSTR title, LPCTSTR keyText, bool addDivider);

    // 把某个设置控件的新值写回 CUserConfig（开机自启动写注册表）并即时持久化。
    //   ctrlId：触发变更的控件 id。
    //   value：新值（开关取 1/0；下拉取选中索引）。
    //   返回：是否为本对话框已知的设置项（true 表示已处理）。
    bool ApplySettingChange(UINT ctrlId, int value);

    // 处理设置页里各按钮的点击（修改密码 / 设备管理 / 清理缓存 / 注销等）。
    //   ctrlId：被点按钮的控件 id。
    //   返回：是否为本对话框已知的按钮（true 表示已处理）。
    bool HandleButtonClick(UINT ctrlId);

    // 机器级配置文件 config\flamingo.ini 的完整路径。存储页里有三处要读写它（数据
    // 存储位置的读与写、启动时清空历史日志的读写），路径只在这里拼一次。
    CString AppIniPath();

    // 本次会话真正在用的数据根目录（g_szDataRoot）。它在进程启动时按 flamingo.ini 的
    // [storage] datapath 算好，运行期不再变。
    CString EffectiveDataRoot();

    // flamingo.ini 里当前写着的数据存储位置；未配置过时为空串。
    CString ConfiguredDataRoot();

    // 存储页那一行要显示的文本：一般就是生效路径；用户刚改完还没重启时显示新路径并
    // 标注"（重启后生效）"，免得他以为没改上。
    CString DataRootDisplayText();

    // "更改"按钮：弹选目录框 → 确认框（讲清后果与手动迁移步骤）→ 写 flamingo.ini →
    // 刷新显示并提示重启。<u>不</u>改动本次会话的任何行为，也不做数据迁移。
    void ChooseDataRoot();

    // 仅切换各分页的可见性 + 更新 m_curPage，不触发重新布局（建窗期用）。
    //   index：要显示的页索引（会经夹取）。
    void ApplyPageVisibility(int index);

    // 运行期切页：切换可见性后强制重排内容容器（让新显示页拿到完整矩形）并重绘。
    //   index：要显示的页索引（与当前页相同则无操作）。
    void ShowPage(int index);

    // ---- 「聊天」页的「默认聊天字体」一行 ----

    // 构建该行：左侧标题 + 副标题，右侧预览文字、「修改」与「恢复默认」两个按钮，行末加分隔线。
    //   page：聊天页容器。
    void AddChatFontRow(balloonwjui::DuiVBox* page);

    // 按当前全局默认字体刷新该行：预览文字的外观，以及「恢复默认」是否可点（未设置时不可点）。
    void RefreshChatFontRow();

    // 「修改」按钮：在按钮上方打开字体浮层（与聊天窗「AF」同一个浮层，但没有「使用默认
    // 字体」按钮），浮层里每改一项即经 OnDefaultChatFontChanged 生效并存盘。
    void OpenDefaultChatFontPopup();

    // 字体浮层变化回调（userdata 为本对话框 this）：把新字体设为全局默认。
    static void OnDefaultChatFontChanged(void* userdata, const net::TextFont& font);

    // 设置全局默认聊天字体、存盘并刷新该行。
    //   font：新的全局默认；m_valid 为 false 表示清除（「恢复默认」按钮）。
    void ApplyDefaultChatFont(const net::TextFont& font);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（配置 / 消息源），引用不持有

    bool m_built;                     // 骨架是否已构建（避免重复构建）
    int  m_curPage;                   // 当前显示页索引；初值 -1 表示尚未定页
    HWND m_ownerHwnd;                 // 打开本对话框的宿主（主面板）HWND；"修改密码"按钮据此转发
    bool m_autoLoginOn;               // 「自动登录」开关的初始状态，由主面板经 SetAutoLoginSwitchState 传入；默认 false

    // 控件指针（借用，控件树持有所有权）：切页 / 重排时用。
    balloonsettings::CSettingsNavList* m_pNav;          // 左侧导航
    balloonwjui::DuiVBox*              m_pContentHost;  // 右侧内容容器（承载 7 分页）
    std::vector<balloonwjui::DuiControl*> m_pages;      // 7 个分页容器，按页序号索引
    balloonwjui::DuiLabel*             m_pRecvPathLabel; // 存储页"文件保存位置"路径标签（借用），改路径后就地刷新
    HBITMAP                            m_hTitleIcon;     // 标题栏左侧图标位图（程序图标合成到标题栏底色，本类持有、析构释放）

    // 「立即清理」弹出的清理对话框。按值持有（与主面板持有改密对话框同套路），
    // 由本窗负责其生命周期；未打开时不占任何资源。
    CBalloonCacheCleanDlg              m_cacheCleanDlg;

    // 聊天页「清理聊天记录」弹出的对话框。按值持有，由本窗负责其生命周期；未打开时不占资源。
    CBalloonChatCleanDlg               m_chatCleanDlg;

    // 当前是否已连接服务器，由主面板经 SetServerConnected 传入；打开清理对话框时据此置灰「清理」。
    // 默认 true：主面板在登录成功后才创建，打开本窗之前也会先传一次实际值。
    bool                               m_serverConnected;

    // 「默认聊天字体」的字体浮层（按需打开，本类持有）。
    CFontSettingPopup                  m_chatFontPopup;

    // 「默认聊天字体」预览标签当前使用的字体句柄。DuiLabel 不接管字体所有权，故由本类
    // 持有：每次刷新预览时替换并释放上一只（见 ApplyChatFontPreview），析构时释放。
    // NULL 表示未设置、标签用默认字体。
    HFONT                              m_hChatFontPreview;
};
