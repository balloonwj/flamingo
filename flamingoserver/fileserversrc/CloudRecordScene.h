/**
 *  文件会话里云端录像场景（scene 6）的运行参数与路径解析，CloudRecordScene.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）：云端录像的上传与下载走 imgserver 的图片端口、复用 FileSession 的分片收发，
 *  但文件不进 imgcachedir、不按内容 MD5 命名，而是放在 cloudrecorddir 下、以「rec_<录像 id>.<后缀>」命名。令牌（scene 为
 *  file_scene_cloud_record）的 md5 字段放的是文件名（点换成下划线，见 utils/CloudRecordCtlMsg.h 的 CloudRecordTokenKey）。
 *  本模块记下录像目录与「上传完成」回调（imgserver 启动时设置一次；fileserver 不设置，scene 6 一律被拒），并把令牌里的键
 *  解析成磁盘路径。fileserver 与 imgserver 共用 FileSession，所以本文件两者都编译。单元测试见 tests/test_cloudrecord.cpp（CRSC 系列）。
 *
 *  典型用法：
 *      imgserver 启动：fileserver::SetCloudRecordScene(cloudRecordDir, &onUploaded);
 *      FileSession 收到 scene 6 的分片：fileserver::ResolveCloudRecordTarget(payload.md5, dir, fileName)
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>

namespace fileserver {

/**
 *  录像文件上传完成的回调（在 I/O 线程上调用，实现方须线程安全）。
 *  @param fileName 录像目录里的文件名，如 rec_42.mp4
 *  @param bytes    文件字节数
 */
typedef void (*CloudRecordUploadedFn)(const std::string& fileName, uint64_t bytes);

/**
 *  设置云端录像场景的运行参数。须在服务开始接受连接之前调用；之后只读。
 *  @param dir        录像目录，末尾带分隔符；为空表示不支持 scene 6
 *  @param onUploaded 上传完成回调；可为 NULL
 */
void SetCloudRecordScene(const std::string& dir, CloudRecordUploadedFn onUploaded);

//是否支持 scene 6（设置过录像目录）
bool CloudRecordSceneEnabled();

/**
 *  把令牌的 md5 字段解析成录像目录与文件名。
 *  @param tokenKey 令牌的 md5 字段，如 rec_42_mp4
 *  @param dir      [出参] 录像目录（末尾带分隔符）
 *  @param fileName [出参] 文件名，如 rec_42.mp4
 *  @return 支持 scene 6 且键是合法的录像文件名时返回 true
 */
bool ResolveCloudRecordTarget(const std::string& tokenKey, std::string& dir, std::string& fileName);

/**
 *  录像目录里的某个文件是否已经完整存在（上传前判断：已经传完的重复上传直接回完成）。
 *  @param path          完整路径
 *  @param expectedBytes 期望大小
 *  @return 文件存在且大小相等返回 true
 */
bool CloudRecordFileComplete(const std::string& path, int64_t expectedBytes);

//上传完成：调用回调（没有设置回调时什么也不做）
void NotifyCloudRecordUploaded(const std::string& fileName, uint64_t bytes);

} // namespace fileserver
