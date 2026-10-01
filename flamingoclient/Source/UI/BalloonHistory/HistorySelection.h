/**
 *  HistorySelection.h
 *
 *  消息记录窗口多选模式下的选择集合：按 msgid 记住用户勾选的消息，翻页后仍然保留，退出多选
 *  或换会话时清空。同时记下每条的消息类型，组装转发请求时据此判断哪些能转发（规则见
 *  HistoryRowMenu.h 的 CanForwardHistoryRow），不能转发的计入"跳过 N 条"。
 *
 *  纯逻辑，不碰窗口、不查库，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用（见
 *  Tests/HistorySelectionTests.cpp）。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      HistorySelection sel(net::kForwardMaxMessages);
 *      bool full = false;
 *      sel.Toggle(rec.m_msgid, rec.m_msgtype, &full);          //点行前的复选框
 *      std::vector<int64_t> msgids;
 *      size_t skipped = 0;
 *      sel.BuildForward(msgids, skipped);                       //点「合并转发」
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <vector>

namespace balloonhistory {

//多选模式下的选择集合
class HistorySelection
{
public:
    //@param maxCount  最多能选几条；0 表示不限
    explicit HistorySelection(size_t maxCount);

    /**
     *  切换一条消息的选中状态。
     *    msgid：服务器 msgid；不为正数时什么都不做、返回 false（这条既不能转发也不能删除）。
     *    msgType：消息内容类型（net/Protocol.h 的 message_type），组装转发时判断能否转发用。
     *    outFull：[出参，可为 NULL] 这条原本没选中、但已经选满时置 true，否则置 false。
     *  @return 切换之后这条是否处于选中状态
     */
    bool Toggle(int64_t msgid, int msgType, bool* outFull);

    //这条消息是否已选中
    bool IsSelected(int64_t msgid) const;

    //已选中的条数
    size_t Count() const;

    //清空选择（退出多选、换会话时调用）
    void Clear();

    //全部已选中的 msgid，升序（删除时逐条发请求用）
    std::vector<int64_t> SelectedMsgids() const;

    /**
     *  组装转发请求的消息列表。
     *    outMsgids：[出参] 能转发的 msgid，升序、不重复（与 balloonmain::ForwardRequest 的约定一致）；
     *               调用前会被清空。
     *    outSkipped：[出参] 选中了但不能转发的条数，用于提示「已跳过 N 条」。
     */
    void BuildForward(std::vector<int64_t>& outMsgids, size_t& outSkipped) const;

private:
    //已选中的消息：msgid → 消息内容类型。用 map 自然按 msgid 升序、不重复
    std::map<int64_t, int> m_items;
    //最多能选几条；0 表示不限
    size_t                 m_maxCount;
};

} // namespace balloonhistory
