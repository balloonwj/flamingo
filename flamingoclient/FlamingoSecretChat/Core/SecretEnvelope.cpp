/**
 *  密聊帧的明文信封与图片分块的实现，见 SecretEnvelope.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretEnvelope.h"

#include <string.h>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "SecretCrypto.h"

namespace secretchat {

namespace {

//信封头：类型 1 字节 + JSON 长度 2 字节
const size_t kEnvelopeHeaderBytes = 3;

//图片块数的上限：10 MB / 48 KB 向上取整为 214，留余量
const uint32_t kMaxImageChunks = 1024;

//图片总字节数上限（与接口头的 kMaxImageBytes 一致）
const uint32_t kMaxAssembledBytes = 10 * 1024 * 1024;

//字节的位宽，用于拼两字节长度
const int kBitsPerByte = 8;

//单字节掩码
const uint32_t kByteMask = 0xFF;

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取非负 32 位整数字段
bool getUInt(const Json::Value& root, const char* key, uint32_t& out)
{
    if (!root.isMember(key) || !root[key].isUInt())
        return false;
    out = root[key].asUInt();
    return true;
}

} // namespace

bool EncodeEnvelope(const SecretEnvelope& env, std::string& out)
{
    out.clear();
    Json::Value v(Json::objectValue);
    v["id"] = env.id;
    switch (env.type)
    {
    //文字：编号、正文与发送时刻
    case envelope_text:
        v["text"] = env.text;
        v["ts"] = static_cast<Json::Int64>(env.ts);
        break;

    //图片块：块序号与整张图片的元数据
    case envelope_image_chunk:
        v["i"] = env.chunkIndex;
        v["n"] = env.chunkCount;
        v["size"] = env.totalBytes;
        v["w"] = env.width;
        v["h"] = env.height;
        v["hash"] = ToBase64(reinterpret_cast<const unsigned char*>(env.hash.data()), env.hash.size());
        break;

    //不认识的类型：不编
    default:
        return false;
    }
    const std::string json = writeCompact(v);
    if (json.size() > kEnvelopeMaxJsonBytes)
        return false;
    out.reserve(kEnvelopeHeaderBytes + json.size() + env.payload.size());
    out.push_back(static_cast<char>(env.type));
    out.push_back(static_cast<char>((json.size() >> kBitsPerByte) & kByteMask));
    out.push_back(static_cast<char>(json.size() & kByteMask));
    out += json;
    if (env.type == envelope_image_chunk)
        out += env.payload;
    return true;
}

bool DecodeEnvelope(const std::string& in, SecretEnvelope& env)
{
    env = SecretEnvelope();
    if (in.size() < kEnvelopeHeaderBytes)
        return false;
    const int type = static_cast<unsigned char>(in[0]);
    const size_t jsonLen = (static_cast<size_t>(static_cast<unsigned char>(in[1])) << kBitsPerByte)
                           | static_cast<size_t>(static_cast<unsigned char>(in[2]));
    if (in.size() < kEnvelopeHeaderBytes + jsonLen)
        return false;
    Json::Value v;
    if (!parseObject(in.substr(kEnvelopeHeaderBytes, jsonLen), v) || !getUInt(v, "id", env.id))
        return false;
    env.type = type;
    switch (type)
    {
    //文字
    case envelope_text:
        if (!v.isMember("text") || !v["text"].isString())
            return false;
        env.text = v["text"].asString();
        if (v.isMember("ts") && v["ts"].isInt64())
            env.ts = v["ts"].asInt64();
        return in.size() == kEnvelopeHeaderBytes + jsonLen;

    //图片块
    case envelope_image_chunk:
    {
        std::vector<unsigned char> hash;
        if (!getUInt(v, "i", env.chunkIndex) || !getUInt(v, "n", env.chunkCount) || !getUInt(v, "size", env.totalBytes)
            || !getUInt(v, "w", env.width) || !getUInt(v, "h", env.height) || !v.isMember("hash")
            || !v["hash"].isString() || !FromBase64(v["hash"].asString(), hash) || hash.empty())
            return false;
        env.hash.assign(reinterpret_cast<const char*>(&hash[0]), hash.size());
        env.payload = in.substr(kEnvelopeHeaderBytes + jsonLen);
        return true;
    }

    //不认识的类型（以后的版本可能新增）：解不出
    default:
        return false;
    }
}

uint32_t ImageChunkCount(uint32_t totalBytes)
{
    if (totalBytes == 0)
        return 0;
    return (totalBytes + kImageChunkBytes - 1) / kImageChunkBytes;
}

void SplitImage(uint32_t imageId, const std::string& bytes, uint32_t width, uint32_t height,
                std::vector<SecretEnvelope>& out)
{
    out.clear();
    const uint32_t total = static_cast<uint32_t>(bytes.size());
    const uint32_t count = ImageChunkCount(total);
    const std::string hash = Hash32(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        SecretEnvelope env;
        env.type = envelope_image_chunk;
        env.id = imageId;
        env.chunkIndex = i;
        env.chunkCount = count;
        env.totalBytes = total;
        env.width = width;
        env.height = height;
        env.hash = hash;
        const uint32_t offset = i * kImageChunkBytes;
        const uint32_t len = (total - offset < kImageChunkBytes) ? (total - offset) : kImageChunkBytes;
        env.payload = bytes.substr(offset, len);
        out.push_back(env);
    }
}

int CImageAssembler::AddChunk(const SecretEnvelope& env, uint32_t& receivedBytes)
{
    receivedBytes = 0;
    //元数据自洽：块数与总字节数对得上、块序号不越界、这一块的长度正确
    if (env.totalBytes == 0 || env.totalBytes > kMaxAssembledBytes || env.chunkCount == 0
        || env.chunkCount > kMaxImageChunks || env.chunkCount != ImageChunkCount(env.totalBytes)
        || env.chunkIndex >= env.chunkCount)
    {
        m_images.erase(env.id);
        return add_failed;
    }
    const uint32_t offset = env.chunkIndex * kImageChunkBytes;
    const uint32_t expectLen =
        (env.totalBytes - offset < kImageChunkBytes) ? (env.totalBytes - offset) : kImageChunkBytes;
    if (env.payload.size() != expectLen)
    {
        m_images.erase(env.id);
        return add_failed;
    }

    bool first = false;
    std::map<uint32_t, Partial>::iterator it = m_images.find(env.id);
    if (it == m_images.end())
    {
        Partial p;
        p.totalBytes = env.totalBytes;
        p.chunkCount = env.chunkCount;
        p.width = env.width;
        p.height = env.height;
        p.hash = env.hash;
        p.data.assign(env.totalBytes, '\0');
        p.got.assign(env.chunkCount, false);
        it = m_images.insert(std::make_pair(env.id, p)).first;
        first = true;
    }
    Partial& p = it->second;
    if (p.totalBytes != env.totalBytes || p.chunkCount != env.chunkCount || p.hash != env.hash)
    {
        m_images.erase(it);
        return add_failed;
    }
    if (p.got[env.chunkIndex] || p.complete)
    {
        receivedBytes = p.received;
        return add_duplicate;
    }
    memcpy(&p.data[offset], env.payload.data(), env.payload.size());
    p.got[env.chunkIndex] = true;
    p.received += static_cast<uint32_t>(env.payload.size());
    receivedBytes = p.received;
    if (p.received < p.totalBytes)
        return first ? add_first_chunk : add_progress;

    //收齐：校验摘要
    if (Hash32(reinterpret_cast<const unsigned char*>(p.data.data()), p.data.size()) != p.hash)
    {
        m_images.erase(it);
        return add_failed;
    }
    p.complete = true;
    return add_complete;
}

bool CImageAssembler::Take(uint32_t imageId, std::string& bytes, uint32_t& width, uint32_t& height)
{
    bytes.clear();
    std::map<uint32_t, Partial>::iterator it = m_images.find(imageId);
    if (it == m_images.end() || !it->second.complete)
        return false;
    bytes.swap(it->second.data);
    width = it->second.width;
    height = it->second.height;
    m_images.erase(it);
    return true;
}

std::vector<uint32_t> CImageAssembler::PendingIds() const
{
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, Partial>::const_iterator it = m_images.begin(); it != m_images.end(); ++it)
        ids.push_back(it->first);
    return ids;
}

void CImageAssembler::Clear()
{
    for (std::map<uint32_t, Partial>::iterator it = m_images.begin(); it != m_images.end(); ++it)
    {
        if (!it->second.data.empty())
            memset(&it->second.data[0], 0, it->second.data.size());
    }
    m_images.clear();
}

} // namespace secretchat
