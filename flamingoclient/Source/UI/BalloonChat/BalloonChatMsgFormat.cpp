/**
 *  BalloonChatMsgFormat.cpp
 *
 *  聊天消息纯逻辑处理的实现。本 .cpp 故意<u>不</u> include Flamingo 的
 *  stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程
 *  逐字共用，自己引入所需的 ATL（CString）依赖。在 Flamingo.vcxproj 里
 *  本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026.05.21
 */

// FlamingoUnit.vcxproj 命令行也可能定义该宏，加卫哨避免 C4005 重定义。
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <atlbase.h>
#include <atlstr.h>
#include <tchar.h>
#include <ctime>

#include "BalloonChatMsgFormat.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchat {

namespace {

// 时间戳缓冲区字符数。"YYYY-MM-DD HH:MM:SS" 占 19 字符 + 结尾 0，
// 取 32 留足余量。
const int kTimeBufChars = 32;

} // anonymous namespace

SendValidateResult ValidateOutgoing(const CString& text)
{
    // 去首尾空白后判空 —— 纯空白消息视同空消息，不允许发送。
    CString trimmed = text;
    trimmed.Trim();
    if (trimmed.IsEmpty())
    {
        return kSendEmpty;
    }

    // 超长判定用原始文本长度（与旧版 CBuddyChatDlg::OnBtn_Send 口径一致）。
    if (text.GetLength() > kMaxOutgoingChars)
    {
        return kSendTooLong;
    }

    return kSendOk;
}

CString StripControlTags(const CString& raw)
{
    CString out;
    const int n = raw.GetLength();
    int i = 0;
    while (i < n)
    {
        const TCHAR c = raw[i];

        // 协议标签起始模式："/" + 单个字母 + "["。命中则整段跳到匹配的 "]"。
        if (c == _T('/')
            && i + 2 < n
            && _istalpha((_TUCHAR)raw[i + 1])
            && raw[i + 2] == _T('['))
        {
            const int close = raw.Find(_T(']'), i + 3);
            if (close >= 0)
            {
                i = close + 1;   // 跳过整个 "/x[...]"
                continue;
            }
            // 找不到匹配的 "]"：残缺标签，按普通字符处理，避免吞掉正文。
        }

        out += c;
        ++i;
    }
    return out;
}

CString FormatDisplayLine(const CString& sender, time_t when, const CString& text)
{
    TCHAR szTime[kTimeBufChars] = { 0 };

    struct tm tmLocal;
    // localtime_s：time_t → 本地时区分解时间。返回 0 表示成功；失败时
    // szTime 保持空串，格式化结果里时间戳为空但不崩。
    if (localtime_s(&tmLocal, &when) == 0)
    {
        _tcsftime(szTime, kTimeBufChars, _T("%Y-%m-%d %H:%M:%S"), &tmLocal);
    }

    // 形如 "发送者  2026-05-21 10:30:00\r\n正文\r\n"
    CString line;
    line.Format(_T("%s  %s\r\n%s\r\n"),
                sender.GetString(), szTime, text.GetString());
    return line;
}

