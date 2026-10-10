/*
 * 所有界面文字放在这里， UIText.h
 * zhangyl  2017.10.24
 */
#ifndef __UITEXT_H__
#define __UITEXT_H__

#include "../Common/I18n/I18n.h"   // TR_NOOP：标记需要翻译的文字（使用处再调 TR）

// 含中文的界面文字一律放在本文件（UTF-8 带 BOM，编译期编码正确），不要再写进
// config/flamingo.ini —— 那个文件经 GetPrivateProfileString 按 ANSI 代码页读取，用 UTF-8
// 写的中文会读成乱码。例外只有 [ui] apptitle、slogan 与 copyrighttext 三项：它们由
// App/BrandText 按字节读取并自动识别 UTF-8 / ANSI，缺省值也定义在那里。
//
// 多语言（docs/多语言方案.md）：已改造的宏写成 TR_NOOP(L"原文")，只做标记、供提取脚本收录，
// 使用处必须再包一层 TR(...) 才会换成当前语言的译文。

#define UI_DEFAULT_MYWEBSITE    _T("http://blog.csdn.net/analogous_love")

// 密码学库初始化失败时的提示。这一步失败意味着协议加密不可用，而服务端开着加密
// 时任何一条链路都连不上，所以只能终止启动、不能带病继续跑。
#define UI_MSG_CRYPTO_INIT_FAILED   TR_NOOP(L"安全组件初始化失败，程序无法启动。请重新安装客户端。")

// 登录窗内就地显示的提示。原先这些是 MessageBox 的内容，改成显示在窗口里之后，
// 用户不必先关掉一个弹窗才能改账号口令重试，账号也不会被清空。
// 措辞上尽量说清"下一步该做什么"，只说"失败了"对使用者没有帮助。
#define UI_LOGIN_STATUS_LOGINING        TR_NOOP(L"正在登录…")
// 登录按钮在两种状态下的文字。登录期间它就是中止按钮——主按钮占满整宽，
// 旁边塞不下第二个按钮，而登录中再点"登录"本来也没有意义。
#define UI_LOGIN_BTN_LOGIN              TR_NOOP(L"登 录")
#define UI_LOGIN_BTN_CANCEL             TR_NOOP(L"取消登录")
#define UI_LOGIN_ERR_NETWORK            TR_NOOP(L"网络故障，登录失败。请检查网络后重试。")
#define UI_LOGIN_ERR_UNREGISTERED       TR_NOOP(L"该账号或手机号未注册。")
#define UI_LOGIN_ERR_PASSWORD           TR_NOOP(L"密码错误，请重新输入。")
#define UI_LOGIN_ERR_REFUSED            TR_NOOP(L"服务器拒绝了登录请求，请联系管理员。")
#define UI_LOGIN_ERR_NO_RESPONSE        TR_NOOP(L"服务器没有响应，请稍后重试。")
#define UI_LOGIN_ERR_VERSION_TOO_OLD    TR_NOOP(L"客户端版本过低，请升级后再登录。")
#define UI_LOGIN_ERR_UNKNOWN            TR_NOOP(L"登录失败，请稍后重试。")
#define UI_LOGIN_CANCELLED              TR_NOOP(L"已取消登录。")

#define UI_COPYRIGHT_TEXT       _T("Flamingo IM 2017 v1.000")
#define UI_COMPANY_LINK         _T("http://www.hootina.org/")


#endif //!__UITEXT_H__