/**
 *  flamingoAdmin —— 敏感词管理页的纯文本逻辑实现，说明见 SensitiveWordText.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Util/SensitiveWordText.h"

namespace flamingoadmin { namespace swtext {

namespace {

// UTF-8 BOM 的三个字节与长度
const unsigned char kUtf8Bom[] = { 0xEF, 0xBB, 0xBF };
const size_t kUtf8BomLen = sizeof(kUtf8Bom);

// UTF-16 BOM 的长度（FF FE 或 FE FF）
const size_t kUtf16BomLen = 2;

// GBK 对应的 Windows 代码页
const UINT kCodePageGbk = 936;

// 全角空格 U+3000 的 UTF-8 编码与长度，与服务端 SensitiveWordManager 的 kIdeographicSpace 相同
const char kIdeographicSpace[] = "\xE3\x80\x80";
const size_t kIdeographicSpaceLen = sizeof(kIdeographicSpace) - 1;

// 各分类的中文名，下标为分类取值减一；与服务端 SensitiveWordManager 的 kCategoryNames 一致，
// 导出文件里写的、导入时识别的都是这几个名字
const wchar_t* const kCategoryNames[kCategoryCount] =
{
    L"违法违规",   // 1
    L"色情低俗",   // 2
    L"攻击辱骂",   // 3
    L"商业机密",   // 4
    L"广告引流",   // 5
    L"其他",       // 6
};

// 各拦截来源的中文名，下标为来源取值减一；取值见服务端 SensitiveWordDefs.h 的 sensitive_hit_source
const wchar_t* const kHitSourceNames[kHitSourceCount] =
{
    L"文字消息",     // 1 文字消息正文与引用块
    L"转发",         // 2 逐条转发与合并转发
    L"文件名",       // 3 文件消息与申请上传
    L"卡片与接龙",   // 4 投票 / 接龙 / 待办卡片与参与接龙
    L"个人资料",     // 5 改个人资料
    L"群资料",       // 6 建群与改群资料
    L"群名片",       // 7 群名片
    L"加群申请",     // 8 加群申请的验证消息
    L"禁言原因",     // 9 禁言原因
    L"文件夹名",     // 10 单聊发文件夹
    L"自助注册",     // 11 自助注册时的昵称
    L"群公告",       // 12 群公告的标题与正文
    L"屏幕共享",     // 13 屏幕共享的标题与描述（第 15 期）
    L"群备忘",       // 14 群备忘的标题与备注
    L"文件共享",     // 15 文件共享的共享名称
    L"云端录像",     // 16 云端录像的标题（屏幕共享与录制方案 12.6）
    L"群问答",       // 17 群问答的问题与答案（docs/群问答方案.md）
    L"帖子",         // 18 帖子群的帖子标题、正文与楼主补充（docs/帖子群方案.md）
    L"帖子留言",     // 19 帖子群的留言与回复（docs/帖子群方案.md）
    L"机器人配置",   // 20 群主、员工给群机器人与个人机器人填写的名称、简介、关键词与回复文字（docs/机器人方案.md）
    L"机器人外部内容", // 21 外部系统经 Webhook 推来的内容与呼叫外部服务后收到的回复（docs/机器人方案.md §5.6）
};

// 按代码页把一段多字节文字解码成宽字符串。
//   data / len：待解码的字节；len 为 0 时返回空串。
//   codePage：代码页（CP_UTF8 或 kCodePageGbk）。
CString DecodeMultiByte(const char* data, size_t len, UINT codePage)
{
    CString out;
    if (len == 0)
        return out;

    const int srcLen = static_cast<int>(len);
    const int wideLen = ::MultiByteToWideChar(codePage, 0, data, srcLen, NULL, 0);
    if (wideLen <= 0)
        return out;
    wchar_t* buf = out.GetBuffer(wideLen);
    ::MultiByteToWideChar(codePage, 0, data, srcLen, buf, wideLen);
    out.ReleaseBuffer(wideLen);
    return out;
}

// 判断一段字节是否整段都是合法的 UTF-8：MB_ERR_INVALID_CHARS 下遇到非法序列转换会失败。
bool IsValidUtf8(const std::string& bytes)
{
    if (bytes.empty())
        return true;
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                 static_cast<int>(bytes.size()), NULL, 0) > 0;
}

// 从 offset 起按 UTF-16 解码；字节数为奇数时最后一个字节舍去。
//   bigEndian：true 按大端，false 按小端。
CString DecodeUtf16(const std::string& bytes, size_t offset, bool bigEndian)
{
    CString out;
    const size_t charCount = (bytes.size() - offset) / 2;
    for (size_t i = 0; i < charCount; ++i)
    {
        const unsigned int first  = static_cast<unsigned char>(bytes[offset + i * 2]);
        const unsigned int second = static_cast<unsigned char>(bytes[offset + i * 2 + 1]);
        const unsigned int unit = bigEndian ? ((first << 8) | second) : ((second << 8) | first);
        out.AppendChar(static_cast<wchar_t>(unit));
    }
    return out;
}

// 判断 bytes 是否以 prefix 开头。
bool StartsWith(const std::string& bytes, const unsigned char* prefix, size_t prefixLen)
{
    if (bytes.size() < prefixLen)
        return false;
    for (size_t i = 0; i < prefixLen; ++i)
    {
        if (static_cast<unsigned char>(bytes[i]) != prefix[i])
            return false;
    }
    return true;
}

// 宽字符串转 UTF-8。
std::string Utf8FromWide(const wchar_t* text, int len)
{
    std::string out;
    if (len <= 0)
        return out;
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text, len, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return out;
    out.resize(static_cast<size_t>(bytes));
    ::WideCharToMultiByte(CP_UTF8, 0, text, len, &out[0], bytes, NULL, NULL);
    return out;
}

// 判断一个字节是否是 ASCII 空白，口径与服务端 SensitiveWordManager 的 IsAsciiSpace 相同。
bool IsAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

} // namespace

CString DecodeImportFile(const std::string& bytes, TextEncoding* detected)
{
    const unsigned char kUtf16LeBom[] = { 0xFF, 0xFE };
    const unsigned char kUtf16BeBom[] = { 0xFE, 0xFF };

    TextEncoding encoding = TextEncoding::Utf8;
    CString text;
    if (StartsWith(bytes, kUtf8Bom, kUtf8BomLen))
    {
        encoding = TextEncoding::Utf8Bom;
        text = DecodeMultiByte(bytes.data() + kUtf8BomLen, bytes.size() - kUtf8BomLen, CP_UTF8);
    }
    else if (StartsWith(bytes, kUtf16LeBom, kUtf16BomLen))
    {
        encoding = TextEncoding::Utf16Le;
        text = DecodeUtf16(bytes, kUtf16BomLen, false);
    }
    else if (StartsWith(bytes, kUtf16BeBom, kUtf16BomLen))
    {
        encoding = TextEncoding::Utf16Be;
        text = DecodeUtf16(bytes, kUtf16BomLen, true);
    }
    else if (IsValidUtf8(bytes))
    {
        encoding = TextEncoding::Utf8;
        text = DecodeMultiByte(bytes.data(), bytes.size(), CP_UTF8);
    }
    else
    {
        //不是合法 UTF-8 的无 BOM 文本，多半是中文 Windows 上记事本以「ANSI」保存的文件
        encoding = TextEncoding::Gbk;
        text = DecodeMultiByte(bytes.data(), bytes.size(), kCodePageGbk);
    }

    if (detected != NULL)
        *detected = encoding;
    return text;
}

std::vector<std::string> SplitLines(const CString& text)
{
    std::vector<std::string> lines;
    const int len = text.GetLength();
    const wchar_t* data = text.GetString();
    int start = 0;
    int i = 0;
    while (i < len)
    {
        const wchar_t c = data[i];
        if (c != L'\r' && c != L'\n')
        {
            ++i;
            continue;
        }

        lines.push_back(Utf8FromWide(data + start, i - start));
        //\r\n 算一个换行
        if (c == L'\r' && i + 1 < len && data[i + 1] == L'\n')
            ++i;
        ++i;
        start = i;
    }
    //最后一行没有换行结尾时也算一行；以换行结尾时 start 已等于 len，不再多出一个空行
    if (start < len)
        lines.push_back(Utf8FromWide(data + start, len - start));
    return lines;
}

bool IsBlankLine(const std::string& line)
{
    size_t i = 0;
    while (i < line.size())
    {
        if (IsAsciiSpace(line[i]))
        {
            ++i;
            continue;
        }
        if (line.compare(i, kIdeographicSpaceLen, kIdeographicSpace) == 0)
        {
            i += kIdeographicSpaceLen;
            continue;
        }
        return false;
    }
    return true;
}

int CountNonBlankLines(const std::vector<std::string>& lines)
{
    int count = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (!IsBlankLine(lines[i]))
            ++count;
    }
    return count;
}

std::vector<ImportBatch> MakeImportBatches(const std::vector<std::string>& lines, int maxLines)
{
    const size_t batchSize = static_cast<size_t>(maxLines > 0 ? maxLines : kImportBatchMaxLines);
    std::vector<ImportBatch> batches;
    for (size_t begin = 0; begin < lines.size(); begin += batchSize)
    {
        const size_t end = (lines.size() - begin > batchSize) ? begin + batchSize : lines.size();
        ImportBatch batch;
        batch.m_firstLine = static_cast<int>(begin) + 1;
        batch.m_lines.assign(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                             lines.begin() + static_cast<std::ptrdiff_t>(end));
        //全是空白行的批次提交了也一条都不会加，不提交；其后批次的首行行号仍按原文计算
        if (CountNonBlankLines(batch.m_lines) == 0)
            continue;
        batches.push_back(batch);
    }
    return batches;
}

std::string BuildExportFileBytes(const std::string& exportUtf8)
{
    std::string out(reinterpret_cast<const char*>(kUtf8Bom), kUtf8BomLen);
    out.reserve(kUtf8BomLen + exportUtf8.size() * 2);
    for (size_t i = 0; i < exportUtf8.size(); ++i)
    {
        const char c = exportUtf8[i];
        //单独的 \n 换成 \r\n；原本就是 \r\n 的保持不变，不会变成 \r\r\n
        if (c == '\n' && (i == 0 || exportUtf8[i - 1] != '\r'))
            out += '\r';
        out += c;
    }
    return out;
}

CString CategoryName(int category)
{
    if (category < 1 || category > kCategoryCount)
        category = kCategoryOther;
    return CString(kCategoryNames[category - 1]);
}

CString HitSourceName(int source)
{
    if (source < 1 || source > kHitSourceCount)
        return CString(L"未知来源");
    return CString(kHitSourceNames[source - 1]);
}

}} // namespace flamingoadmin::swtext
