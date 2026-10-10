/**
 *  品牌文字的读取与解码：产品名称（flamingo.ini 的 [ui] apptitle）、登录窗 / 注册窗 /
 *  关于对话框的标语（[ui] slogan）、关于对话框页脚的版权文字（[ui] copyrighttext）与
 *  注册窗「公司名称」的默认项（[ui] defaultcompany）。部署方改这几项即可把界面上的产品名
 *  换成自己的品牌、把注册的默认公司换成自己的企业。
 *
 *  flamingo.ini 没有 BOM，系统的 GetPrivateProfileStringW 会先按 ANSI 代码页转换再返回，
 *  用 UTF-8 保存的中文读出来就是乱码。本模块改为自己按字节解析这几项，再自动识别编码：
 *  原始字节是合法 UTF-8 就按 UTF-8 解码，否则按系统 ANSI 代码页解码。这样无论用记事本
 *  保存成 UTF-8 还是 ANSI，中文都能正确显示。程序自身用 WritePrivateProfileStringW 回写
 *  其它配置项时不会改动这几行的原始字节（BrandTextTests 的 BR6 用例实测）。
 *
 *  典型用法（启动时读一次，结果存进全局变量供各窗口使用）：
 *      g_strAppTitle  = brandtext::ReadAppTitle(iniPath);
 *      g_strAppSlogan = brandtext::ReadSlogan(iniPath);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

// 产品名称：flamingo.ini 的 [ui] apptitle，未配置时为 FlamingoIM。进程启动时由 Startup.cpp
// 读一次（ReadAppTitle），之后只读。用于各窗口标题、弹窗标题、登录窗与注册窗的品牌大字，
// 以及「关于」「退出」等文字里的产品名。定义在 BrandText.cpp。
extern std::wstring g_strAppTitle;

// 登录窗与注册窗的标语：flamingo.ini 的 [ui] slogan。没有这一项时为内置标语，配置为空时为
// 空串、表示不显示标语。与 g_strAppTitle 同时读取（ReadSlogan），之后只读。定义在 BrandText.cpp。
extern std::wstring g_strAppSlogan;

// 关于对话框页脚的版权文字：flamingo.ini 的 [ui] copyrighttext，没有这一项或值为空白时为
// kDefaultCopyright。与 g_strAppTitle 同时读取（ReadCopyright），之后只读。定义在 BrandText.cpp。
extern std::wstring g_strAppCopyright;

// 注册窗「公司名称」的默认项（默认公司的全称）：flamingo.ini 的 [ui] defaultcompany，没有这一项
// 或值为空白时为 kDefaultCompany。须与服务端 chatserver.conf 的 defaultenterprise 配成同一个值
// （两边各自配置，程序不做核对，由部署的人保证一致）。与 g_strAppTitle 同时读取
// （ReadDefaultCompany），之后只读；读取之前为空串（单元测试不走启动流程，始终如此），使用方
// 须改用 kDefaultCompany，注册模块经 balloonregister::RegDefaultCompany 取值。定义在 BrandText.cpp。
extern std::wstring g_strDefaultCompany;

namespace brandtext {

// 产品名称的缺省值：flamingo.ini 没有 [ui] apptitle、或配置值为空白时使用。
extern const wchar_t* const kDefaultAppTitle;

// 登录窗、注册窗与关于对话框标语的缺省值：flamingo.ini 没有 [ui] slogan 这一项时使用。
// 配置了这一项但值为空时不显示标语，不使用本缺省值。
extern const wchar_t* const kDefaultSlogan;

// 关于对话框页脚版权文字的缺省值：flamingo.ini 没有 [ui] copyrighttext、或值为空白时使用。
extern const wchar_t* const kDefaultCopyright;

// 注册窗默认公司的缺省值 FlamingoIM.org：flamingo.ini 没有 [ui] defaultcompany、或值为空白时
// 使用。与服务端 chatserversrc/EnterpriseManager.h 的 DEFAULT_ENTERPRISE_NAME 相同。
extern const wchar_t* const kDefaultCompany;

/**
 *  在 ini 文件内容中查找某个配置项，取出它的原始字节值。
 *  按系统读取 ini 的惯例解析：节名与键名不区分大小写（仅 ASCII 字母）；值去掉首尾的
 *  空格与制表符，若整体被一对双引号包围则去掉引号；以分号开头的行视为注释；同一个键
 *  出现多次时取第一次出现的值。文件开头的 UTF-8 BOM 会被跳过。
 *  @param fileBytes 整个 ini 文件的原始字节。不得是 UTF-16 编码的内容（调用方应先识别）。
 *  @param section   节名（ASCII），不含方括号，如 "ui"。
 *  @param key       键名（ASCII），如 "apptitle"。
 *  @param outValue  [出参] 找到时填入值的原始字节，未经任何编码转换；未找到时不修改。
 *  @return 找到该配置项（即便值为空）返回 true；没有这一项返回 false。
 */
