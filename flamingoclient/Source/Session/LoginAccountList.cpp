/**
 *  本机历史登录账号列表的读写实现，见 LoginAccountList.h。
 *
 *  磁盘格式（Bin\Users\LoginAccountList.dat，每次写入的数据块按字节异或
 *  0x88，见 Encrypt / Decrypt）：
 *
 *    [账号条数 int][最后登录索引 int][基础记录 * 条数]        <- 主体，格式自始未变
 *    [魔数 int][版本号 int][账号条数 int][姓名 * 条数]        <- 姓名尾块，本次新增
 *
 *  姓名之所以<u>追加在文件末尾</u>、而不是并进记录里或加个文件头，是为了
 *  双向兼容：
 *    · 老版本客户端读新文件时，读完主体就 fclose，尾块被它无视，一切照旧；
 *      若把魔数放在文件开头，老版本会把魔数当成账号条数（十亿量级）拿去
 *      开循环 new，直接把自己撑崩。
 *    · 新版本客户端读老文件时，主体照读，读尾块时遇到文件末尾，姓名留空，
 *      登录窗退回显示账号。
 *  代价是老版本客户端保存过一次后尾块会丢失（姓名退回为空），可以接受。
 *
 *  本文件与 FlamingoUnit 测试工程逐字共用，故不 include stdafx.h（主工程
 *  Flamingo.vcxproj 里对本文件把预编译头设为 NotUsing），依赖显式引入。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "LoginAccountList.h"

#include <stdio.h>
#include <string.h>

// 姓名尾块的起始魔数。取 'F''L''A''1' 四个字符的 ASCII 拼接，用来确认文件
// 末尾这段确实是本客户端写的姓名，而不是别的什么残留字节。
static const int kNickNameSectionMagic = 0x464C4131;

// 姓名尾块的版本号。1 = 每条账号一个定长 kLoginAccountNickNameLen 字符的姓名。
// 日后姓名段若要改布局，递增此值，读到不认识的版本就整块忽略。
static const int kNickNameSectionVersion = 1;

// 账号条数的合理上限 —— 文件损坏时条数字段可能是个天文数字，据此挡掉，
// 避免按错误条数疯狂分配内存。本机历史登录账号不可能有这么多。
static const int kMaxAccountCount = 1000;

/**
 *  账号记录的磁盘布局 —— 即加姓名字段之前的 LOGIN_ACCOUNT_INFO，文件主体
 *  里逐条存的就是它。老版本客户端按这个布局读写，故它<u>不得</u>随产品的
 *  LOGIN_ACCOUNT_INFO 一起演进，改动它会读坏老用户已有的文件。
 *  姓名不在这里，单独存在文件末尾的姓名尾块中。
 */
struct LOGIN_ACCOUNT_RECORD
{
	TCHAR szUser[32];		// 用户名
	TCHAR szPwd[32];		// 密码
	int nStatus;			// 登录状态
	BOOL bRememberPwd;		// 记住密码
	BOOL bAutoLogin;		// 自动登录
};

CLoginAccountList::CLoginAccountList(void)
{
	m_nLastLoginUser = 0;
}

CLoginAccountList::~CLoginAccountList(void)
{
	Clear();
}

