/**
 *  主面板里与消息转发相关的实现：接收聊天窗投来的转发请求、打开转发目标选择窗、
 *  把选定的目标交给转发调度器逐个发出 cmd_forward_message。
 *
 *  这里的函数都是 CBalloonMainPanel 的成员，与 BalloonMainPanel.cpp 同属一个类，只是
 *  单独成文：转发是一块相对独立的功能，放在一起便于阅读，也避免主面板的大文件继续膨胀。
 *  整体流程见 docs/聊天消息转发方案.md §3、§5。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <memory>
#include <vector>

#include <objbase.h>                        // CoCreateGuid（生成 clientmsgid）
#include <stdio.h>                          // sprintf_s

#include "../../App/FlamingoClient.h"       // CFlamingoClient::m_UserMgr（取本人昵称生成合并标题）
#include "../../Common/Log/IULog.h"         // LOG_INFO
#include "../../Common/Util/EncodeUtil.h"   // UTF-8 与 Unicode 互转（群名等资料为 UTF-8）
#include "../../net/IUSocket.h"             // CIUSocket::ForwardMessage / RequestSyncMessages
#include "../../net/MessageProtocolClient.h" // net::ForwardMessageResponse
#include "../../net/Protocol.h"             // conversation_single / conversation_group
#include "ForwardDispatcher.h"              // balloonmain::ForwardDispatcher / BuildForwardFailureText
#include "ForwardRequest.h"                 // balloonmain::ForwardRequest
#include "ForwardTargetModel.h"             // balloonmain::ForwardTarget / BuildRecentForwardTargets

namespace {

//转发目标选择窗候选行的头像边长（逻辑像素），与选择窗里的头像控件一致
const int kForwardAvatarSizePx = 32;

//转发应答缺消息体时补拉的条数：拉该会话最新的一页，与聊天窗首屏一页的条数同一量级
const int kForwardResyncLimit = 20;

//生成 clientmsgid 用的缓冲区长度：32 位十六进制 GUID 加结尾零，留有余量
const size_t kClientMsgIdBufLen = 40;

//生成一个转发请求的 clientmsgid（GUID 的十六进制串，纯 ASCII）。写法与聊天窗生成发送
//消息 clientmsgid 的 GenClientMsgId 相同；极少数取不到 GUID 时用开机以来的毫秒数兜底
std::string GenForwardClientMsgId()
{
    GUID guid = { 0 };
    char sz[kClientMsgIdBufLen] = { 0 };
    if (::CoCreateGuid(&guid) == S_OK)
    {
        sprintf_s(sz, sizeof(sz),
                  "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        sprintf_s(sz, sizeof(sz), "FW-%llu", (unsigned long long)::GetTickCount64());
    }
    return std::string(sz);
}

//宽字符串转 UTF-8。空串安全
std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    //UTF-16 一个单元最多对应 UTF-8 的 3 个字节，代理对两个单元对应 4 个字节，按 4 倍留足
    const size_t kMaxUtf8BytesPerUnit = 4;
    std::vector<char> buf(text.size() * kMaxUtf8BytesPerUnit + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.c_str(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

//UTF-8 转宽字符串。空串安全
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return std::wstring(buf.data());
}

} // anonymous namespace

LRESULT CBalloonMainPanel::OnOpenForwardPickerMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为聊天窗堆分配的 balloonmain::ForwardRequest，处理后须 delete
    std::unique_ptr<balloonmain::ForwardRequest> pReq((balloonmain::ForwardRequest*)lParam);
    if (!pReq || pReq->m_msgids.empty())
    {
        return 0;
    }
    const int64_t selfId = (int64_t)SelfUserId();

    //合并转发的标题在这里生成，不用聊天窗的会话显示名：单聊的显示名是「真名（备注）」，
    //备注是本人给对方起的私人称呼，写进标题就随聊天记录发给了别人。这里一律用真名
    //（昵称，没有昵称时用账号）；群聊用群名
    if (pReq->m_merge)
    {
        std::wstring selfName;
        std::wstring otherName;
        if (m_lpFMGClient != NULL)
        {
            selfName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName;
        }
        if (pReq->m_srcCtype == conversation_single)
        {
            const net::ContactItem* pc = FindCachedContact((UINT)pReq->m_srcCid);
            if (pc != NULL)
            {
                otherName = Utf8ToWide(!pc->m_nickname.empty() ? pc->m_nickname : pc->m_username);
            }
        }
        else
        {
            otherName = (LPCWSTR)GroupDisplayName(pReq->m_srcCid);
        }
        pReq->m_title = balloonmain::BuildMergedForwardTitle(pReq->m_srcCtype, selfName, otherName);
    }

    //「最近会话」：会话列表已按置顶在前、时间倒序排好，名字与头像与会话列表同一口径
    std::vector<balloonmain::ForwardTarget> convs;
    convs.reserve(m_conversations.size());
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        convs.push_back(MakeForwardTargetForConversation(m_conversations[i].m_ctype,
                                                         m_conversations[i].m_cid));
    }
    const std::vector<balloonmain::ForwardTarget> recent =
        balloonmain::BuildRecentForwardTargets(convs, selfId);

    //「群组」：本人所在的全部群，名字与头像取群列表里的资料
    std::vector<balloonmain::ForwardTarget> groups;
    groups.reserve(m_myGroups.size());
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        const net::UserGroupItem& g = m_myGroups[i];
        balloonmain::ForwardTarget t;
        t.m_ctype = conversation_group;
        t.m_cid = g.m_groupid;
        t.m_name = Utf8ToWide(g.m_name);
        t.m_avatar = GetEntityAvatarBitmap(g.m_groupid, CString(t.m_name.c_str()),
                                           g.m_facetype, g.m_customface, kForwardAvatarSizePx);
        groups.push_back(t);
    }

    LOG_INFO("Forward picker requested, src ctype=%d, cid=%lld, merge=%d, count=%u, skipped=%u, "
             "recent=%u, groups=%u, contacts=%u.",
             pReq->m_srcCtype, (long long)pReq->m_srcCid, pReq->m_merge ? 1 : 0,
             (unsigned)pReq->m_msgids.size(), (unsigned)pReq->m_skipped,
             (unsigned)recent.size(), (unsigned)groups.size(), (unsigned)m_contacts.size());

    m_forwardDlg.Open(this, selfId, *pReq, recent, groups, m_contacts, m_departments);
    return 0;
}

balloonmain::ForwardTarget CBalloonMainPanel::MakeForwardTargetForConversation(int ctype, int64_t cid)
{
    balloonmain::ForwardTarget t;
    t.m_ctype = ctype;
    t.m_cid = cid;

    CString  title;
    CString  glyph;
    COLORREF color = RGB(0, 0, 0);
    ComputeConvDisplay(ctype, cid, title, glyph, color);
    t.m_name = (LPCWSTR)title;

    //头像：与会话列表 FillConversationRow 同一取法，资料没到时回退首字头像
    if (ctype == conversation_single)
    {
        const net::ContactItem* pc = FindCachedContact((UINT)cid);
        if (pc != NULL)
        {
            t.m_account = Utf8ToWide(pc->m_username);
            t.m_avatar = GetEntityAvatarBitmap(cid, title, pc->m_facetype, pc->m_customface,
                                               kForwardAvatarSizePx);
        }
    }
    else
    {
        std::map<int64_t, GroupBrief>::const_iterator git = m_groupInfo.find(cid);
        if (git != m_groupInfo.end())
        {
            t.m_avatar = GetEntityAvatarBitmap(cid, title, git->second.m_facetype,
                                               git->second.m_customface, kForwardAvatarSizePx);
        }
    }
    if (t.m_avatar == NULL)
    {
        t.m_avatar = m_avatarCache.GetMonogram(glyph, color, kForwardAvatarSizePx);
    }
    return t;
}

void CBalloonMainPanel::OnForwardTargetsConfirmed(const balloonmain::ForwardRequest& request,
                                                  const std::vector<balloonmain::ForwardTarget>& targets)
{
    LOG_INFO("Forward targets confirmed, src ctype=%d, cid=%lld, merge=%d, messages=%u, targets=%u.",
             request.m_srcCtype, (long long)request.m_srcCid, request.m_merge ? 1 : 0,
             (unsigned)request.m_msgids.size(), (unsigned)targets.size());
    if (targets.empty() || request.m_msgids.empty())
    {
        return;
    }

    //协议里一次请求只有一个目标：逐个目标发出，各用一个新的 clientmsgid
    const std::string title = request.m_merge ? WideToUtf8(request.m_title) : std::string();
    std::vector<uint32_t> seqs;
    seqs.reserve(targets.size());
    for (size_t i = 0; i < targets.size(); ++i)
    {
        seqs.push_back(CIUSocket::GetInstance().ForwardMessage(
            GenForwardClientMsgId(), targets[i].m_ctype, targets[i].m_cid,
            request.m_merge, title, request.m_msgids));
    }

    balloonmain::ForwardBatchOutcome outcome;
    bool done = false;
    const int batchId = m_forwardDispatcher.StartBatch(request.m_srcWnd, request.m_merge, targets,
                                                       seqs, (int64_t)::GetTickCount64(),
                                                       &outcome, &done);
    LOG_INFO("Forward batch %d started, targets=%u, done=%d.", batchId,
             (unsigned)targets.size(), done ? 1 : 0);
    if (done)
    {
        //一个请求都没能发出（离线或未连接）：当场提示
        ShowForwardOutcome(outcome);
    }
    UpdateForwardTimeoutTimer();
}

void CBalloonMainPanel::OnForwardResponse(uint32_t seq, const net::ForwardMessageResponse& resp)
{
    balloonmain::ForwardTarget target;
    balloonmain::ForwardBatchOutcome outcome;
    const balloonmain::ForwardResponseKind kind =
        m_forwardDispatcher.OnResponse(seq, resp.m_code, &target, &outcome);
    if (kind == balloonmain::kForwardResponseUnknown)
    {
        //已超时、重复的应答，或不是本面板发出的请求
        LOG_INFO("Forward response ignored, seq=%u, code=%d.", (unsigned)seq, resp.m_code);
        return;
    }

    LOG_INFO("Forward response, seq=%u, target ctype=%d, cid=%lld, code=%d, created=%u, bodies=%u.",
             (unsigned)seq, target.m_ctype, (long long)target.m_cid, resp.m_code,
             (unsigned)resp.m_msgids.size(), (unsigned)resp.m_messages.size());
    if (resp.m_code == 0)
    {
        ApplyForwardedMessages(target, resp);
    }
    if (kind == balloonmain::kForwardResponseBatchDone)
    {
        ShowForwardOutcome(outcome);
    }
    UpdateForwardTimeoutTimer();
}

void CBalloonMainPanel::ApplyForwardedMessages(const balloonmain::ForwardTarget& target,
                                               const net::ForwardMessageResponse& resp)
{
    //群：服务端向全体群成员（含本人）推送 cmd_message_notify，走普通的收消息流程即可
    if (target.m_ctype != conversation_single)
    {
        return;
    }

    //单聊：服务端不把新消息推回给本人，只能用应答里的消息体。逐条交给收消息的流程：
    //落本地库、显示到已打开的聊天窗（按发送方是本人画成本人的气泡）、刷新会话列表
    for (size_t i = 0; i < resp.m_messages.size(); ++i)
    {
        OnRecvChatMessage(resp.m_messages[i]);
    }

    //消息体不全：服务端的体积预算用尽，或旧服务端不带消息体。拉一次该会话的最新一页补齐，
    //应答到达后照常落本地库、刷新已打开的聊天窗
    if (resp.m_messages.size() < resp.m_msgids.size())
    {
        LOG_INFO("Forward response lacks bodies, resync conversation, cid=%lld, created=%u, bodies=%u.",
                 (long long)target.m_cid, (unsigned)resp.m_msgids.size(),
                 (unsigned)resp.m_messages.size());
        CIUSocket::GetInstance().RequestSyncMessages(conversation_single, target.m_cid, 0,
                                                     kForwardResyncLimit);
    }
}

void CBalloonMainPanel::ShowForwardOutcome(const balloonmain::ForwardBatchOutcome& outcome)
{
    LOG_INFO("Forward batch %d finished, targets=%u, succeeded=%u, failed=%u.",
             outcome.m_batchId, (unsigned)outcome.m_targetCount,
             (unsigned)outcome.m_succeeded.size(), (unsigned)outcome.m_failed.size());

    const bool srcAlive = (::IsWindow(outcome.m_srcWnd) != FALSE);
    if (outcome.m_failed.empty())
    {
        //全部成功：在发起转发的聊天窗或消息记录窗顶部轻提示；窗口已被用户关掉时不提示
        if (!srcAlive)
        {
            return;
        }
        for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
             it != m_chatWindows.end(); ++it)
        {
            if (it->second != NULL && it->second->m_hWnd == outcome.m_srcWnd)
            {
                it->second->ShowToast(_T("已转发"));
                return;
            }
        }
        for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
             it != m_groupChatWindows.end(); ++it)
        {
            if (it->second != NULL && it->second->m_hWnd == outcome.m_srcWnd)
            {
                it->second->ShowToast(_T("已转发"));
                return;
            }
        }
        //从消息记录窗的行右键菜单「转发」发起
        if (m_historyWindow.IsWindow() && m_historyWindow.m_hWnd == outcome.m_srcWnd)
        {
            m_historyWindow.ShowToast(_T("已转发"));
        }
        return;
    }

    //有失败：弹框列出失败的目标与原因。发起转发的窗口还开着就以它为父窗口，否则以主面板为父窗口
    const std::wstring text = balloonmain::BuildForwardFailureText(outcome);
    ::MessageBox(srcAlive ? outcome.m_srcWnd : m_hWnd, text.c_str(), _T("转发"),
                 MB_OK | MB_ICONWARNING);
}

void CBalloonMainPanel::UpdateForwardTimeoutTimer()
{
    const bool want = m_forwardDispatcher.HasPending();
    if (want == m_forwardTimerOn || !IsWindow())
    {
        return;
    }
    if (want)
    {
        m_forwardTimerOn = (SetTimer(balloonmain::kForwardTimeoutTimerId,
                                     balloonmain::kForwardTimeoutCheckMs) != 0);
    }
    else
    {
        KillTimer(balloonmain::kForwardTimeoutTimerId);
        m_forwardTimerOn = false;
    }
}

void CBalloonMainPanel::OnForwardTimeoutTimer()
{
    const std::vector<balloonmain::ForwardBatchOutcome> finished =
        m_forwardDispatcher.CheckTimeouts((int64_t)::GetTickCount64(),
                                          balloonmain::kForwardResponseTimeoutMs);
    for (size_t i = 0; i < finished.size(); ++i)
    {
        ShowForwardOutcome(finished[i]);
    }
    UpdateForwardTimeoutTimer();
}

// =======================================================================
// 聊天记录查看窗（点合并卡片、或在查看窗里双击嵌套记录时打开）
// =======================================================================

LRESULT CBalloonMainPanel::OnOpenMergedViewerMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 为堆上的 content 原文，无论成败都在这里释放
    std::unique_ptr<std::string> pContent((std::string*)lParam);
    if (!pContent || pContent->empty())
    {
        return 0;
    }

    //子消息没带 fromname 的（老服务端转发的记录），按 userid 解析好名字交给查看窗。
    //只解析本层：嵌套的一层打开时会再经过这里
    net::MergedRecord record;
    if (!net::MessageProtocolClient::parseMergedContent(*pContent, record))
    {
        LOG_ERROR("open merged viewer: content not parsable, length=%u.",
                  (unsigned)pContent->size());
        return 0;
    }
    std::map<int64_t, CString> fallbackNames;
    for (size_t i = 0; i < record.m_messages.size(); ++i)
    {
        const net::MessageNotify& sub = record.m_messages[i];
        if (sub.m_fromName.empty() && fallbackNames.find(sub.m_from) == fallbackNames.end())
        {
            fallbackNames[sub.m_from] = ResolveSenderName(sub.m_from);
        }
    }

    //先清理已关闭的查看窗，再新开一个。每份记录各开一个窗口，不复用：用户可能想把外层
    //与嵌套的一层并排对照
    NotifyMergedViewersDownload(0, false, std::wstring(), false, false);
    std::unique_ptr<CMergedRecordWindow> viewer(new CMergedRecordWindow());
    if (!viewer->Open(*pContent, fallbackNames, m_hWnd))
    {
        return 0;
    }
    m_mergedViewers.push_back(std::move(viewer));
    LOG_INFO("merged viewer opened by main panel, items=%u, open viewers=%u.",
             (unsigned)record.m_messages.size(), (unsigned)m_mergedViewers.size());
    return 0;
}

void CBalloonMainPanel::NotifyMergedViewersDownload(int64_t msgid, bool ok,
                                                    const std::wstring& localPath,
                                                    bool isFile, bool cancelled)
{
    //已关闭的查看窗对象留着没有用处，趁这次遍历清掉
    std::vector<std::unique_ptr<CMergedRecordWindow> >::iterator it = m_mergedViewers.begin();
    while (it != m_mergedViewers.end())
    {
        if (!(*it) || !(*it)->IsWindow())
        {
            it = m_mergedViewers.erase(it);
            continue;
        }
        //msgid 为 0 表示只清理、没有下载结果要转
        if (msgid > 0)
        {
            if (isFile)
            {
                (*it)->OnFileDownloadFinished(msgid, ok, localPath, cancelled);
            }
            else
            {
                (*it)->OnImageDownloadFinished(msgid, ok, localPath);
            }
        }
        ++it;
    }
}
