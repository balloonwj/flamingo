/**
 *  模型资源 DLL（FlamingoCaptionModel.dll）里各资源的编号，CaptionModelRes.h
 *
 *  FlamingoCaptionModel/FlamingoCaptionModel.rc 与本 DLL 的模型加载代码（Engine/CaptionEngine.cpp）共用本文件。
 *  资源类型一律为 RCDATA。本文件会被资源编译器包含，只能写 #define。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

//encoder（ORT 格式，预先做过 EXTENDED 级图优化）
#define IDR_CAPTION_MODEL_ENCODER   101
//decoder（ONNX）
#define IDR_CAPTION_MODEL_DECODER   102
//joiner（ONNX，int8）
#define IDR_CAPTION_MODEL_JOINER    103
//tokens.txt（词表，UTF-8 文本）
#define IDR_CAPTION_MODEL_TOKENS    104
//model_info.txt：模型名称与转换所用的 onnxruntime 版本（UTF-8 文本），写日志用
#define IDR_CAPTION_MODEL_INFO      105
