#include "stdafx.h"
#include "CRegisterSkinHyperLink.h"
#include "..\UTalkClient\UTalkProtocolData.h"
#include "CRegisterDialog.h"

CRegisterSkinHyperLink::CRegisterSkinHyperLink()
{
	memset(&m_RegisterUser, 0, sizeof(m_RegisterUser));
}

CRegisterSkinHyperLink::~CRegisterSkinHyperLink()
{
}

void CRegisterSkinHyperLink::OnLButtonUp(UINT nFlags, CPoint point)
{
	__super::OnLButtonUp(nFlags, point);

	//TODO: 增加显示注册登录对话框
	CRegisterDialog m_RegDlg;
	if(m_RegDlg.DoModal(m_hWnd, NULL) != IDOK)
		return;

	//从注册对话框里面获得数据
	
	_tcscpy_s(m_RegisterUser.szAccountNo, 16, m_RegDlg.GetAccountNo());
	_tcscpy_s(m_RegisterUser.szNickName, 16, m_RegDlg.GetNickName());
	_tcscpy_s(m_RegisterUser.szPassword, 16, m_RegDlg.GetPassword());

	::PostMessage(::GetParent(m_hWnd), UTalk_MSG_REGISTER, 0, (LPARAM)&m_RegisterUser);
}