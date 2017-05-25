#pragma once

#include "resource.h"
#include "CustomMsgDef.h"
#include "SkinLib/SkinLib.h"
#include "FlamingoClient.h"
#include "RichEditUtil.h"
#include "CascadeWinManager.h"
#include "FontSelDlg.h"
#include "FaceSelDlg.h"
#include "PicBarDlg.h"

class CSessChatDlg : public CDialogImpl<CSessChatDlg>, public CMessageFilter
{
public:
	CSessChatDlg(void);
	~CSessChatDlg(void);

	enum { IDD = IDD_BUDDYCHATDLG };

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP_EX(CSessChatDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_COPYDATA(OnCopyData)
		MSG_WM_MEASUREITEM(OnMeasureItem)
		MSG_WM_DRAWITEM(OnDrawItem)
		MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
		MSG_WM_MOVE(OnMove)
		MSG_WM_SIZE(OnSize)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)

		COMMAND_HANDLER_EX(ID_LINK_BUDDYNAME, BN_CLICKED, OnLnk_BuddyName)
		COMMAND_ID_HANDLER_EX(201, OnBtn_Font)
		COMMAND_ID_HANDLER_EX(202, OnBtn_Face)
		COMMAND_ID_HANDLER_EX(208, OnBtn_Image)
		COMMAND_ID_HANDLER_EX(214, OnBtn_MsgLog)
		COMMAND_ID_HANDLER_EX(302, OnBtn_SaveAs)

		COMMAND_HANDLER_EX(ID_BTN_CLOSE, BN_CLICKED, OnBtn_Close)
		COMMAND_ID_HANDLER_EX(ID_BTN_SEND, OnBtn_Send)
		COMMAND_HANDLER_EX(ID_BTN_ARROW, BN_CLICKED, OnBtn_Arrow)

		NOTIFY_HANDLER_EX(ID_TOOLBAR_TOP, TBN_DROPDOWN, OnToolbarDropDown)
		NOTIFY_HANDLER_EX(ID_TOOLBAR_MID, TBN_DROPDOWN, OnToolbarDropDown)

		MESSAGE_HANDLER_EX(WM_UPDATE_FONTINFO, OnUpdateFontInfo)
		MESSAGE_HANDLER_EX(FACE_CTRL_SEL, OnFaceCtrlSel)
		MESSAGE_HANDLER_EX(WM_SETDLGINITFOCUS, OnSetDlgInitFocus)

