/**
 *  OcrResultWindow.h
 *
 *  「图片文字识别」结果窗口（docs/图片文字识别方案.md 第 8.3 节）。聊天图片右键菜单、看图窗工具栏、
 *  截图工具栏三个入口都打开它：窗口打开即在后台识别，结果显示在可编辑、可选取的文字框里，
 *  识别错的字可以就地修改，「复制全部」把文字框里当前的内容放进剪贴板。顶部可以换识别语言，换了即重新识别。
 *  主面板系统菜单「图片文字识别」打开的是空窗口（OpenEmpty），用左下角「打开图片」选一张本地图片识别；
 *  这个按钮在从任何入口打开的窗口里都有（第 14 节）。
 *
 *  进程内只有一个窗口（GetInstance）：已打开时再从别处打开，就取消上一次识别、换成新图片。
 *  不设 owner，独立于发起识别的窗口，聊天窗或看图窗关闭时本窗口照常保留。
 *  只用 balloonui 现成控件（DuiLabel / DuiComboBox / DuiRichEdit / DuiButton / DuiVBox / DuiHBox），不改 balloonui。
 *
 *  窗口私有消息：WM_APP + 0xF8（kMsgOcrRecognized），已登记在开发约定文档的 UI 约定第 6 条。
 *
 *  典型用法：
 *      if (ocr::COcrModuleHost::GetInstance().CanShowEntry())
 *          balloonocr::COcrResultWindow::GetInstance().OpenForFile(m_hWnd, imagePath, 0);
 *  （点击之前应先经 ocr::CheckOcrBeforeRecognize 判断，系统不支持或没有识别语言时给出提示。）
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "../../Feature/Ocr/OcrRecognizer.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

namespace balloonwjui { class DuiLabel; class DuiButton; class DuiComboBox; class DuiRichEdit; }

namespace balloonocr {

//窗口私有消息：识别线程把结果投回本窗口，lParam 为 ocr::OcrTextResult*，由本窗口 delete
const UINT kMsgOcrRecognized = WM_APP + 0xF8;

/**
 *  「图片文字识别」结果窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 */
class COcrResultWindow : public balloonwjui::DuiFrameWindow
{
public:
    COcrResultWindow();
    ~COcrResultWindow();

    /**
     *  进程内唯一的窗口对象（堆上分配，进程退出时不析构；窗口本身按需创建）。
     */
    static COcrResultWindow& GetInstance();

    /**
     *  打开窗口并识别一个图片文件。
     *  @param anchor       发起识别的窗口：新建窗口时居中摆在它上面；为 NULL 或无效时居中于屏幕
     *  @param path         图片文件的完整路径
     *  @param quarterTurns 顺时针旋转的 90° 次数（看图窗的当前旋转），取值 0 ~ 3
     *  @return 窗口已显示并开始识别时返回 true
     */
    bool OpenForFile(HWND anchor, const std::wstring& path, int quarterTurns);

    /**
     *  打开窗口并识别内存中的图像（截图选区）。
     *  @param anchor 同 OpenForFile
     *  @param image  图像（复制一份保存，换语言重新识别时使用）
     *  @return 窗口已显示并开始识别时返回 true
     */
    bool OpenForImage(HWND anchor, const ocr::OcrBgraImage& image);

    /**
     *  打开窗口但先不识别（系统菜单「图片文字识别」）：状态行提示「请打开一张图片」，用户点「打开图片」选图后识别。
     *  窗口已打开时保留其中的图片与结果，只调到前台。
     *  @param anchor 同 OpenForFile
     *  @return 窗口已显示时返回 true
     */
    bool OpenEmpty(HWND anchor);

    BEGIN_MSG_MAP(COcrResultWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgOcrRecognized, OnRecognized)
        //销毁时取消正在进行的识别，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //打开窗口（需要时创建）、记下图片来源、开始识别
    bool Open(HWND anchor, const ocr::OcrRequest& request);

    //窗口不存在时创建并搭好客户区；成功返回 true
    bool EnsureWindow();

    //搭客户区控件树，捕获各控件指针
    void BuildClient();

    //按识别模块缓存的语言列表填下拉框，选中默认识别语言（不触发重新识别）
    void FillLanguages();

    //下拉框当前选中的识别语言代码；没有选中时为空（表示默认语言）
    std::wstring SelectedLanguage() const;

    //取消上一次识别（如有），按当前图片与语言提交新的识别
    void StartRecognition();

    //取消正在进行的识别（如有）
    void CancelRunning();

    //改写状态行
    void SetStatus(const std::wstring& text);

    //把文字框里的内容放进剪贴板
    void CopyAll();

    //「打开图片」：弹出选图对话框，选好后换成这张图并开始识别；取消时什么也不做
    void ChooseImageFile();

    //新建窗口时居中摆放：anchor 有效时摆在它上面，否则居中于屏幕
    void CenterOnAnchor(HWND anchor);

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRecognized(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    ocr::OcrRequest           m_request;         //当前图片来源（文件或内存图像）；换语言、重新识别时复用
    uint32_t                  m_taskId;          //正在进行的识别任务号；0 表示没有
    std::vector<std::wstring> m_languageTags;    //下拉框各项对应的识别语言代码，与下拉框的顺序一致

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiComboBox* m_pLanguage;       //识别语言下拉框
    balloonwjui::DuiLabel*    m_pStatus;         //状态行
    balloonwjui::DuiRichEdit* m_pText;           //识别结果文字框
    balloonwjui::DuiButton*   m_pRetry;          //「重新识别」；窗口里没有图片时不可用
    balloonwjui::DuiButton*   m_pCopy;           //「复制全部」
    balloonwjui::DuiButton*   m_pOpen;           //「打开图片」

    //标题栏左侧图标位图。DuiFrameWindow::SetIcon 只借用不释放，故本类持有、析构时 DeleteObject
    HBITMAP                   m_hTitleIcon;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
};

} // namespace balloonocr
