/**
 *  识别结果的组装与释放：把引擎内部用 STL 表示的结果（OcrResultData）拼成接口里的 OcrResult，
 *  整个结果（结构、各行、各词、全部字符串）放在<u>一整块</u>内存里，宿主只需调用一次 m_freeResult。
 *
 *  内存块前面带一个校验头，FreeOcrResult 据此拒绝释放不是本模块分配的指针，并能发现重复释放。
 *
 *  典型用法：
 *
 *      OcrResultData data;          //引擎填写
 *      OcrResult* result = BuildOcrResult(data);
 *      ...交给宿主...
 *      FreeOcrResult(result);       //宿主经 m_freeResult 调用
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>

#include "../FlamingoOcrApi.h"

namespace flamingoocr {

/**
 *  引擎内部的一个词。
 */
struct OcrWordData
{
    std::wstring m_text;   //词的文字
    OcrRect      m_rect;   //词的外接矩形，输入图像的像素坐标

    OcrWordData();
};

/**
 *  引擎内部的一行。
 */
struct OcrLineData
{
    std::wstring             m_text;    //整行文字（已做拼行整理）
    OcrRect                  m_rect;    //行的外接矩形
    std::vector<OcrWordData> m_words;   //本行的词，从左到右

    OcrLineData();
};

/**
 *  引擎内部的一次识别结果，字段含义与 OcrResult 一一对应。
 */
struct OcrResultData
{
    std::vector<OcrLineData> m_lines;          //各行，按阅读顺序
    std::wstring             m_languageTag;    //实际使用的识别语言
    float                    m_textAngle;      //文字倾斜角（度）
    uint32_t                 m_scalePercent;   //识别前对图像做的缩放，百分比

    OcrResultData();
};

/**
 *  把引擎内部的结果拼成一整块 OcrResult。
 *  @param data 引擎填写的结果
 *  @return 新分配的结果，经 FreeOcrResult 释放；内存不足时返回 NULL
 */
OcrResult* BuildOcrResult(const OcrResultData& data);

/**
 *  释放 BuildOcrResult 分配的结果。
 *  @param result 要释放的结果；NULL 时什么也不做
 *  @return 释放成功或 result 为 NULL 时返回 true；指针不是本模块分配的（或已释放过）时不释放并返回 false
 */
bool FreeOcrResult(OcrResult* result);

/**
 *  当前仍未释放的结果块个数，供单元测试检查泄漏。
 *  @return 已分配未释放的块数
 */
long OcrResultLiveCount();

} // namespace flamingoocr
