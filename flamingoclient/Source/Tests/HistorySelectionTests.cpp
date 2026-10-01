/**
 *  消息记录窗口多选集合与「多选」菜单项的单元测试（HFW2～HFW4），HistorySelectionTests.cpp
 *
 *  覆盖：选中 / 取消、翻页后仍保留（集合只按 msgid 记，与页无关）、上限、清空、组装转发请求时
 *  升序去重并统计跳过的条数，以及右键菜单里「多选」出现的条件与位置。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"
#include <vector>
#include "../UI/BalloonHistory/HistorySelection.h"
#include "../UI/BalloonHistory/HistoryRowMenu.h"
#include "../net/Protocol.h"   // message_type_*

using namespace balloonhistory;

TEST_CASE("HFW2 组装转发：能转发的 msgid 升序去重，不能转发的计入跳过", "[historymultiselect]")
{
    HistorySelection sel(0);
    sel.Toggle(5, ::message_type_text, NULL);
    sel.Toggle(3, ::message_type_image, NULL);
    sel.Toggle(9, ::message_type_folder, NULL);   //文件夹记录不能转发
    sel.Toggle(7, ::message_type_file, NULL);

    std::vector<int64_t> msgids;
    size_t skipped = 99;
    sel.BuildForward(msgids, skipped);
    const std::vector<int64_t> expect = { 3, 5, 7 };
    CHECK(msgids == expect);
    CHECK(skipped == 1);
}

TEST_CASE("HFW3 选择集合：选中与取消、跨页保留、上限、清空", "[historymultiselect]")
{
    const size_t kMax = 3;
    HistorySelection sel(kMax);
    bool full = true;

    //选中再取消
    CHECK(sel.Toggle(10, ::message_type_text, &full));
    CHECK_FALSE(full);
    CHECK(sel.IsSelected(10));
    CHECK_FALSE(sel.Toggle(10, ::message_type_text, &full));
    CHECK_FALSE(sel.IsSelected(10));
    CHECK(sel.Count() == 0);

    //"翻页"：集合只按 msgid 记，不同页的消息照样累积
    CHECK(sel.Toggle(30, ::message_type_text, &full));   //第 1 页的一条
    CHECK(sel.Toggle(10, ::message_type_text, &full));   //第 3 页的一条
    CHECK(sel.Toggle(20, ::message_type_text, &full));   //第 2 页的一条
    CHECK(sel.Count() == kMax);
    const std::vector<int64_t> expect = { 10, 20, 30 };
    CHECK(sel.SelectedMsgids() == expect);

    //已满：再选新的不生效并报满；取消已选的不受上限影响
    CHECK_FALSE(sel.Toggle(40, ::message_type_text, &full));
    CHECK(full);
    CHECK_FALSE(sel.IsSelected(40));
    CHECK_FALSE(sel.Toggle(20, ::message_type_text, &full));
    CHECK_FALSE(full);
    CHECK(sel.Count() == kMax - 1);

    //msgid 未分配的不能选
    CHECK_FALSE(sel.Toggle(0, ::message_type_text, &full));
    CHECK(sel.Count() == kMax - 1);

    sel.Clear();
    CHECK(sel.Count() == 0);
    CHECK(sel.SelectedMsgids().empty());
}

TEST_CASE("HFW4 「多选」在 msgid 已分配时出现，紧跟「转发」", "[historymultiselect][historymenu]")
{
    HistoryRowMenuContext ctx;
    ctx.m_msgType = ::message_type_text;
    ctx.m_msgId = 100;
    std::vector<HistoryRowMenuItem> model = BuildHistoryRowMenuModel(ctx);
    bool adjacent = false;
    for (size_t i = 0; i + 1 < model.size(); ++i)
    {
        if (model[i].m_id == (int)kHistoryRowMenuForward && model[i + 1].m_id == (int)kHistoryRowMenuMultiSelect)
        {
            adjacent = true;
        }
    }
    CHECK(adjacent);

    //文件夹记录不能转发，但能删除，照样给「多选」
    ctx.m_msgType = ::message_type_folder;
    model = BuildHistoryRowMenuModel(ctx);
    bool hasMulti = false;
    for (size_t i = 0; i < model.size(); ++i)
    {
        hasMulti = hasMulti || model[i].m_id == (int)kHistoryRowMenuMultiSelect;
    }
    CHECK(hasMulti);

    //msgid 未分配：不给
    ctx.m_msgType = ::message_type_text;
    ctx.m_msgId = kHistoryRowMsgIdUnassigned;
    model = BuildHistoryRowMenuModel(ctx);
    hasMulti = false;
    for (size_t i = 0; i < model.size(); ++i)
    {
        hasMulti = hasMulti || model[i].m_id == (int)kHistoryRowMenuMultiSelect;
    }
    CHECK_FALSE(hasMulti);
}
