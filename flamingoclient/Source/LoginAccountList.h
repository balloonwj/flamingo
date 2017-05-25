#pragma once

#include <vector>

struct LOGIN_ACCOUNT_INFO	// 登录账号信息
{
	TCHAR szUser[32];		// 用户名
	TCHAR szPwd[32];		// 密码
	int nStatus;			// 登录状态
	BOOL bRememberPwd;		// 记住密码
	BOOL bAutoLogin;		// 自动登录
	//BOOL bAlreadyLogin;		// 是否已经登录
};

class CLoginAccountList		// 登录账号列表
{
public:
	CLoginAccountList(void);
	~CLoginAccountList(void);

	BOOL LoadFile(LPCTSTR lpszFileName);					// 加载登录帐号列表文件
	BOOL SaveFile(LPCTSTR lpszFileName);					// 保存登录帐号列表文件

	BOOL Add(LPCTSTR lpszUser, LPCTSTR lpszPwd,				// 添加帐号
		int nStatus, BOOL bRememberPwd, BOOL bAutoLogin);
	BOOL Del(int nIndex);									// 删除帐号
	BOOL Modify(int nIndex, LPCTSTR lpszUser, LPCTSTR lpszPwd, // 修改帐号
		int nStatus, BOOL bRememberPwd, BOOL bAutoLogin);
	void Clear();											// 清除所有帐号

	int GetCount();											// 获取帐号总数
	LOGIN_ACCOUNT_INFO* GetAccountInfo(int nIndex);		// 获取帐号信息

	LOGIN_ACCOUNT_INFO* Find(LPCTSTR lpszUser);			// 查找帐号

	int GetLastLoginUser();
	void SetLastLoginUser(LPCTSTR lpszUser);

	LOGIN_ACCOUNT_INFO* GetLastLoginAccountInfo();
	BOOL GetLastLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount);

	BOOL IsAutoLogin();

private:
	void Encrypt(char* lpBuf, int nLen);					// 加密
	void Decrypt(char* lpBuf, int nLen);					// 解密
	int _fread(void* lpBuf, int nSize, int nCount, FILE* fp);
	int _fwrite(const void* lpBuf, int nSize, int nCount, FILE* fp);

private:
	int m_nLastLoginUser;									// 最后登录用户索引
	std::vector<LOGIN_ACCOUNT_INFO*> m_arrLoginAccount;	// 登录账号列表
};