// 加载登录帐号列表文件
BOOL CLoginAccountList::LoadFile(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	FILE* fp = _tfopen(lpszFileName, _T("rb"));
	if (NULL == fp)
		return FALSE;

	int nCount = 0;
	if (_fread(&nCount, sizeof(nCount), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}

	// 条数不合理（文件损坏 / 根本不是账号文件）时直接放弃，
	// 免得按天文数字的条数开循环分配内存。
	if (nCount < 0 || nCount > kMaxAccountCount)
	{
		fclose(fp);
		return FALSE;
	}

	if (_fread(&m_nLastLoginUser, sizeof(m_nLastLoginUser), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}

	int nLoaded = ReadAccountRecords(fp, nCount);

	// 记录之后若还跟着姓名尾块（新版本客户端写的），把姓名补进各条账号；
	// 老版本客户端写的文件到此就结束了，姓名留空。
	ReadNickNameSection(fp, nLoaded);

	// 姓名补齐之后再清理空账号 —— 尾块按位置对应主体记录，先删会让姓名错位。
	PurgeBlankAccounts();

	// 修复之前的客户端写下的、没勾"记住密码"却带着密码的记录，读入时清掉密码。
	ClearUnrememberedPasswords();

	fclose(fp);
	return TRUE;
}

// 清空没勾"记住密码"的账号里残留的密码
void CLoginAccountList::ClearUnrememberedPasswords()
{
	for (size_t i = 0; i < m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && !lpAccount->bRememberPwd)
			memset(lpAccount->szPwd, 0, sizeof(lpAccount->szPwd));
	}
}

// 丢弃用户名为空的账号记录，并同步修正最后登录索引
void CLoginAccountList::PurgeBlankAccounts()
{
	// 从后往前遍历：删除某一项不会改变它前面各项的下标，循环变量因而不必回退。
	for (int i = (int)m_arrLoginAccount.size() - 1; i >= 0; i--)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && lpAccount->szUser[0] != _T('\0'))
			continue;

		if (lpAccount != NULL)
			delete lpAccount;
		m_arrLoginAccount.erase(m_arrLoginAccount.begin() + i);

		// 修正最后登录索引，口径与 Del 一致：被删的排在它前面则整体前移一位；
		// 被删的正是它指向的那条，则退回第一个账号 —— 真正的最后登录账号已
		// 无从考证，让登录窗至少有个存在的账号可预填。
		if (m_nLastLoginUser > i)
			m_nLastLoginUser--;
		else if (m_nLastLoginUser == i)
			m_nLastLoginUser = 0;
	}

	// 兜底夹取：删到列表变短（含删空）时下标不能越界。列表空了取 0 也无妨，
	// GetAccountInfo 会因越界返回 NULL，调用方本就要判空。
	if (m_nLastLoginUser < 0 || m_nLastLoginUser >= (int)m_arrLoginAccount.size())
		m_nLastLoginUser = 0;
}

// 按条数读取账号记录并追加到列表，返回实际读出的条数
int CLoginAccountList::ReadAccountRecords(FILE* fp, int nCount)
{
	int nLoaded = 0;
	for (int i = 0; i < nCount; i++)
	{
		LOGIN_ACCOUNT_RECORD stRecord;
		memset(&stRecord, 0, sizeof(stRecord));
		if (_fread(&stRecord, sizeof(stRecord), 1, fp) != 1)
		{
			// 文件被截断 —— 读到哪算哪，前面读出的账号仍然可用。
			break;
		}

		LOGIN_ACCOUNT_INFO* lpAccount = new LOGIN_ACCOUNT_INFO;
		if (NULL == lpAccount)
			break;

		// 先清零 —— 记录里没有姓名，它要么由后面的姓名尾块填上，
		// 要么保持空串，绝不能是随机内存。
		memset(lpAccount, 0, sizeof(LOGIN_ACCOUNT_INFO));
		memcpy(lpAccount->szUser, stRecord.szUser, sizeof(stRecord.szUser));
		memcpy(lpAccount->szPwd, stRecord.szPwd, sizeof(stRecord.szPwd));
		lpAccount->nStatus = stRecord.nStatus;
		lpAccount->bRememberPwd = stRecord.bRememberPwd;
		lpAccount->bAutoLogin = stRecord.bAutoLogin;

		// 防御：文件损坏时字符串字段可能缺结束符，补齐后再入列，
		// 避免后续 _tcscmp / 显示时越界。
		lpAccount->szUser[sizeof(lpAccount->szUser)/sizeof(TCHAR) - 1] = _T('\0');
		lpAccount->szPwd[sizeof(lpAccount->szPwd)/sizeof(TCHAR) - 1] = _T('\0');

		m_arrLoginAccount.push_back(lpAccount);
		nLoaded++;
	}
	return nLoaded;
}