namespace {

// 尝试从 body[i] 起解析一个 /f["<整数>"] 表情标签。命中则返回 true，并把 faceId
// 写入 outId、把解析后的新位置（标签之后）写入 outNext；未命中返回 false。
bool TryParseFaceTag(const CString& body, int i, int n, int& outId, int& outNext)
{
    // 形如 /f["123"]：至少需要 /f[" 加一个数字加 "]，故 i+5 <= n。
    if (!(i + 3 < n))
    {
        return false;
    }
    if (body[i] != _T('/') || body[i + 1] != _T('f')
        || body[i + 2] != _T('[') || body[i + 3] != _T('"'))
    {
        return false;
    }

    int j = i + 4;
    int id = 0;
    bool hasDigit = false;
    while (j < n && body[j] >= _T('0') && body[j] <= _T('9'))
    {
        id = id * 10 + (int)(body[j] - _T('0'));
        hasDigit = true;
        ++j;
    }
    // 数字后须紧跟 "]
    if (!hasDigit || !(j + 1 < n))
    {
        return false;
    }
    if (body[j] != _T('"') || body[j + 1] != _T(']'))
    {
        return false;
    }

    outId = id;
    outNext = j + 2;   // 跳过整个 /f["..."]
    return true;
}

/**
 *  把一段纯文本编码进发送正文：<u>只</u>给"看上去像表情标记"的那个 '/' 前面多写
 *  一个 '/'，其余斜杠原样保留。
 *
 *  为什么不是把每个 '/' 都翻倍（那是本函数早先的做法）：翻倍会让网线上的正文变成
 *  `http:////x` 这种样子，跨端（安卓、以及任何不认这套 PC 端约定的地方）直接照原样
 *  显示就错了；而"只在真会被误判成表情的地方加一个转义"既保住了网线上的原貌，
 *  也仍然挡住了"用户手打 /f["3"] 被渲染成表情"这一种情形 —— 转义本来就只为这一件事
 *  存在。（用户报的 bug 正是翻倍带来的：输入 http://www.baidu.com 发出去，
 *  对端解析回来少一个斜杠。）
 */
CString EscapeTextForSend(const CString& text)
{
    CString out;
    const int n = text.GetLength();
    int i = 0;
    while (i < n)
    {
        int faceId = 0;
        int next   = 0;
        if (text[i] == _T('/') && TryParseFaceTag(text, i, n, faceId, next))
        {
            //这个位置起是一个能被解析出来的表情标记，补一个 '/' 让接收侧还原成字面 '/'。
            out += _T('/');
        }
        out += text[i];
        ++i;
    }
    return out;
}

} // anonymous namespace

CString BuildSendBody(const std::vector<ChatSegment>& segs)
{
    CString body;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const ChatSegment& seg = segs[i];
        if (seg.m_bFace)
        {
            // 表情段 → /f["<id>"]，与 HandleSysFaceId 的解析格式一致。
            CString tag;
            tag.Format(_T("/f[\"%d\"]"), seg.m_nFaceId);
            body += tag;
        }
        else
        {
            // 文本段 → 仅对"会被误判成表情标记"的斜杠加转义（见 EscapeTextForSend）。
            body += EscapeTextForSend(seg.m_strText);
        }
    }
    return body;
}

std::vector<ChatSegment> ParseSendBody(const CString& body)
{
    std::vector<ChatSegment> segs;
    CString cur;
    const int n = body.GetLength();
    int i = 0;
    while (i < n)
    {
        const TCHAR c = body[i];
        if (c == _T('/'))
        {
            // "//" 只有在<u>紧跟一个表情标记</u>时才是转义，还原成文本里的一个字面 '/'。
            // 例：`//f["3"]` → 字面的 /f["3"]。
            //
            // 为什么不是"凡 // 都还原成一个 /"（那是早先的做法）：那样 `http://x` 会被
            // 吃掉一个斜杠，而正文里的双斜杠绝大多数来自网址，不是转义。转义只为
            // "挡住用户手打的表情标记"存在，故只在那一种情形下认它。
            if (i + 1 < n && body[i + 1] == _T('/'))
            {
                int escId   = 0;
                int escNext = 0;
                if (TryParseFaceTag(body, i + 1, n, escId, escNext))
                {
                    cur += _T('/');
                    i += 2;   // 吃掉两个斜杠中的第一个（转义位）与第二个（字面位）
                    continue;
                }
                // 不是转义：落到下面"孤立斜杠当普通字符"那一支，逐个原样收进正文。
            }
            // /f["<整数>"] → 表情段
            int faceId = 0;
            int next = 0;
            if (TryParseFaceTag(body, i, n, faceId, next))
            {
                if (!cur.IsEmpty())
                {
                    segs.push_back(ChatSegment::Text(cur));
                    cur.Empty();
                }
                segs.push_back(ChatSegment::Face(faceId));
                i = next;
                continue;
            }
            // 其余 '/'（残缺标签 / 孤立斜杠）：当普通字符，不吞正文。
            cur += c;
            ++i;
            continue;
        }
        cur += c;
        ++i;
    }
    if (!cur.IsEmpty())
    {
        segs.push_back(ChatSegment::Text(cur));
    }
    return segs;
}

