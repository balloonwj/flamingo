// CursorInfo.h: interface for the CCursorInfo class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#define MAX_CURSOR_TYPE	16

class CCursorInfo  
{
private:
	LPCTSTR	m_CursorResArray[MAX_CURSOR_TYPE];
	HCURSOR	m_CursorHandleArray[MAX_CURSOR_TYPE];

public:
	CCursorInfo()
	{
		LPCTSTR	CursorResArray[MAX_CURSOR_TYPE] = 
		{
			IDC_APPSTARTING,
			IDC_ARROW,
			IDC_CROSS,
			IDC_HAND,
			IDC_HELP,
			IDC_IBEAM,
			IDC_ICON,
			IDC_NO,
			IDC_SIZE,
			IDC_SIZEALL,
			IDC_SIZENESW,
			IDC_SIZENS,
			IDC_SIZENWSE,
			IDC_SIZEWE,
			IDC_UPARROW,
			IDC_WAIT
		};
		
		for (int i = 0; i < MAX_CURSOR_TYPE; i++)
		{
			m_CursorResArray[i] = CursorResArray[i];
			m_CursorHandleArray[i] = LoadCursor(NULL, CursorResArray[i]);
		}
	}

	virtual ~CCursorInfo()
	{
		for (int i = 0; i < MAX_CURSOR_TYPE; i++)
		DestroyCursor(m_CursorHandleArray[i]);
	}


	int getCurrentCursorIndex()
	{
		CURSORINFO	ci;
		ci.cbSize = sizeof(CURSORINFO);
		if (!GetCursorInfo(&ci) || ci.flags != CURSOR_SHOWING)
			return -1;
		
		int i = 0;
		for (; i < MAX_CURSOR_TYPE; i++)
		{
			if (ci.hCursor == m_CursorHandleArray[i])
				break;
		}
		DestroyCursor(ci.hCursor);
		
		int	nIndex = i == MAX_CURSOR_TYPE ? -1 : i;
		return nIndex;
	}
	
	HCURSOR getCursorHandle( int nIndex )
	{
		if (nIndex >= 0 && nIndex < MAX_CURSOR_TYPE)
			return	m_CursorHandleArray[nIndex];
		else
			return NULL;
	}
};
