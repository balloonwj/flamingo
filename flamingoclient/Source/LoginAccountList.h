/**
 *  本机历史登录账号列表 —— 记录每个曾在本机登录成功过的账号，含密码、
 *  登录状态、记住密码 / 自动登录勾选项，以及该账号的姓名(昵称)，持久化到
 *  Bin\Users\LoginAccountList.dat。登录窗(CBalloonLoginDlg)用它填充账号
 *  下拉框、切换账号时回填密码与勾选项，并在头像下方显示该账号的姓名。
 *
 *  本文件不 include stdafx.h，自带所需前置依赖 —— 与 FlamingoUnit 测试
 *  工程逐字共用同一份源码，见 Tests/LoginAccountListTests.cpp。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <vector>

// 姓名(昵称)字段可容纳的字符数(含结束符)。与登录应答载体
// CLoginResult::m_szNickName[64] 对齐 —— 那 64 是 UTF-8 字节数，转成宽
// 字符后字符数只会更少，故此处 64 个字符必定装得下。
const int kLoginAccountNickNameLen = 64;

struct LOGIN_ACCOUNT_INFO	// 登录账号信息
{
	TCHAR szUser[32];		// 用户名
	TCHAR szPwd[32];		// 密码
	int nStatus;			// 登录状态
	BOOL bRememberPwd;		// 记住密码
	BOOL bAutoLogin;		// 自动登录
	//BOOL bAlreadyLogin;		// 是否已经登录

	// 该账号的姓名(昵称)，来自登录应答的 profile.nickname，登录成功后由
	// CMainDlg 写入；空串表示尚未取到(例如老版本存下的账号、或服务器返回
	// 的昵称为空)。登录窗据它在头像下方显示姓名，为空则退回显示账号。
	TCHAR szNickName[kLoginAccountNickNameLen];
};

class CLoginAccountList		// 登录账号列表
{
public:
	CLoginAccountList(void);
	~CLoginAccountList(void);

	BOOL LoadFile(LPCTSTR lpszFileName);					// 加载登录帐号列表文件
	BOOL SaveFile(LPCTSTR lpszFileName);					// 保存登录帐号列表文件

	/**
	 *  更新指定账号的姓名(昵称)。登录成功拿到 profile.nickname 后、以及
	 *  用户在个人资料里改了昵称后调用，随后由调用方 SaveFile 落盘。
	 *    lpszUser：    账号名，不可为 NULL；账号须已在列表里。
	 *    lpszNickName：姓名(昵称)；NULL 或空串表示清空该账号的姓名。
	 *  返回：账号存在并已更新返回 TRUE；lpszUser 为 NULL 或账号不在列表里
	 *        返回 FALSE(此时列表不变)。
	 */
	BOOL SetNickName(LPCTSTR lpszUser, LPCTSTR lpszNickName);

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
	/**
	 *  读取文件主体里的账号记录并追加到列表，姓名留空(由姓名尾块补)。
	 *  文件被截断时读到哪算哪，已读出的账号仍然可用。
	 *    fp：     已打开的文件，读取位置停在第一条记录处；
	 *    nCount： 待读取的记录条数，调用方须先校验其合法性。
	 *  返回：实际读出并入列的账号条数。
	 */
	int ReadAccountRecords(FILE* fp, int nCount);

	/**
	 *  读取文件末尾的姓名尾块，按顺序把姓名填进刚读出的那批账号。
	 *  文件没有尾块(老版本客户端写的)、魔数或版本号对不上、条数与账号数
	 *  不匹配时，都只是让姓名保持空串，不影响已读出的账号数据。
	 *    fp：      已打开的文件，读取位置停在主体记录之后；
	 *    nLoaded： 本次刚读出的账号条数，即尾块里应有的姓名条数。
	 */
	void ReadNickNameSection(FILE* fp, int nLoaded);

	/**
	 *  在文件末尾写出姓名尾块。nCount <= 0 时什么都不写。
	 *    fp：     已打开的文件，写入位置停在主体记录之后；
	 *    nCount： 实际写出的账号条数，须与主体里的记录条数一致。
	 */
	void WriteNickNameSection(FILE* fp, int nCount);

	void Encrypt(char* lpBuf, int nLen);					// 加密
	void Decrypt(char* lpBuf, int nLen);					// 解密
	int _fread(void* lpBuf, int nSize, int nCount, FILE* fp);
	int _fwrite(const void* lpBuf, int nSize, int nCount, FILE* fp);

private:
	int m_nLastLoginUser;									// 最后登录用户索引
	std::vector<LOGIN_ACCOUNT_INFO*> m_arrLoginAccount;	// 登录账号列表
};
