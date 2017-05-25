#pragma once

//窗口收缩类型
enum AFX_HIDE_TYPE
{
	en_None = 0,		//不收缩
	en_Top,				//向上收缩
	en_Bottom,			//向下收缩
	en_Left,			//向左收缩
	en_Right			//向右收缩
};

//////////////////////////////////////////////////////////////////////////

class CHideWindow
{
protected:
	bool					m_bSized;					//窗口大小是否改变 
	bool					m_bTimed;					//是否设置了检测鼠标的Timer

	INT 					m_nWindowHeight;			//旧的窗口宽度
	INT 					m_nTaskBarHeight;			//任务栏高度
	INT 					m_nEdgeHeight;				//边缘高度
	INT 					m_nEdgeWidth;				//边缘宽度

	AFX_HIDE_TYPE 			m_enHideType;				//隐藏模式
	bool					m_bFinished;				//隐藏或显示过程是否完成
	bool					m_bHiding;					//该参数只有在!m_hsFinished才有效,真:正在隐藏,假:正在显示

	HWND					m_hOwnHwnd;

public:
	CHideWindow(void);
	~CHideWindow(void);

	//设置函数
public:
	//设置隐藏类型
	void SetType(AFX_HIDE_TYPE nType);
	//设置窗口属性
	void SetHideWindow(HWND hWnd,int nEdgeHeight=3,int nEdgeWidth=3);
	//修正移动时窗口的大小
	void FixMoving(UINT fwSide, LPRECT pRect);
	//修正改改变窗口大小时窗口的大小
	void FixSizing(UINT fwSide, LPRECT pRect);
	//从收缩状态显示窗口
	void ShowWindow();
	//从显示状态收缩窗口
	void HideWindow();
	//计时器消息
	void HideLoop( UINT nIDEvent );
	//开始隐藏
	void BeginHide(CPoint point);
	//重载函数
	BOOL SetWindowPos(HWND hWndInsertAfter,LPCRECT pCRect, UINT nFlags = SWP_SHOWWINDOW);
};
