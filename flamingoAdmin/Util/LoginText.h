/**
 *  flamingoAdmin —— 登录页上随状态变化的文字：版本行、账号密码框的回填。
 *
 *  这两处原先都是写死的：版本行写死某个旧日期，本机第一次运行时还预填测试库的账号与口令。
 *  现在由本文件的纯函数按实际状态给出，界面层（Screens/LoginScreen.cpp、AdminFrame）只负责
 *  显示，单元测试见 Tests/LoginTextTests.cpp（LGN 系列）。
 *
 *  典型用法：
 *      versionLabel->SetText(logintext::VersionLine());
 *      logintext::ResolvePrefill(AppConfig::Instance().Login(), account, password);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace flamingoadmin {

struct LoginConfig;

namespace logintext {

// 控制台的版本号，点分十进制。与 PC 客户端同属 2.0 产品线，发版时在这里修改。
const TCHAR* const kAdminVersion = _T("2.0.0");

// 把编译器 __DATE__ 形式的日期（「Mmm dd yyyy」，日不足两位时前面补空格，如「Oct  4 2026」）
// 转成「yyyy.mm.dd」。
//   compilerDate：日期字符串，可为空指针。
// 返回：转换结果；格式不对（长度不足、月份缩写未知、数字位不是数字）或空指针时返回空串。
CString FormatBuildDate(const char* compilerDate);

// 拼出版本行。
//   version：版本号，如「2.0.0」；buildDate：构建日期，如「2026.10.04」，可为空。
// 返回：「v2.0.0 · 构建 2026.10.04」；buildDate 为空时只有「v2.0.0」。
CString MakeVersionLine(const CString& version, const CString& buildDate);

// 登录页左下角实际显示的版本行：kAdminVersion 加本次编译的日期。
// LoginText.cpp 由工程的预生成步骤在每次构建前更新时间戳，保证它总被重新编译，
// 因此这里的日期就是当次构建的日期。
CString VersionLine();

// 按上次保存的登录信息决定账号与密码框的回填内容。
//   saved：AppConfig 读到的登录信息；outAccount / outPassword：输出的回填文字。
// 结果：账号取上次保存的账号（从未登录过时为空）；密码只在勾了「记住密码」时回填，否则为空。
// 不再在从未登录过时预填测试账号。
void ResolvePrefill(const LoginConfig& saved, CString& outAccount, CString& outPassword);

} // namespace logintext

} // namespace flamingoadmin
