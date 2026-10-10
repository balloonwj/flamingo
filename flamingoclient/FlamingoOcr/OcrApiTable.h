/**
 *  FlamingoOcr.dll 导出的函数表：每个导出函数都在这里检查参数、在模块边界上捕获 C++ 异常并转成返回码，
 *  再把真正的工作交给引擎后端（IOcrBackend）。
 *
 *  正式 DLL 由 DllMain.cpp 的 FlamingoOcr_GetApi 装上 Windows.Media.Ocr 后端再返回本函数表；
 *  单元测试直接编译本文件，用 SetOcrBackendForTest 装上假后端。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "FlamingoOcrApi.h"
#include "OcrBackend.h"

namespace flamingoocr {

/**
 *  取函数表。
 *  @return 静态的函数表，永不为 NULL，生命周期与进程相同
 */
const OcrApi* GetOcrApiTable();

/**
 *  装上正式的引擎后端。只在第一次调用时生效（FlamingoOcr_GetApi 每次都会调用）。
 *  @param backend 后端，归调用方所有，须活到进程结束；为 NULL 时忽略
 */
void InstallOcrBackend(IOcrBackend* backend);

/**
 *  测试用：换上指定的后端，并把初始化状态清零（下一次 m_init 会重新调用后端的 Init）。
 *  @param backend 后端，归调用方所有；为 NULL 表示卸掉后端
 *  @return 换上之前的后端
 */
IOcrBackend* SetOcrBackendForTest(IOcrBackend* backend);

} // namespace flamingoocr
