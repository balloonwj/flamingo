/**
 *  BalloonProfileWindow.cpp
 *
 *  个人信息对话框的实现。查看 / 编辑两态切换 + 字段编辑 + 保存回写
 *  数据模型（ProfileModel，当前仅内存暂存）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"
#include "BalloonProfileWindow.h"

#include <memory>

#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"                 // LOG_INFO / LOG_WARNING / LOG_ERROR
#include "ProfileLayout.h"            // BuildProfileBody / ProfileCtrlId / helper
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace balloonprofile;

// ---- 文件作用域常量 -----------------------------------------------------

namespace {

// 个人信息对话框固定客户区宽度（逻辑像素，设计稿 Modal width 420）。
const int kProfileWindowWidth = 420;

// 标题栏高度（逻辑像素，设计稿 Modal header 44）。
const int kProfileTitleBarHeight = 44;

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonProfileWindow::CBalloonProfileWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_editMode(false)
{
}

CBalloonProfileWindow::~CBalloonProfileWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonProfileWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("个人信息对话框 Create 失败。"));
        return;
    }

    SetTitle(_T("个人信息"));
    SetTitleBarHeight(kProfileTitleBarHeight);
    SetButtons(false, false, true);   // 只保留关闭按钮
    SetResizable(false);              // 固定尺寸对话框

    // 初始为查看态，构建客户区并按查看态高度定窗口尺寸。
    m_editMode = false;
    SetMinSize(kProfileWindowWidth, ProfileBodyHeight(false));
    RebuildUi();

    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    LOG_INFO(_T("个人信息对话框已创建并显示（查看态）。"));
}

// =======================================================================
// 客户区构建 / 模型同步
// =======================================================================

void CBalloonProfileWindow::RebuildUi()
{
    // 按当前模式构造对应的客户区控件树。
    std::unique_ptr<DuiControl> root = BuildProfileBody(m_editMode);
    if (!root)
    {
        LOG_ERROR(_T("BuildProfileBody 返回空 —— 个人信息对话框客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 编辑态的文本框要画出边框，与只读态的纯文本行区分开。
    if (m_editMode)
    {
        const UINT editIds[] =
        {
            kIdEditNickname, kIdEditSignature,
            kIdEditBirthday, kIdEditRegion, kIdEditEmail,
        };
        const int editCount = sizeof(editIds) / sizeof(editIds[0]);
        for (int i = 0; i < editCount; ++i)
        {
            DuiEdit* edit = dynamic_cast<DuiEdit*>(
                content->FindCtrlById(editIds[i]));
            if (edit == NULL)
            {
                continue;
            }
            edit->SetShowBorder(true);
        }
    }

    // 把模型数据灌进当前模式的控件。
    LoadModelIntoUi();

    // 按当前模式同步窗口尺寸（两态布局高度不同）。
    ResizeClient(kProfileWindowWidth, ProfileBodyHeight(m_editMode));

    LOG_INFO(_T("个人信息对话框客户区已重建（模式：%s）。"),
             m_editMode ? _T("编辑态") : _T("查看态"));
}

void CBalloonProfileWindow::LoadModelIntoUi()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 头像字母占位：取姓名首字。
    CString letter;
    if (!m_model.Name().IsEmpty())
    {
        letter = m_model.Name().Left(1);
    }
    SetProfileAvatarLetter(content, kIdAvatar, letter);

    // 状态胶囊：按模型当前状态刷新三个胶囊的选中态。
    const UINT pillIds[kProfileStatusCount] =
    {
        kIdPillOnline, kIdPillAway, kIdPillBusy,
    };
    for (int i = 0; i < kProfileStatusCount; ++i)
    {
        const int kind = GetProfilePillStatus(content, pillIds[i]);
        SetProfilePillSelected(content, pillIds[i], kind == m_model.Status());
    }

    if (m_editMode)
    {
        // 编辑态：把可编辑字段灌进文本框 / 下拉框。
        DuiEdit* nick = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdEditNickname));
        if (nick != NULL)
        {
            nick->SetText(m_model.Nickname());
        }

        DuiEdit* sign = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdEditSignature));
        if (sign != NULL)
        {
            sign->SetText(m_model.Signature());
        }

        DuiComboBox* gender = dynamic_cast<DuiComboBox*>(
            content->FindCtrlById(kIdEditGender));
        if (gender != NULL)
        {
            // 找到与模型性别匹配的项并选中；找不到则不动默认项。
            for (int i = 0; i < gender->GetCount(); ++i)
            {
                if (gender->GetItemText(i) == m_model.Gender())
                {
                    gender->SetCurSel(i, false);
                    break;
                }
            }
        }

        DuiEdit* birth = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdEditBirthday));
        if (birth != NULL)
        {
            birth->SetText(m_model.Birthday());
        }

        DuiEdit* region = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdEditRegion));
        if (region != NULL)
        {
            region->SetText(m_model.Region());
        }

        DuiEdit* email = dynamic_cast<DuiEdit*>(
            content->FindCtrlById(kIdEditEmail));
        if (email != NULL)
        {
            email->SetText(m_model.Email());
        }
    }
    else
    {
        // 查看态：把只读字段值灌进文本控件。
        SetProfileViewText(content, kIdViewNickname,  m_model.Nickname());
        SetProfileViewText(content, kIdViewSignature, m_model.Signature());
        SetProfileViewText(content, kIdViewGender,    m_model.Gender());
        SetProfileViewText(content, kIdViewBirthday,  m_model.Birthday());
        SetProfileViewText(content, kIdViewRegion,    m_model.Region());
        SetProfileViewText(content, kIdViewEmail,     m_model.Email());
    }
}

void CBalloonProfileWindow::CommitUiToModel()
{
    DuiControl* content = GetClientContent();
    if (content == NULL || !m_editMode)
    {
        return;
    }

    DuiEdit* nick = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdEditNickname));
    if (nick != NULL)
    {
        CString value = nick->GetText();
        value.Trim();
        m_model.SetNickname(value);
    }

    DuiEdit* sign = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdEditSignature));
    if (sign != NULL)
    {
        m_model.SetSignature(sign->GetText());
    }

    DuiComboBox* gender = dynamic_cast<DuiComboBox*>(
        content->FindCtrlById(kIdEditGender));
    if (gender != NULL)
    {
        m_model.SetGender(gender->GetText());
    }

    DuiEdit* birth = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdEditBirthday));
    if (birth != NULL)
    {
        CString value = birth->GetText();
        value.Trim();
        m_model.SetBirthday(value);
    }

    DuiEdit* region = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdEditRegion));
    if (region != NULL)
    {
        CString value = region->GetText();
        value.Trim();
        m_model.SetRegion(value);
    }

    DuiEdit* email = dynamic_cast<DuiEdit*>(
        content->FindCtrlById(kIdEditEmail));
    if (email != NULL)
    {
        CString value = email->GetText();
        value.Trim();
        m_model.SetEmail(value);
    }

    LOG_INFO(_T("个人信息编辑态内容已提交回数据模型。"));
}

// =======================================================================
// 模式切换 / 按钮动作
// =======================================================================

void CBalloonProfileWindow::SwitchMode(bool toEditMode)
{
    if (toEditMode == m_editMode)
    {
        return;   // 已是目标模式
    }

    m_editMode = toEditMode;
    RebuildUi();
    CenterWindow();   // 两态高度不同，重新居中

    LOG_INFO(_T("个人信息对话框切换到%s。"),
             m_editMode ? _T("编辑态") : _T("查看态"));
}

void CBalloonProfileWindow::DoSave()
{
    // 先把编辑态控件内容收回模型，再切回查看态（查看态重建时会读模型）。
    CommitUiToModel();
    SwitchMode(false);

    LOG_INFO(_T("个人信息已保存（昵称=%s，性别=%s）。"),
             (LPCTSTR)m_model.Nickname(), (LPCTSTR)m_model.Gender());
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonProfileWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口（--profile）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 交由 CMainDlg 统一收尾。
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

LRESULT CBalloonProfileWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdAvatar:
            // 头像点击 = 换头像入口（功能开发中，当前仅占位提示）。
            ::MessageBox(m_hWnd,
                         _T("更换头像功能开发中。"),
                         _T("个人信息"), MB_OK | MB_ICONINFORMATION);
            return 0;

        case kIdPillOnline:
            m_model.SetStatus(kProfileStatusOnline);
            LoadModelIntoUi();
            return 0;
        case kIdPillAway:
            m_model.SetStatus(kProfileStatusAway);
            LoadModelIntoUi();
            return 0;
        case kIdPillBusy:
            m_model.SetStatus(kProfileStatusBusy);
            LoadModelIntoUi();
            return 0;

        case kIdBtnEdit:
            SwitchMode(true);
            return 0;
        case kIdBtnCancel:
            SwitchMode(false);
            return 0;
        case kIdBtnSave:
            DoSave();
            return 0;
        case kIdBtnClose:
            // "关闭"按钮等价于点标题栏关闭按钮。
            PostMessage(WM_CLOSE);
            return 0;

        default:
            break;
        }
    }

    // 其它通知交回 DuiFrameWindow 链。
    bHandled = FALSE;
    return 0;
}
