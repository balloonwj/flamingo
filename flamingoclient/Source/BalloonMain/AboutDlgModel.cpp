/**
 *  AboutDlgModel.cpp
 *
 *  AboutDlgModel.h 里纯函数的实现。只装配字符串，不碰任何窗口 / GDI / 业务对象，
 *  故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。
 *
 *  本文件含中文宽字符字面量（标题前缀），工程已开启 /utf-8 编译选项，源码按 UTF-8
 *  解析，L"中文" 字面量正常，无需 BOM。在 Flamingo.vcxproj 里本文件标
 *  PrecompiledHeader=NotUsing（不 include stdafx.h）。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "AboutDlgModel.h"

namespace balloonmain {

std::wstring BuildAboutTitle(unsigned long processId)
{
    // 形如「关于 FlamingoIM [12345]」：固定前缀「关于 」+ 应用名 +「 [」+ pid +「]」。
    std::wstring title;
    title.reserve(32);
    title += L"关于 ";
    title += kAboutAppName;
    title += L" [";
    title += std::to_wstring(processId);
    title += L"]";
    return title;
}

} // namespace balloonmain
