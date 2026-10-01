/**
 *  联系人资料池的 userid 索引：按 userid 直接定位到资料池（std::vector<net::ContactItem>）
 *  中的下标，代替逐个比对的线性查找。
 *
 *  主面板的会话列表重建、头像下载完成、上下线推送等路径都要反复"按 userid 找联系人"。
 *  资料池有几千人时，逐个比对的查找每次要比较上千次，一次会话列表重建累计近一秒。
 *
 *  <u>使用约定</u>：索引记录的是下标，资料池整体替换（赋值、清空、增删元素）之后必须调用
 *  Rebuild，否则查到的下标会指向错误的联系人甚至越界。只修改某个联系人的字段（改备注、
 *  改在线状态）不影响下标，无需重建。
 *
 *  本文件不依赖 stdafx.h，与 FlamingoUnit 测试工程共用同一份源码
 *  （见 Tests/ContactLookupIndexTests.cpp）。
 *
 *  典型用法：
 *      m_contacts = result.m_contacts;
 *      m_contactIndex.Rebuild(m_contacts);
 *      int idx = m_contactIndex.Find(userId);
 *      const net::ContactItem* c = (idx >= 0) ? &m_contacts[idx] : NULL;
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <map>
#include <vector>

#include "../../net/ContactsProtocolClient.h"   // net::ContactItem

namespace balloonmain {

/**
 *  ContactLookupIndex —— userid → 资料池下标。说明见文件头。
 */
class ContactLookupIndex
{
public:
    /**
     *  按资料池重建索引，原有内容全部丢弃。
     *    contacts：资料池。本类只记录下标，不持有、不复制资料。
     *  同一个 userid 出现多次时（服务端承诺去重，这里只是防御）记第一次出现的位置，
     *  与原先"从头逐个比对、找到即返回"的结果一致。
     *  键取 userid 的低 32 位（(UINT)m_userid），与主面板各处按 UINT 传递 userid 的口径一致。
     */
    void Rebuild(const std::vector<net::ContactItem>& contacts);

    /**
     *  按 userid 查下标。
     *    userId：要找的联系人 userid。
     *  返回：资料池中的下标；不存在时返回 -1。
     */
    int Find(unsigned int userId) const;

    // 清空索引。
    void Clear();

    // 索引中的 userid 个数。
    size_t Count() const;

private:
    std::map<unsigned int, int> m_indexById;   // userid（低 32 位）→ 资料池下标
};

} // namespace balloonmain