// 读取文件末尾的姓名尾块，按顺序填进刚读出的那批账号
void CLoginAccountList::ReadNickNameSection(FILE* fp, int nLoaded)
{
	if (nLoaded <= 0)
		return;

	// 没有尾块（老版本客户端写的文件）—— 读到文件末尾，姓名保持空串。
	int nMagic = 0;
	if (_fread(&nMagic, sizeof(nMagic), 1, fp) != 1)
		return;

	if (nMagic != kNickNameSectionMagic)
		return;

	int nVersion = 0;
	if (_fread(&nVersion, sizeof(nVersion), 1, fp) != 1)
		return;

	// 尾块版本不认识（用户装过更新的客户端后又降级）—— 整块忽略，
	// 姓名留空，主体数据不受影响。
	if (nVersion != kNickNameSectionVersion)
		return;

	int nNickNameCount = 0;
	if (_fread(&nNickNameCount, sizeof(nNickNameCount), 1, fp) != 1)
		return;

	// 条数对不上说明文件被别的版本改写过（例如老客户端保存过一次，
	// 账号数变了而尾块没跟着变）—— 宁可不显示姓名，也不能张冠李戴。
	if (nNickNameCount != nLoaded)
		return;

	// 姓名与账号按存入顺序一一对应；尾块紧跟在刚读出的这批账号之后，
	// 故从数组末尾往前数 nLoaded 条即是它们。
	int nBase = (int)m_arrLoginAccount.size() - nLoaded;
	for (int i = 0; i < nLoaded; i++)
	{
		TCHAR szNickName[kLoginAccountNickNameLen] = { 0 };
		if (_fread(szNickName, sizeof(szNickName), 1, fp) != 1)
			return;

		szNickName[kLoginAccountNickNameLen-1] = _T('\0');
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[nBase + i];
		if (lpAccount != NULL)
			_tcscpy(lpAccount->szNickName, szNickName);
	}
}

// 保存登录帐号列表文件
BOOL CLoginAccountList::SaveFile(LPCTSTR lpszFileName)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	int nCount = (int)m_arrLoginAccount.size();
	if (nCount <= 0)
		return FALSE;

	FILE* fp = _tfopen(lpszFileName, _T("wb"));
	if (NULL == fp)
		return FALSE;

	// 文件主体：格式与老版本客户端完全一致，姓名不在这里。
	_fwrite(&nCount, sizeof(nCount), 1, fp);
	_fwrite(&m_nLastLoginUser, sizeof(m_nLastLoginUser), 1, fp);

	int nCount2 = 0;
	for (int i = 0; i < nCount; i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL)
		{
			LOGIN_ACCOUNT_RECORD stRecord;
			memset(&stRecord, 0, sizeof(stRecord));
			memcpy(stRecord.szUser, lpAccount->szUser, sizeof(stRecord.szUser));
			memcpy(stRecord.szPwd, lpAccount->szPwd, sizeof(stRecord.szPwd));
			stRecord.nStatus = lpAccount->nStatus;
			stRecord.bRememberPwd = lpAccount->bRememberPwd;
			stRecord.bAutoLogin = lpAccount->bAutoLogin;

			_fwrite(&stRecord, sizeof(stRecord), 1, fp);
			nCount2++;
		}
	}

	// 姓名尾块：接在主体之后，老版本客户端读到主体末尾就收工，看不见这一段。
	WriteNickNameSection(fp, nCount2);

	// 有条目为空指针被跳过时，回填实际写出的条数。条数是文件的第一个字段，
	// 故 fseek 回文件开头。
	if (nCount2 != nCount)
	{
		fseek(fp, 0, SEEK_SET);
		_fwrite(&nCount2, sizeof(nCount2), 1, fp);
	}

	fclose(fp);
	return TRUE;
}

