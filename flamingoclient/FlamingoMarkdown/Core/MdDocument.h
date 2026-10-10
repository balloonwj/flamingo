/**
 *  FlamingoMarkdown.dll 的文档对象：接口里不透明的 flamingomd::MdDocument 的真正定义。
 *
 *  一个文档持有解析得到的模型，以及最近一次排版的样式、宽度与结果；样式与宽度都不变时 m_layout 直接复用。
 *  文档由 m_parse 用 new 创建、由 m_freeDoc 用 delete 释放，两者都在 DLL 内，堆一致。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include "../FlamingoMarkdownApi.h"
#include "MdLayout.h"
#include "MdModel.h"

namespace flamingomd {

//文档的校验值（"MDOC"），m_freeDoc 等函数据此拒绝不是本 DLL 创建的指针
const uint32_t kMdDocumentMagic = 0x434F444D;

/**
 *  文档对象。
 */
struct MdDocument
{
    uint32_t        m_magic;      //校验值，有效文档为 kMdDocumentMagic，释放前清零
    MdModel         m_model;      //解析得到的模型
    bool            m_laidOut;    //是否已经排版过
    MdResolvedStyle m_style;      //最近一次排版使用的样式
    int             m_maxWidth;   //最近一次排版使用的最大宽度
    MdLayoutResult  m_layout;     //最近一次排版的结果

    MdDocument()
        : m_magic(kMdDocumentMagic), m_laidOut(false), m_maxWidth(0)
    {
    }
};

/**
 *  判断指针是否为有效文档。
 */
inline bool IsValidMdDocument(const MdDocument* doc)
{
    return doc != NULL && doc->m_magic == kMdDocumentMagic;
}

} // namespace flamingomd
