/**
 *  聊天记录管理器「链接」页与「与我有关」页的行内容纯逻辑（不依赖界面与 SQLite），HistoryLibraryLinks.h
 *
 *  链接页每条消息一行（docs/聊天记录管理器方案.md §12.4）：第一个链接的域名与完整链接，一条消息有多个链接时
 *  注明「等 N 个链接」；行操作「复制链接」复制这条消息里的全部链接。与我有关页（§12.5）每行显示正文预览。
 *  正文取 HistoryLibraryItem::m_name（本地库的 search_text）：文字消息的 search_text 是还原过的正文
 *  （内容 JSON 的 text 里 '/' 被转义成 "//"，不能直接用，见 V2MsgQuery.cpp 的 DigestFaceTokens）。
 *  本文件识别链接、算出域名。链接识别沿用聊天气泡的 balloonchatwnd::DetectLinks（只认 http / https /
 *  www. 开头的串），与聊天窗里能点开的链接口径一致。
 *
 *  单测见 Tests/HistoryLibraryTests.cpp（标签 [histlinks]）。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      HistoryLinkSummary s;
 *      if (SummarizeLibraryLinks(item.m_name, s))
 *          ShowRow(s.m_domain, s.m_url, s.m_linkCount);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

namespace balloonhistory {

//一条消息里链接的摘要：链接页一行显示的内容
struct HistoryLinkSummary
{
    std::wstring m_url;         //第一个链接，规范化后的完整 URL（www. 开头的已补 https://），可直接交给 ShellExecute
    std::wstring m_domain;      //第一个链接的主机名：小写，去掉开头的 "www."、端口与用户信息；取不出时为空串
    int          m_linkCount;   //这条消息里一共识别出几个链接（不少于 1）

    HistoryLinkSummary()
        : m_linkCount(0)
    {
    }
};

/**
 *  识别一段正文里的全部链接，规范化后按出现先后返回。
 *  @param bodyUtf8 正文（UTF-8）
 *  @return 链接表（UTF-16）；没有链接时为空表
 */
std::vector<std::wstring> LibraryLinksOfText(const std::string& bodyUtf8);

/**
 *  从完整 URL 取出显示用的主机名：去掉协议、用户信息（'@' 之前）、端口与路径，ASCII 字母转小写，
 *  再去掉开头的 "www."（去掉后为空时保留原样）。
 *  @param url 完整 URL（如 "https://User@WWW.Example.com:8080/a?b"）
 *  @return 主机名（如 "example.com"）；没有协议分隔符 "://" 时按整串当主机名处理，空串返回空串
 */
std::wstring LinkDomainOf(const std::wstring& url);

/**
 *  链接页一行的摘要：第一个链接、它的域名与链接总数。
 *  @param bodyUtf8 正文（UTF-8）
 *  @param out      [出参] 摘要；返回 false 时内容不确定
 *  @return 正文里至少有一个链接时返回 true
 */
bool SummarizeLibraryLinks(const std::string& bodyUtf8, HistoryLinkSummary& out);

/**
 *  把链接表拼成「复制链接」放进剪贴板的文字：一个链接时就是它本身，多个链接每行一个（以 CRLF 分隔，末尾不加）。
 *  @param links LibraryLinksOfText 的结果
 *  @return 拼好的文字；空表返回空串
 */
std::wstring JoinLinksForClipboard(const std::vector<std::wstring>& links);

} // namespace balloonhistory