std::vector<int> BuildFlatIndexMap(const CString& body)
{
    // 走法必须与 ParseSendBody 完全一致 —— 两者只要有一处对转义的判断不同，
    // @ 高亮就会与文字错位。故这里刻意照抄它的分支结构，而不是另写一套扫描。
    const int n = body.GetLength();
    std::vector<int> map;
    map.resize((std::size_t)n + 1, 0);

    int flat = 0;
    int i = 0;
    while (i < n)
    {
        const TCHAR c = body[i];
        if (c == _T('/'))
        {
            // "//" 紧跟表情标记时才是转义 → 扁平文本里的一个字面 '/'：两个原文字符
            // 共用同一个扁平位置。判断口径与 ParseSendBody 逐字一致（见那边的注释）。
            if (i + 1 < n && body[i + 1] == _T('/'))
            {
                int escId   = 0;
                int escNext = 0;
                if (TryParseFaceTag(body, i + 1, n, escId, escNext))
                {
                    map[(std::size_t)i]     = flat;
                    map[(std::size_t)i + 1] = flat;
                    ++flat;
                    i += 2;
                    continue;
                }
                // 不是转义：落到下面"孤立斜杠当普通字符"那一支，各占一个扁平位置。
            }
            // /f["<整数>"] → 一个表情占位字符：整个标记的所有原文字符共用同一位置。
            int faceId = 0;
            int next = 0;
            if (TryParseFaceTag(body, i, n, faceId, next))
            {
                for (int k = i; k < next; ++k)
                {
                    map[(std::size_t)k] = flat;
                }
                ++flat;
                i = next;
                continue;
            }
            // 残缺标签 / 孤立斜杠：当普通字符，一对一。
            map[(std::size_t)i] = flat;
            ++flat;
            ++i;
            continue;
        }
        map[(std::size_t)i] = flat;
        ++flat;
        ++i;
    }
    // 末元素 = 扁平文本总长度，使区间右端点也能直接查表。
    map[(std::size_t)n] = flat;
    return map;
}

CString BodyToDigest(const CString& body)
{
    std::vector<ChatSegment> segs = ParseSendBody(body);
    CString digest;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_bFace)
        {
            digest += TR(L"[表情]");
        }
        else
        {
            digest += segs[i].m_strText;
        }
    }
    return digest;
}

CString BuildRecallTipText(bool bIsSelf, const CString& operatorName)
{
    if (bIsSelf)
    {
        return TR(L"你撤回了一条消息");
    }

    //拿不到撤回者昵称时退化成"对方"：昵称可能因联系人缓存尚未拉到而为空，
    //此时也要给出一句完整的话，不能拼出"撤回了一条消息"这种缺主语的句子。
    if (operatorName.IsEmpty())
    {
        return TR(L"对方撤回了一条消息");
    }

    CString tip;
    tip = operatorName;
    tip += TR(L"撤回了一条消息");
    return tip;
}

CString BuildDeleteMessagesConfirmText(size_t count)
{
    if (count <= 1)
    {
        return TR(L"删除后，本机和你的其他设备都不再显示这条消息，对方不受影响。\n确定要删除吗？");
    }

    CString text;
    text.Format(TR(L"删除后，本机和你的其他设备都不再显示这 %u 条消息，对方不受影响。\n确定要删除吗？"),
                (unsigned int)count);
    return text;
}

} // namespace balloonchat
