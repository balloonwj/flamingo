/**
 *  ContactTagModel.cpp
 *
 *  联系人标签纯函数的实现：归一、忽略大小写查找与去重、新增 / 修改两次交互的
 *  判定。全部只做字符串运算，不依赖任何控件、窗口与网络。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-14
 */

// ---- ATL/WTL 前置依赖（与同目录的 ContactModel.cpp 保持一致）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "ContactTagModel.h"

namespace ballooncontact {

CString NormalizeTag(LPCTSTR tag)
{
    if (tag == NULL)
    {
        return CString();
    }
    CString normalized(tag);
    // CString::Trim 去掉的是首尾的全部空白字符（空格、制表符、回车换行等），
    // 正是这里想要的口径；中间的空白一概保留。
    normalized.Trim();
    return normalized;
}

int FindTagIndexIgnoreCase(const std::vector<CString>& tags, const CString& tag)
{
    const CString target = NormalizeTag(tag);
    // 空标签不参与匹配：列表里本来就不该有空标签，让空串命中只会把调用方的
    // 判定引向"重复"这种错误分支。
    if (target.IsEmpty())
    {
        return -1;
    }

    const int count = (int)tags.size();
    for (int i = 0; i < count; ++i)
    {
        const CString current = NormalizeTag(tags[i]);
        if (current.CompareNoCase(target) == 0)
        {
            return i;
        }
    }
    return -1;
}

void NormalizeTagList(const std::vector<CString>& tags,
                      std::vector<CString>& outTags)
{
    outTags.clear();

    const int count = (int)tags.size();
    for (int i = 0; i < count; ++i)
    {
        const CString normalized = NormalizeTag(tags[i]);
        // 归一后为空的项直接丢弃 —— 空标签既存不进库（f_tag 是主键的一部分），
        // 界面上也只会画出一枚空胶囊。
        if (normalized.IsEmpty())
        {
            continue;
        }
        // 忽略大小写重复：只留首次出现的那一条，连同它的原始大小写。
        if (FindTagIndexIgnoreCase(outTags, normalized) >= 0)
        {
            continue;
        }
        outTags.push_back(normalized);
    }
}

TagAddDecision DecideTagAdd(const std::vector<CString>& tags,
                            const CString& input,
                            bool online,
                            int maxLen,
                            int maxCount)
{
    TagAddDecision decision;
    decision.m_normalized = NormalizeTag(input);

    // 用户什么都没输入（或只输了空白）：取消。排在最前面，是为了让"点开输入框
    // 又直接关掉"这种什么也没做的操作不弹任何提示，离线时也一样。
    if (decision.m_normalized.IsEmpty())
    {
        decision.m_action = kTagAddCancel;
        return decision;
    }

    // 超长：拦截。库里 f_tag 是 varchar(16)，放过去会被悄悄截断。
    if (decision.m_normalized.GetLength() > maxLen)
    {
        decision.m_action = kTagAddTooLong;
        return decision;
    }

    // 条数已达上限：拦截。界面在满额时本来就不显示「+ 添加标签」按钮，走到这里
    // 属于防御性判定（例如两处操作交叉、列表已被别处刷新过）。
    if ((int)tags.size() >= maxCount)
    {
        decision.m_action = kTagAddFull;
        return decision;
    }

    // 与已有标签重复（忽略大小写）：不追加。追加了也是同一个标签，只会在界面上
    // 出现两枚看起来一样的胶囊。
    const int dupIndex = FindTagIndexIgnoreCase(tags, decision.m_normalized);
    if (dupIndex >= 0)
    {
        decision.m_action   = kTagAddDuplicate;
        decision.m_dupIndex = dupIndex;
        return decision;
    }

    // 确实要追加、但发不出去：拦截。判定放在最后，见头文件里对判定顺序的说明。
    if (!online)
    {
        decision.m_action = kTagAddOffline;
        return decision;
    }

    decision.m_action = kTagAddAppend;
    return decision;
}

TagEditDecision DecideTagEdit(const std::vector<CString>& tags,
                              int index,
                              const CString& input,
                              bool online,
                              int maxLen)
{
    TagEditDecision decision;

    // 下标越界：控件 ctrlId 与列表不同步等异常路径，什么都不做。放在最前面，
    // 后面的分支就可以放心地按下标取原值。
    if (index < 0 || index >= (int)tags.size())
    {
        decision.m_action = kTagEditBadIndex;
        return decision;
    }

    decision.m_normalized = NormalizeTag(input);

    const CString current = NormalizeTag(tags[index]);

    // 与原值等价（忽略大小写、忽略首尾空白）：视为没改动，不发包也不提示。
    // 排在离线之前的理由与 DecideRemarkCommit 一致：用户点进编辑框又点出来、
    // 一个字没动时不该弹网络提示。
    if (!decision.m_normalized.IsEmpty()
        && current.CompareNoCase(decision.m_normalized) == 0)
    {
        decision.m_action = kTagEditNoChange;
        return decision;
    }

    // 超长：拦截。与新增一样，库里是 varchar(16)。
    if (decision.m_normalized.GetLength() > maxLen)
    {
        decision.m_action = kTagEditTooLong;
        return decision;
    }

    // 确有改动但发不出去：拦截。删除、合并、替换三种最终动作都要发一份完整
    // 列表上去，故统一在这里挡住。
    if (!online)
    {
        decision.m_action = kTagEditOffline;
        return decision;
    }

    // 内容被全删光：等价于删除这枚标签。这是产品定的交互 —— 编辑框里清空再确认
    // 就是删，用户不必再去找那个「×」。
    if (decision.m_normalized.IsEmpty())
    {
        decision.m_action = kTagEditRemove;
        return decision;
    }

    // 改成了与列表里另一枚等价的文本：两枚合并成一枚。留下来的是原来就在的那
    // 一枚（位置不变），文本取用户这次输入的写法。
    const int dupIndex = FindTagIndexIgnoreCase(tags, decision.m_normalized);
    if (dupIndex >= 0 && dupIndex != index)
    {
        decision.m_action     = kTagEditMerge;
        decision.m_mergeIndex = dupIndex;
        return decision;
    }

    decision.m_action = kTagEditUpdate;
    return decision;
}

} // namespace ballooncontact
