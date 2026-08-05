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

#define UI_COPYRIGHT_TEXT       _T("Flamingo IM 2017 v1.000")
#define UI_COMPANY_LINK         _T("http://www.hootina.org/")
#define UI_DEFAULT_COMPANYNAME  _T("hootina.org 版权所有")


#endif //!__UITEXT_H__