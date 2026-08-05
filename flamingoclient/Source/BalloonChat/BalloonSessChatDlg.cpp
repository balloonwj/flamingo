/**
 *  BalloonSessChatDlg.cpp
 *
 *  P0 skeleton -- empty stubs. P1 will reuse the buddy_chat.xml layout
 *  for the session window (functionally a 1-on-1 chat without buddy
 *  list persistence).
 *
 *  balloonwj@qq.com   2026.05.20
 */

#include "stdafx.h"
#include "BalloonSessChatDlg.h"

CBalloonSessChatDlg::CBalloonSessChatDlg()
    : m_lpFMGClient(NULL)
    , m_lpFaceList(NULL)
    , m_lpCascadeWinManager(NULL)
    , m_hMainDlg(NULL)
    , m_nGroupCode(0)
    , m_nUTalkUin(0)
{
    SetTitle(_T("Session"));
    SetTitleBarHeight(28);
    SetButtons(true, true, true);
    SetMinSize(480, 360);
}

CBalloonSessChatDlg::~CBalloonSessChatDlg()
{
}

void CBalloonSessChatDlg::OnRecvMsg(UINT /*nUTalkUin*/, UINT /*nMsgId*/)
{
    // P1: route incoming msg into recv RichEdit.
}

void CBalloonSessChatDlg::OnUpdateGMemberNumber()
{
    // P1: refresh peer number label.
}

void CBalloonSessChatDlg::OnUpdateGMemberHeadPic()
{
    // P1: re-load peer avatar from cache.
}
