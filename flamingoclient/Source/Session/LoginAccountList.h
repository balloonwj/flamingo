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
	// 会话控制器写入；空串表示尚未取到(例如老版本存下的账号、或服务器返回
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

	/**
	 *  按系统设置里「自动登录」开关的新状态修改指定账号(见 docs/自动登录方案.md §4)。
	 *  只改内存里的列表，随后由调用方 SaveFile 落盘。
	 *    lpszUser：   账号名，不可为 NULL；账号须已在列表里。
	 *    bAutoLogin： TRUE=打开自动登录：同时打开"记住密码"，并存下 lpszPwd
	 *                 (否则下次启动没有密码可用)；FALSE=关闭自动登录：只取消该
	 *                 标志，不动"记住密码"与已存的密码。
	 *    lpszPwd：    打开时要存下的密码(本次登录所用的密码)；NULL 或空串表示
	 *                 不改动已存的密码。关闭时忽略。超长时截断。
	 *  返回：账号存在并已修改返回 TRUE；lpszUser 为 NULL 或账号不在列表里返回
	 *        FALSE(此时列表不变)。
	 */
	BOOL ApplyAutoLoginSetting(LPCTSTR lpszUser, BOOL bAutoLogin, LPCTSTR lpszPwd);

	/**
	 *  登录成功后，把本次登录所用的账号信息记入列表。只改内存里的列表，随后由调用方
	 *  SaveFile 落盘。
	 *    lpInfo：本次登录的账号信息(登录窗采集的账号、密码、登录状态与两个勾选项)，
	 *            由调用方持有，本函数只读、不修改；szNickName 字段被忽略。
	 *  行为：账号已在列表里时更新其密码、登录状态与两个勾选项，已存的姓名保持不变；
	 *        不在列表里时新增一条。无论哪种情况，bRememberPwd 为假时列表里<u>不</u>
	 *        保存密码(已存的密码一并清空)。
	 *  返回：已记入返回 TRUE；lpInfo 为 NULL 或账号为空串返回 FALSE(此时列表不变)。
	 */
	BOOL RecordLoginSuccess(const LOGIN_ACCOUNT_INFO* lpInfo);

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

	/**
	 *  丢弃用户名为空的账号记录，并同步修正最后登录索引。
	 *
	 *  用户名为空的记录不该存在 —— 它在登录窗的账号下拉框里表现为一个空白
	 *  行，若最后登录索引又恰好指向它，登录窗启动时便预填出一个空账号，看起来
	 *  就像"历史账号没加载"。2026-08-13 客户端曾因登录成功时账号信息尚未回填
	 *  而写出过这样一条记录（已在会话控制器的 OnLoginResult 修正），但已经落盘的
	 *  坏数据仍在用户机器上，故读取时一并清理，下次保存即写回干净的文件。
	 *
	 *  须在姓名尾块读完之后调用：尾块按位置与主体记录一一对应，提前删记录会
	 *  让姓名整体错位。
	 */
	void PurgeBlankAccounts();

	/**
	 *  清空没勾"记住密码"的账号里残留的密码，由 LoadFile 在读入之后调用。
	 *
	 *  2026-09-30 之前，会话控制器在登录成功时把登录窗采集的整条信息复制进列表，没勾
	 *  "记住密码"时密码也照样落盘。这样的记录已经存在于用户机器
	 *  上，读入时清掉，下次保存即写回不含密码的文件；否则一个账号只要不再登录，它的
	 *  密码就会一直留在文件里。勾了"记住密码"的账号不受影响。
	 */
	void ClearUnrememberedPasswords();

	void Encrypt(char* lpBuf, int nLen);					// 加密
	void Decrypt(char* lpBuf, int nLen);					// 解密
	int _fread(void* lpBuf, int nSize, int nCount, FILE* fp);
	int _fwrite(const void* lpBuf, int nSize, int nCount, FILE* fp);

private:
	int m_nLastLoginUser;									// 最后登录用户索引
	std::vector<LOGIN_ACCOUNT_INFO*> m_arrLoginAccount;	// 登录账号列表
};
