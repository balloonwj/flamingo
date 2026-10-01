/**
 *  GroupManageInputCarry.cpp
 *
 *  GroupManageInputCarry.h 的实现：群管理窗重建时输入框草稿的带入规则，以及重建后的滚动位置。
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用同一份源码。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "GroupManageInputCarry.h"

namespace balloongroupmanage {

bool InputDraftTable::OnInputBuilt(int ctrlId, const std::wstring& builtText, std::wstring* draft)
{
    m_baselines[ctrlId] = builtText;

    std::map<int, std::wstring>::iterator it = m_drafts.find(ctrlId);
    if (it == m_drafts.end())
    {
        return false;
    }
    //草稿与新建出来的文字相同：用户的修改已经生效（例如保存成功后服务端推回同一段文字），
    //不必再当作草稿保留，否则之后服务端再推新值时会被这份过时的草稿盖住
    if (it->second == builtText)
    {
        m_drafts.erase(it);
        return false;
    }
    if (draft != NULL)
    {
        *draft = it->second;
    }
    return true;
}

void InputDraftTable::OnInputLeaving(int ctrlId, const std::wstring& currentText)
{
    std::wstring baseline;
    std::map<int, std::wstring>::const_iterator bit = m_baselines.find(ctrlId);
    if (bit != m_baselines.end())
    {
        baseline = bit->second;
    }

    if (currentText == baseline)
    {
        m_drafts.erase(ctrlId);
        return;
    }
    m_drafts[ctrlId] = currentText;
}

bool InputDraftTable::Revert(int ctrlId, std::wstring* baseline)
{
    m_drafts.erase(ctrlId);

    std::map<int, std::wstring>::const_iterator bit = m_baselines.find(ctrlId);
    if (bit == m_baselines.end())
    {
        return false;
    }
    if (baseline != NULL)
    {
        *baseline = bit->second;
    }
    return true;
}

bool InputDraftTable::HasDraft(int ctrlId) const
{
    return m_drafts.find(ctrlId) != m_drafts.end();
}

int ScrollPosAfterRebuild(int pageBefore, int pageAfter, int scrollPosBefore)
{
    if (pageBefore != pageAfter || scrollPosBefore < 0)
    {
        return 0;
    }
    return scrollPosBefore;
}

} // namespace balloongroupmanage
