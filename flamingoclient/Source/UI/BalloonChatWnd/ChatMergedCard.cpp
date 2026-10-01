/**
 *  ChatMergedCard.cpp —— 合并转发聊天记录卡片内容的解析，见 ChatMergedCard.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatQuoteModel.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "ChatMergedCard.h"
#include "../../net/MessageProtocolClient.h"   // parseMergedContent / digestForMsgType

namespace balloonchatwnd {

namespace {

//卡片标题的缺省值：content 不可用、或记录里没有标题时使用
const wchar_t* const kDefaultMergedTitle = L"聊天记录";
//合并卡片的 text 字段：引用回复、复制等按 text 取摘要的地方拿到的就是它
const wchar_t* const kMergedTypeText = L"[聊天记录]";

//UTF-8 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.GetBuffer(len), len);
    out.ReleaseBuffer(len);
    return out;
}

} // anonymous namespace

bool BuildMergedCardPreview(const std::string& contentJson, MergedCardPreview& out)
{
    out = MergedCardPreview();

    net::MergedRecord record;
    if (!net::MessageProtocolClient::parseMergedContent(contentJson, record))
    {
        return false;
    }

    out.m_title = record.m_title.empty() ? CString(kDefaultMergedTitle) : Utf8ToCStr(record.m_title);
    out.m_count = (int)record.m_messages.size();
    for (size_t i = 0; i < record.m_messages.size() && out.m_lines.size() < kMergedCardPreviewLines; ++i)
    {
        const net::MessageNotify& sub = record.m_messages[i];
        CString name = Utf8ToCStr(sub.m_fromName);
        if (name.IsEmpty())
        {
            name.Format(_T("用户%lld"), (long long)sub.m_from);
        }
        const CString digest = Utf8ToCStr(
            net::MessageProtocolClient::digestForMsgType(sub.m_msgtype, sub.m_text));
        out.m_lines.push_back(name + _T("：") + digest);
    }
    return true;
}

void ApplyMergedCard(ChatMessage& msg, const std::string& contentJson)
{
    MergedCardPreview preview;
    if (!BuildMergedCardPreview(contentJson, preview))
    {
        preview.m_title = kDefaultMergedTitle;
    }

    msg.kind          = kMsgMerged;
    msg.text          = kMergedTypeText;
    msg.mergedTitle   = preview.m_title;
    msg.mergedLines   = preview.m_lines;
    msg.mergedCount   = preview.m_count;
    msg.mergedContent = contentJson;
}

} // namespace balloonchatwnd
