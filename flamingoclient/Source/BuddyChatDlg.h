/** 
 * 单人聊天窗口, BuddyChatDlg.h
 * zhangyl 2017.05.26
 */
#pragma once

#include "resource.h"
#include "CustomMsgDef.h"
#include "SkinLib/SkinLib.h"
#include "RichEditUtil.h"
#include "CascadeWinManager.h"
#include "FontSelDlg.h"
#include "FaceSelDlg.h"
#include "PicBarDlg.h"
#include "FileTransferCtrl.h"
#include "SplitterCtrl.h"
#include "ChatTabMgr.h"


class CFlamingoClient;
class CBuddyChatDlg : public CDialogImpl<CBuddyChatDlg>, public CMessageFilter
{
public:
	CBuddyChatDlg(void);
	~CBuddyChatDlg(void);

	enum { IDD = IDD_BUDDYCHATDLG };

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP_EX(CBuddyChatDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_COPYDATA(OnCopyData)
		MSG_WM_MEASUREITEM(OnMeasureItem)
		MSG_WM_DRAWITEM(OnDrawItem)
		MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOVE(OnMove)
		MSG_WM_SIZE(OnSize)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_DROPFILES(OnDropFiles)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)

		COMMAND_HANDLER_EX(ID_PIC_HEAD, BN_CLICKED, OnLnk_BuddyName)			//点击头像按钮
		COMMAND_HANDLER_EX(ID_LINK_BUDDYNAME, BN_CLICKED, OnLnk_BuddyName)
        COMMAND_ID_HANDLER_EX(104, OnBtn_RemoteDesktop)
		COMMAND_ID_HANDLER_EX(201, OnBtn_Font)
		COMMAND_ID_HANDLER_EX(202, OnBtn_Face)
		COMMAND_ID_HANDLER_EX(204, OnShakeWindow)								//窗口抖动
		COMMAND_ID_HANDLER_EX(208, OnBtn_Image)
		COMMAND_ID_HANDLER_EX(211, OnBtn_ScreenShot)							//截图工具
		COMMAND_ID_HANDLER_EX(214, OnBtn_MsgLog)								//消息记录
		COMMAND_ID_HANDLER_EX(302, OnBtn_SaveAs)

		COMMAND_RANGE_HANDLER_EX(IDC_FIRSTMSGLOG, IDC_LASTMSGLOG, OnMsgLogPage) //消息记录最前一条，上一条，下一条，最后一条四个按钮
		
		COMMAND_ID_HANDLER_EX(ID_32852, OnPressEnterMenuItem)					//回车键发送消息
		COMMAND_ID_HANDLER_EX(ID_32853, OnPressCtrlEnterMenuItem)				//Ctrl+回车发送消息
		COMMAND_ID_HANDLER_EX(IDM_AUTOREPLY, OnAutoReply);

		COMMAND_HANDLER_EX(ID_BTN_CLOSE, BN_CLICKED, OnBtn_Close)
		COMMAND_ID_HANDLER_EX(ID_BTN_SEND, OnBtn_Send)
		COMMAND_HANDLER_EX(ID_BTN_ARROW, BN_CLICKED, OnBtn_Arrow)

		COMMAND_ID_HANDLER_EX(IDM_OPENFILE, OnOpenTransferFileItem)
		COMMAND_ID_HANDLER_EX(IDM_OPENDIRECTORY, OnOpenTransferFileItem)

		NOTIFY_HANDLER_EX(ID_TOOLBAR_TOP, TBN_DROPDOWN, OnToolbarDropDown)
		NOTIFY_HANDLER_EX(ID_TOOLBAR_MID, TBN_DROPDOWN, OnToolbarDropDown)
		NOTIFY_HANDLER_EX(ID_TABCTRL_CHAT, TCN_DROPDOWN, OnTabCtrlDropDown)
		NOTIFY_HANDLER_EX(ID_TABCTRL_CHAT, TCN_SELCHANGE, OnTabCtrlSelChange)
		NOTIFY_HANDLER_EX(ID_CHAT_TAB_MGR, NM_CLICK, OnClickTabMgr)
		NOTIFY_HANDLER_EX(ID_FILE_TRANSFER, NM_CLICK, OnBtn_FileTransfer)

