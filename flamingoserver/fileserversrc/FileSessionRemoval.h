/**
 *  按连接在会话列表中定位并移除会话的纯逻辑，FileSessionRemoval.h
 *
 *  fileserver 与 imgserver 共用的 FileServer 在连接断开时，要把「属于这条连接」的
 *  那一个会话从会话列表里摘掉。这段查找与移除本身不依赖任何网络类型，为便于单元
 *  测试（见 tests/test_filesessionremoval.cpp），把它抽成本文件中的函数模板：
 *  会话类型只需提供 getConnectionPtr()，连接类型只需支持相等比较与布尔取值
 *  （std::shared_ptr 与裸指针都满足），测试中因而可以代入假类型，不必构造真实连接。
 *
 *  抽出这段逻辑的直接原因是修一个缺陷：原先的实现遍历会话列表却没有比对连接，
 *  直接移除首个会话就结束，导致任何一条连接断开、被移除的都是最早建立的那个会话，
 *  真正断开的会话反而残留在表里。判定方式现与 ChatServer / RelayServer 保持一致。
 *
 *  典型用法（见 fileserversrc/FileServer.cpp 的 onDisconnected）：
 *      std::lock_guard<std::mutex> guard(m_sessionMutex);
 *      if (fileserver::RemoveSessionByConnection(m_sessions, conn))
 *          LOGI("client disconnected: %s", conn->peerAddress().toIpPort().c_str());
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

namespace fileserver {

/**
 *  在会话列表中找到「其 getConnectionPtr() 等于 conn」的那一个会话并移除。
 *
 *  一条连接至多对应一个会话，因此命中首个匹配项后立即返回，不再继续遍历。
 *  列表中没有匹配项时，任何元素都不会被移除。列表里可能存在连接指针已为空的
 *  会话（连接对象先行销毁），这类会话与任何有效连接都不相等，遍历时跳过即可，
 *  不能因为遇到它就中断查找 —— 否则排在其后的匹配项永远找不到。
 *
 *  本函数不涉及任何同步，调用方需自行持有保护会话列表的互斥量。
 *
 *  @param sessions [入出参] 会话列表。元素需支持 (*iter)->getConnectionPtr()，
 *                  容器需支持 begin() / end() / erase(iterator)。命中时该容器
 *                  少一个元素；元素通常是 std::shared_ptr，移除即释放一份引用。
 *  @param conn     待移除会话所属的连接，按相等比较定位。传空指针时一律视为无匹配，
 *                  以免把连接指针恰好为空的会话误当成匹配项删掉。本函数不获取、
 *                  也不释放该指针的所有权。
 *  @return 找到并移除了对应会话返回 true；列表中不存在该连接对应的会话返回 false。
 */
template <typename SessionListType, typename ConnectionPtrType>
bool RemoveSessionByConnection(SessionListType& sessions, const ConnectionPtrType& conn)
{
    //空连接不参与匹配，否则会与连接指针已为空的会话相等而误删
    if (!conn)
        return false;

    for (typename SessionListType::iterator iter = sessions.begin(); iter != sessions.end(); ++iter)
    {
        //通过比对 connection 对象找到对应的 session
        if ((*iter)->getConnectionPtr() == conn)
        {
            sessions.erase(iter);
            return true;
        }
    }

    return false;
}

} // namespace fileserver