		NOTIFY_HANDLER_EX(ID_RICHEDIT_RECV, EN_LINK, OnRichEdit_Recv_Link)
		NOTIFY_HANDLER_EX(ID_RICHEDIT_SEND, EN_PASTE, OnRichEdit_Send_Paste)
		COMMAND_ID_HANDLER_EX(ID_MENU_CUT, OnMenu_Cut)
		COMMAND_ID_HANDLER_EX(ID_MENU_COPY, OnMenu_Copy)
		COMMAND_ID_HANDLER_EX(ID_MENU_PASTE, OnMenu_Paste)
		COMMAND_ID_HANDLER_EX(ID_MENU_SELALL, OnMenu_SelAll)
		COMMAND_ID_HANDLER_EX(ID_MENU_CLEAR, OnMenu_Clear)
		COMMAND_RANGE_HANDLER_EX(ID_MENU_ZOOMRATIO_400, ID_MENU_ZOOMRATIO_50, OnMenu_ZoomRatio)
		COMMAND_ID_HANDLER_EX(ID_MENU_SAVEAS, OnMenu_SaveAs)
	END_MSG_MAP()

public:
	void OnRecvMsg(UINT nUTalkUin, UINT nMsgId);
	void OnUpdateGMemberNumber();				// 更新群成员号码
	void OnUpdateGMemberHeadPic();				// 更新群成员头像

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	BOOL OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct);
	void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	void OnGetMinMaxInfo(LPMINMAXINFO lpMMI);
	void OnMove(CPoint ptPos);
	void OnSize(UINT nType, CSize size);
	void OnTimer(UINT_PTR nIDEvent);
	void OnClose();
	void OnDestroy();

	void OnLnk_BuddyName(UINT uNotifyCode, int nID, CWindow wndCtl);// “好友名称”超链接控件
	void OnBtn_Font(UINT uNotifyCode, int nID, CWindow wndCtl);		// “字体选择工具栏”按钮
	void OnBtn_Face(UINT uNotifyCode, int nID, CWindow wndCtl);		// “表情”按钮
	void OnBtn_Image(UINT uNotifyCode, int nID, CWindow wndCtl);	// “发送图片”按钮
	void OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl);	// “消息记录”按钮
	void OnBtn_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl);	// “点击另存为”按钮

	void OnBtn_Close(UINT uNotifyCode, int nID, CWindow wndCtl);	// “关闭”按钮
	void OnBtn_Send(UINT uNotifyCode, int nID, CWindow wndCtl);		// “发送”按钮
	void OnBtn_Arrow(UINT uNotifyCode, int nID, CWindow wndCtl);	// “箭头”按钮

	LRESULT OnToolbarDropDown(LPNMHDR pnmh);

	LRESULT OnUpdateFontInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新字体信息
	LRESULT OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam);		// “表情”控件选取消息
	LRESULT OnSetDlgInitFocus(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 设置对话框初始焦点

	LRESULT OnRichEdit_Recv_Link(LPNMHDR pnmh);						//	“接收消息”富文本框链接点击消息
	LRESULT OnRichEdit_Send_Paste(LPNMHDR pnmh);
	void OnMenu_Cut(UINT uNotifyCode, int nID, CWindow wndCtl);		// “剪切”菜单
	void OnMenu_Copy(UINT uNotifyCode, int nID, CWindow wndCtl);	// “复制”菜单
	void OnMenu_Paste(UINT uNotifyCode, int nID, CWindow wndCtl);	// “粘贴”菜单
	void OnMenu_SelAll(UINT uNotifyCode, int nID, CWindow wndCtl);	// “全部选择”菜单
	void OnMenu_Clear(UINT uNotifyCode, int nID, CWindow wndCtl);	// “清屏”菜单
	void OnMenu_ZoomRatio(UINT uNotifyCode, int nID, CWindow wndCtl);// “显示比例”菜单
	void OnMenu_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl);	// “另存为”菜单

	BOOL OnRichEdit_MouseMove(MSG* pMsg);		// 发送/接收文本框的鼠标移动消息
	BOOL OnRichEdit_LBtnDblClk(MSG* pMsg);		// 发送/接收文本框的鼠标双击消息
	BOOL OnRichEdit_RBtnDown(MSG* pMsg);		// 发送/接收文本框的鼠标右键按下消息

	CGroupInfo* GetGroupInfoPtr();		// 获取群信息指针
	CBuddyInfo* GetBuddyInfoPtr();		// 获取好友信息指针
	CBuddyInfo* GetUserInfoPtr();		// 获取用户信息指针

	void UpdateData();					// 更新数据
	void UpdateDlgTitle();				// 更新对话框标题栏
	void UpdateBuddyNameCtrl();			// 更新好友名称控件
	void UpdateGroupNameCtrl();			// 更新群名称控件
	void CalcTitleBarRect();
	BOOL InitTopToolBar();				// 初始化Top工具栏
	BOOL InitMidToolBar();				// 初始化Middle工具栏
	BOOL InitRichEditOleCallback();		// 初始化IRichEditOleCallback接口


	BOOL Init();						// 初始化
	BOOL UnInit();						// 反初始化

	void AddMsg(CSessMessage* lpSessMsg);
	void _RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText);
	BOOL _RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId, int nFaceIndex);
	BOOL HandleSysFaceId(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	BOOL HandleSysFaceIndex(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	BOOL HandleCustomPic(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	void AddMsg(HWND hRichEditWnd, LPCTSTR lpText);
	void AddMsgToSendEdit(LPCTSTR lpText);
	void AddMsgToRecvEdit(time_t nTime, LPCTSTR lpText);
	void AddMsgToRecvEdit(CSessMessage* lpSessMsg);

	void OpenMsgLogBrowser();			// 打开消息记录浏览窗口
	void CloseMsgLogBrowser();			// 关闭消息记录浏览窗口

public:
	CFlamingoClient* m_lpFMGClient;
	CFaceList* m_lpFaceList;
	CCascadeWinManager* m_lpCascadeWinManager;
	HWND m_hMainDlg;
	UINT m_nGroupCode;
	UINT m_nUTalkUin;

private:
	CSkinDialog m_SkinDlg;
	CSkinPictureBox m_picHead;
	CSkinHyperLink m_lnkBuddyName;
	CSkinStatic m_staGroupName;
	//CSkinPictureBox m_picAD_1, m_picAD_2, m_picAD_3;
	CSkinButton m_btnClose, m_btnSend, m_btnArrow;
	CSkinMenu m_SkinMenu;
	CSkinToolBar m_tbTop, m_tbMid;
	CFaceSelDlg m_FaceSelDlg;
	CFontSelDlg m_FontSelDlg;
	CPicBarDlg m_PicBarDlg;
	CSkinRichEdit m_richRecv, m_richSend;

	CAccelerator m_Accelerator;
	HICON m_hDlgIcon, m_hDlgSmallIcon;
	HFONT m_fontBuddyNameLink;
	HWND m_hRBtnDownWnd;
	POINT m_ptRBtnDown;
	IImageOle* m_pLastImageOle;
	int m_cxPicBarDlg, m_cyPicBarDlg;
	CRect m_rcTitleBar;
	BOOL m_bHasMinBtn, m_bHasMaxBtn, m_bHasCloseBtn;
	CRect m_rcMinBtn, m_rcMaxBtn, m_rcCloseBtn;
	UINT m_nGroupId;
	UINT m_nUTalkNumber;
	UINT m_nUserNumber;
	CString m_strBuddyName;
	CString m_strGroupName;
	CString m_strUserName;

	DWORD m_dwThreadId;			// MsgLogBrowser.exe的主线程ID
};
