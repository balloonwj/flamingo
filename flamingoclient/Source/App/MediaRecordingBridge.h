/**
 *  IMediaHost 录像记录方法的实现：把 FlamingoMedia.dll 传来的 MediaRecordingInfo 与 exe 的录像记录本地库
 *  （balloonhistory::CRecordLocalStore）互相转换，MediaRecordingBridge.h
 *
 *  背景：录制管理窗口在 DLL 里，录像记录存在 exe 持有的本账号本地库里（docs/屏幕共享与录制方案.md 第 11.6 节 F9），
 *  DLL 经 IMediaHost 的 AddRecording / ListRecordings / UpdateRecordingPath / RemoveRecording 读写。
 *  CMediaHostAdapter 的这四个方法各自直接转调本文件的同名函数；逻辑放在这里而不是适配器里，是因为适配器依赖主面板、
 *  主连接等一整套对象，无法编进单元测试工程，而本文件只依赖本地库与接口头文件（用例见
 *  Source/Tests/MediaRecordingBridgeTests.cpp，标签 [recordbridge]）。
 *
 *  典型用法（MediaHostAdapter.cpp）：
 *      bool CMediaHostAdapter::AddRecording(const MediaRecordingInfo* info)
 *      {
 *          return balloonapp::AddRecordingToStore(info);
 *      }
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonapp {

/**
 *  见 IMediaHost::AddRecording：把一条录像记录写入本账号的本地库，同一路径已有记录时覆盖。
 *  @param info 录像信息；为 NULL、m_cbSize 不等于 sizeof(MediaRecordingInfo) 或路径为空时拒绝
 *  @return 写入成功返回 true；本地库没有打开时返回 false
 */
bool AddRecordingToStore(const flamingomedia::MediaRecordingInfo* info);

/**
 *  见 IMediaHost::ListRecordings：按录制结束时间从新到旧列出记录。
 *  @param out      [出参] 调用方分配的数组，每个元素的 m_cbSize 须等于 sizeof(MediaRecordingInfo)，
 *                  大小不符的元素不填写；capacity 为 0 时可为 NULL
 *  @param capacity out 的元素个数
 *  @return 记录总数（路径长度达到 kMediaRecordingPathChars 的记录不计入）；本地库没有打开时返回 0
 */
uint32_t ListRecordingsFromStore(flamingomedia::MediaRecordingInfo* out, uint32_t capacity);

/**
 *  见 IMediaHost::UpdateRecordingPath：修改一条记录的路径。
 *  @param id      记录 id
 *  @param newPath 新的完整路径；NULL 或空串时拒绝
 *  @return 修改成功返回 true
 */
bool UpdateRecordingPathInStore(int64_t id, const wchar_t* newPath);

/**
 *  见 IMediaHost::RemoveRecording：删除一条记录（不删文件）。
 *  @param id 记录 id
 *  @return 删掉了一条返回 true
 */
bool RemoveRecordingFromStore(int64_t id);

} // namespace balloonapp
