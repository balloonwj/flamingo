/**
 *  聊天内容剪贴板模型的实现，ChatClipboardContent.cpp
 *
 *  本 .cpp 被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，不 include stdafx.h
 *  （在 Flamingo.vcxproj 里 PrecompiledHeader=NotUsing），自行引入 ATL 的 CString。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "ChatClipboardContent.h"
#include "../../Common/Util/EncodeUtil.h"                    // UTF-8 ↔ 宽字符
#include "../../third-party/jsoncpp-1.9.0/json.h"

#include <memory>
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonchatwnd {

const wchar_t* const kChatClipFormatName = L"Flamingo.ChatContent.v1";
const wchar_t* const kChatClipFacePlaceholder  = TR_NOOP(L"[表情]");
const wchar_t* const kChatClipImagePlaceholder = TR_NOOP(L"[图片]");

namespace {

// ---- 序列化数据里的字段名与取值 ----

// 顶层：格式版本号。
const char* const kKeyVersion = "v";
// 顶层：片段列表。
const char* const kKeySegments = "segs";
// 片段：类型。
const char* const kKeyType = "t";
// 文字片段：文字原文。
const char* const kKeyText = "s";
// 表情片段：表情编号。
const char* const kKeyFaceId = "id";
// 图片片段：本地文件路径。
const char* const kKeyImagePath = "path";

// 片段类型的三种取值。
const char* const kTypeText  = "text";
const char* const kTypeFace  = "face";
const char* const kTypeImage = "image";

// 往片段序列末尾追加一段文字：末尾本来就是文字时并进去，不另起一段。空文字不追加。
void AppendText(std::vector<ChatClipSegment>& segs, const std::wstring& text)
{
    if (text.empty())
    {
        return;
    }
    if (!segs.empty() && segs.back().m_kind == kChatClipText)
    {
        segs.back().m_text += text;
        return;
    }
    ChatClipSegment seg;
    seg.m_kind = kChatClipText;
    seg.m_text = text;
    segs.push_back(seg);
}

void AppendFace(std::vector<ChatClipSegment>& segs, int faceId)
{
    ChatClipSegment seg;
    seg.m_kind   = kChatClipFace;
    seg.m_faceId = faceId;
    segs.push_back(seg);
}

// 把 JSON 里的一个片段对象还原成片段。合法时写入 out 并返回 true；非法（类型不认识、
// 缺字段、表情编号越界、图片路径为空、文字为空）时返回 false。
bool ParseOneSegment(const Json::Value& item, ChatClipSegment& out)
{
    if (!item.isObject() || !item[kKeyType].isString())
    {
        return false;
    }
    const std::string type = item[kKeyType].asString();
    if (type == kTypeText)
    {
        if (!item[kKeyText].isString())
        {
            return false;
        }
        out.m_kind = kChatClipText;
        out.m_text = EncodeUtil::Utf8ToUnicode(item[kKeyText].asString());
        return !out.m_text.empty();
    }
    if (type == kTypeFace)
    {
        if (!item[kKeyFaceId].isInt())
        {
            return false;
        }
        const int faceId = item[kKeyFaceId].asInt();
        if (faceId < 0 || faceId >= kChatClipFaceIdLimit)
        {
            return false;
        }
        out.m_kind   = kChatClipFace;
        out.m_faceId = faceId;
        return true;
    }
    if (type == kTypeImage)
    {
        if (!item[kKeyImagePath].isString())
        {
            return false;
        }
        out.m_kind      = kChatClipImage;
        out.m_imagePath = EncodeUtil::Utf8ToUnicode(item[kKeyImagePath].asString());
        return !out.m_imagePath.empty();
    }
    return false;   //不认识的类型：可能来自更新的版本，跳过它而不是整体失败
}

} // anonymous namespace

std::vector<ChatClipSegment> BuildClipSegmentsFromBody(const CString& body)
{
    std::vector<ChatClipSegment> segs;
    const std::vector<balloonchat::ChatSegment> parsed = balloonchat::ParseSendBody(body);
    for (size_t i = 0; i < parsed.size(); ++i)
    {
        if (parsed[i].m_bFace)
        {
            AppendFace(segs, parsed[i].m_nFaceId);
        }
        else
        {
            AppendText(segs, std::wstring((LPCWSTR)parsed[i].m_strText));
        }
    }
    return segs;
}

std::vector<ChatClipSegment> BuildClipSegmentsFromSelection(
    const std::vector<balloonchat::ChatSegment>& segs, int selStart, int selEnd)
{
    std::vector<ChatClipSegment> out;
    if (selStart < 0)
    {
        selStart = 0;
    }
    if (selStart >= selEnd)
    {
        return out;
    }

    //沿扁平串逐段推进：pos 是当前这一段在扁平串里的起点。文字段占其原文长度，表情段
    //占一个占位字符（与 BuildFlatText 的构成一致）。
    int pos = 0;
    for (size_t i = 0; i < segs.size() && pos < selEnd; ++i)
    {
        if (segs[i].m_bFace)
        {
            if (pos >= selStart)
            {
                AppendFace(out, segs[i].m_nFaceId);
            }
            ++pos;
            continue;
        }

        const std::wstring text((LPCWSTR)segs[i].m_strText);
        const int len = (int)text.size();
        //这一段与选区的交集 [from, to)，换算成段内下标。
        const int from = (selStart > pos) ? selStart : pos;
        const int to   = (selEnd < pos + len) ? selEnd : pos + len;
        if (from < to)
        {
            AppendText(out, text.substr((size_t)(from - pos), (size_t)(to - from)));
        }
        pos += len;
    }
    return out;
}

std::vector<ChatClipSegment> BuildClipSegmentsForImage(const CString& imagePath)
{
    std::vector<ChatClipSegment> segs;
    if (imagePath.IsEmpty())
    {
        return segs;   //图片还没下载到本地：没有文件可粘贴，只写纯文字
    }
    ChatClipSegment seg;
    seg.m_kind      = kChatClipImage;
    seg.m_imagePath = (LPCWSTR)imagePath;
    segs.push_back(seg);
    return segs;
}

std::wstring ClipSegmentsToPlainText(const std::vector<ChatClipSegment>& segs)
{
    std::wstring text;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        switch (segs[i].m_kind)
        {
        //文字：原样
        case kChatClipText:
            text += segs[i].m_text;
            break;

        //表情：写成 [表情]
        case kChatClipFace:
            text += TR(kChatClipFacePlaceholder);
            break;

        //图片：写成 [图片]
        case kChatClipImage:
            text += TR(kChatClipImagePlaceholder);
            break;

        //不会出现的类型：不输出任何文字
        default:
            break;
        }
    }
    return text;
}

std::string SerializeClipSegments(const std::vector<ChatClipSegment>& segs)
{
    Json::Value list(Json::arrayValue);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        Json::Value item(Json::objectValue);
        switch (segs[i].m_kind)
        {
        //文字：类型 + UTF-8 原文
        case kChatClipText:
            item[kKeyType] = kTypeText;
            item[kKeyText] = EncodeUtil::UnicodeToUtf8(segs[i].m_text);
            break;

        //表情：类型 + 编号
        case kChatClipFace:
            item[kKeyType]   = kTypeFace;
            item[kKeyFaceId] = segs[i].m_faceId;
            break;

        //图片：类型 + UTF-8 本地路径
        case kChatClipImage:
            item[kKeyType]      = kTypeImage;
            item[kKeyImagePath] = EncodeUtil::UnicodeToUtf8(segs[i].m_imagePath);
            break;

        //不会出现的类型：不写这个片段
        default:
            continue;
        }
        list.append(item);
    }

    Json::Value root(Json::objectValue);
    root[kKeyVersion]  = kChatClipVersion;
    root[kKeySegments] = list;

    //不要缩进与换行：剪贴板数据给程序读，不给人看。
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

bool ParseClipSegments(const std::string& data, std::vector<ChatClipSegment>& out)
{
    out.clear();
    if (data.empty())
    {
        return false;
    }

    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errors;
    if (!reader->parse(data.data(), data.data() + data.size(), &root, &errors))
    {
        return false;
    }
    if (!root.isObject() || !root[kKeyVersion].isInt()
        || root[kKeyVersion].asInt() != kChatClipVersion || !root[kKeySegments].isArray())
    {
        return false;
    }

    const Json::Value& list = root[kKeySegments];
    for (Json::ArrayIndex i = 0; i < list.size() && out.size() < kChatClipMaxSegments; ++i)
    {
        ChatClipSegment seg;
        if (ParseOneSegment(list[i], seg))
        {
            out.push_back(seg);
        }
    }
    return true;
}

} // namespace balloonchatwnd
