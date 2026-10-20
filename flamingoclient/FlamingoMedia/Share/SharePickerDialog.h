/**
 *  屏幕共享的「选择要共享的内容」窗口，SharePickerDialog.h
 *
 *  点聊天窗的「共享屏幕」后弹出（docs/屏幕共享与录制方案.md 5.2 第 1 步）：「屏幕」页按显示器列出、「窗口」页列出
 *  可共享的顶层窗口，都带缩略图与名称；选中一项后点「开始共享」。共享过程中点控制条的「切换内容」、或共享的窗口
 *  被关闭时，也用本窗口重新选择（此时按钮文字是「切换」，顶部给出提示）。结果经 kMsgSharePickerResult 投给通知窗口
 *  （屏幕共享控制器），由控制器决定发起或切换。发起时可勾选同时开启麦克风、共享电脑声音。
 *  独立录屏（第 7 期）也用本窗口选择录制范围：标题与按钮换成录制的说法，底部多一个「框选区域」按钮，勾选项为
 *  录制麦克风、录制电脑声音、摄像头画中画。
 *  第 12 期：标题栏左侧显示程序图标；页签行右侧有「刷新」按钮，重新列出显示器与窗口（原先选中的内容还在就继续选中）；
 *  发起共享时底部多一个「设置密码」勾选项与 6 位数字密码框，勾选时自动生成密码、可以改，见 SharePickerPolicy.h。
 *  第 13 期：发起共享时底部改为两行，第一行是麦克风、电脑声音、「强制模式」「禁止观看者录制」，第二行是密码与按钮。
 *  语音字幕（docs/语音字幕方案.md 6.1）：录制模式下字幕可用时，勾选项一行末尾多「生成字幕」一项，默认值由调用方经
 *  SetCaptionOptions 给出（按账号记住上次的选择）。勾了就识别麦克风与电脑声音两路（2026-10-06 起不再单设「为电脑声音
 *  生成字幕」）；后面跟一行提示文字，两路声音都没勾时提示「没有录制声音，不会生成字幕」。
 *  2026-10-07 起发起共享（含通话中发起）时字幕可用的话，按钮一行多一个「为我的发言生成字幕」，默认值为上次的选择。
 *  窗口已经开着而用途变了时，标题与最小尺寸按新用途重新设置。
 *
 *  只用 balloonui 现有控件：DuiTab 标签页、DuiScrollView 滚动区、DuiGrid 网格；每个缩略图格子是一个以缩略图为背景位图的
 *  DuiButton（普通、悬停、选中三张位图由本类按缩略图合成）加一个名称 DuiLabel，不新写自绘控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程）：
 *      m_picker.Open(hostMainWnd, m_msgWnd, false, std::wstring());
 *      ...（控制器收到 kMsgSharePickerResult）
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons（任务栏图标，2026-10-05）

#include <string>
#include <vector>
#include "ShareHostStreamer.h"
#include "ShareMessages.h"
#include "SharePickerPolicy.h"
#include "ShareSources.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiGrid;
    class DuiEdit;
    class DuiScrollView;
    class DuiLabel;
    class DuiTab;
}

namespace screenshare {

//选择窗的结果（随 kMsgSharePickerResult 投递的堆对象，控制器处理后 delete）
struct SharePickerResult
{
    ShareSourceSpec m_source;        //要共享（录制）的内容；框选区域时为区域所在的显示器
    bool            m_mic;           //是否同时开启麦克风（录制模式下为是否录制麦克风；切换内容时为 false）
    bool            m_systemAudio;   //是否同时共享电脑声音（录制模式下为是否录制电脑声音）
    bool            m_camera;        //录制模式：是否画摄像头画中画
    bool            m_region;        //录制模式：用户点了「框选区域」，接下来要在 m_source 的显示器上框选
    bool            m_whiteboard;    //录制模式：用户点了「白板」（第 21 期），打开一块白板并录下它；m_source 不用
    bool            m_audioOnly;     //录制模式：用户点了「只录声音」（第 24 期），按 m_mic / m_systemAudio 只录声音；m_source、m_camera 不用
    std::string     m_password;      //发起共享时设的加入密码（6 位 ASCII 数字）；没设密码、或不是发起共享时为空
    bool            m_forced;        //发起共享时勾了「强制模式」（第 13 期）
    bool            m_recordBlocked; //发起共享时勾了「禁止观看者录制」（第 13 期）
    bool            m_captionOffered;   //录制模式：这次是否提供了字幕勾选项（没提供时下面两项无意义，调用方不要据此改记住的选择）
    bool            m_caption;          //录制模式：勾了「生成字幕」（识别麦克风与电脑声音两路）
    bool            m_saveToCloud;      //录制模式：勾了「保存到云端」（第 27 期），录完上传到云端，本机文件照常保留
    std::string     m_title;         //共享标题（第 15 期，UTF-8，已去掉首尾空白）；不是发起共享时为空
    std::string     m_desc;          //共享描述（第 15 期，UTF-8，已去掉首尾空白）；没填或不是发起共享时为空

    SharePickerResult()
        : m_mic(false)
        , m_systemAudio(false)
        , m_camera(false)
        , m_region(false)
        , m_whiteboard(false)
        , m_audioOnly(false)
        , m_forced(false)
        , m_recordBlocked(false)
        , m_captionOffered(false)
        , m_caption(false)
        , m_saveToCloud(false)
    {
    }
};

//选择窗的用途 SharePickerMode 定义在 SharePickerPolicy.h

/**
 *  选择窗某种用途下的设计高度（整窗，含标题栏；也是最小高度）。发起共享多一行选项与一行标题、描述，录制多一行勾选项。
 *  @param mode 用途，取值见 SharePickerMode
 *  @return 高度（像素）
 */
