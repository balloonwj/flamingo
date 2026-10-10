/**
 *  MarkdownEditorManager.cpp
 *
 *  Markdown 编辑器窗口进程级管理器的实现，说明见 MarkdownEditorManager.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownEditorManager.h"

#include "MarkdownEditorHost.h"
#include "MarkdownEditorWindow.h"
#include "MarkdownPreviewWindow.h"
#include "../../Feature/Markdown/MarkdownDocument.h"   // IsMarkdownAvailable
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

namespace balloonmd {

CMarkdownEditorManager& CMarkdownEditorManager::GetInstance()
{
    //进程内唯一实例：堆上分配、从不释放，退出时不必关心析构顺序
    static CMarkdownEditorManager* s_instance = new CMarkdownEditorManager();
    return *s_instance;
}

CMarkdownEditorManager::CMarkdownEditorManager()
    : m_host(NULL)
    , m_cascade(0)
{
}

void CMarkdownEditorManager::SetHost(IMarkdownEditorHost* host)
{
    m_host = host;
    LOG_INFO("markdown editor: host %s.", (host != NULL) ? "attached" : "detached");
    OnLoginStateChanged();
}

IMarkdownEditorHost* CMarkdownEditorManager::GetHost() const
{
    return m_host;
}

bool CMarkdownEditorManager::OpenEditor(const std::string& sourceUtf8, const MarkdownSendTarget* target)
{
    if (!markdown::IsMarkdownAvailable())
    {
        LOG_WARNING("markdown editor: module unavailable, editor not opened.");
        return false;
    }
    CMarkdownEditorWindow* window = new CMarkdownEditorWindow();
    if (!window->Open(sourceUtf8, target, m_cascade))
    {
        //没有创建出窗口时 OnFinalMessage 不会发生，在这里释放
        delete window;
        return false;
    }
    m_editors.push_back(window);
    ++m_cascade;
    LOG_INFO("markdown editor: opened, open count=%u.", (unsigned)m_editors.size());
    return true;
}

size_t CMarkdownEditorManager::GetOpenCount() const
{
    return m_editors.size();
}

void CMarkdownEditorManager::OnLoginStateChanged()
{
    for (size_t i = 0; i < m_editors.size(); ++i)
        m_editors[i]->RefreshSendArea();
}

bool CMarkdownEditorManager::ConfirmCloseAllForExit(HWND owner)
{
    //有未保存修改的编辑器：列出文档名统一询问一次
    std::wstring names;
    int dirtyCount = 0;
    for (size_t i = 0; i < m_editors.size(); ++i)
    {
        if (!m_editors[i]->IsDirty())
            continue;
        //列举之间的分隔符随界面语言（中文为顿号，英文为逗号）
        if (!names.empty())
            names += TR(L"、");
        names += m_editors[i]->DocumentName();
        ++dirtyCount;
    }
    if (dirtyCount > 0)
    {
        const std::wstring tip = TRF(L"以下 Markdown 文档有未保存的修改：{0}。仍要退出吗？", names.c_str());
        if (::MessageBoxW(owner, tip.c_str(), TR(L"Markdown 编辑器"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        {
            LOG_INFO("markdown editor: exit cancelled, dirty editors=%d.", dirtyCount);
            return false;
        }
    }
    CloseAll();
    return true;
}

void CMarkdownEditorManager::CloseAll()
{
    //逐个销毁：销毁过程中 OnEditorDestroyed 会改动列表，先复制一份
    const std::vector<CMarkdownEditorWindow*> editors = m_editors;
    for (size_t i = 0; i < editors.size(); ++i)
        editors[i]->CloseWithoutPrompt();
    if (!editors.empty())
        LOG_INFO("markdown editor: closed all, count=%u.", (unsigned)editors.size());
    ClosePreviews();
}

bool CMarkdownEditorManager::OpenPreview(const MarkdownPreviewDoc& doc)
{
    if (!markdown::IsMarkdownAvailable())
    {
        LOG_WARNING("markdown preview: module unavailable, preview not opened.");
        return false;
    }
    //同一条消息已有窗口：激活它
    if (!doc.m_key.empty())
    {
        for (size_t i = 0; i < m_previews.size(); ++i)
        {
            if (m_previews[i]->Key() == doc.m_key && m_previews[i]->IsWindow())
            {
                m_previews[i]->Activate();
                LOG_INFO("markdown preview: reused window for key=%s.", doc.m_key.c_str());
                return true;
            }
        }
    }
    CMarkdownPreviewWindow* window = new CMarkdownPreviewWindow();
    if (!window->Open(doc, m_cascade))
    {
        delete window;
        return false;
    }
    m_previews.push_back(window);
    ++m_cascade;
    LOG_INFO("markdown preview: opened, open count=%u.", (unsigned)m_previews.size());
    return true;
}

size_t CMarkdownEditorManager::GetPreviewCount() const
{
    return m_previews.size();
}

void CMarkdownEditorManager::ClosePreviews()
{
    const std::vector<CMarkdownPreviewWindow*> previews = m_previews;
    for (size_t i = 0; i < previews.size(); ++i)
    {
        if (previews[i]->IsWindow())
            previews[i]->DestroyWindow();
    }
    if (!previews.empty())
        LOG_INFO("markdown preview: closed all, count=%u.", (unsigned)previews.size());
}

void CMarkdownEditorManager::OnPreviewDestroyed(CMarkdownPreviewWindow* window)
{
    for (std::vector<CMarkdownPreviewWindow*>::iterator it = m_previews.begin(); it != m_previews.end(); ++it)
    {
        if (*it == window)
        {
            m_previews.erase(it);
            break;
        }
    }
    delete window;
}

void CMarkdownEditorManager::OnEditorDestroyed(CMarkdownEditorWindow* window)
{
    for (std::vector<CMarkdownEditorWindow*>::iterator it = m_editors.begin(); it != m_editors.end(); ++it)
    {
        if (*it == window)
        {
            m_editors.erase(it);
            break;
        }
    }
    delete window;
    LOG_INFO("markdown editor: destroyed, open count=%u.", (unsigned)m_editors.size());
}

} // namespace balloonmd