// 在文件末尾写出姓名尾块
void CLoginAccountList::WriteNickNameSection(FILE* fp, int nCount)
{
	if (nCount <= 0)
		return;

	_fwrite(&kNickNameSectionMagic, sizeof(kNickNameSectionMagic), 1, fp);
	_fwrite(&kNickNameSectionVersion, sizeof(kNickNameSectionVersion), 1, fp);
	_fwrite(&nCount, sizeof(nCount), 1, fp);

	// 姓名逐条定长写出，顺序与主体里的账号记录一一对应。
	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (NULL == lpAccount)
			continue;

		TCHAR szNickName[kLoginAccountNickNameLen] = { 0 };
		_tcsncpy(szNickName, lpAccount->szNickName, kLoginAccountNickNameLen);
		szNickName[kLoginAccountNickNameLen-1] = _T('\0');
		_fwrite(szNickName, sizeof(szNickName), 1, fp);
	}
}

// 添加帐号
BOOL CLoginAccountList::Add(LPCTSTR lpszUser, LPCTSTR lpszPwd, 
							int nStatus, BOOL bRememberPwd, BOOL bAutoLogin)
{
	// 用户名为空的账号一律不收 —— 这种记录在登录窗的账号下拉框里是一个空白行，
	// 一旦又被设成最后登录账号，登录窗启动时便预填出空账号。调用方传进空串通常
	// 意味着它那边的账号信息还没填好，属于上游的问题，在此挡住不让其落盘。
	if (NULL == lpszUser || _T('\0') == *lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = Find(lpszUser);
	if (lpAccount != NULL)
		return FALSE;

	lpAccount = new LOGIN_ACCOUNT_INFO;
	if (NULL == lpAccount)
		return FALSE;

	// 先整体清零 —— 不记住密码时不会写 szPwd，姓名也要等登录成功后才由
	// SetNickName 填入，两者都必须是空串而非未初始化内存（否则会被原样
	// 写进 .dat 文件）。
	memset(lpAccount, 0, sizeof(LOGIN_ACCOUNT_INFO));

	int nCount = sizeof(lpAccount->szUser)/sizeof(TCHAR);
	_tcsncpy(lpAccount->szUser, lpszUser, nCount);
	lpAccount->szUser[nCount-1] = _T('\0');

	if (bRememberPwd && lpszPwd != NULL)
	{
		nCount = sizeof(lpAccount->szPwd)/sizeof(TCHAR);
		_tcsncpy(lpAccount->szPwd, lpszPwd, nCount);
		lpAccount->szPwd[nCount-1] = _T('\0');
	}

	lpAccount->nStatus = nStatus;
	lpAccount->bRememberPwd = bRememberPwd;
	lpAccount->bAutoLogin = bAutoLogin;
	m_arrLoginAccount.push_back(lpAccount);
	return TRUE;
}

// 删除帐号
BOOL CLoginAccountList::Del(int nIndex)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size())
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[nIndex];
	if (lpAccount != NULL)
		delete lpAccount;
	m_arrLoginAccount.erase(m_arrLoginAccount.begin()+nIndex);

	// 修正"最后登录账号"。m_nLastLoginUser 存的是<u>下标</u>而不是账号名，
	// 删掉一项之后它前面的项整体前移，不跟着调整就会指向另一个人 ——
	// 表现为下次打开登录窗，自动填的是刚才没删的那个别的账号。
	if (m_nLastLoginUser > nIndex)
	{
		// 被删的排在它前面：整体前移一位。
		--m_nLastLoginUser;
	}
	else if (m_nLastLoginUser == nIndex)
	{
		// 被删的正是"上次登录"的那个：退回第一个账号。
		m_nLastLoginUser = 0;
	}

	// 兜底夹取：删到列表变短（含删空）时，下标不能越界。列表空了取 0 也无妨，
	// GetAccountInfo 会因越界返回 NULL，调用方本就要判空。
	if (m_nLastLoginUser < 0 || m_nLastLoginUser >= (int)m_arrLoginAccount.size())
	{
		m_nLastLoginUser = 0;
	}

	return TRUE;
}

