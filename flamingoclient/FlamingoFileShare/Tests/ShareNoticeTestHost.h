/**
 *  第 4 期单元测试（后台传输 [sharebg]、弹窗通知 [sharenotice]）共用的桩，ShareNoticeTestHost.h（只有头文件）
 *
 *  - NoticeTestHost：IFileShareHost 的桩。发信令时按顺序分配 seq 并记下命令与包体；QueryNoticePolicy 返回用例设定的值；
 *    PlayNoticeSound 只计数；GetUserDisplayName 对登记过的用户给名字，其余返回失败（弹窗用服务端给的后备名字）。
 *  - FinishRecorder / ActivityRecorder：业务总控两种通知的监听者桩，记下每一次回调。
 *  - TransferRecorder：传输观察者桩，记下进度与结束。
 *  - VisitorRecorder：访问状态机观察者桩，记下状态变化。
 *  - 几段服务端应答与推送的 JSON（2220、2222、2224、2223）。
 *
 *  典型用法：
 *      noticetest::NoticeTestHost host;
 *      service.Attach(&host, NULL, 0);
 *      service.HandleServerFrame(cmd_share_open, host.LastSeq(cmd_share_open), noticetest::OpenOk("acc-1"));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <stdio.h>    //_snprintf_s
#include <string.h>   //strcpy_s
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../FileShareApi.h"
#include "../Engine/ShareService.h"
#include "../Engine/ShareVisitor.h"

namespace noticetest {

//宿主桩
class NoticeTestHost : public fileshare::IFileShareHost
{
public:
    NoticeTestHost()
        : m_nextSeq(500)
        , m_policy(fileshare::fileshare_notice_show)
        , m_sounds(0)
        , m_userId(41001)
    {
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        (void)text;
    }

    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        m_cmds.push_back(cmd);
        m_bodies.push_back(std::string(json != NULL ? json : "", len));
        m_seqs.push_back(++m_nextSeq);
        return m_nextSeq;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    virtual bool GetSessionInfo(fileshare::FileShareSessionInfo* info) override
    {
        if (info == NULL || info->m_cbSize != sizeof(fileshare::FileShareSessionInfo))
            return false;
        info->m_userId = m_userId;
        strcpy_s(info->m_account, fileshare::kFileShareTextBytes, "noticetester");
        strcpy_s(info->m_nickName, fileshare::kFileShareTextBytes, "noticetester");
        strcpy_s(info->m_deviceId, fileshare::kFileShareTextBytes, "PC-NOTICE");
        wcsncpy_s(info->m_personalDir, fileshare::kFileSharePathChars, m_personalDir.c_str(), _TRUNCATE);
        return true;
    }

    virtual bool RequestConversationMembers(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
        return true;
    }

    //登记过的用户给名字；其余返回失败，弹窗退回服务端给的后备名字
    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) override
    {
        std::map<int64_t, std::string>::const_iterator it = m_names.find(userId);
        if (buf == NULL || bufBytes == 0 || it == m_names.end())
            return false;
        _snprintf_s(buf, bufBytes, _TRUNCATE, "%s", it->second.c_str());
        return true;
    }

    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) override
    {
        (void)shareId;
        (void)state;
        (void)reason;
        (void)myPerms;
    }

    virtual bool GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes) override
    {
        (void)ctype;
        (void)cid;
        (void)buf;
        (void)bufBytes;
        return false;
    }

    virtual bool OpenConversation(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
        return true;
    }

    virtual bool OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable) override
    {
        (void)owner;
        (void)imagePath;
        (void)navigable;
        return false;
    }

    virtual void NotifyConversationShareCount(int ctype, int64_t cid, int count) override
    {
        (void)ctype;
        (void)cid;
        (void)count;
    }

    virtual bool PickShareTargets(HWND owner) override
    {
        (void)owner;
        return false;
    }

    virtual bool CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count) override
    {
        (void)groupName;
        (void)memberIds;
        (void)count;
        return false;
    }

    virtual bool GetUserAvatar(int64_t userId, fileshare::FileShareAvatarInfo* out) override
    {
        (void)userId;
        (void)out;
        return false;
    }

    //不翻译，原样返回原文
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }

    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
            return false;
        wcsncpy_s(langBuf, langChars, L"zh-CN", _TRUNCATE);
        wcsncpy_s(fontBuf, fontChars, L"Microsoft YaHei", _TRUNCATE);
        return true;
    }

    //不走 P2P
    virtual p2p::IP2PService* GetP2PService() override
    {
        return NULL;
    }

    //第 12 版：用例设定的判定
    virtual int QueryNoticePolicy() override
    {
        return m_policy;
    }

    //第 12 版：只计数
    virtual void PlayNoticeSound() override
    {
        ++m_sounds;
    }

    //某个命令发送的次数
    int Count(uint16_t cmd) const
    {
        int n = 0;
        for (size_t i = 0; i < m_cmds.size(); ++i)
        {
            if (m_cmds[i] == cmd)
                ++n;
        }
        return n;
    }

    //某个命令最近一次发送时分配的 seq；没发过时为 0
    uint32_t LastSeq(uint16_t cmd) const
    {
        for (size_t i = m_cmds.size(); i > 0; --i)
        {
            if (m_cmds[i - 1] == cmd)
                return m_seqs[i - 1];
        }
        return 0;
    }

    //某个命令最近一次发送的包体；没发过时为空
    std::string LastBody(uint16_t cmd) const
    {
        for (size_t i = m_cmds.size(); i > 0; --i)
        {
            if (m_cmds[i - 1] == cmd)
                return m_bodies[i - 1];
        }
        return std::string();
    }

    uint32_t                       m_nextSeq;      //最近一次分配的 seq
    int                            m_policy;       //QueryNoticePolicy 的返回值（用例可改）
    int                            m_sounds;       //PlayNoticeSound 被调用的次数
    int64_t                        m_userId;       //本人 userid
    std::wstring                   m_personalDir;  //个人目录（带结尾反斜杠）
    std::map<int64_t, std::string> m_names;        //GetUserDisplayName 给出的名字（UTF-8）
    std::vector<uint16_t>          m_cmds;         //发出的命令
    std::vector<std::string>       m_bodies;       //发出的包体，与 m_cmds 一一对应
    std::vector<uint32_t>          m_seqs;         //分配的 seq，与 m_cmds 一一对应
};

//访问者一端传输结束的监听者桩
class FinishRecorder : public fileshare::IShareTransferFinishListener
{
public:
    virtual void OnVisitorTransferFinished(const fileshare::ShareVisitorTransferResult& result) override
    {
        m_results.push_back(result);
    }

    std::vector<fileshare::ShareVisitorTransferResult> m_results;   //收到的结果，按到达顺序
};

//分享者一端操作的监听者桩
class ActivityRecorder : public fileshare::IShareOwnerActivityListener
{
public:
    virtual void OnOwnerActivity(const fileshare::ShareOwnerActivity& activity) override
    {
        m_activities.push_back(activity);
    }

    std::vector<fileshare::ShareOwnerActivity> m_activities;   //收到的操作，按到达顺序
};

//传输观察者桩
class TransferRecorder : public fileshare::IShareTransferObserver
{
public:
    virtual void OnTransferProgress(uint32_t localId, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                    int64_t filesTotal) override
    {
        (void)localId;
        (void)bytesDone;
        (void)bytesTotal;
        (void)filesDone;
        (void)filesTotal;
        ++m_progress;
    }

    virtual void OnTransferFinished(uint32_t localId, int code) override
    {
        m_finished.push_back(std::make_pair(localId, code));
    }

    int                                     m_progress = 0;   //进度回调的次数
    std::vector<std::pair<uint32_t, int> >  m_finished;       //结束回调：(编号, 结果码)
};

//访问状态机观察者桩
class VisitorRecorder : public fileshare::IShareVisitorObserver
{
public:
    virtual void OnVisitorStateChanged(int state, int errCode) override
    {
        m_states.push_back(std::make_pair(state, errCode));
    }

    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<fileshare::ShareEntry>& entries) override
    {
        (void)path;
        (void)code;
        (void)entries;
    }

    virtual void OnVisitorPermsChanged(int perms) override
    {
        (void)perms;
    }

    virtual void OnVisitorShareState(int state, int reason) override
    {
        (void)state;
        (void)reason;
    }

    std::vector<std::pair<int, int> > m_states;   //状态变化：(状态, 原因)
};

//结束回调桩（拖出任务用的那种）
class CompletionRecorder : public fileshare::IShareTransferCompletion
{
public:
    virtual void OnTransferDone(int code) override
    {
        m_codes.push_back(code);
    }

    std::vector<int> m_codes;   //收到的结果码，按到达顺序
};

//2220 的成功应答
inline std::string OpenOk(const std::string& accessId)
{
    return "{\"code\":0,\"msg\":\"ok\",\"accessid\":\"" + accessId + "\",\"name\":\"team docs\"}";
}

//2220 的失败应答
inline std::string OpenFail(int code)
{
    return "{\"code\":" + std::to_string(code) + ",\"msg\":\"x\"}";
}

//推给访问者的 2222（访问会话本身）：中转地址取本机一个不监听的端口，连接立即失败（事件在没有通知窗口时被丢弃）
inline std::string VisitorRelay(const std::string& accessId)
{
    return "{\"accessid\":\"" + accessId + "\",\"relayip\":\"127.0.0.1\",\"relayport\":1,\"token\":\"t\",\"role\":10,"
           "\"opentimeoutms\":15000,\"idletimeoutms\":600000}";
}

//2224 的应答
inline std::string TransferOk(const std::string& transferId)
{
    return "{\"code\":0,\"msg\":\"ok\",\"transferid\":\"" + transferId + "\"}";
}

//2223：访问会话（transferId 为空）或传输子会话已结束
inline std::string AccessEnded(const std::string& accessId, const std::string& transferId, int errCode)
{
    std::string s = "{\"accessid\":\"" + accessId + "\"";
    if (!transferId.empty())
        s += ",\"transferid\":\"" + transferId + "\"";
    return s + ",\"errcode\":" + std::to_string(errCode) + "}";
}

} // namespace noticetest
