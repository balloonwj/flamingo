/**
 *  文字测量缓存的预热，说明见 ChatTextWarmup.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj
 *  两个工程共用，故与 ChatBubble.cpp 一样自己引入 balloonui / ATL 所需的前置依赖。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 ChatBubble.cpp 相同）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>

#include "ChatTextWarmup.h"
#include "ChatBubble.h"
#include "ChatMockData.h"                 // MakeTextMessage
#include "ChatTextExtentCache.h"          // 完成时把缓存条目数写进日志
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/PerfStopWatch.h"

namespace balloonchatwnd {

namespace {

//GB2312 的代码页编号
const UINT kCodePageGb2312 = 936;
//GB2312 一级汉字所在的区：区号（高字节）0xB0 至 0xD7
const int kLevel1FirstRow = 0xB0;
const int kLevel1LastRow  = 0xD7;
//每个区的位号（低字节）0xA1 至 0xFE
const int kCellFirst = 0xA1;
const int kCellLast  = 0xFE;
//一级汉字的最后一个区（0xD7）只用到 0xF9，0xFA 至 0xFE 是空位
const int kLastRowCellLast = 0xF9;

//常用中文标点：中文里它们各自单独成一个折行词元，与汉字一样逐个测量
const wchar_t* const kWarmupPunctuation = L"，。！？、；：“”‘’（）《》【】…—～·";

//预热时气泡测量用的可用宽度（像素）。取值不影响逐字测量的结果，只决定折成几行
const int kWarmupAvailWidth = 620;

} // namespace

std::wstring BuildWarmupCharacters()
{
    std::wstring chars;
    for (int row = kLevel1FirstRow; row <= kLevel1LastRow; ++row)
    {
        const int cellLast = (row == kLevel1LastRow) ? kLastRowCellLast : kCellLast;
        for (int cell = kCellFirst; cell <= cellLast; ++cell)
        {
            char bytes[2];
            bytes[0] = (char)row;
            bytes[1] = (char)cell;
            wchar_t ch = 0;
            if (::MultiByteToWideChar(kCodePageGb2312, MB_ERR_INVALID_CHARS, bytes, 2, &ch, 1) == 1)
                chars += ch;
        }
    }

    //标点放在最后；与汉字不会重复
    chars += kWarmupPunctuation;
    return chars;
}

ChatTextWarmup& ChatTextWarmup::Inst()
{
    static ChatTextWarmup s_inst;
    return s_inst;
}

ChatTextWarmup::ChatTextWarmup()
    : m_nNext(0)
    , m_nTimerId(0)
    , m_bStarted(false)
    , m_nSlices(0)
    , m_dTotalMs(0.0)
{
    m_strChars = BuildWarmupCharacters();
}

void ChatTextWarmup::Start()
{
    if (m_bStarted)
        return;
    m_bStarted = true;

    m_nTimerId = ::SetTimer(NULL, 0, kSliceIntervalMs, OnTimer);
    if (m_nTimerId == 0)
    {
        LOG_WARNING("text extent warmup: SetTimer failed, error=%u; warmup skipped.", ::GetLastError());
        return;
    }
    LOG_INFO("text extent warmup started: %u chars, %d chars per slice, interval %u ms.",
             (unsigned)m_strChars.size(), kSliceChars, kSliceIntervalMs);
}

void ChatTextWarmup::Stop()
{
    if (m_nTimerId != 0)
    {
        ::KillTimer(NULL, m_nTimerId);
        m_nTimerId = 0;
    }
}

bool ChatTextWarmup::RunSlice(int maxChars)
{
    if (maxChars <= 0 || m_nNext >= m_strChars.size())
        return m_nNext < m_strChars.size();

    size_t count = (size_t)maxChars;
    if (count > m_strChars.size() - m_nNext)
        count = m_strChars.size() - m_nNext;

    PerfStopWatch sw;
    //与真实消息走同一条测量路径：新建文字气泡、设消息、按可用宽度测量。
    //中文每个字单独成一个折行词元，标点同样，于是每个字都会被逐个测量并写进缓存
    const std::wstring slice = m_strChars.substr(m_nNext, count);
    ChatBubble bubble;
    bubble.SetMessage(MakeTextMessage(false, CString(slice.c_str())));
    bubble.Measure(kWarmupAvailWidth);
    m_dTotalMs += sw.ElapsedMs();

    m_nNext += count;
    ++m_nSlices;
    return m_nNext < m_strChars.size();
}

size_t ChatTextWarmup::Remaining() const
{
    return m_strChars.size() - m_nNext;
}

bool ChatTextWarmup::IsRunning() const
{
    return m_nTimerId != 0;
}

void ChatTextWarmup::Reset()
{
    Stop();
    m_strChars = BuildWarmupCharacters();
    m_nNext = 0;
    m_bStarted = false;
    m_nSlices = 0;
    m_dTotalMs = 0.0;
}

void CALLBACK ChatTextWarmup::OnTimer(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/)
{
    ChatTextWarmup& self = Inst();
    if (self.RunSlice(kSliceChars))
        return;

    self.Stop();
    LOG_INFO("text extent warmup done: %u chars in %d slices, %.1f ms measuring in total, "
             "cache entries %u.",
             (unsigned)self.m_strChars.size(), self.m_nSlices, self.m_dTotalMs,
             (unsigned)ChatTextExtentCache::Inst().EntryCount());
}

} // namespace balloonchatwnd
