/**
 *  zip 条目名的安全校验与归一化，ZipPathSafety.h
 *
 *  <u>这是整个更新器最要紧的一道关卡。</u>老版本是直接把 zip 里的条目名拼到安装目录
 *  后面就写盘（`wsprintf(fullpath, TEXT("%s%s"), __main_home__, ze.name)`），条目名里
 *  写 `..\..\Windows\System32\xxx.dll` 就能覆盖到系统目录去 —— 一个被构造过的升级包
 *  可以借更新器往任意位置写文件，而更新器往往还是提权运行的。
 *
 *  本文件把"条目名 → 可安全落盘的相对路径"这一步单独拎出来做成纯函数，不碰文件系统、
 *  不依赖 windows.h，便于用单测把各种穿越写法逐条钉住（Suite ZPS）。
 *
 *  判定规则（任一条命中即拒绝）：
 *    · 空串；
 *    · 以 / 或 \ 开头的绝对路径；
 *    · 带盘符（形如 C:\ 或 C:foo）；
 *    · UNC 路径（以 \\ 开头）与 \\?\ 前缀；
 *    · 任何一段是 ..（无论在开头、中间还是结尾）；
 *    · 归一化后不剩任何有效路径段（例如 "./" 或 "."）。
 *
 *  归一化动作：正斜杠一律转成反斜杠；去掉多余的分隔符与 . 段。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>

namespace iupdate {

/**
 *  校验并归一化一个 zip 条目名。
 *
 *    raw：  zip 中央目录里读出来的原始条目名（已转成宽字符）
 *    out：  [出参] 归一化后的<u>相对</u>路径，以反斜杠分隔，不以分隔符开头或结尾；
 *           返回 false 时置为空串
 *
 *  @return 该条目名可以安全地拼到安装目录之后使用时返回 true；
 *          任何一条判定规则命中都返回 false，调用方必须<u>整包中止</u>而不是跳过这一条
 *          —— 包里出现穿越路径说明这个包不可信，继续解剩下的没有意义。
 *
 *  注意：本函数只管路径形态，不判断目标文件是否存在、能否写入，那是 Installer 的事。
 */
bool NormalizeZipEntryName(const std::wstring& raw, std::wstring& out);

/**
 *  判断条目名是否表示一个目录项。
 *
 *    raw： 原始条目名
 *
 *  @return 以 / 或 \ 结尾即视为目录项。zip 规范里目录项就是这么表示的，
 *          它没有内容，解压时只需建目录。
 */
bool IsDirectoryEntry(const std::wstring& raw);

} // namespace iupdate
