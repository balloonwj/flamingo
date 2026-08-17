/**
 *  ContactModel.cpp
 *
 *  联系人资料对话框数据模型的实现。当前以 mock 数据初始化，对照设计稿
 *  dialogs.jsx 的 ContactInfoDialog —— remark=「设计组组长」、
 *  name=「江屿白」那一组。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "ContactModel.h"

namespace ballooncontact {

namespace {

// mock 联系人「江屿白」的两个初始标签（设计稿 useState(['朋友','设计'])）。
const TCHAR* const kMockTags[] =
{
    _T("朋友"),
    _T("设计"),
};
const int kMockTagCount = sizeof(kMockTags) / sizeof(kMockTags[0]);

// mock 分组列表（设计稿用 CONTACT_GROUPS，此处给一组等价的中文分组名）。
const TCHAR* const kMockGroups[] =
{
    _T("我的好友"),
    _T("产品部 · 设计组"),
    _T("同事"),
    _T("家人"),
};
const int kMockGroupCount = sizeof(kMockGroups) / sizeof(kMockGroups[0]);

// mock 默认选中的分组下标 —— 对应「产品部 · 设计组」（见 kMockGroups[1]）。
const int kMockDefaultGroup = 1;

} // anonymous namespace

ContactModel::ContactModel()
    : m_name(_T("江屿白"))
    , m_remark(_T("设计组组长"))
    , m_account(_T("10923847"))
    , m_region(_T("北京"))
    , m_title(_T("设计组组长"))
    , m_dept(_T("产品部 / 设计组"))
    , m_signature(_T("在出差路上…"))
    , m_status(kContactStatusAway)        // 设计稿头像状态点为 away（橙）
    , m_selectedGroup(kMockDefaultGroup)
{
    // 初始标签。
    for (int i = 0; i < kMockTagCount; ++i)
    {
        m_tags.push_back(CString(kMockTags[i]));
    }

    // 可选分组。
    for (int i = 0; i < kMockGroupCount; ++i)
    {
        m_groups.push_back(CString(kMockGroups[i]));
    }

    // 三个开关默认全关。
    for (int i = 0; i < kContactSwitchCount; ++i)
    {
        m_switches[i] = false;
    }
}

bool ContactModel::IsValidSwitch(int kind)
{
    return kind >= 0 && kind < kContactSwitchCount;
}

// ---- 真实数据注入 ----

void ContactModel::SetBasicInfo(const CString& name, const CString& account,
                                const CString& region, const CString& title,
                                const CString& dept, const CString& signature,
                                int status)
{
    m_name      = name;
    m_account   = account;
    m_region    = region;
    m_title     = title;
    m_dept      = dept;
    m_signature = signature;
    m_status    = status;
    // 真实数据暂无独立备注名，备注名清空（窗口会回退用真名显示）。
    m_remark.Empty();
}

void ContactModel::ClearTags()
{
    m_tags.clear();
}

void ContactModel::SetGroups(const std::vector<CString>& groups, int selected)
{
    m_groups = groups;
    if (selected < 0 || selected >= (int)m_groups.size())
    {
        selected = 0;
    }
    m_selectedGroup = selected;
}

// ---- 只读资料 ----

const CString& ContactModel::Name() const
{
    return m_name;
}

const CString& ContactModel::Remark() const
{
    return m_remark;
}

void ContactModel::SetRemark(LPCTSTR remark)
{
    m_remark = (remark != NULL) ? remark : _T("");
}

const CString& ContactModel::Account() const
{
    return m_account;
}

const CString& ContactModel::Region() const
{
    return m_region;
}

const CString& ContactModel::Title() const
{
    return m_title;
}

const CString& ContactModel::Dept() const
{
    return m_dept;
}

const CString& ContactModel::Signature() const
{
    return m_signature;
}

int ContactModel::Status() const
{
    return m_status;
}

CString ContactModel::AvatarLetter() const
{
    // 头像占位取真名首字。真名为空时返回空串。
    if (m_name.IsEmpty())
    {
        return CString();
    }
    return m_name.Left(1);
}

// ---- 标签 ----

int ContactModel::TagCount() const
{
    return (int)m_tags.size();
}

CString ContactModel::Tag(int index) const
{
    if (index < 0 || index >= (int)m_tags.size())
    {
        return CString();
    }
    return m_tags[index];
}

const std::vector<CString>& ContactModel::Tags() const
{
    return m_tags;
}

void ContactModel::SetTags(const std::vector<CString>& tags)
{
    m_tags = tags;
}

bool ContactModel::AddTag(LPCTSTR tag)
{
    if (tag == NULL)
    {
        return false;
    }

    // 去首尾空白后为空则忽略 —— 对应设计稿 newTag.trim() 判空。
    CString trimmed(tag);
    trimmed.Trim();
    if (trimmed.IsEmpty())
    {
        return false;
    }

    m_tags.push_back(trimmed);
    return true;
}

void ContactModel::RemoveTag(int index)
{
    if (index < 0 || index >= (int)m_tags.size())
    {
        return;
    }
    m_tags.erase(m_tags.begin() + index);
}

// ---- 分组 ----

int ContactModel::GroupCount() const
{
    return (int)m_groups.size();
}

CString ContactModel::Group(int index) const
{
    if (index < 0 || index >= (int)m_groups.size())
    {
        return CString();
    }
    return m_groups[index];
}

int ContactModel::SelectedGroup() const
{
    return m_selectedGroup;
}

void ContactModel::SetSelectedGroup(int index)
{
    if (index >= 0 && index < (int)m_groups.size())
    {
        m_selectedGroup = index;
    }
}

// ---- 开关 ----

bool ContactModel::Switch(int kind) const
{
    if (!IsValidSwitch(kind))
    {
        return false;
    }
    return m_switches[kind];
}

void ContactModel::SetSwitch(int kind, bool on)
{
    if (IsValidSwitch(kind))
    {
        m_switches[kind] = on;
    }
}

} // namespace ballooncontact
