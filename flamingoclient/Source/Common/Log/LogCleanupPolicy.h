/**
 *  LogCleanupPolicy.h
 *
 *  「启动时清空历史日志」这个开关的取值规则与读写收口。
 *
 *  这个开关有两个使用方 —— 启动流程（Startup.cpp，进程刚起来就要按它决定清不清）
 *  与系统设置对话框的存储页（用户在界面上改它）。两边共用本模块，段名 / 键名、
 *  默认值、以及"配置写坏了怎么办"只有一份实现，不会漂移。
 *
 *  为什么不放进 CUserConfig：那份配置是<u>按账号</u>的（<数据根>\Users\<账号>\
 *  <账号>.cfg），而且登录之后才加载；清理日志发生在 WinMain 里、比登录早得多，
 *  那时既没有账号也没有 CUserConfig。所以本项留在机器级的 config\flamingo.ini 里。
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故不走主工程的预编译头。
 *
 *  balloonwj@qq.com   2026-08-17
 */

#pragma once

#include <windows.h>

namespace logcleanup {

//配置项在 flamingo.ini 里的段名与键名。读的一侧（启动流程）与写的一侧（设置对话框）
//必须用同一份，故收口成常量，不允许各处再写字面量。
//键名沿用历史上的 clearexpirelog —— 它其实名不副实（清的是全部历史日志，不是"过期"
//的那些），但改键名会让老配置文件里的用户选择失效，不值当。
extern const wchar_t* const kIniSection;
extern const wchar_t* const kIniKey;

/**
 *  本构建的默认取值 —— 配置项不存在、或者被写成无法识别的内容时采用它。
 *
 *  Debug 构建关闭：开发期要能翻前几次运行的日志，启动就清掉会很碍事。
 *  Release 构建开启：用户机器上日志会一直堆积，没人会去手工清理。
 *
 *  @return 是否在启动时清空历史日志。
 */
bool DefaultClearLogOnStart();

/**
 *  解析从配置文件里读到的原始文本。纯函数、无副作用、不碰磁盘。
 *
 *  识别规则：去掉首尾空白后<u>只认</u>"0"（关）与"1"（开）两种写法；空指针、空串、
 *  以及其它任何内容（"abc"、"-1"、"2"、"true" 等）一律当作"没有有效取值"，回落到
 *  defaultOn。这样配置文件被手工改坏时，行为是可预期的默认值，而不是某个碰巧解析
 *  出来的结果。
 *
 *    rawValue： 配置里读到的原始文本；NULL 或空串表示该配置项不存在。
 *    defaultOn：无有效取值时采用的默认值，正常传 DefaultClearLogOnStart()。
 *  @return 是否在启动时清空历史日志。
 */
bool ParseClearLogOnStart(const wchar_t* rawValue, bool defaultOn);

/**
 *  从 flamingo.ini 读出当前设置。
 *    iniPath：flamingo.ini 的完整路径；为空或文件不存在时按"配置项不存在"处理。
 *  @return 是否在启动时清空历史日志；读不到时为 DefaultClearLogOnStart()。
 */
bool ReadClearLogOnStart(const wchar_t* iniPath);

/**
 *  把设置写回 flamingo.ini。写入的是显式的 "0" / "1"，此后本机不再受构建默认影响。
 *    iniPath：flamingo.ini 的完整路径；为空时直接返回 false。
 *    on：     是否在启动时清空历史日志。
 *  @return 写入成功返回 true；路径为空或系统写入失败时返回 false。
 *  副作用：修改磁盘上的配置文件。
 */
bool WriteClearLogOnStart(const wchar_t* iniPath, bool on);

} // namespace logcleanup