// 修改帐号
BOOL CLoginAccountList::Modify(int nIndex, LPCTSTR lpszUser, LPCTSTR lpszPwd, 
							   int nStatus, BOOL bRememberPwd, BOOL bAutoLogin)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size() || NULL == lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[nIndex];
	if (NULL == lpAccount)
		return FALSE;

	int nCount = sizeof(lpAccount->szUser)/sizeof(TCHAR);
	_tcsncpy(lpAccount->szUser, lpszUser, nCount);
	lpAccount->szUser[nCount-1] = _T('\0');

	if (bRememberPwd && lpszPwd != NULL)
	{
		nCount = sizeof(lpAccount->szPwd)/sizeof(TCHAR);
		_tcsncpy(lpAccount->szPwd, lpszPwd, nCount);
		lpAccount->szPwd[nCount-1] = _T('\0');
	}

	lpAccount->nStatus = nStatus;
	lpAccount->bRememberPwd = bRememberPwd;
	lpAccount->bAutoLogin = bAutoLogin;
	return TRUE;
}

// 更新指定帐号的姓名(昵称)
BOOL CLoginAccountList::SetNickName(LPCTSTR lpszUser, LPCTSTR lpszNickName)
{
	if (NULL == lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = Find(lpszUser);
	if (NULL == lpAccount)
		return FALSE;

	if (NULL == lpszNickName)
	{
		// 显式清空 —— 服务器没给昵称时，登录窗回退显示账号。
		lpAccount->szNickName[0] = _T('\0');
		return TRUE;
	}

	_tcsncpy(lpAccount->szNickName, lpszNickName, kLoginAccountNickNameLen);
	lpAccount->szNickName[kLoginAccountNickNameLen-1] = _T('\0');
	return TRUE;
}

BOOL CLoginAccountList::RecordLoginSuccess(const LOGIN_ACCOUNT_INFO* lpInfo)
{
	if (NULL == lpInfo || _T('\0') == lpInfo->szUser[0])
		return FALSE;

	//新账号：Add 本身就只在勾了"记住密码"时保存密码
	LOGIN_ACCOUNT_INFO* lpAccount = Find(lpInfo->szUser);
	if (NULL == lpAccount)
	{
		return Add(lpInfo->szUser, lpInfo->szPwd, lpInfo->nStatus,
			lpInfo->bRememberPwd, lpInfo->bAutoLogin);
	}

	//已有账号：逐项更新，不整条覆盖 —— 登录窗采集的信息里没有姓名，
	//整条覆盖会把已存的姓名清空
	const size_t nPwdCapacity = sizeof(lpAccount->szPwd) / sizeof(TCHAR);
	if (lpInfo->bRememberPwd)
	{
		_tcsncpy(lpAccount->szPwd, lpInfo->szPwd, nPwdCapacity);
		lpAccount->szPwd[nPwdCapacity - 1] = _T('\0');
	}
	else
	{
		//没勾"记住密码"：不保存本次密码，此前存下的密码也一并清空
		memset(lpAccount->szPwd, 0, sizeof(lpAccount->szPwd));
	}
	lpAccount->nStatus = lpInfo->nStatus;
	lpAccount->bRememberPwd = lpInfo->bRememberPwd;
	lpAccount->bAutoLogin = lpInfo->bAutoLogin;
	return TRUE;
}

BOOL CLoginAccountList::ApplyAutoLoginSetting(LPCTSTR lpszUser, BOOL bAutoLogin, LPCTSTR lpszPwd)
{
	if (NULL == lpszUser)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount = Find(lpszUser);
	if (NULL == lpAccount)
		return FALSE;

	//关闭：只取消自动登录，"记住密码"与已存的密码保持原样，
	//下次打开登录窗仍能回填密码，只是不再自动开始登录
	if (!bAutoLogin)
	{
		lpAccount->bAutoLogin = FALSE;
		return TRUE;
	}

	//打开：自动登录依赖已存的密码，故同时打开"记住密码"；
	//传入的密码为空时保留已存的密码，不以空串覆盖
	lpAccount->bAutoLogin = TRUE;
	lpAccount->bRememberPwd = TRUE;
	if (lpszPwd != NULL && lpszPwd[0] != _T('\0'))
	{
		const size_t nPwdCapacity = sizeof(lpAccount->szPwd) / sizeof(TCHAR);
		_tcsncpy(lpAccount->szPwd, lpszPwd, nPwdCapacity);
		lpAccount->szPwd[nPwdCapacity - 1] = _T('\0');
	}
	return TRUE;
}

// 清除所有帐号
void CLoginAccountList::Clear()
{
	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL)
			delete lpAccount;
	}
	m_arrLoginAccount.clear();
}

