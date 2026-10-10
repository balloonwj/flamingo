/**
 *  FlamingoMarkdown.dll 导出的函数表：每个导出函数都在这里检查参数、在模块边界上捕获 C++ 异常并转成返回码，
 *  再把真正的工作交给 Core/ 下的各模块。
 *
 *  正式 DLL 由 DllMain.cpp 的 FlamingoMarkdown_GetApi 返回本函数表；单元测试直接编译本文件与 Core/ 下的源文件，
 *  不经过 DLL 就能调用同一张函数表。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "FlamingoMarkdownApi.h"

namespace flamingomd {

/**
 *  取函数表。
 *  @return 静态的函数表，永不为 NULL，生命周期与进程相同
 */
const MdApi* GetMdApiTable();

} // namespace flamingomd
