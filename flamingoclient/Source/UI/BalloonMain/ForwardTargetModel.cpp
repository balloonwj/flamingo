/**
 *  转发目标选择窗纯逻辑模型的实现，见 ForwardTargetModel.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "ForwardTargetModel.h"

#include <stdio.h>                         // _snwprintf_s：预览文字里沿用 %s / %u 写法的旧模板
#include "../../net/Protocol.h"            // conversation_single / conversation_group
#include "../BalloonSearch/ContactMatch.h" // contactsearch::Match（与主面板搜索同一套匹配规则）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

//预览文字每一行格式化用的缓冲区长度（UTF-16 字符）。合并转发标题最长 kForwardTitleMaxChars 个字符，加上前后文字远小于它
const size_t kForwardPreviewBufChars = 256;

//去掉首尾空白（空格、制表符、换行）
std::wstring TrimQuery(const std::wstring& s)
{
    const wchar_t* const kSpaces = L" \t\r\n";
    const std::wstring::size_type first = s.find_first_not_of(kSpaces);
    if (first == std::wstring::npos)
    {
        return std::wstring();
    }
    const std::wstring::size_type last = s.find_last_not_of(kSpaces);
    return s.substr(first, last - first + 1);
}

//判断会话类型是否可以作为转发目标
bool IsForwardableConvType(int ctype)
{
    return ctype == conversation_single || ctype == conversation_group;
}

} // anonymous namespace

ForwardTargetSelection::ForwardTargetSelection(int64_t selfUserId)
    : m_selfUserId(selfUserId)
{
}

ForwardToggleResult ForwardTargetSelection::Toggle(const ForwardTarget& target)
{
    const int index = IndexOf(target.m_ctype, target.m_cid);
    if (index >= 0)
    {
        m_selected.erase(m_selected.begin() + index);
        return kForwardToggleRemoved;
    }
    if (!IsSelectable(target))
    {
        return kForwardToggleInvalid;
    }
    if (IsFull())
    {
        return kForwardToggleFull;
    }
    m_selected.push_back(target);
    return kForwardToggleAdded;
}

bool ForwardTargetSelection::Remove(int ctype, int64_t cid)
{
    const int index = IndexOf(ctype, cid);
    if (index < 0)
    {
        return false;
    }
    m_selected.erase(m_selected.begin() + index);
    return true;
}

bool ForwardTargetSelection::IsSelected(int ctype, int64_t cid) const
{
    return IndexOf(ctype, cid) >= 0;
}

bool ForwardTargetSelection::IsFull() const
{
    return m_selected.size() >= kForwardMaxTargets;
}

const std::vector<ForwardTarget>& ForwardTargetSelection::Selected() const
{
    return m_selected;
}

void ForwardTargetSelection::Clear()
{
    m_selected.clear();
}

bool ForwardTargetSelection::IsSelectable(const ForwardTarget& target) const
{
    if (!IsForwardableConvType(target.m_ctype) || target.m_cid <= 0)
    {
        return false;
    }
    //转发给自己服务端会以 error_code_bad_request 拒绝
    return !(target.m_ctype == conversation_single && target.m_cid == m_selfUserId);
}

int ForwardTargetSelection::IndexOf(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_selected.size(); ++i)
    {
        if (m_selected[i].m_ctype == ctype && m_selected[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

std::vector<size_t> FilterForwardTargets(const std::vector<ForwardTarget>& candidates,
                                         const std::wstring& query)
{
    std::vector<size_t> hits;
    const std::wstring q = TrimQuery(query);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        //空查询不过滤；地区一项转发目标没有，传空串
        if (q.empty() || contactsearch::Match(q, candidates[i].m_name,
                                              candidates[i].m_account, std::wstring()).m_matched)
        {
            hits.push_back(i);
        }
    }
    return hits;
}

std::vector<ForwardTarget> BuildRecentForwardTargets(const std::vector<ForwardTarget>& conversations,
                                                     int64_t selfUserId)
{
    //能否作为目标的判断与 ForwardTargetSelection::IsSelectable 口径一致；这里整理的是
    //全部候选，不受选择上限约束，故单独逐项比较。会话条数不过几百，逐项判重的开销可以忽略
    std::vector<ForwardTarget> out;
    for (size_t i = 0; i < conversations.size(); ++i)
    {
        const ForwardTarget& t = conversations[i];
        if (!IsForwardableConvType(t.m_ctype) || t.m_cid <= 0)
        {
            continue;
        }
        if (t.m_ctype == conversation_single && t.m_cid == selfUserId)
        {
            continue;
        }

        bool duplicated = false;
        for (size_t k = 0; k < out.size(); ++k)
        {
            if (out[k].m_ctype == t.m_ctype && out[k].m_cid == t.m_cid)
            {
                duplicated = true;
                break;
            }
        }
        if (!duplicated)
        {
            out.push_back(t);
        }
    }
    return out;
}

std::wstring MakeFileForwardPreview(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (name.empty())
        return std::wstring(TR(L"[文件]"));
    return TRF(L"[文件] {0}", name);
}

std::wstring BuildForwardPreviewText(const ForwardRequest& request)
{
    //主菜单发起通话：说明选什么、选了之后会怎样
    if (request.m_callMedia == kForwardCallVideo)
        return TR(L"选择一个联系人或群发起视频通话。选择群后，再选要呼叫的群成员。");
    if (request.m_callMedia > 0)
        return TR(L"选择一个联系人或群发起语音通话。选择群后，再选要呼叫的群成员。");
    //主菜单发起屏幕共享：选好会话后打开它的聊天窗并弹出共享内容选择窗（屏幕共享第 17 期）
    if (request.m_screenShare)
        return TR(L"选择一个联系人或群，向对方共享你的屏幕。");
    //FlamingoMedia 请用户选会话（云端录制第 27 期）：预览区写它给的标题
    if (request.m_mediaPick)
        return request.m_mediaPickTitle;
    //Markdown 编辑器选发送对象：第一行「[Markdown] 标题」，第二行起为摘要（docs/Markdown消息方案.md 第 11.3 节）
    if (request.m_markdownPick)
    {
        std::wstring text = TRF(L"[Markdown] {0}", request.m_title);
        if (!request.m_markdownDigest.empty())
        {
            text += L"\n";
            text += request.m_markdownDigest;
        }
        return text;
    }
    //发送本地文件（录屏完成后选会话发送）
    if (!request.m_filePath.empty())
        return MakeFileForwardPreview(request.m_filePath);
    //发送一段文字（屏幕共享第 25 期的分享链接邀请）：预览就是要发的文字本身
    if (!request.m_text.empty())
        return request.m_text;

    wchar_t buf[kForwardPreviewBufChars] = { 0 };
    std::wstring text;
    if (request.m_merge)
    {
        _snwprintf_s(buf, kForwardPreviewBufChars, _TRUNCATE, TR(L"[聊天记录] %s"), request.m_title.c_str());
        text = buf;
    }
    else if (request.m_msgids.empty() && request.m_reuploadFiles.size() == 1)
    {
        //只重新发送一个在线直传文件、没有别的消息：与「发送到会话」一样显示文件名
        text = MakeFileForwardPreview(request.m_reuploadFiles[0]);
    }
    else
    {
        //要重新发送的文件在对方那里同样是一条消息，计入条数
        const size_t count = request.m_msgids.size() + request.m_reuploadFiles.size();
        _snwprintf_s(buf, kForwardPreviewBufChars, _TRUNCATE, TR(L"逐条转发 %u 条消息"), (unsigned)count);
        text = buf;
    }

    if (request.m_skipped > 0)
    {
        _snwprintf_s(buf, kForwardPreviewBufChars, _TRUNCATE, TR(L"\n已跳过 %u 条不能转发的消息"), (unsigned)request.m_skipped);
        text += buf;
    }
    //下面几行的换行符不放进译文：译者只管一句话本身
    if (request.m_skippedDirect > 0)
    {
        text += L"\n";
        text += TRF(L"在线直传的文件无法转发：{0} 个", (unsigned)request.m_skippedDirect);
    }
    if (!request.m_merge && !request.m_reuploadFiles.empty())
    {
        text += L"\n";
        text += TRF(L"{0} 个在线直传的文件将以离线文件重新发送", (unsigned)request.m_reuploadFiles.size());
    }
    if (request.m_merge && request.m_directPlaceholders > 0)
    {
        text += L"\n";
        text += TRF(L"{0} 个在线直传的文件不随记录转发", (unsigned)request.m_directPlaceholders);
    }
    return text;
}

std::wstring ForwardPickerTitle(const ForwardRequest& request)
{
    if (request.m_callMedia == kForwardCallVideo)
        return TR(L"发起视频通话");
    if (request.m_callMedia > 0)
        return TR(L"发起语音通话");
    if (request.m_screenShare)
        return TR(L"发起屏幕共享");
    //Markdown 编辑器选发送对象
    if (request.m_markdownPick)
        return TR(L"选择发送对象");
    //FlamingoMedia 请用户选会话（云端录制第 27 期）
    if (request.m_mediaPick)
        return TR(L"选择会话");
    //发送分享链接的邀请（屏幕共享第 25 期）
    if (!request.m_text.empty())
        return TR(L"发送邀请");
    //同一个窗口也用来发送本地文件（录屏完成后的「发送到会话…」），标题按用途区分
    return request.m_filePath.empty() ? TR(L"转发") : TR(L"发送到会话");
}

std::wstring ForwardPickerConfirmText(const ForwardRequest& request)
{
    if (request.m_callMedia > 0)
        return TR(L"呼叫");
    //Markdown 编辑器选发送对象、FlamingoMedia 请用户选会话：只把对象交回去，不在这里发送
    if (request.m_markdownPick || request.m_mediaPick)
        return TR(L"确定");
    return request.m_screenShare ? TR(L"共享") : TR(L"发送");
}

std::wstring ForwardPickerContentTitle(const ForwardRequest& request)
{
    if (request.m_markdownPick)
        return TR(L"文档");
    //FlamingoMedia 请用户选会话（云端录制第 27 期）
    if (request.m_mediaPick)
        return TR(L"内容");
    //发送分享链接的邀请（屏幕共享第 25 期）
    if (!request.m_text.empty())
        return TR(L"邀请内容");
    return (request.m_callMedia > 0 || request.m_screenShare) ? TR(L"说明") : TR(L"转发内容");
}

std::wstring ForwardPickerTargetTitle(const ForwardRequest& request)
{
    return (request.m_callMedia > 0) ? TR(L"通话对象") : TR(L"发送给");
}

bool ForwardPickerSingleSelect(const ForwardRequest& request)
{
    //发起通话与发起屏幕共享都只能选一个会话
    return request.m_callMedia > 0 || request.m_screenShare;
}

} // namespace balloonmain