int SharePickerDialogHeight(int mode);

//选择窗的设计宽度（整窗；也是最小宽度），与用途无关
int SharePickerDialogWidth();

//「选择要共享的内容」窗口
class CSharePickerDialog : public balloonwjui::DuiFrameWindow
{
public:
    CSharePickerDialog();
    ~CSharePickerDialog();

    /**
     *  打开窗口。已经开着、且用途与提示都没变时只恢复（最小化了的）并激活，不重新加载列表，已选的内容与已填的选项保留
     *  （2026-10-05 用户要求：重复打开录制时激活前一个窗口）；用途或提示变了时按新的用途重建。
     *  @param owner      属主窗口（exe 主面板），窗口居中在它所在的显示器上；可为 NULL
     *  @param notifyWnd  结果通知窗口；不拥有
     *  @param mode       用途，取值见 SharePickerMode（按钮分别为「开始共享」「切换」「开始录制」）
     *  @param hint       顶部提示文字（如「共享的窗口已关闭，请重新选择」）；为空时不显示
     *  @param selfName   本人显示名（第 15 期），共享标题的默认值据此写成「本人显示名的屏幕共享」；为空时写「我的屏幕共享」
     */
    void Open(HWND owner, HWND notifyWnd, int mode, const std::wstring& hint,
              const std::wstring& selfName = std::wstring());

    /**
     *  是否提供字幕勾选项及其默认值（语音字幕）：录制模式为「生成字幕」，发起共享（含通话中发起）时为「为我的发言生成字幕」
     *  （2026-10-07 起）。在 Open 之前调用；窗口已经开着时下次重建界面才生效。
     *  @param offer     是否提供（字幕功能可用时为 true）
     *  @param captionOn 勾选项默认是否勾选
     */
    void SetCaptionOptions(bool offer, bool captionOn);

    /**
     *  录制模式下「保存到云端」勾选项是否可勾（屏幕共享第 27 期，docs/屏幕共享与录制方案.md 12.6.4）。录制模式总有这一项，
     *  默认不勾；企业没有开通云端录制时不可勾，悬停说明原因。在 Open 之前调用；窗口已经开着时下次重建界面才生效。
     *  @param available 企业开关 cloudrecord 是否打开（IMediaHost::IsCloudRecordEnabled）
     */
    void SetCloudSaveOption(bool available);

    //关闭窗口（不投递结果）；可重复调用
    void CloseDialog();

    //窗口开着时恢复（最小化了的）并提到前台；没开着时不做任何事
    void BringToFront();

    //缩略图网格当前的列数（2026-10-05 起随窗口宽度变化，设计宽度下为 3）
    int GridColumns() const;

