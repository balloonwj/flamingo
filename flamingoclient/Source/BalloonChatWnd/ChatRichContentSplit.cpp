/**
 *  ChatRichContentSplit.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ChatRichContentSplit.h"

#include "ChatFaceTag.h"                          // IsFaceTag / FaceIdFromTag（表情标记编码）
#include "../BalloonChat/BalloonChatMsgFormat.h"  // BuildSendBody / ParseSendBody / ChatSegment

namespace balloonchatwnd {

namespace {

// RichEdit 纯文本里代表一个内嵌对象（OLE 图片等）的占位符。
const wchar_t kEmbedding = 0xFFFC;

// 判断一段文本是否"只含空白"（空格 / 制表 / 回车 / 换行）。空串视为全空白。
bool IsWhitespaceOnly(const CString& s)
{
    for (int i = 0; i < s.GetLength(); ++i)
    {
        wchar_t c = s[i];
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n')
        {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

std::vector<RichOutgoingPart> SplitRichInputToParts(const wchar_t* textWithEmbeds)
{
    std::vector<RichOutgoingPart> parts;
    if (textWithEmbeds == NULL)
    {
        return parts;
    }

    CString run;          // 当前累积的文字段
    int     imageIndex = 0;

    for (const wchar_t* p = textWithEmbeds; *p != L'\0'; ++p)
    {
        if (*p == kEmbedding)
        {
            // 先收尾前面的文字段（非全空白才发），再追加一个图片部分。
            if (!IsWhitespaceOnly(run))
            {
                RichOutgoingPart t;
                t.kind = kRichPartText;
                t.text = run;
                t.imageIndex = -1;
                parts.push_back(t);
            }
            run.Empty();

            RichOutgoingPart img;
            img.kind = kRichPartImage;
            img.imageIndex = imageIndex;
            parts.push_back(img);
            ++imageIndex;
        }
        else
        {
            run += *p;
        }
    }

    // 收尾最后一段文字。
    if (!IsWhitespaceOnly(run))
    {
        RichOutgoingPart t;
        t.kind = kRichPartText;
        t.text = run;
        t.imageIndex = -1;
        parts.push_back(t);
    }

    return parts;
}

namespace {

// 把当前累积的"文本 + 表情"组收尾成一个 text 部分（追加进 parts），并清空组。
// 编码规则见头文件 BuildOutgoingParts 注释。
void FlushRichGroup(std::vector<balloonchat::ChatSegment>& group,
                    std::vector<RichOutgoingPart>& parts)
{
    if (group.empty())
    {
        return;
    }

    bool hasFace = false;
    CString joined;   // 组内文本原样拼接（仅无表情时用作正文）
    for (size_t i = 0; i < group.size(); ++i)
    {
        if (group[i].m_bFace)
        {
            hasFace = true;
        }
        else
        {
            joined += group[i].m_strText;
        }
    }

    CString body;
    if (hasFace)
    {
        // 含表情：编码成 "文字（已转义）+ /f["id"]" 的正文。
        body = balloonchat::BuildSendBody(group);
    }
    else
    {
        // 无表情的纯文本组：全空白丢弃；原文恰好含可解析的 /f["N"] 标签（用户手打）
        // 时仍转义，避免对端把字面文本误判成表情。
        if (IsWhitespaceOnly(joined))
        {
            group.clear();
            return;
        }
        bool looksLikeFaceTag = false;
        std::vector<balloonchat::ChatSegment> probe = balloonchat::ParseSendBody(joined);
        for (size_t i = 0; i < probe.size(); ++i)
        {
            if (probe[i].m_bFace)
            {
                looksLikeFaceTag = true;
                break;
            }
        }
        body = looksLikeFaceTag ? balloonchat::BuildSendBody(group) : joined;
    }

    RichOutgoingPart t;
    t.kind = kRichPartText;
    t.text = body;
    t.imageIndex = -1;
    parts.push_back(t);
    group.clear();
}

} // anonymous namespace

std::vector<RichOutgoingPart> BuildOutgoingParts(const std::vector<RichInputSegment>& segs)
{
    std::vector<RichOutgoingPart> parts;
    std::vector<balloonchat::ChatSegment> group;   // 当前累积的"文本 + 表情"组
    int imageIndex = 0;                            // 非表情内嵌图序号（对应 m_pendingShotPaths）

    for (size_t i = 0; i < segs.size(); ++i)
    {
        const RichInputSegment& seg = segs[i];
        if (!seg.isImage)
        {
            group.push_back(balloonchat::ChatSegment::Text(seg.text));
        }
        else if (IsFaceTag(seg.tag))
        {
            // 表情：归入当前富文本组，随文字一起作为文本消息发出。
            group.push_back(balloonchat::ChatSegment::Face(FaceIdFromTag(seg.tag)));
        }
        else
        {
            // 截图等非表情内嵌图：先收尾当前组，再单独成一个 image 部分。
            FlushRichGroup(group, parts);

            RichOutgoingPart img;
            img.kind = kRichPartImage;
            img.text = _T("");
            img.imageIndex = imageIndex;
            parts.push_back(img);
            ++imageIndex;
        }
    }
    FlushRichGroup(group, parts);

    return parts;
}

} // namespace balloonchatwnd
