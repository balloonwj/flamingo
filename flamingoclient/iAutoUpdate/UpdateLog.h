/**
 *  更新器的日志，UpdateLog.h
 *
 *  老版本更新器一行日志都不写：升级失败时用户那边只看到"客户端起不来了"，开发这边
 *  拿不到任何线索。而升级过程恰恰是最需要留痕的——它在主程序退出之后运行、覆盖的是
 *  程序自身的文件，出了问题没有第二次机会复现。
 *
 *  日志固定写到 <安装目录>\update\update.log，追加模式，每次运行前后各写一条分隔行。
 *  编码用 UTF-8（带 BOM，方便直接用记事本打开看中文）。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>
#include <windows.h>

namespace iupdate {

/**
 *  更新器日志。进程内单实例，未 Open 时所有写入都是空操作 —— 这样调用方不必到处
 *  判断日志是否可用，日志写不了也绝不该影响升级本身。
 */
class UpdateLog
{
public:
    //取进程内单实例。
    static UpdateLog& Instance();

    /**
     *  打开日志文件（不存在则创建，已存在则追加）。
     *
     *    logDir： 日志所在目录（通常是 <安装目录>\update）；目录不存在会尝试创建
     *
     *  @return 打开成功返回 true。失败不影响后续调用，只是所有写入变成空操作。
     */
    bool Open(const std::wstring& logDir);

    //关闭日志文件。析构时会自动调用，一般不必显式调。
    void Close();

    /**
     *  写一行日志，自动加时间戳前缀与换行。
     *    text： 日志正文（宽字符，内部转 UTF-8 写入）
     */
    void Write(const std::wstring& text);

    /**
     *  写一行带 Windows 错误码的日志，形如「<text>，错误码 5」。
     *    text：     日志正文
     *    lastError： ::GetLastError() 的返回值
     */
    void WriteWithError(const std::wstring& text, DWORD lastError);

private:
    UpdateLog();
    ~UpdateLog();
    UpdateLog(const UpdateLog&) = delete;
    UpdateLog& operator=(const UpdateLog&) = delete;

private:
    //日志文件句柄；INVALID_HANDLE_VALUE 表示未打开，此时所有写入是空操作。
    HANDLE m_file;
};

} // namespace iupdate
