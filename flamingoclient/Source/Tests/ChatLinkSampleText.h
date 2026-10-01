/**
 *  ChatLinkSampleText.h
 *
 *  测试公用：造一段多行文字，每行形如「[第 N 课　标题](https://example.com/s/22 位编号)」，
 *  行间用 \r\n 分隔。结构（行数、每行长度、每行一个链接）与 2026-10-01 用户反馈「发出后界面
 *  卡顿约 9 秒」的那段消息相同，网址换成了示例域名。
 *
 *  使用方：ChatScrollPerfTests.cpp 的 CPF11（耗时基准）、InlineRestyleTests.cpp（换样式重画的
 *  像素一致与工作量用例）。
 *
 *  典型用法：
 *      const CString text = chattest::BuildLinkSampleText(68);
 *
 *  包含本文件之前须已引入 ATL（用到 CString），测试文件先包含 stdafx.h 即可。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stdio.h>
#include <string>

namespace chattest {

// 每行标题从这段文字里截取，长度在 kLinkSampleTitleMinLen 与
// kLinkSampleTitleMinLen + kLinkSampleTitleSpan - 1 之间轮换，与用户那段文字每行标题的长度相近
const wchar_t* const kLinkSampleTitleSource =
    L"远程屏幕采集编码推流与解码显示鼠标键盘输入回传文件管理双栏浏览传输引擎与队列";
const int kLinkSampleTitleMinLen = 8;
const int kLinkSampleTitleSpan = 16;

// 标题起点在标题源文字里轮换的位置数
const int kLinkSampleTitleOffsets = 7;

// 链接路径末段的长度（字符），与用户那段文字里的链接相同
const int kLinkSampleIdLen = 22;

// 生成链接编号时用的两个步长（互质，使各行编号看起来各不相同），取值无特殊含义
const int kLinkSampleIdLineStep = 31;
const int kLinkSampleIdCharStep = 17;

/**
 *  造一段 lines 行、每行带一个链接的文字。
 *    lines：行数；不大于 0 时返回空串。
 *  @return 文字内容；同样的 lines 每次返回完全相同的结果。
 */
inline CString BuildLinkSampleText(int lines)
{
    static const wchar_t kIdChars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const int idCharCount = (int)(sizeof(kIdChars) / sizeof(kIdChars[0])) - 1;
    const std::wstring source(kLinkSampleTitleSource);
    std::wstring text;
    for (int i = 0; i < lines; ++i)
    {
        if (i > 0)
            text += L"\r\n";
        wchar_t head[32] = { 0 };
        swprintf_s(head, L"[第 %d 课\x3000", i + 1);
        text += head;
        const int titleLen = kLinkSampleTitleMinLen + (i % kLinkSampleTitleSpan);
        text += source.substr((size_t)(i % kLinkSampleTitleOffsets), (size_t)titleLen);
        text += L"](https://example.com/s/";
        for (int k = 0; k < kLinkSampleIdLen; ++k)
            text += kIdChars[(i * kLinkSampleIdLineStep + k * kLinkSampleIdCharStep) % idCharCount];
        text += L")";
    }
    return CString(text.c_str());
}

} // namespace chattest
