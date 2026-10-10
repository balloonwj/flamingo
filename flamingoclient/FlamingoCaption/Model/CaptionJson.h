/**
 *  语音字幕模块共用的 JSON 小工具（基于 Source/third-party 里的 jsoncpp 1.9.0），CaptionJson.h
 *
 *  替换表、字幕草稿都以 JSON 存盘；这里集中放解析与序列化，避免各处重复。
 *
 *  典型用法：
 *      Json::Value root;
 *      if (caption::ParseJson(text, root)) { ... }
 *      std::string s = caption::WriteJson(root, true);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace caption {

/**
 *  解析 JSON 文本（UTF-8，可带 BOM）。
 *  @return 解析成功返回 true；失败时 root 内容未定义
 */
bool ParseJson(const std::string& text, Json::Value& root);

/**
 *  序列化为 JSON 文本（UTF-8，不带 BOM）。
 *  @param pretty 为 true 时带缩进与换行（存盘，便于人工查看），否则紧凑输出
 */
std::string WriteJson(const Json::Value& root, bool pretty);

//取整型字段：整型直接取，字符串按十进制解析，其余返回 fallback
int64_t JsonInt64(const Json::Value& v, int64_t fallback);

//取字符串字段（UTF-8）：不是字符串时返回空串
std::string JsonString(const Json::Value& v);

} // namespace caption
