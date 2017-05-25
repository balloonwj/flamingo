#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"
#include "FlamingoClient.h"
#include "Utils.h"
#include "FontSelDlg.h"

#define		WM_UPDATE_FONTINFO		WM_USER + 2

extern CFontInfo g_fontInfo;				// 字体信息
extern TCHAR g_cHotKey;						// 提取消息热键

//BOOL LoadMiscConfig(LPCTSTR lpszFileName);	// 加载杂项配置
//BOOL SaveMiscConfig(LPCTSTR lpszFileName);	// 保存杂项配置


class CFontSelDlg : public CDialogImpl<CFontSelDlg>
{
public:
	CFontSelDlg(void);
	~CFontSelDlg(void);

	enum { IDD = IDD_FONTSELDLG };

	BEGIN_MSG_MAP_EX(CFontSelDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(101, OnBtn_SysFont)
		COMMAND_ID_HANDLER_EX(102, OnBtn_MemberFont)
		COMMAND_HANDLER_EX(ID_COMBO_FONTNAME, CBN_SELCHANGE, OnCbo_SelChange_FontName)
		COMMAND_HANDLER_EX(ID_COMBO_FONTSIZE, CBN_SELCHANGE, OnCbo_SelChange_FontSize)
		COMMAND_ID_HANDLER_EX(104, OnBtn_Bold)
		COMMAND_ID_HANDLER_EX(105, OnBtn_Italic)
		COMMAND_ID_HANDLER_EX(106, OnBtn_UnderLine)
		COMMAND_HANDLER_EX(107, BN_CLICKED, OnBtn_Color)
	END_MSG_MAP()

public:
	CFontInfo& GetPublicFontInfo();		// 获取全局字体信息
	CFontInfo& GetFontInfo();			// 获取字体信息

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();
	void OnBtn_SysFont(UINT uNotifyCode, int nID, CWindow wndCtl);		// “系统字体”按钮
	void OnBtn_MemberFont(UINT uNotifyCode, int nID, CWindow wndCtl);	// “会员炫彩字”按钮
	void OnCbo_SelChange_FontName(UINT uNotifyCode, int nID, CWindow wndCtl);	// “字体名称”组合框
	void OnCbo_SelChange_FontSize(UINT uNotifyCode, int nID, CWindow wndCtl);	// “字体大小”组合框
	void OnBtn_Bold(UINT uNotifyCode, int nID, CWindow wndCtl);			// “加粗”按钮
	void OnBtn_Italic(UINT uNotifyCode, int nID, CWindow wndCtl);		// “倾斜”按钮
	void OnBtn_UnderLine(UINT uNotifyCode, int nID, CWindow wndCtl);	// “下划线”按钮
	void OnBtn_Color(UINT uNotifyCode, int nID, CWindow wndCtl);		// “颜色”按钮

	BOOL InitFontClassToolBar();	// 初始化字体分类工具栏
	BOOL InitFontNameComboBox();	// 初始化字体名称组合框
	BOOL InitFontSizeComboBox();	// 初始化字体大小组合框
	BOOL InitFontAttrToolBar();		// 初始化字体属性工具栏

	BOOL Init();					// 初始化
	void UnInit();					// 反初始化

public:
	CFlamingoClient*		m_pFMGClient;

private:
	CSkinDialog			m_SkinDlg;
	CSkinToolBar		m_tbFontClass; 
	CSkinToolBar		m_tbFontAttr;
	CSkinComboBox		m_cboFontName;
	CSkinComboBox		m_cboFontSize;
	CFontInfo			m_fontInfo;
};
