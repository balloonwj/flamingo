#include "stdafx.h"
#include "SplitterCtrl.h"

CSplitterCtrl::CSplitterCtrl()
{
	m_hCursorNS = NULL;
}

CSplitterCtrl::~CSplitterCtrl()
{

}


int CSplitterCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	m_hCursorNS = ::LoadCursor(NULL, IDC_SIZENS);
	return 1;
}

BOOL CSplitterCtrl::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CSplitterCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetCapture();
}

void CSplitterCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	ReleaseCapture();
}

void CSplitterCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if(nFlags==MK_LBUTTON && ::GetCapture()==m_hWnd)
	{
		::ClientToScreen(m_hWnd, &point);
		::ScreenToClient(::GetParent(m_hWnd), &point);
		::SendMessage(::GetParent(m_hWnd), WM_MOUSEMOVE, (WPARAM)nFlags, MAKELPARAM(point.x, point.y));
	}
}

BOOL CSplitterCtrl::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
	if(wnd.m_hWnd == m_hWnd)
	{
		::SetCursor(m_hCursorNS);
	}
	return TRUE;
}