// 获取帐号总数
int CLoginAccountList::GetCount()
{
	return (int)m_arrLoginAccount.size();
}

// 获取帐号信息
LOGIN_ACCOUNT_INFO* CLoginAccountList::GetAccountInfo(int nIndex)
{
	if (nIndex < 0 || nIndex >= (int)m_arrLoginAccount.size())
		return NULL;

	return m_arrLoginAccount[nIndex];
}

// 查找帐号
LOGIN_ACCOUNT_INFO* CLoginAccountList::Find(LPCTSTR lpszUser)
{
	if (NULL == lpszUser)
		return NULL;

	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && _tcscmp(lpAccount->szUser, lpszUser) == 0)
			return lpAccount;
	}
	return NULL;
}

int CLoginAccountList::GetLastLoginUser()
{
	return m_nLastLoginUser;
}

LOGIN_ACCOUNT_INFO* CLoginAccountList::GetLastLoginAccountInfo()
{
	return GetAccountInfo(m_nLastLoginUser);
}

BOOL CLoginAccountList::GetLastLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount)
{
	if (NULL == lpAccount)
		return FALSE;

	LOGIN_ACCOUNT_INFO* lpAccount2 = GetAccountInfo(m_nLastLoginUser);
	if (NULL == lpAccount2)
		return FALSE;

	memcpy(lpAccount, lpAccount2, sizeof(LOGIN_ACCOUNT_INFO));
	return TRUE;
}

void CLoginAccountList::SetLastLoginUser(LPCTSTR lpszUser)
{
	if (NULL == lpszUser)
		return;

	for (int i = 0; i < (int)m_arrLoginAccount.size(); i++)
	{
		LOGIN_ACCOUNT_INFO* lpAccount = m_arrLoginAccount[i];
		if (lpAccount != NULL && _tcscmp(lpAccount->szUser, lpszUser) == 0)
		{
			m_nLastLoginUser = i;
			return;
		}
	}
}

BOOL CLoginAccountList::IsAutoLogin()
{
	LOGIN_ACCOUNT_INFO* lpAccount = GetAccountInfo(m_nLastLoginUser);
	return (lpAccount != NULL) ? lpAccount->bAutoLogin : FALSE;
}

// 加密
void CLoginAccountList::Encrypt(char* lpBuf, int nLen)
{
	for (int i = 0; i < nLen; i++)
	{
		lpBuf[i] = lpBuf[i] ^ 0x88;
	}
}

// 解密
void CLoginAccountList::Decrypt(char* lpBuf, int nLen)
{
	for (int i = 0; i < nLen; i++)
	{
		lpBuf[i] = lpBuf[i] ^ 0x88;
	}
}

int CLoginAccountList::_fread(void* lpBuf, int nSize, int nCount, FILE* fp)
{
	//fread 返回实际读到的元素个数，其上限就是形参 nCount（int 类型），
	//因此把 size_t 的返回值收窄为 int 不会丢失数据
	int nRet = static_cast<int>(fread(lpBuf, nSize, nCount, fp));
	Decrypt((char*)lpBuf, nSize*nCount);
	return nRet;
}

int CLoginAccountList::_fwrite(const void* lpBuf, int nSize, int nCount, FILE* fp)
{
	int nLen = nSize*nCount;
	if (nLen > sizeof(LOGIN_ACCOUNT_INFO))
		return 0;
	char cBuf[sizeof(LOGIN_ACCOUNT_INFO)];
	memcpy(cBuf, lpBuf, nLen);
	Encrypt(cBuf, nLen);
	//fwrite 返回实际写入的元素个数，其上限就是形参 nCount（int 类型），
	//因此把 size_t 的返回值收窄为 int 不会丢失数据
	return static_cast<int>(fwrite(cBuf, nSize, nCount, fp));
}