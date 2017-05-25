#pragma once

//该窗口用于聊天窗口中拖拽发送文本框
class CSplitterCtrl : public CWindowImpl<CSplitterCtrl, CWindow>
{
public:
	CSplitterCtrl();
	~CSplitterCtrl();

	DECLARE_WND_CLASS(_T("__SplitterCtrl__"))

	BEGIN_MSG_MAP_EX(CSplitterCtrl)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_SETCURSOR(OnSetCursor)
	END_MSG_MAP()

protected:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);

private:
	HCURSOR		m_hCursorNS;		//上下拖拽的箭头
};