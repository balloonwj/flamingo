/**
 *  BalloonSessChatDlg.h
 *
 *  Ad-hoc session chat dialog (open a chat with a group member without
 *  joining the group's main thread), balloonui-based replacement for
 *  the legacy CSessChatDlg in SessChatDlg.h.
 *
 *  P0 scope: skeleton -- public surface matches legacy class; business
 *  methods are empty stubs.
 *
 *  balloonwj@qq.com   2026.05.20
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

class CFlamingoClient;
class CFaceList;
class CCascadeWinManager;

/**
 *  CBalloonSessChatDlg
 *
 *  Drop-in replacement for the legacy CSessChatDlg. Public surface
 *  mirrors the legacy class so MainDlg's m_mapSessChatDlg code stays
 *  unchanged beyond the #include line.
 */
class CBalloonSessChatDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonSessChatDlg();
    ~CBalloonSessChatDlg();

    // ---- Business notification entry points (called by MainDlg) ----

    /// Called when a chat message addressed to this session arrives.
    ///   nUTalkUin: peer user id (must match m_nUTalkUin)
    ///   nMsgId:    message id assigned by the receive pipeline
    void OnRecvMsg(UINT nUTalkUin, UINT nMsgId);

    /// The peer group member's contact number changed.
    void OnUpdateGMemberNumber();

    /// The peer group member's avatar changed.
    void OnUpdateGMemberHeadPic();

public:
    // ---- Public fields the caller sets right after `new` ----

    CFlamingoClient*    m_lpFMGClient;          // owner client (not owned)
    CFaceList*          m_lpFaceList;           // shared face library (not owned)
    CCascadeWinManager* m_lpCascadeWinManager;  // window cascade manager (not owned)
    HWND                m_hMainDlg;             // main dialog HWND (for posting back)
    UINT                m_nGroupCode;           // originating group id
    UINT                m_nUTalkUin;            // peer user id
};

// Backwards-compat alias.
typedef CBalloonSessChatDlg CSessChatDlg;
