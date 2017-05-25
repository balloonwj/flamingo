
/////////////////////////////////////////////////////////////////////////////
// CTrackerEx - simple rectangular tracking rectangle w/resize handles

/////////////////////////////////////////////////////////////////////////
//				Copy from MFC CRectTracker class 
//					Add Some member function 
//						Modify some place
////////////////////////////////////////////////////////////////////////

#define TRACKEREX_

#define CX_BORDER   1
#define CY_BORDER   1

#define CRIT_RECTTRACKER    5
void AFXAPI AfxLockGlobals(int nLockType);
void AFXAPI AfxUnlockGlobals(int nLockType);
void AFXAPI AfxDeleteObject(HGDIOBJ* pObject);

class CTrackerEx
{
public:
// Constructors
	CTrackerEx();
	CTrackerEx(LPCRECT lpSrcRect, UINT nStyle);

// Style Flags
	enum StyleFlags
	{
		solidLine = 1, dottedLine = 2, hatchedBorder = 4,
		resizeInside = 8, resizeOutside = 16, hatchInside = 32,
		resizeMiddle =80 //设置中间
	};

// Hit-Test codes
	enum TrackerHit
	{
		hitNothing = -1,
		hitTopLeft = 0, hitTopRight = 1, hitBottomRight = 2, hitBottomLeft = 3,
		hitTop = 4, hitRight = 5, hitBottom = 6, hitLeft = 7, hitMiddle = 8
	};

// Attributes
	UINT m_nStyle;      // current state
	CRect m_rect;       // current position (always in pixels)
	CSize m_sizeMin;    // minimum X and Y size during track operation
	int m_nHandleSize;  // size of resize handles (default from WIN.INI)

// Operations
	void Draw(CDC* pDC) const;
	void GetTrueRect(LPRECT lpTrueRect) const;
	BOOL SetCursor(CWnd* pWnd, UINT nHitTest) const;
	BOOL Track(CWnd* pWnd, CPoint point, BOOL bAllowInvert =TRUE,
		CWnd* pWndClipTo = NULL);
	BOOL TrackRubberBand(CWnd* pWnd, CPoint point, BOOL bAllowInvert = TRUE);
	int HitTest(CPoint point) const;
	int NormalizeHit(int nHandle) const;

// Overridables
	virtual void DrawTrackerRect(LPCRECT lpRect, CWnd* pWndClipTo,
		CDC* pDC, CWnd* pWnd);
	virtual void AdjustRect(int nHandle, LPRECT lpRect);
	virtual void OnChangedRect(const CRect& rectOld);
	virtual UINT GetHandleMask() const;

// Implementation
public:
	virtual ~CTrackerEx();

public:
	//***********************************************************
	//设置调整光标
	void SetResizeCursor(UINT nID_N_S,UINT nID_W_E,UINT nID_NW_SE,
							UINT nID_NE_SW,UINT nIDMiddle);
	//创建军画刷,内部调用
	void CreatePen();
	//设置矩形颜色
	void SetRectColor(COLORREF rectColor);

	//**************************************************************

    //**************************************************************
	//当前矩形颜色
	COLORREF m_rectColor;
    //**************************************************************
	BOOL m_bAllowInvert;    // flag passed to Track or TrackRubberBand
	CRect m_rectLast;
	CSize m_sizeLast;
	BOOL m_bErase;          // TRUE if DrawTrackerRect is called for erasing
	BOOL m_bFinalErase;     // TRUE if DragTrackerRect called for final erase

	// implementation helpers
	int HitTestHandles(CPoint point) const;
	void GetHandleRect(int nHandle, CRect* pHandleRect) const;
	void GetModifyPointers(int nHandle, int**ppx, int**ppy, int* px, int*py);
	virtual int GetHandleSize(LPCRECT lpRect = NULL) const;
	BOOL TrackHandle(int nHandle, CWnd* pWnd, CPoint point, CWnd* pWndClipTo);
	void Construct();
};

