/**
 *  ContactRemarkModel.cpp
 *
 *  联系人显示名解析与备注提交判定的实现。只做字符串运算（trim / 比较 / 长度），
 *  不碰窗口、不碰网络，与 FlamingoUnit 测试工程共用、不 include stdafx.h，
 *  PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "ContactRemarkModel.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

//取 trim 后非空的那一个；s 去掉首尾空白后为空则返回 false。
//服务端下发的昵称有可能是几个空格，直接判 IsEmpty 会让这种值蒙混过关、
//界面上显示成一片空白，故一律按 trim 后的结果判定。
bool PickNonBlank(const CString& s, CString& out)
{
    CString t = s;
    t.Trim();
    if (t.IsEmpty())
    {
        return false;
    }
    out = t;
    return true;
}

} // anonymous namespace

CString ResolveContactRealName(const CString& nickname,
                               const CString& username,
                               UINT userid)
{
    CString name;

    if (PickNonBlank(nickname, name))
    {
        return name;
    }
    if (PickNonBlank(username, name))
    {
        return name;
    }

    //两者都没有：拼一个能让人对上号的兜底名，总好过显示空白。
    name.Format(TR(L"用户%u"), userid);
    return name;
}

CString ResolveContactDisplayName(const CString& remark,
                                  const CString& nickname,
                                  const CString& username,
                                  UINT userid)
{
    const CString realName = ResolveContactRealName(nickname, username, userid);

    //没有备注（或备注只有空白字符）：就显示真名，末尾不要留一对空括号。
    CString trimmedRemark;
    if (!PickNonBlank(remark, trimmedRemark))
    {
        return realName;
    }

    //备注与真名一模一样时不再重复一遍，否则会显示成"张三（张三）"。
    if (trimmedRemark == realName)
    {
        return realName;
    }

    //括号用全角：显示名两侧多是中文，半角括号夹在中文之间会显得两边贴得太紧。
    CString display = realName;
    display += _T("（");
    display += trimmedRemark;
    display += _T("）");
    return display;
}

CString ResolveContactSpeakerName(const CString& remark,
                                  const CString& nickname,
                                  const CString& username,
                                  UINT userid)
{
    //有备注就只用备注（已去掉首尾空白）；没有备注时与真名的取法相同
    CString trimmedRemark;
    if (PickNonBlank(remark, trimmedRemark))
    {
        return trimmedRemark;
    }
    return ResolveContactRealName(nickname, username, userid);
}

CString AppendSelfMark(const CString& name, bool isSelf)
{
    if (!isSelf)
    {
        return name;
    }
    CString text = name;
    text += TR(L"（我）");
    return text;
}

RemarkCommitDecision DecideRemarkCommit(const CString& edited,
                                        const CString& current,
                                        bool online,
                                        int maxLen)
{
    RemarkCommitDecision d;
    d.m_trimmed = edited;
    d.m_trimmed.Trim();   // 去首尾空白后再比较 / 判长

    //先看有没有变化：用户点进编辑框又点出来、一个字没改时，即便当前离线也不该
    //弹"网络不可用"来烦他。current 也 trim 一遍，免得历史脏数据带的首尾空白
    //让"没改"被误判成"改了"。
    CString cur = current;
    cur.Trim();
    if (d.m_trimmed == cur)
    {
        d.m_action = kRemarkCommitNoChange;
        return d;
    }

    //确有变化但发不出去：拦截，由调用方提示并把界面还原成原值——不还原的话
    //用户会以为改成了，下次刷新又变回去。
    if (!online)
    {
        d.m_action = kRemarkCommitOffline;
        return d;
    }

    //超长：拦截。库里是 VARCHAR(32)，放过去会被悄悄截断。
    if (d.m_trimmed.GetLength() > maxLen)
    {
        d.m_action = kRemarkCommitTooLong;
        return d;
    }

    //确有变化、在线、未超长：发送。trim 后可能为空串，即"清除备注"，是合法提交。
    d.m_action = kRemarkCommitSend;
    return d;
}

} // namespace balloonmain