		MESSAGE_HANDLER_EX(WM_UPDATE_FONTINFO, OnUpdateFontInfo)
		MESSAGE_HANDLER_EX(FACE_CTRL_SEL, OnFaceCtrlSel)
		MESSAGE_HANDLER_EX(WM_SETDLGINITFOCUS, OnSetDlgInitFocus)

		MESSAGE_HANDLER_EX(FMG_MSG_SENDCHATMSG_RESULT, OnSendChatMsgResult)	//给好友发消息的反馈结果
		//MESSAGE_HANDLER_EX(FMG_MSG_RECVFILE_REQUEST, OnRequestRecvFile)		//请求接受文件
		MESSAGE_HANDLER_EX(FMG_MSG_SEND_FILE_PROGRESS, OnSendFileProgress)
		MESSAGE_HANDLER_EX(FMG_MSG_SEND_FILE_RESULT, OnSendFileResult)
		MESSAGE_HANDLER_EX(FMG_MSG_RECV_FILE_PROGRESS, OnRecvFileProgress)
		MESSAGE_HANDLER_EX(FMG_MSG_RECV_FILE_RESULT, OnRecvFileResult)
		

		NOTIFY_HANDLER_EX(ID_RICHEDIT_RECV, EN_LINK, OnRichEdit_Recv_Link)
		NOTIFY_HANDLER_EX(ID_RICHEDIT_SEND, EN_PASTE, OnRichEdit_Send_Paste)
		COMMAND_ID_HANDLER_EX(ID_MENU_CUT, OnMenu_Cut)
		COMMAND_ID_HANDLER_EX(ID_MENU_COPY, OnMenu_Copy)
		COMMAND_ID_HANDLER_EX(ID_MENU_PASTE, OnMenu_Paste)
		COMMAND_ID_HANDLER_EX(ID_MENU_SELALL, OnMenu_SelAll)
		COMMAND_ID_HANDLER_EX(ID_MENU_CLEAR, OnMenu_Clear)
		COMMAND_RANGE_HANDLER_EX(ID_MENU_ZOOMRATIO_400, ID_MENU_ZOOMRATIO_50, OnMenu_ZoomRatio)
		COMMAND_ID_HANDLER_EX(ID_MENU_SAVEAS, OnMenu_SaveAs)
		COMMAND_ID_HANDLER_EX(IDM_EXPORT_MSGLOG, OnMenu_ExportMsgLog)			//导出消息记录
		COMMAND_ID_HANDLER_EX(IDM_FINDINMSGLOG, OnMenu_FindInMsgLog)			//查找消息记录
		COMMAND_ID_HANDLER_EX(IDM_CLEARMSGLOG, OnMenu_ClearMsgLog)				//清除消息记录
		COMMAND_ID_HANDLER_EX(IDM_DELETESELECTMSGLOG, OnMenu_DeleteSelectMsgLog)//删除选中消息记录
		COMMAND_ID_HANDLER_EX(IDM_SENDFILE, OnMenu_SendFile)					//发送文件
		COMMAND_ID_HANDLER_EX(IDM_SENDOFFLINEFILE, OnMenu_SendOfflineFile)		//发送离线文件
		COMMAND_ID_HANDLER_EX(IDM_SENDDIRECTORY, OnMenu_SendDirectory)			//发送文件夹
		COMMAND_ID_HANDLER_EX(IDM_SENDFILESETTINGS, OnMenu_SendFileSettings)	//发送文件设置


	END_MSG_MAP()

public:
	void OnRecvMsg(UINT nUTalkUin, UINT nMsgId);								// 接收好友消息
	void OnUpdateBuddyNumber();													// 更新好友号码通知
	void OnUpdateBuddySign();													// 更新好友签名通知
	void OnUpdateBuddyHeadPic();												// 更新好友头像通知

