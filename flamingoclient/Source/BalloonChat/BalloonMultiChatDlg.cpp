/**
 *  BalloonMultiChatDlg.cpp
 *
 *  P0 skeleton -- empty stubs. P1 will load multi_chat.xml and wire
 *  the broadcast send flow.
 *
 *  balloonwj@qq.com   2026.05.20
 */

#include "stdafx.h"
#include "BalloonMultiChatDlg.h"

CBalloonMultiChatDlg::CBalloonMultiChatDlg()
    : m_lpFMGClient(NULL)
    , m_lpFaceList(NULL)
    , m_hMainDlg(NULL)
{
    SetTitle(_T("Broadcast"));
    SetTitleBarHeight(28);
    SetButtons(true, true, true);
    SetMinSize(480, 360);
}

CBalloonMultiChatDlg::~CBalloonMultiChatDlg()
{
}