    //对外可见的控件 id（单元测试按它找控件核对排版）
    enum
    {
        kCtrlOk           = 11,   //「开始共享 / 切换 / 开始录制」
        kCtrlCancel       = 12,   //「取消」
        kCtrlMicCheck     = 13,   //麦克风勾选项
        kCtrlSysAudioCheck = 14,  //电脑声音勾选项
        kCtrlCameraCheck  = 15,   //录制模式：摄像头画中画勾选项
        kCtrlRegion       = 16,   //录制模式：「框选区域」
        kCtrlPasswordCheck = 18,  //发起共享：「设置密码」勾选项（2026-10-07 起公开，单元测试核对排版）
        kCtrlPasswordEdit = 19,   //发起共享：6 位数字密码框（同上）
        kCtrlWhiteboard   = 24,   //录制模式：「白板」
        kCtrlCaptionCheck = 25,   //录制模式：「生成字幕」；发起共享时：「为我的发言生成字幕」（2026-10-07）。语音字幕可用时才有
        //26 已作废：原「为电脑声音生成字幕」勾选项（2026-10-06 起「生成字幕」同时识别电脑声音），不要复用
        kCtrlAudioOnly    = 27,   //录制模式：「只录声音」（第 24 期）；两个声音勾选项都没勾时不可用
        kCtrlCaptionHint  = 28,   //录制模式：「生成字幕」后面的提示文字（语音字幕）；两个声音勾选项都没勾时有文字
        kCtrlCloudSave    = 29,   //录制模式：「保存到云端」勾选项（第 27 期），在按钮一行的左端；企业没有开通时不可勾
    };

