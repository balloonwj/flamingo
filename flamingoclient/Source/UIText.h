/*
 * 所有界面文字放在这里， UIText.h
 * zhangyl  2017.10.24
 */
#ifndef __UITEXT_H__
#define __UITEXT_H__

// 含中文的界面文字一律放在本文件（UTF-8 带 BOM，编译期编码正确），不要再写进
// config/flamingo.ini —— 那个文件经 ANSI 版 GetPrivateProfileString 读取，必须保持
// 纯 ASCII，放中文进去会以"锟斤拷"形式显示出来。ini 里只保留 ASCII 值的配置项。
#define UI_APP_TITLE            _T("Flamingo")

#define UI_DEFAULT_MYWEBSITE_TOOLTIP    _T("打开我的博客")
#define UI_DEFAULT_MYWEBSITE    _T("http://blog.csdn.net/analogous_love")

// 带 IM 后缀：原先由 ini 的 aboutdlgtitle 覆盖成"关于Hootina Flamingo IM"，该键因
// 含中文已从 ini 移除，故把后缀并入默认值，保持关于对话框标题不变。
#define UI_ABOUTWND_TITLE       _T("关于Hootina Flamingo IM")

// 密码学库初始化失败时的提示。这一步失败意味着协议加密不可用，而服务端开着加密
// 时任何一条链路都连不上，所以只能终止启动、不能带病继续跑。
#define UI_MSG_CRYPTO_INIT_FAILED   _T("安全组件初始化失败，程序无法启动。请重新安装客户端。")

// 登录窗内就地显示的提示。原先这些是 MessageBox 的内容，改成显示在窗口里之后，
// 用户不必先关掉一个弹窗才能改账号口令重试，账号也不会被清空。
// 措辞上尽量说清"下一步该做什么"，只说"失败了"对使用者没有帮助。
#define UI_LOGIN_STATUS_LOGINING        _T("正在登录…")
// 登录按钮在两种状态下的文字。登录期间它就是中止按钮——主按钮占满整宽，
// 旁边塞不下第二个按钮，而登录中再点"登录"本来也没有意义。
#define UI_LOGIN_BTN_LOGIN              _T("登 录")
#define UI_LOGIN_BTN_CANCEL             _T("取消登录")
#define UI_LOGIN_ERR_NETWORK            _T("网络故障，登录失败。请检查网络后重试。")
#define UI_LOGIN_ERR_UNREGISTERED       _T("该账号未注册。")
#define UI_LOGIN_ERR_PASSWORD           _T("密码错误，请重新输入。")
#define UI_LOGIN_ERR_REFUSED            _T("服务器拒绝了登录请求，请联系管理员。")
#define UI_LOGIN_ERR_NO_RESPONSE        _T("服务器没有响应，请稍后重试。")
#define UI_LOGIN_ERR_VERSION_TOO_OLD    _T("客户端版本过低，请升级后再登录。")
#define UI_LOGIN_ERR_UNKNOWN            _T("登录失败，请稍后重试。")
#define UI_LOGIN_CANCELLED              _T("已取消登录。")

#define UI_COPYRIGHT_TEXT       _T("Flamingo IM 2017 v1.000")
#define UI_COMPANY_LINK         _T("http://www.hootina.org/")
#define UI_DEFAULT_COMPANYNAME  _T("hootina.org 版权所有")


#endif //!__UITEXT_H__