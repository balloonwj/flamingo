/**
 *  SensitiveWordTips.cpp
 *
 *  主面板一侧保存 / 提交被拒的提示文案，说明见 SensitiveWordTips.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "SensitiveWordTips.h"
#include "../../net/Protocol.h"   // error_code_profile_sensitive_word / error_code_group_sensitive_word
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

CString ProfileSaveFailureText(int code)
{
    //昵称、签名、地址、邮箱、岗位中有命中发送者所属企业敏感词库的字段
    if (code == error_code_profile_sensitive_word)
    {
        return CString(TR(L"资料中包含敏感词，未能保存。"));
    }
    return CString(TR(L"保存资料失败，请稍后重试。"));
}

CString SignatureSaveFailureText(int code)
{
    if (code == error_code_profile_sensitive_word)
    {
        return CString(TR(L"个性签名包含敏感词，未能保存。"));
    }
    return CString(TR(L"修改个性签名失败，请稍后重试。"));
}

CString CreateGroupFailureText(int code)
{
    if (code == error_code_group_sensitive_word)
    {
        return CString(TR(L"群名称包含敏感词，请修改后重试。"));
    }
    //建帖子群时本企业关闭了帖子群（建群窗口打开之后管理员关掉了开关）
    if (code == error_code_thread_disabled)
    {
        return CString(TR(L"本企业已关闭帖子群，请改建普通群。"));
    }
    CString tip;
    tip.Format(TR(L"创建群聊失败（错误码 %d）。"), code);
    return tip;
}

CString GroupCardSaveFailureText(int code, const CString& serverMsg)
{
    if (code == error_code_group_sensitive_word)
    {
        return CString(TR(L"群昵称包含敏感词，未能保存。"));
    }
    //没有专门说法时把服务端原文附上，免得线索被吞掉
    if (serverMsg.IsEmpty())
    {
        return CString(TR(L"修改群昵称失败，请稍后重试。"));
    }
    return CString(TRF(L"修改群昵称失败：{0}", serverMsg).c_str());
}

} // namespace balloonmain
