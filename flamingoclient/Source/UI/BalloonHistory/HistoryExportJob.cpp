/**
 *  聊天记录导出增强的执行部分实现，说明见 HistoryExportJob.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "HistoryExportJob.h"

#include <windows.h>
#include <stdio.h>
#include "HistoryExport.h"      // SanitizeExportFileName / UniqueExportFileName
#include "HistoryZipWriter.h"   // 打包为 zip

namespace balloonhistory {

namespace {

//UTF-8 的字节顺序标记：写进文档开头，记事本等程序据此按 UTF-8 打开纯文本
const char kUtf8Bom[] = "\xEF\xBB\xBF";
//时间下拉里与 HistoryTimeRange 一一对应的那几项的个数
const int kTimeRangeItems = 6;
//会话文件夹名整理后为空时用的名字（与 SanitizeExportFileName 的兜底不同，那个是「image」）
const wchar_t kFolderFallback[] = L"chat";

//UTF-16 转 UTF-8
std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, NULL, NULL);
    return out;
}

//把 '/' 换成 '\'（相对路径落到磁盘时用）
std::wstring ToBackslash(const std::wstring& rel)
{
    std::wstring out = rel;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == L'/')
        {
            out[i] = L'\\';
        }
    }
    return out;
}

//逐级建好一个文件所在的目录（已存在的跳过）
void EnsureParentDirs(const std::wstring& filePath)
{
    for (size_t i = 0; i < filePath.size(); ++i)
    {
        if (filePath[i] == L'\\' && i > 0 && filePath[i - 1] != L':')
        {
            ::CreateDirectoryW(filePath.substr(0, i).c_str(), NULL);
        }
    }
}

//写一个文件（覆盖）
bool WriteFileBytes(const std::wstring& path, const std::string& bytes)
{
    EnsureParentDirs(path);
    FILE* f = NULL;
    if (::_wfopen_s(&f, path.c_str(), L"wb") != 0 || f == NULL)
    {
        return false;
    }
    const bool ok = bytes.empty() || ::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = (::fclose(f) == 0);
    return ok && closed;
}

//文档的字节：UTF-8 加字节顺序标记
std::string DocBytes(const std::wstring& text)
{
    return std::string(kUtf8Bom) + ToUtf8(text);
}

//路径的目录部分（含结尾的反斜杠）；没有反斜杠时为空串
std::wstring DirOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash + 1);
}

//目录路径补上结尾的反斜杠
std::wstring WithSlash(const std::wstring& dir)
{
    if (dir.empty() || dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')
    {
        return dir;
    }
    return dir + L"\\";
}

//报告一步进度；返回是否要中止
bool Step(IHistoryExportProgress* progress, int& done, int total)
{
    ++done;
    if (progress == NULL)
    {
        return false;
    }
    progress->OnExportProgress(done, total);
    return progress->ExportCancelled();
}

} // namespace

int ExportRangeToTimeRange(int range)
{
    if (range >= 0 && range < kTimeRangeItems)
    {
        return range;
    }
    return -1;
}

std::wstring ExportFolderName(const std::wstring& name, std::set<std::wstring>& used)
{
    std::wstring base = SanitizeExportFileName(name);
    //SanitizeExportFileName 整理后为空时给的是图片用的兜底名，会话文件夹换成自己的
    if (name.empty() || base == L"image")
    {
        base = kFolderFallback;
    }
    return UniqueExportFileName(base, used);
}

HistoryExportResult RunHistoryExportJob(const HistoryExportJob& job, IHistoryExportProgress* progress)
{
    HistoryExportResult result;
    result.m_target = job.m_target;
    int total = 0;
    for (size_t i = 0; i < job.m_convs.size(); ++i)
    {
        total += 1 + (int)job.m_convs[i].m_attachments.size();
    }
    int done = 0;
    bool ok = !job.m_target.empty() && !job.m_convs.empty();

    //打包为 zip
    if (job.m_zip)
    {
        CHistoryZipWriter zip;
        ok = ok && zip.Open(job.m_target);
        for (size_t i = 0; ok && i < job.m_convs.size(); ++i)
        {
            const HistoryExportConvJob& c = job.m_convs[i];
            const std::string folder = ToUtf8(c.m_folderName) + "/";
            if (!zip.AddData(folder + ToUtf8(c.m_docName), DocBytes(c.m_docText)))
            {
                ok = false;
                break;
            }
            ++result.m_convs;
            result.m_messages += c.m_messageCount;
            if (Step(progress, done, total))
            {
                result.m_cancelled = true;
                ok = false;
                break;
            }
            for (size_t k = 0; k < c.m_attachments.size(); ++k)
            {
                if (zip.AddFile(folder + ToUtf8(c.m_attachments[k].m_relPath), c.m_attachments[k].m_source))
                {
                    ++result.m_attachmentsDone;
                }
                else if (zip.Failed())
                {
                    ok = false;
                    break;
                }
                else
                {
                    //源文件读不了：跳过这一个
                    ++result.m_attachmentsFailed;
                }
                if (Step(progress, done, total))
                {
                    result.m_cancelled = true;
                    ok = false;
                    break;
                }
            }
        }
        const bool closed = zip.Close();
        result.m_tooLarge = zip.TooLarge();
        result.m_ok = ok && closed;
        if (!result.m_ok)
        {
            //写了一半的 zip 打不开，删掉
            ::DeleteFileW(job.m_target.c_str());
        }
        return result;
    }

    //写到磁盘：单个会话写到选定的文档路径，多个会话各占目标文件夹下的一个子文件夹
    for (size_t i = 0; ok && i < job.m_convs.size(); ++i)
    {
        const HistoryExportConvJob& c = job.m_convs[i];
        std::wstring docPath;
        std::wstring baseDir;
        if (job.m_singleFile)
        {
            docPath = job.m_target;
            baseDir = DirOf(job.m_target);
        }
        else
        {
            baseDir = WithSlash(job.m_target) + c.m_folderName + L"\\";
            docPath = baseDir + c.m_docName;
        }
        if (!WriteFileBytes(docPath, DocBytes(c.m_docText)))
        {
            ok = false;
            break;
        }
        ++result.m_convs;
        result.m_messages += c.m_messageCount;
        if (Step(progress, done, total))
        {
            result.m_cancelled = true;
            ok = false;
            break;
        }
        for (size_t k = 0; k < c.m_attachments.size(); ++k)
        {
            const std::wstring target = baseDir + ToBackslash(c.m_attachments[k].m_relPath);
            EnsureParentDirs(target);
            if (::CopyFileW(c.m_attachments[k].m_source.c_str(), target.c_str(), FALSE))
            {
                ++result.m_attachmentsDone;
            }
            else
            {
                ++result.m_attachmentsFailed;
            }
            if (Step(progress, done, total))
            {
                result.m_cancelled = true;
                ok = false;
                break;
            }
        }
    }
    result.m_ok = ok;
    return result;
}

} // namespace balloonhistory