bool FindIniValueBytes(const std::string& fileBytes, const char* section, const char* key,
                       std::string& outValue);

/**
 *  把配置值的原始字节解码为宽字符串。
 *  @param raw 原始字节。是合法 UTF-8（纯 ASCII 也属于合法 UTF-8）时按 UTF-8 解码，
 *             否则按系统 ANSI 代码页解码。
 *  @return 解码结果；raw 为空时返回空串。
 *
 *  局限：极少数用 ANSI（GBK）保存的中文恰好也是合法的 UTF-8 字节序列，会被误按 UTF-8
 *  解码成别的字。遇到这种情况把 ini 改存成 UTF-8 即可。
 */
std::wstring DecodeIniValue(const std::string& raw);

/**
 *  从 ini 文件读取某个配置项，并按 DecodeIniValue 的规则解码。
 *  @param iniPath  ini 文件的完整路径。
 *  @param section  节名（ASCII）。
 *  @param key      键名（ASCII）。
 *  @param outText  [出参] 找到时填入解码后的值；未找到时不修改。
 *  @return 找到该配置项返回 true；文件不存在、无法读取、或没有这一项时返回 false。
 *
 *  文件以 UTF-16 LE BOM 开头时（被人另存成了 Unicode），改用 GetPrivateProfileStringW
 *  读取，系统对这种文件按 UTF-16 解码，结果本来就是正确的。
 */
bool ReadIniText(const wchar_t* iniPath, const char* section, const char* key, std::wstring& outText);

/**
 *  读取产品名称（[ui] apptitle）。
 *  @param iniPath flamingo.ini 的完整路径。
 *  @return 配置的产品名称；没有这一项、文件读不到、或值为空白时返回 kDefaultAppTitle。
 */
std::wstring ReadAppTitle(const wchar_t* iniPath);

/**
 *  读取登录窗与注册窗的标语（[ui] slogan）。
 *  @param iniPath flamingo.ini 的完整路径。
 *  @return 没有这一项（或文件读不到）时返回 kDefaultSlogan；配置了这一项但值为空时返回
 *          空串，表示不显示标语；否则返回配置的标语。
 */
std::wstring ReadSlogan(const wchar_t* iniPath);

/**
 *  读取关于对话框页脚的版权文字（[ui] copyrighttext）。
 *  @param iniPath flamingo.ini 的完整路径。
 *  @return 配置的版权文字；没有这一项、文件读不到、或值为空白时返回 kDefaultCopyright。
 */
std::wstring ReadCopyright(const wchar_t* iniPath);

/**
 *  读取注册窗「公司名称」的默认项（[ui] defaultcompany）。
 *  @param iniPath flamingo.ini 的完整路径。
 *  @return 去掉首尾空白后的配置值；没有这一项、文件读不到、或值为空白时返回 kDefaultCompany。
 */
std::wstring ReadDefaultCompany(const wchar_t* iniPath);

} // namespace brandtext
