/**
 *  「特别关注」上下线提示窗行模型的实现，StarredPopupModel.cpp
 *  设计与规则见 StarredPopupModel.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing），与同目录的 GroupRowText.cpp 同例。
 *
 *  本文件含中文宽字符字面量。两个工程均已开启 /utf-8 编译选项，源码按 UTF-8 解析，
 *  _T("中文") 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "StarredPopupModel.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

CString FormatStarredPresenceText(const CString& name, int64_t userid, bool online)
{
    //显示名缺失时回落成「用户<id>」——与通讯录树、会话列表查不到昵称时的回落一致。
    CString shown = name;
    if (shown.IsEmpty())
    {
        shown.Format(TR(L"用户%lld"), (long long)userid);
    }

    CString text;
    text.Format(_T("%s %s"), (LPCTSTR)shown, online ? TR(L"已上线") : TR(L"已离线"));
    return text;
}

std::vector<StarredPopupRow> BuildStarredPopupRows(
    const std::vector<StarredPresenceEvent>& events, int maxRows)
{
    std::vector<StarredPopupRow> rows;
    if (maxRows <= 0)
    {
        return rows;
    }

    //第一步：按 userid 去重，只留每个人最后一次的状态，位置留在他第一次出现处。
    //人数至多几十，线性查找足够，也省得为了顺序再维护一张 map。
    std::vector<StarredPresenceEvent> merged;
    for (std::vector<StarredPresenceEvent>::size_type i = 0; i < events.size(); ++i)
    {
        const StarredPresenceEvent& e = events[i];
        //脏数据不该占一行。
        if (e.m_userid <= 0)
        {
            continue;
        }

        bool found = false;
        for (std::vector<StarredPresenceEvent>::size_type k = 0; k < merged.size(); ++k)
        {
            if (merged[k].m_userid == e.m_userid)
            {
                //就地覆盖：状态与显示名都取最后一次的（名字可能刚被改过备注），
                //但不动它在序列里的位置。
                merged[k] = e;
                found = true;
                break;
            }
        }
        if (!found)
        {
            merged.push_back(e);
        }
    }

    const int total = (int)merged.size();
    if (total == 0)
    {
        return rows;
    }

    //第二步：排行。人数没超过上限就全列；超了则末行让给「等 N 人」。
    const bool needSummary = (total > maxRows);
    const int  shownCount  = needSummary ? (maxRows - 1) : total;

    for (int i = 0; i < shownCount; ++i)
    {
        StarredPopupRow row;
        row.m_text = FormatStarredPresenceText(merged[(std::vector<StarredPresenceEvent>::size_type)i].m_name,
                                               merged[(std::vector<StarredPresenceEvent>::size_type)i].m_userid,
                                               merged[(std::vector<StarredPresenceEvent>::size_type)i].m_online);
        row.m_userid    = merged[(std::vector<StarredPresenceEvent>::size_type)i].m_userid;
        row.m_isSummary = false;
        rows.push_back(row);
    }

    if (needSummary)
    {
        StarredPopupRow row;
        //N 是被折叠掉的人数，不是总人数 —— 上面已经列出来的那几个不该再数一遍。
        row.m_text.Format(TR(L"等 %d 人"), total - shownCount);
        row.m_userid    = 0;   //汇总行没有对应的人，点它不开窗
        row.m_isSummary = true;
        rows.push_back(row);
    }

    return rows;
}

} // namespace balloonmain
