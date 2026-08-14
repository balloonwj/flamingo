/**
 *  BalloonMultiChatDlg.h
 *
 *  Multi-recipient (broadcast) chat dialog, balloonui-based replacement
 *  for the legacy CMultiChatDlg in MultiChatDlg.h.
 *
 *  P0 scope: skeleton -- public surface matches what callers (CMainDlg)
 *  need (only fields + WTL window APIs; no business method calls go
 *  through this dialog from outside). The dialog opens as an empty
 *  DuiFrameWindow.
 *
 *  balloonwj@qq.com   2026.05.20
 */

#pragma once

#include <set>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

class CFlamingoClient;
class CFaceList;

/**
 *  CBalloonMultiChatDlg
 *
 *  Drop-in replacement for the legacy CMultiChatDlg. The caller picks
 *  a set of recipients (m_setTargetIDs), then Create()s and Show()s
 *  this dialog; typing in the dialog and pressing Send broadcasts the
 *  text to every id in the set.
 *
 *  This dialog is held by CMainDlg as a *value* member (not a pointer),
 *  so its header must be includable from MainDlg.h. Keep the public
 *  surface minimal and dependencies forward-declared.
 */
class CBalloonMultiChatDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonMultiChatDlg();
    ~CBalloonMultiChatDlg();

public:
    // ---- Public fields the caller sets between construction and Create() ----

    CFlamingoClient*    m_lpFMGClient;      // owner client (not owned)
    CFaceList*          m_lpFaceList;       // shared face library (not owned)
    HWND                m_hMainDlg;         // main dialog HWND (for posting back)
    std::set<UINT>      m_setTargetIDs;     // recipients picked by the user
};

// Backwards-compat alias: keeps legacy CMultiChatDlg references compiling
// while transparently routing them to the balloonui implementation.
typedef CBalloonMultiChatDlg CMultiChatDlg;