    BEGIN_MSG_MAP(CSharePickerDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(kMsgSharePickerDeferred, OnDeferredMsg)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //经 kMsgSharePickerDeferred 延后处理的事
    enum DeferredAction
    {
        deferred_refresh         = 1,   //点了「刷新」：重新列出显示器与窗口并重建界面
        deferred_filter_password = 2,   //密码框的文字变了：过滤掉非数字与超出 6 位的部分，再刷新「开始共享」按钮
        deferred_info_changed    = 3,   //标题或描述框的文字变了（第 15 期）：刷新「开始共享」按钮
    };

    //一个可选项
    struct Item
    {
        ShareSourceSpec m_source;      //选中后要共享的内容
        std::wstring    m_name;        //名称（显示器名或窗口标题）
        HBITMAP         m_normal;      //格子背景：普通
        HBITMAP         m_hover;       //格子背景：悬停
        HBITMAP         m_selected;    //格子背景：选中
        balloonwjui::DuiButton* m_button;   //格子按钮（控件树持有，本指针仅引用）

        Item();
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void EnsureWindow(HWND owner);
    //窗口尺寸变了：按新宽度重排缩略图网格的列数，再交给基类排版（2026-10-05）
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    /**
     *  按客户区宽度算缩略图网格的列数；列数变了就重设两页网格的行列与滚动高度。
     *  @param clientWidth 客户区宽（像素）；DuiFrameWindow 的客户区就是整个窗口
     */
    void regridPages(int clientWidth);
    void loadItems();
    void freeItems();
    void BuildUi();
    //建一页缩略图网格（first / count 指 m_items 里的区间）；gridOut 返回网格（控件树持有，仅引用）
    std::unique_ptr<balloonwjui::DuiScrollView> buildPage(size_t first, size_t count, int ctrlIdBase,
                                                          balloonwjui::DuiGrid** gridOut);
    void showPage(int page);
    void select(int index);
    //重新列出显示器与窗口并重建界面：保留当前页签、各勾选项与密码，原先选中的内容还在就继续选中
    void refresh();
    //按是否选中、密码是否合法、标题与描述是否合格（第 15 期）刷新「开始共享」按钮的可用状态
    void updateOkEnabled();
    //勾选 / 取消「设置密码」：勾选时生成新密码并允许编辑，取消时清空并禁用密码框
    void onPasswordToggled(bool on);
    //密码框文字变了：过滤掉非数字与超出 6 位的部分，再刷新按钮状态
    void onPasswordEdited();
    //region 为 true 表示点的是「框选区域」；whiteboard 为 true 表示点的是「白板」（第 21 期）；
    //audioOnly 为 true 表示点的是「只录声音」（第 24 期）
    void submit(bool region = false, bool whiteboard = false, bool audioOnly = false);
    //按两个声音勾选项刷新「只录声音」按钮的可用状态（第 24 期）；不是录制模式时什么也不做
    void updateAudioOnlyEnabled();
    //按「生成字幕」与两个声音勾选项刷新提示文字（语音字幕）：勾了「生成字幕」而两路声音都没勾时提示不会生成字幕
    void updateCaptionHint();
    void cancel();

    HWND                        m_notifyWnd;     //结果通知窗口（不拥有）
    bool                        m_switchMode;    //是否按切换内容的样子显示（SharePickerIsSwitchLayout：切换内容，或多人共享里经主持人同意后的第一次选择）
    int                         m_mode;          //用途，取值见 SharePickerMode
    std::wstring                m_hint;          //顶部提示
    std::vector<Item>           m_items;         //全部可选项：先显示器后窗口
    size_t                      m_monitorCount;  //其中显示器的个数
    int                         m_selected;      //选中项在 m_items 里的下标；-1 表示未选
    int                         m_page;          //当前标签页：0 屏幕，1 窗口
    balloonwjui::DuiScrollView* m_pages[2];      //两页的滚动区（控件树持有，本指针仅引用）
    balloonwjui::DuiButton*     m_okButton;      //「开始共享 / 切换」按钮（同上）
    balloonwjui::DuiButton*     m_micCheck;      //「同时开启麦克风」勾选项（同上）；切换模式下没有，为 NULL
    balloonwjui::DuiButton*     m_sysAudioCheck; //「同时共享电脑声音」勾选项（同上）；切换模式下没有，为 NULL
    balloonwjui::DuiButton*     m_cameraCheck;   //录制模式的「摄像头画中画」勾选项（同上）；其它模式下为 NULL
    balloonwjui::DuiButton*     m_captionCheck;  //录制模式的「生成字幕」勾选项（同上）；没提供字幕时为 NULL
    balloonwjui::DuiLabel*      m_captionHint;   //录制模式「生成字幕」后面的提示文字（同上）；没提供字幕时为 NULL
    balloonwjui::DuiButton*     m_audioOnlyBtn;  //录制模式的「只录声音」按钮（第 24 期，同上）；其它模式下为 NULL
    bool                        m_offerCaption;  //录制模式是否提供字幕勾选项（SetCaptionOptions 设置）
    bool                        m_captionDefault;    //「生成字幕」新建界面时的默认勾选
    balloonwjui::DuiButton*     m_cloudCheck;    //录制模式的「保存到云端」勾选项（第 27 期，控件树持有）；其它模式下为 NULL
    bool                        m_cloudAvailable;    //「保存到云端」是否可勾（SetCloudSaveOption 设置）
    balloonwjui::DuiButton*     m_passwordCheck; //「设置密码」勾选项（同上）；只有发起共享时有，其它模式下为 NULL
    balloonwjui::DuiEdit*       m_passwordEdit;  //6 位数字密码框（同上）；与 m_passwordCheck 同时存在
    balloonwjui::DuiButton*     m_forcedCheck;   //「强制模式」勾选项（第 13 期，同上）；只有发起共享时有，其它模式下为 NULL
    balloonwjui::DuiButton*     m_recordBlockCheck;  //「禁止观看者录制」勾选项（第 13 期，同上）；同上
    balloonwjui::DuiEdit*       m_titleEdit;     //共享标题框（第 15 期，同上）；只有发起共享（含通话中发起）时有，其它模式下为 NULL
    balloonwjui::DuiEdit*       m_descEdit;      //共享描述框（第 15 期，同上）；与 m_titleEdit 同时存在
    std::wstring                m_selfName;      //本人显示名（第 15 期），标题框的默认值据此生成；每次 Open 时更新
    balloonwjui::DuiTab*        m_tabs;          //「屏幕 / 窗口」页签条（同上）
    HBITMAP                     m_titleIcon;     //标题栏左侧的程序图标位图；本类持有，析构时释放，跨「关闭再打开」复用
    balloonmain::ProgramWindowIcons m_taskIcons; //任务栏图标（2026-10-05 起可最小化，最小化后要能从任务栏找回）；本类持有
    balloonwjui::DuiGrid*       m_grids[2];      //两页的缩略图网格（控件树持有，仅引用）；关窗后为 NULL
    int                         m_gridCols;      //缩略图网格当前的列数，随窗口宽度变化，设计宽度下为 3
    HWND                        m_owner;         //属主窗口（框选区域时没有选中显示器，就用它所在的显示器）
};

} // namespace screenshare