	BOOL IsFilesTransferring();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	BOOL OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct);
	void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	void OnGetMinMaxInfo(LPMINMAXINFO lpMMI);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMove(CPoint ptPos);
	void OnSize(UINT nType, CSize size);
	void OnTimer(UINT_PTR nIDEvent);
	void OnDropFiles(HDROP hDropInfo);
	void OnClose();
	void OnDestroy();

	void OnLnk_BuddyName(UINT uNotifyCode, int nID, CWindow wndCtl);			// “好友名称”超链接控件
    void OnBtn_RemoteDesktop(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnBtn_Font(UINT uNotifyCode, int nID, CWindow wndCtl);					// “字体选择工具栏”按钮
	void OnBtn_Face(UINT uNotifyCode, int nID, CWindow wndCtl);					// “表情”按钮
	void OnShakeWindow(UINT uNotifyCode, int nID, CWindow wndCtl);				// “窗口抖动”按钮
	void OnBtn_Image(UINT uNotifyCode, int nID, CWindow wndCtl);				// “发送图片”按钮
	void OnBtn_ScreenShot(UINT uNotifyCode, int nID, CWindow wndCtl);			// 截图工具
	void OnBtn_MsgLog(UINT uNotifyCode, int nID, CWindow wndCtl);				// “消息记录”按钮
	void OnBtn_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl);				// “点击另存为”按钮

	void OnMsgLogPage(UINT uNotifyCode, int nID, CWindow wndCtl);				// 消息记录翻页按钮

	void OnBtn_Close(UINT uNotifyCode, int nID, CWindow wndCtl);				// “关闭”按钮
	void OnBtn_Send(UINT uNotifyCode, int nID, CWindow wndCtl);					// “发送”按钮
	void OnBtn_Arrow(UINT uNotifyCode, int nID, CWindow wndCtl);				// “箭头”按钮

	void OnOpenTransferFileItem(UINT uNotifyCode, int nID, CWindow wndCtl);		// 接受文件成功以后的“打开”与“打开文件夹”菜单项
	
	void OnPressEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl);		//回车键发送消息
	void OnPressCtrlEnterMenuItem(UINT uNotifyCode, int nID, CWindow wndCtl);	//Ctrl+回车发送消息
	void OnAutoReply(UINT uNotifyCode, int nID, CWindow wndCtl);				// 自动回复菜单
	
	LRESULT OnToolbarDropDown(LPNMHDR pnmh);

	LRESULT OnUpdateFontInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 更新字体信息
	LRESULT OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam);				// “表情”控件选取消息
	LRESULT OnSetDlgInitFocus(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 设置对话框初始焦点

	LRESULT OnSendChatMsgResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnSendFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnSendFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnRecvFileProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnRecvFileResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnTabCtrlDropDown(LPNMHDR pnmh);
	LRESULT OnTabCtrlSelChange(LPNMHDR pnmh);
	LRESULT OnClickTabMgr(LPNMHDR pnmh);
	LRESULT OnBtn_FileTransfer(LPNMHDR pnmh);
 	LRESULT OnRichEdit_Recv_Link(LPNMHDR pnmh);						//	“接收消息”富文本框链接点击消息
	LRESULT OnRichEdit_Send_Paste(LPNMHDR pnmh);
 	void OnMenu_Cut(UINT uNotifyCode, int nID, CWindow wndCtl);		// “剪切”菜单
 	void OnMenu_Copy(UINT uNotifyCode, int nID, CWindow wndCtl);	// “复制”菜单
 	void OnMenu_Paste(UINT uNotifyCode, int nID, CWindow wndCtl);	// “粘贴”菜单
 	void OnMenu_SelAll(UINT uNotifyCode, int nID, CWindow wndCtl);	// “全部选择”菜单
 	void OnMenu_Clear(UINT uNotifyCode, int nID, CWindow wndCtl);	// “清屏”菜单
	void OnMenu_ZoomRatio(UINT uNotifyCode, int nID, CWindow wndCtl);// “显示比例”菜单
	void OnMenu_SaveAs(UINT uNotifyCode, int nID, CWindow wndCtl);	// “另存为”菜单
	void OnMenu_ExportMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl);//“导出消息记录”菜单
	void OnMenu_FindInMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl); //“查找消息记录”菜单
	void OnMenu_DeleteSelectMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl);//“删除选中消息记录”菜单
	void OnMenu_ClearMsgLog(UINT uNotifyCode, int nID, CWindow wndCtl); //“清空消息记录”菜单
	
	void OnMenu_SendFile(UINT uNotifyCode, int nID, CWindow wndCtl); //“发送文件”菜单
	void OnMenu_SendOfflineFile(UINT uNotifyCode, int nID, CWindow wndCtl); //“发送离线文件”菜单
	void OnMenu_SendDirectory(UINT uNotifyCode, int nID, CWindow wndCtl); //“发送文件夹”菜单
	void OnMenu_SendFileSettings(UINT uNotifyCode, int nID, CWindow wndCtl); //“发送文件设置”菜单



	BOOL OnRichEdit_MouseMove(MSG* pMsg);		// 发送/接收文本框的鼠标移动消息
	BOOL OnRichEdit_LBtnDblClk(MSG* pMsg);		// 发送/接收文本框的鼠标双击消息
	BOOL OnRichEdit_RBtnDown(MSG* pMsg);		// 发送/接收文本框的鼠标右键按下消息

	CBuddyInfo* GetBuddyInfoPtr();		// 获取好友信息指针
	CBuddyInfo* GetUserInfoPtr();		// 获取用户信息指针

	void UpdateData();					// 更新数据
	void UpdateDlgTitle();				// 更新对话框标题栏
	void UpdateBuddyNameCtrl();			// 更新好友名称控件
	void UpdateBuddySignCtrl();			// 更新好友签名控件

	BOOL InitTopToolBar();				// 初始化Top工具栏
	BOOL InitMidToolBar();				// 初始化Middle工具栏
	BOOL InitRightTabWindow();			// 初始化右边包含消息记录和发送文件功能的窗口
	BOOL InitRichEditOleCallback();		// 初始化IRichEditOleCallback接口

	void ShowMsgLogButtons(BOOL bShow);

	void InitFileTransferCtrl();		// 初始化文件发送系列控件
	void ShowFileTransferCtrl(BOOL bShow);//显示或隐藏文件传输控件
	void DestroyFileTransferCtrl();		// 销毁文件发送系列控件

	BOOL Init();						// 初始化
	BOOL UnInit();						// 反初始化
	//void CalcTitleBarRect();
	void SetHotRgn();					//设置热点区域

	void _RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText);
	BOOL _RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId, int nFaceIndex);
	BOOL HandleSysFaceId(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	BOOL HandleSysFaceIndex(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	BOOL HandleCustomPic(HWND hRichEditWnd, LPCTSTR& p, CString& strText);
	void AddMsg(HWND hRichEditWnd, LPCTSTR lpText);
	void AddMsgToSendEdit(LPCTSTR lpText);
	void AddMsgToRecvEdit(time_t nTime, LPCTSTR lpText);
	void AddMsgToRecvEdit(CBuddyMessage* lpBuddyMsg);
	void AddMsgToMsgLogEdit(std::vector<CContent*>& arrContent);
	void AddMsgToMsgLogEdit(std::vector<BUDDY_MSG_LOG*>& arrMsgLog);
	void AddMsgToRecvEdit(std::vector<BUDDY_MSG_LOG*>& arrMsgLog);
	void AddMsgToRecvEdit(std::vector<CContent*>& arrContent);

	void InsertAutoReplyContent();

	void OpenMsgLogBrowser();			// 打开消息记录浏览窗口
	void CloseMsgLogBrowser();			// 关闭消息记录浏览窗口
	void CalculateMsgLogCountAndOffset();

	BOOL GetImageDisplaySizeInRichEdit(PCTSTR pszFileName, HWND hWnd, long& nWidth, long& nHeight);	//获取富文本控件中图片的显示的尺寸
	void ResizeImageInRecvRichEdit();

	void DisplayFileTransfer(BOOL bShow);
	void AnalyseContent(tstring& strContent, HWND hRichWnd=NULL);
	BOOL HandleShake(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleText(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleFontInfo(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleSysFaceId(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleCustomPic(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	BOOL HandleFile(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent);
	
	BOOL HandleFileDragResult(PCTSTR lpszFileName);
	BOOL SendOfflineFile(PCTSTR pszFileName);
	BOOL RecvOfflineFile(PCTSTR lpszDownloadName, PCTSTR pszFileName, long nFileSize);
	BOOL ShowRequestRecvFile(BOOL bOnlineFile, PCTSTR lpszDownloadName, PCTSTR pszFileName, long nFileSize);	//接收文件请求

    void SendConfirmMessage(const CUploadFileResult* pUploadFileResult);

	void ShowLastMsgInRecvRichEdit();

	void RecordWindowSize();
	void ReCaculateCtrlPostion(long nMouseY);

    static UINT __stdcall RemoteDesktopProc(void* p);

public:
	CFlamingoClient*	m_lpFMGClient;
	CFaceList*			m_lpFaceList;
	CCascadeWinManager* m_lpCascadeWinManager;
	HWND				m_hMainDlg;
	int				    m_nUTalkUin;                
    int                 m_UserId;                   //当前聊天好友id
    int                 m_LoginUserId;              //登录用户id

private:
	CSkinDialog			m_SkinDlg;							
	CSkinPictureBox		m_picHead;
	CSkinHyperLink		m_lnkBuddyName;
	CSkinStatic			m_staBuddySign;
	//CSkinPictureBox	m_picAD_1, m_picAD_2, m_picAD_3;
	CSkinButton			m_btnMsgLog, m_btnClose, m_btnSend, m_btnArrow;
	CSkinButton			m_btnFirstMsgLog, m_btnPrevMsgLog, m_btnNextMsgLog, m_btnLastMsgLog;
	CSkinStatic			m_staMsgLogPage;
	CSkinMenu			m_SkinMenu;
	CSkinToolBar		m_tbTop, m_tbMid;
	CFaceSelDlg			m_FaceSelDlg;
	CFontSelDlg			m_FontSelDlg;
	CPicBarDlg			m_PicBarDlg;
	CSkinRichEdit		m_richRecv, m_richSend;
	CChatTabMgr			m_TabMgr;
	CSkinTabCtrl		m_RightTabCtrl;				//聊天窗口右边的Tab窗口
	CSkinRichEdit		m_richMsgLog;				//消息记录富文本控件
	CSplitterCtrl		m_SplitterCtrl;

	CSkinStatic			m_staPicUploadProgress;		//图片上传进度控件

	//文件传输“窗口”控件
	CSkinPictureBox		m_SendFileThumbPicture;		//文件图标
	CSkinStatic			m_staSendFileDesc;			//文件描述（文件名+大小）
	CProgressBarCtrl	m_ProgressSendFile;			//文件进度条
	CSkinHyperLink		m_lnkSendOffline;			//转离线发送按钮
	CSkinHyperLink		m_lnkSendFileCancel;		//取消发送按钮

	CFileTransferCtrl	m_FileTransferCtrl;

	CRect				m_rcTitleBar;
	BOOL				m_bHasMinBtn, m_bHasMaxBtn, m_bHasCloseBtn;
	CRect				m_rcMinBtn, m_rcMaxBtn, m_rcCloseBtn;
	
	CAccelerator		m_Accelerator;
	BOOL				m_bPressEnterToSendMessage;	//TRUE按回车键发送消息, FALSE按Ctrl+Enter发送消息
	BOOL				m_bMsgLogWindowVisible;		//聊天记录窗口当前是否可见
	BOOL				m_bFileTransferVisible;

	HICON				m_hDlgIcon, m_hDlgSmallIcon;
	HWND				m_hRBtnDownWnd;
	POINT				m_ptRBtnDown;
	IImageOle*			m_pLastImageOle;
	int					m_cxPicBarDlg, m_cyPicBarDlg;
	int					m_nMsgLogIndexInToolbar;	//消息记录在toolbar上的索引

	CMessageLogger		m_MsgLogger;				// 消息记录
	UINT				m_nUTalkNumber;
	UINT				m_nUserNumber;
	CString				m_strBuddyName;
	CString				m_strBuddySign;
	CString				m_strUserName;

	CString				m_strLinkUrl;
	
	HRGN				m_HotRgn;

	BOOL				m_bEnableAutoReply;

	long				m_nMsgLogRecordOffset;		//当前消息记录的偏移量+1
	long				m_nMsgLogCurrentPageIndex;	//消息记录当前页码

	std::map<CString, long>	m_mapSendFileInfo;		//发送文件队列，键是文件名，如果键值为-1则是文件；如果键值大于0则是图片在窗口位置的索引
	std::map<CString, long>	m_mapRecvFileInfo;		//接收文件队列，键是文件显示名，键值为-1是收取的离线文件；键值大于1是收取的聊天图片应插入的位置索引


	BOOL				m_bDraged;					//用户是否拖拽过发送文本框的尺寸
	RECT				m_rtRichRecv;
	RECT				m_rtMidToolBar;
	RECT				m_rtSplitter;
	RECT				m_rtRichSend;

    time_t              m_nLastSendShakeWindowTime; //最近一次发送窗口抖动的的时间
};
