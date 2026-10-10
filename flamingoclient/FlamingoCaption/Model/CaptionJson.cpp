/**
 *  语音字幕模块共用的 JSON 小工具，实现见 CaptionJson.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionJson.h"

#include <stdlib.h>

namespace caption {

namespace {

//UTF-8 BOM 的字节数
const size_t kUtf8BomBytes = 3;

bool HasUtf8Bom(const std::string& text)
{
    return text.size() >= kUtf8BomBytes && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
}

} // namespace

bool ParseJson(const std::string& text, Json::Value& root)
{
    const size_t skip = HasUtf8Bom(text) ? kUtf8BomBytes : 0;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str() + skip, text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

std::string WriteJson(const Json::Value& root, bool pretty)
{
    //jsoncpp 1.9.0 没有 emitUTF8 选项，非 ASCII 字符写成 \uXXXX 转义，读回时还原，内容不受影响
    Json::StreamWriterBuilder wb;
    wb["indentation"] = pretty ? "  " : "";
    return Json::writeString(wb, root);
}

int64_t JsonInt64(const Json::Value& v, int64_t fallback)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isDouble())
        return static_cast<int64_t>(v.asDouble());
    if (v.isString())
        return _strtoi64(v.asCString(), NULL, 10);
    return fallback;
}

std::string JsonString(const Json::Value& v)
{
    if (v.isString())
        return v.asString();
    return std::string();
}

} // namespace caption
