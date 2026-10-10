/**
 *  本机历史登录账号列表 —— 记录每个曾在本机登录成功过的账号，含密码、
 *  登录状态、记住密码 / 自动登录勾选项，以及该账号的姓名(昵称)、userid 与
 *  头像(2026-10-04 起)、服务端返回的规范账号(2026-10-06 起)，持久化到
 *  程序目录\Users\LoginAccountList.dat。登录窗(CBalloonLoginDlg)用它填充账号下拉框
 *  (每行头像、账号、姓名)、切换账号时回填密码与勾选项，并在大头像处画该账号的
 *  头像、下方显示该账号的姓名。
 *
 *  记录的键(szUser)是用户在登录窗里输入的原文：账号与手机号都能登录、账号不区分
 *  大小写(docs/登录账号与手机号登录方案.md)，所以它可能是手机号或大小写不同的账号，
 *  与服务端返回的账号不一定相同。本地数据目录 Users\<账号>\ 按服务端返回的账号建，
 *  记录里另存一份(szAccountName)，删除账号连同本地数据时据它定位目录。
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

// 姓名(昵称)字段可容纳的字符数(含结束符)。服务端昵称上限是 64 个字，写入时按本长度
// 截断(_tcsncpy)，故 64 个字的昵称在登录窗账号列表里只显示前 63 个字符。本长度同时是
// 本地账号文件里每条记录姓名块的定长(见 LoginAccountList.cpp)，改它会改变文件格式。
const int kLoginAccountNickNameLen = 64;

// 头像内容字段可容纳的字节数(UTF-8，含结束符)。协议里 customface 最长 32 字节
// (见 UI/BalloonMain/AvatarSpec.h 文件头)，这里留出一倍余量。
const int kLoginAccountCustomFaceBytes = 64;

// 规范账号字段可容纳的字符数(含结束符)，与 szUser 相同。服务端生成的账号最长为
// 10 个字符的前缀加 8 位数字(docs/登录账号与手机号登录方案.md)，存量账号也都短于此。
// 本长度同时是本地账号文件里规范账号尾块每条的定长，改它会改变文件格式。
const int kLoginAccountNameLen = 32;

struct LOGIN_ACCOUNT_INFO	// 登录账号信息
{
	TCHAR szUser[32];		// 用户在登录窗里输入的登录名(账号或手机号)，是本列表查找记录的键
	TCHAR szPwd[32];		// 密码
	int nStatus;			// 登录状态
	BOOL bRememberPwd;		// 记住密码
	BOOL bAutoLogin;		// 自动登录
	//BOOL bAlreadyLogin;		// 是否已经登录

	// 该账号的姓名(昵称)，来自登录应答的 profile.nickname，登录成功后由
	// 会话控制器写入；空串表示尚未取到(例如老版本存下的账号、或服务器返回
	// 的昵称为空)。登录窗据它在头像下方显示姓名，为空则退回显示账号。
	TCHAR szNickName[kLoginAccountNickNameLen];

	// 该账号的 userid 与头像(来自登录应答的 profile，2026-10-04 起)，登录成功后由
	// 会话控制器写入，登录状态下本人改了头像后同步更新。登录窗据它画账号下拉列表
	// 每行的头像与上方的大头像。nUserId 为 0 表示尚未取到(这次升级之前存下的账号、
	// 或还没在新版本里登录过)，此时登录窗不画该账号的头像。
	long long nUserId;		// 该账号的 userid；0 表示尚未取到
	int nFaceType;			// 头像类型(profile.facetype，取值见 AvatarSpec.h 的 AvatarKind)
	char szCustomFace[kLoginAccountCustomFaceBytes];	// 头像内容(profile.customface 的 UTF-8 原样字节：字形头像为「字形|色槽」，照片为文件 id)

	// 服务端返回的规范账号(登录应答的 profile.username，2026-10-06 起)，登录成功后由会话
	// 控制器写入。用手机号或大小写不同的账号登录时它与 szUser 不同；本地数据目录
	// Users\<账号>\ 按它命名。空串表示尚未取到(这次升级之前存下的记录)，此时按 szUser 处理。
	TCHAR szAccountName[kLoginAccountNameLen];
};

/**
 *  取一条历史账号对应的本地数据目录名，即 Users\<名>\ 中的那一段。
 *    lpInfo：历史账号记录，由调用方持有；可为 NULL。
 *  返回：记录里存有规范账号时返回它，否则(2026-10-06 之前存下的记录)返回 szUser；
 *        lpInfo 为 NULL 时返回空串。返回的指针指向记录内部或静态空串，不得释放。
 */
