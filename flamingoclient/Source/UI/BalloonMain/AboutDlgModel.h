/**
 *  AboutDlgModel.h
 *
 *  "关于"对话框的<u>纯数据 / 纯逻辑</u>部分 —— 把对话框要展示的固定文案（应用名、
 *  版本号、Build、标语、版权、官网链接）以及"标题栏标题（含进程 id）的拼装"
 *  从 UI 类 CBalloonAboutDlg 里抽出来，做成不依赖 HWND / balloonui / 业务对象的
 *  纯函数与常量，便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  设计稿：design/关于.png（应用名 + 版本/Build + 标语 + 五个功能行 + 版权页脚）。
 *  与设计稿的差异（已与用户确认）：版本号写 2.0.0；对话框里的品牌图标换成 app
 *  自身图标（IDR_MAINFRAME，这部分在 CBalloonAboutDlg 里做，本文件不涉及）。
 *
 *  本文件不 include stdafx.h，工程里须把对应 .cpp 标记 PrecompiledHeader=NotUsing。
 *  只用 std::wstring，刻意不引入 ATL / CString，保证测试工程零额外依赖。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include "../../ClientVersion.h"

#include <string>

namespace balloonmain {

// ====================================================================
// "关于"对话框的固定展示文案（对照 design/关于.png）
// ====================================================================
// 说明：均为宽字符串字面量常量。namespace 作用域下 const/constexpr 变量默认内部
// 链接，故放在头文件里每个翻译单元各持一份、无 ODR 冲突。

// 应用名（品牌区主标题）、标语与页脚版权文字不在本文件定义：三者分别取 flamingo.ini 的
// [ui] apptitle / slogan / copyrighttext（全局变量 g_strAppTitle / g_strAppSlogan /
// g_strAppCopyright，缺省值见 App/BrandText.h）。

// 版本号。此处曾写死 2.0.0（当时确认不取设计稿的 3.2.1），
// 接入自动更新后改为取自 ClientVersion.h：服务端是拿登录上报的
// 版本号判定要不要升级的，关于页又显示另一个数字的话，
// 用户会看到“明明刚升完级，关于页还是旧版本”。
constexpr const wchar_t* kAboutVersion = FLAMINGO_CLIENT_VERSION_W;

// 构建时间戳，形如 20260819-1530（年月日-时分）。仅作展示，不参与任何逻辑：
// 用户报问题时报一句这个数字，就能对上是哪次构建出来的包。
//
// 取值来自编译本模块时的 __DATE__ / __TIME__，发版时不必手工维护。工程给
// AboutDlgModel.cpp 挂了「每次构建先更新其时间戳」的预生成步骤，保证它每次都被
// 重新编译 —— 否则增量编译时本文件没被重编，显示的就会是上一次的时间。
//
// 返回的指针指向进程级的静态字符串，调用方可直接持有。
const wchar_t* AboutBuildStamp();

// 品牌区版本行完整文案，如：版本 2.0.0 (Build 20260819-1530)。
// 版本号取自 ClientVersion.h、构建时间戳取自 AboutBuildStamp，不在本处另写一份 ——
// 关于页显示的版本必须与登录上报的一致，否则升级完用户看到的还是旧号。
std::wstring AboutVersionLine();

// "官方网站"行的<u>默认</u> URL。实际取值改为运行期从 flamingo.ini 的
// [ui] websiteurl 读取（见 CBalloonAboutDlg::LoadWebsiteUrlFromIni），本常量只在
// 配置文件缺该键 / 读不到时兜底，同时也是随包发布的 ini 里写的值。
// 行右侧显示的链接文本就是这个 URL 原文（含协议头），点击打开的也是它。
constexpr const wchar_t* kAboutWebsiteUrl = L"https://github.com/balloonwj/flamingo";

/**
 *  拼装「关于 <产品名称>」—— 系统菜单的「关于」项与关于对话框内提示框的标题都用它。
 *
 *    appName：产品名称（调用方传 g_strAppTitle）；NULL 或空串时按 FlamingoIM 处理。
 *
 *  @return 例：appName=L"FlamingoIM" → L"关于 FlamingoIM"。纯函数、无副作用，可直接单测。
 */
std::wstring BuildAboutCaption(const wchar_t* appName);

/**
 *  拼装"关于"对话框的标题栏标题 —— 形如「关于 FlamingoIM [12345]」，方括号内是
 *  当前进程 id（用户要求标题栏带上进程 id，便于多开 / 排障时区分实例）。
 *
 *    appName：  产品名称（调用方传 g_strAppTitle）；NULL 或空串时按 FlamingoIM 处理。
 *    processId：当前进程 id（调用方传 ::GetCurrentProcessId() 的结果；类型与
 *               Win32 DWORD 一致，这里用 unsigned long 避免在纯单元里依赖 windows.h）。
 *
 *  @return 标题字符串，例：appName=L"FlamingoIM"、processId=12345 → L"关于 FlamingoIM [12345]"。
 *          纯函数、无副作用，可直接单测。
 */
std::wstring BuildAboutTitle(const wchar_t* appName, unsigned long processId);

/**
 *  算功能行（用户协议 / 隐私政策 / 开源许可 / 官方网站）右侧那个链接式按钮该多宽。
 *
 *  这一行的右侧按钮原先是写死宽度的，装不下完整的官网地址；而当时按钮文字按
 *  DT_RIGHT 绘制，宽度不够时削掉的是<u>开头</u>，于是
 *  https://github.com/balloonwj/flamingo 显示成 os://github.com/balloonwj/flamingo，
 *  看着像乱码。官网地址还是运行期从 flamingo.ini 读的，长度不可预知，写死宽度不可能
 *  永远合适，故改为按文字实际宽度撑开。本函数只做那一点钳位算术，不碰任何 UI 对象，
 *  便于单测。
 *
 *    textWidth： 链接文字按<u>真正绘制时用的字体</u>量出来的像素宽度。量宽的字体取错
 *                结果就不准，调用方须与控件实际使用的字体保持一致。无文字传 0。
 *    minWidth：  最小宽度（像素）。文字极短时按它走，保证按钮不至于窄得难以点中。
 *    maxWidth：  上限（像素）。由行宽减去左侧标题至少要占的宽度算出，防止标题被挤没。
 *    pad：       在文字宽度之外额外留的余量（像素），防止不同 DPI / 字体版本下文字
 *                正好卡在边界上被削掉一两个像素。
 *
 *  @return 钳到 [minWidth, maxWidth] 区间内的宽度（像素）。
 *          <u>上限优先</u>：maxWidth 小于 minWidth 这种参数本身矛盾的情况下返回
 *          maxWidth —— 宁可按钮比预期窄（文字由 DT_END_ELLIPSIS 收尾），也不能超出
 *          行宽把左侧标题挤没。纯函数、无副作用。
 */
inline int ComputeLinkButtonWidth(int textWidth, int minWidth, int maxWidth, int pad)
{
    int width = textWidth + pad;

    if (width < minWidth)
    {
        width = minWidth;
    }

    //上限放在下限之后判断，故两者矛盾时以上限为准
    if (width > maxWidth)
    {
        width = maxWidth;
    }

    return width;
}

} // namespace balloonmain
