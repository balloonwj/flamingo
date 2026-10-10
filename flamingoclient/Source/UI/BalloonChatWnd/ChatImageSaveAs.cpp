/**
 *  ChatImageSaveAs.cpp
 *
 *  ChatImageSaveAs.h 的实现。做法与看图窗的 ImageCanvas::SaveImageAs 保持一致：
 *  同一套过滤器、同样按原字节 CopyFile，免得同一个功能在两处给出不同观感。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include "stdafx.h"
#include "ChatImageSaveAs.h"

#include "../../Common/Log/IULog.h"   // LOG_INFO / LOG_ERROR
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译

namespace balloonchatwnd {

namespace {

//默认文件名没有后缀时用的扩展名（缓存图的文件名是 md5、不带后缀）。
const TCHAR* const kFallbackSaveExt = _T("png");

} // anonymous namespace

bool SaveChatImageAs(HWND owner, const std::wstring& imagePath,
                     const std::wstring& defaultName)
{
    if (imagePath.empty())
    {
        LOG_ERROR("图片另存为：本地路径为空，放弃.");
        return false;
    }

    //默认扩展名取默认文件名里的那一段：用户在对话框里只输主干时，由它补全后缀。
    CString defName = defaultName.c_str();
    const int dot = defName.ReverseFind(_T('.'));
    CString defExt = (dot >= 0) ? defName.Mid(dot + 1) : CString(kFallbackSaveExt);

    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"图片文件"), filedlg::kImagePatterns, TR(L"所有文件"), L"*.*");
    CFileDialog dlg(FALSE, defExt, defName,
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER,
                    fileFilter.c_str(),
                    owner);
    if (dlg.DoModal() != IDOK)
    {
        return false;   //用户取消，什么都不做
    }

    if (!::CopyFileW(imagePath.c_str(), dlg.m_szFileName, FALSE))
    {
        LOG_ERROR("图片另存为失败: err=%lu.", (unsigned long)::GetLastError());
        ::MessageBox(owner, TR(L"另存为失败，请重试。"),
                     TR(L"图片另存为"), MB_ICONWARNING | MB_OK);
        return false;
    }

    LOG_INFO("图片另存为成功.");
    return true;
}

} // namespace balloonchatwnd