LPCTSTR LoginAccountDataName(const LOGIN_ACCOUNT_INFO* lpInfo);

/**
 *  已登录期间，取本次登录在历史账号列表里那条记录的键。
 *  登录成功后客户端把本人资料里的账号换成了服务端返回的规范账号，而历史列表的记录
 *  以登录窗里输入的原文为键；两者不同(用手机号或大小写不同的账号登录)时，必须用
 *  原文去查列表，否则退出登录时清不掉「自动登录」、密码错误时忘不掉已存的密码。
 *    lpszLoginName：    本次登录时登录窗里输入的原文；可为 NULL 或空串。
 *    lpszServerAccount：服务端返回的规范账号；可为 NULL 或空串。
 *  返回：lpszLoginName 非空时返回它，否则返回 lpszServerAccount(为 NULL 时返回空串)。
 *        返回的指针即传入的指针之一或静态空串，不得释放。
 */
LPCTSTR PickLoginAccountListKey(LPCTSTR lpszLoginName, LPCTSTR lpszServerAccount);

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
	 *  更新指定账号的 userid 与头像。登录成功拿到 profile 后、以及登录状态下本人
	 *  头像变化后调用，随后由调用方 SaveFile 落盘。
	 *    lpszUser：      账号名，不可为 NULL；账号须已在列表里。
	 *    nUserId：       该账号的 userid(字形头像的自动底色按它取)；0 表示未知。
	 *    nFaceType：     头像类型(profile.facetype)。
	 *    lpszCustomFace：头像内容(profile.customface 的 UTF-8 原样字节)；NULL 视为
	 *                    空串；超过 kLoginAccountCustomFaceBytes - 1 字节时截断。
	 *  返回：账号存在并已更新返回 TRUE；lpszUser 为 NULL 或账号不在列表里返回
	 *        FALSE(此时列表不变)。
	 */
	BOOL SetAvatar(LPCTSTR lpszUser, long long nUserId, int nFaceType, const char* lpszCustomFace);

	/**
	 *  记下指定账号对应的服务端规范账号(登录应答的 profile.username)。登录成功后调用，
	 *  随后由调用方 SaveFile 落盘。
	 *    lpszUser：       历史列表里的键(登录窗里输入的原文)，不可为 NULL；账号须已在列表里。
	 *    lpszAccountName：服务端返回的规范账号；NULL 或空串表示清空。超过
	 *                     kLoginAccountNameLen - 1 个字符时截断。
	 *  返回：账号存在并已更新返回 TRUE；lpszUser 为 NULL 或账号不在列表里返回
	 *        FALSE(此时列表不变)。
	 */
	BOOL SetAccountName(LPCTSTR lpszUser, LPCTSTR lpszAccountName);

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
	 *  读取姓名尾块之后的头像尾块(2026-10-04 起)，按顺序把 userid 与头像填进刚读出
	 *  的那批账号。没有这一段(此前的客户端写的文件)、魔数或版本号对不上、条数与
	 *  账号数不匹配时，都只是让头像保持空，不影响已读出的账号与姓名。
	 *    fp：      已打开的文件，读取位置停在姓名尾块之后；
	 *    nLoaded： 本次刚读出的账号条数，即尾块里应有的条数。
	 */
	void ReadAvatarSection(FILE* fp, int nLoaded);

	/**
	 *  在姓名尾块之后写出头像尾块。nCount <= 0 时什么都不写。
	 *    fp：     已打开的文件，写入位置停在姓名尾块之后；
	 *    nCount： 实际写出的账号条数，须与主体里的记录条数一致。
	 */
	void WriteAvatarSection(FILE* fp, int nCount);

	/**
	 *  读取头像尾块之后的规范账号尾块(2026-10-06 起)，按顺序把规范账号填进刚读出的
	 *  那批账号。没有这一段(此前的客户端写的文件)、魔数或版本号对不上、条数与账号数
	 *  不匹配时，都只是让规范账号保持空串，不影响已读出的其它数据。
	 *    fp：      已打开的文件，读取位置停在头像尾块之后；
	 *    nLoaded： 本次刚读出的账号条数，即尾块里应有的条数。
	 */
	void ReadAccountNameSection(FILE* fp, int nLoaded);

	/**
	 *  在头像尾块之后写出规范账号尾块。nCount <= 0 时什么都不写。
	 *    fp：     已打开的文件，写入位置停在头像尾块之后；
	 *    nCount： 实际写出的账号条数，须与主体里的记录条数一致。
	 */
	void WriteAccountNameSection(FILE* fp, int nCount);

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
