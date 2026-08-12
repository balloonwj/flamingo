/**
 *  自动更新的两个纯数据结构，UpdateTaskResult.h
 *
 *  一次"检查更新（可选下载）"的结果，以及下载过程中的进度回报。单独成文件是为了让
 *  <u>纯逻辑层</u>（Update/UpdateViewModel.h）能用上它们而不必跟着引入
 *  UpdateManager.h 里的 <thread> / <mutex> / windows.h —— 那样测试工程编译
 *  UpdateViewModel.cpp 时会因为缺 stdafx.h 里的 Windows 类型而失败。
 *
 *  本文件只依赖 <string> 与 <stdint.h>，任何编译单元都能安全包含。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>

namespace update {

//一次"检查更新（可选下载）"的最终结果。
struct UpdateTaskResult
{
    bool        m_valid;         //本结构是否装了一次真实结果（false 表示还没跑过）
    bool        m_transportOk;   //与服务器的一次往返是否完成（超时 / 未连接为 false）
    int         m_code;          //业务错误码，0 为成功
    bool        m_hasUpdate;     //服务端是否报告有新版本
    bool        m_forced;        //是否强制升级
    std::string m_latestVersion; //服务器最新版本号
    std::string m_releaseNote;   //更新说明

    //服务端给的升级包信息是否齐备（下载令牌 / MD5 / 大小都在）。
    //m_hasUpdate 为 true 但本项为 false，说明发布侧的记录不完整 —— 升级包即便下载
    //完成也无法通过校验，界面须按"无更新"呈现、不提供下载入口，避免用户执行一个
    //必然失败的操作。
    bool        m_packageUsable;
    //升级包字节数，供界面显示"升级包 36.4 MB"。m_packageUsable 为 true 时才有意义。
    //按 64 位存：超过 2GiB 的包用 32 位会溢出成负数。
    int64_t     m_size;

    bool        m_downloaded;    //升级包是否已下载并校验通过
    std::wstring m_localPath;    //下载落地的完整路径（m_downloaded 为 true 时有效）
    std::string m_errmsg;        //失败时的诊断信息，仅供日志

    UpdateTaskResult()
        : m_valid(false), m_transportOk(false), m_code(0)
        , m_hasUpdate(false), m_forced(false)
        , m_packageUsable(false), m_size(0), m_downloaded(false)
    {
    }
};

//一次下载进度回报。经 FMG_MSG_UPDATE_PROGRESS 的 lParam 从工作线程投给升级窗，
//UI 线程处理后 delete。
//
//刻意用堆上结构体而不是把字节数塞进 wParam / lParam：Win32 配置下这两个参数是
//32 位的，大于 4GiB 的包会被悄悄截断，而本仓库四个配置都要能编。
struct UpdateProgressInfo
{
    int64_t m_received;   //已接收并落盘的字节数
    int64_t m_total;      //升级包总字节数；0 表示服务端还没告知

    UpdateProgressInfo()
        : m_received(0), m_total(0)
    {
    }
};

} // namespace update
