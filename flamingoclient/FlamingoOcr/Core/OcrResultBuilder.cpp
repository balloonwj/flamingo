/**
 *  识别结果的组装与释放，用法见 OcrResultBuilder.h。
 *
 *  内存块布局（每一段的起点都按指针大小对齐）：
 *
 *      [校验头 BlockHeader][OcrResult][OcrLine × 行数][OcrWord × 词总数][各字符串，wchar_t，以 0 结尾]
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrResultBuilder.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

namespace flamingoocr {

namespace {

//校验头里的标记值：块有效时为它，释放时清零，用来识别外来指针与重复释放
const uint32_t kBlockMagic = 0x5243434F;   //字节序上读作 "OCCR"

//单个结果允许的最大字节数（256 MiB），超出视为内存不足，防止异常数据导致的超大分配
const size_t kMaxBlockBytes = 256u * 1024u * 1024u;

//内存块开头的校验头
struct BlockHeader
{
    uint32_t m_magic;        //kBlockMagic 表示有效
    uint32_t m_reserved;     //保留，填 0
    size_t   m_totalBytes;   //整块的字节数（含本头）
};

//仍未释放的块数，供测试检查泄漏
volatile long g_liveBlocks = 0;

//把 offset 向上对齐到指针大小
size_t AlignUp(size_t offset)
{
    const size_t align = sizeof(void*);
    return (offset + align - 1) / align * align;
}

//一个字符串（含结尾 0）占的字节数
size_t StringBytes(const std::wstring& s)
{
    return (s.size() + 1) * sizeof(wchar_t);
}

//把字符串拷到 cursor 处并返回其地址，cursor 前移
const wchar_t* CopyString(const std::wstring& s, char*& cursor)
{
    wchar_t* dst = reinterpret_cast<wchar_t*>(cursor);
    if (!s.empty())
        memcpy(dst, s.c_str(), s.size() * sizeof(wchar_t));
    dst[s.size()] = L'\0';
    cursor += StringBytes(s);
    return dst;
}

//由结果指针反推校验头
BlockHeader* HeaderOf(OcrResult* result)
{
    return reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(result) - AlignUp(sizeof(BlockHeader)));
}

} // namespace

OcrWordData::OcrWordData()
{
    m_rect.m_x = 0;
    m_rect.m_y = 0;
    m_rect.m_w = 0;
    m_rect.m_h = 0;
}

OcrLineData::OcrLineData()
{
    m_rect.m_x = 0;
    m_rect.m_y = 0;
    m_rect.m_w = 0;
    m_rect.m_h = 0;
}

OcrResultData::OcrResultData()
    : m_textAngle(0.0f)
    , m_scalePercent(100)
{
}

OcrResult* BuildOcrResult(const OcrResultData& data)
{
    //先算各段的大小
    size_t wordCount = 0;
    size_t stringBytes = StringBytes(data.m_languageTag);
    for (size_t i = 0; i < data.m_lines.size(); ++i)
    {
        const OcrLineData& line = data.m_lines[i];
        wordCount += line.m_words.size();
        stringBytes += StringBytes(line.m_text);
        for (size_t j = 0; j < line.m_words.size(); ++j)
            stringBytes += StringBytes(line.m_words[j].m_text);
    }

    const size_t headerBytes = AlignUp(sizeof(BlockHeader));
    const size_t resultBytes = AlignUp(sizeof(OcrResult));
    const size_t linesBytes = AlignUp(data.m_lines.size() * sizeof(OcrLine));
    const size_t wordsBytes = AlignUp(wordCount * sizeof(OcrWord));
    const size_t totalBytes = headerBytes + resultBytes + linesBytes + wordsBytes + stringBytes;
    if (totalBytes > kMaxBlockBytes || data.m_lines.size() > 0xFFFFFFFFu)
        return NULL;

    char* block = static_cast<char*>(malloc(totalBytes));
    if (block == NULL)
        return NULL;
    memset(block, 0, totalBytes);

    BlockHeader* header = reinterpret_cast<BlockHeader*>(block);
    header->m_magic = kBlockMagic;
    header->m_reserved = 0;
    header->m_totalBytes = totalBytes;

    OcrResult* result = reinterpret_cast<OcrResult*>(block + headerBytes);
    OcrLine* lines = reinterpret_cast<OcrLine*>(block + headerBytes + resultBytes);
    OcrWord* words = reinterpret_cast<OcrWord*>(block + headerBytes + resultBytes + linesBytes);
    char* cursor = block + headerBytes + resultBytes + linesBytes + wordsBytes;

    result->m_cbSize = sizeof(OcrResult);
    result->m_lineCount = static_cast<uint32_t>(data.m_lines.size());
    result->m_lines = data.m_lines.empty() ? NULL : lines;
    result->m_languageTag = CopyString(data.m_languageTag, cursor);
    result->m_textAngle = data.m_textAngle;
    result->m_scalePercent = data.m_scalePercent;

    OcrWord* nextWord = words;
    for (size_t i = 0; i < data.m_lines.size(); ++i)
    {
        const OcrLineData& src = data.m_lines[i];
        OcrLine& dst = lines[i];
        dst.m_text = CopyString(src.m_text, cursor);
        dst.m_rect = src.m_rect;
        dst.m_wordCount = static_cast<uint32_t>(src.m_words.size());
        dst.m_words = src.m_words.empty() ? NULL : nextWord;
        for (size_t j = 0; j < src.m_words.size(); ++j)
        {
            nextWord->m_text = CopyString(src.m_words[j].m_text, cursor);
            nextWord->m_rect = src.m_words[j].m_rect;
            ++nextWord;
        }
    }

    ::InterlockedIncrement(&g_liveBlocks);
    return result;
}

bool FreeOcrResult(OcrResult* result)
{
    if (result == NULL)
        return true;

    BlockHeader* header = HeaderOf(result);
    if (header->m_magic != kBlockMagic)
        return false;

    header->m_magic = 0;   //清掉标记，之后再拿同一指针来释放会被拒绝
    free(header);
    ::InterlockedDecrement(&g_liveBlocks);
    return true;
}

long OcrResultLiveCount()
{
    return g_liveBlocks;
}

} // namespace flamingoocr
