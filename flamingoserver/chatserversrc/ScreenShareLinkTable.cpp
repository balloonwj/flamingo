/**
 *  屏幕共享的分享链接码表，职责见 ScreenShareLinkTable.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "ScreenShareLinkTable.h"

#include <string.h>
#include <sodium.h>

namespace {

//链接码的字符表：数字 2~9、去掉 I O 的大写字母、去掉 l o 的小写字母，共 56 个
const char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";
//字符表的长度（不含结尾的 '\0'）
const uint32_t kAlphabetSize = static_cast<uint32_t>(sizeof(kAlphabet) - 1);

//默认的随机数函数：libsodium 的 randombytes_uniform（密码学安全）。sodium_init 可重复调用，已初始化时直接返回
uint32_t sodiumUniform(uint32_t upperBound)
{
    sodium_init();
    return randombytes_uniform(upperBound);
}

} // namespace

//类内初始化的静态常量的类外定义。C++11 规定这类常量一旦被 ODR-use（如 doctest 的 CHECK 按引用取操作数），
//就必须在某个编译单元里有类外定义；MSVC 不要求，g++ 链接时报未定义符号
const size_t ScreenShareLinkTable::kCodeLength;
const int64_t ScreenShareLinkTable::kEndedKeepMs;
const int64_t ScreenShareLinkTable::kResolveWindowMs;
const int ScreenShareLinkTable::kResolveLimitPerWindow;
const int ScreenShareLinkTable::kMaxGenerateTries;

ScreenShareLinkTable::Link::Link()
    : m_noPassword(false)
    , m_ended(false)
    , m_endedAt(0)
{
}

ScreenShareLinkTable::ResolveCounter::ResolveCounter()
    : m_windowStart(0)
    , m_count(0)
{
}

ScreenShareLinkTable::ScreenShareLinkTable()
    : m_random(&sodiumUniform)
{
}

ScreenShareLinkTable::~ScreenShareLinkTable()
{
}

void ScreenShareLinkTable::setRandomFn(RandomFn fn)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_random = (fn != NULL) ? fn : &sodiumUniform;
}

const char* ScreenShareLinkTable::alphabet()
{
    return kAlphabet;
}

bool ScreenShareLinkTable::isValidCode(const std::string& code)
{
    if (code.size() != kCodeLength)
        return false;
    for (size_t i = 0; i < code.size(); ++i)
    {
        //'\0' 也在 kAlphabet 的末尾，须先排除
        if (code[i] == '\0' || strchr(kAlphabet, code[i]) == NULL)
            return false;
    }
    return true;
}

std::string ScreenShareLinkTable::generateLocked()
{
    std::string code;
    for (int tries = 0; tries < kMaxGenerateTries; ++tries)
    {
        code.clear();
        for (size_t i = 0; i < kCodeLength; ++i)
        {
            const uint32_t index = m_random(kAlphabetSize) % kAlphabetSize;
            code.push_back(kAlphabet[index]);
        }
        //与仍保存着的链接码（含已结束、尚在保留期内的）重复时重新生成
        if (m_links.find(code) == m_links.end())
            return code;
    }
    return code;
}

bool ScreenShareLinkTable::dropActiveLocked(const std::string& roomId, bool& outNoPassword)
{
    std::map<std::string, std::string>::iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return false;
    std::map<std::string, Link>::iterator link = m_links.find(it->second);
    if (link != m_links.end())
    {
        outNoPassword = link->second.m_noPassword;
        m_links.erase(link);
    }
    m_byRoom.erase(it);
    return true;
}

bool ScreenShareLinkTable::getOrCreate(const std::string& roomId, std::string& outCode, bool& outNoPassword)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::string>::const_iterator it = m_byRoom.find(roomId);
    if (it != m_byRoom.end())
    {
        std::map<std::string, Link>::const_iterator link = m_links.find(it->second);
        if (link != m_links.end())
        {
            outCode = it->second;
            outNoPassword = link->second.m_noPassword;
            return false;
        }
    }
    const std::string code = generateLocked();
    Link link;
    link.m_roomId = roomId;
    m_links[code] = link;
    m_byRoom[roomId] = code;
    outCode = code;
    outNoPassword = false;
    return true;
}

bool ScreenShareLinkTable::get(const std::string& roomId, std::string& outCode, bool& outNoPassword) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::string>::const_iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return false;
    std::map<std::string, Link>::const_iterator link = m_links.find(it->second);
    if (link == m_links.end())
        return false;
    outCode = it->second;
    outNoPassword = link->second.m_noPassword;
    return true;
}

void ScreenShareLinkTable::reset(const std::string& roomId, std::string& outCode, bool& outNoPassword)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //免输密码的设置沿用旧链接；原来没有链接时为 false
    bool noPassword = false;
    dropActiveLocked(roomId, noPassword);
    const std::string code = generateLocked();
    Link link;
    link.m_roomId = roomId;
    link.m_noPassword = noPassword;
    m_links[code] = link;
    m_byRoom[roomId] = code;
    outCode = code;
    outNoPassword = noPassword;
}

bool ScreenShareLinkTable::disable(const std::string& roomId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    bool noPassword = false;
    return dropActiveLocked(roomId, noPassword);
}

bool ScreenShareLinkTable::setNoPassword(const std::string& roomId, bool on)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::string>::const_iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return false;
    std::map<std::string, Link>::iterator link = m_links.find(it->second);
    if (link == m_links.end())
        return false;
    link->second.m_noPassword = on;
    return true;
}

int ScreenShareLinkTable::resolve(const std::string& code, std::string& outRoomId, bool& outNoPassword) const
{
    if (!isValidCode(code))
        return screenshare_link_invalid;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, Link>::const_iterator it = m_links.find(code);
    if (it == m_links.end())
        return screenshare_link_invalid;
    outRoomId = it->second.m_roomId;
    if (it->second.m_ended)
        return screenshare_link_ended;
    outNoPassword = it->second.m_noPassword;
    return screenshare_link_active;
}

void ScreenShareLinkTable::onRoomEnded(const std::string& roomId, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::string>::iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return;
    std::map<std::string, Link>::iterator link = m_links.find(it->second);
    if (link != m_links.end())
    {
        link->second.m_ended = true;
        link->second.m_endedAt = now;
    }
    //房间已经没有「有效链接」了；已结束的链接码留在 m_links 里供解析，到期由 reap 清除
    m_byRoom.erase(it);
}

void ScreenShareLinkTable::reap(int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, Link>::iterator it = m_links.begin(); it != m_links.end();)
    {
        if (it->second.m_ended && now - it->second.m_endedAt >= kEndedKeepMs)
            it = m_links.erase(it);
        else
            ++it;
    }
    for (std::map<int64_t, ResolveCounter>::iterator it = m_counters.begin(); it != m_counters.end();)
    {
        if (now - it->second.m_windowStart >= kResolveWindowMs)
            it = m_counters.erase(it);
        else
            ++it;
    }
}

bool ScreenShareLinkTable::allowResolve(int64_t userId, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    ResolveCounter& counter = m_counters[userId];
    //新用户或窗口已过期：从本次起重新计数
    if (counter.m_count == 0 || now - counter.m_windowStart >= kResolveWindowMs)
    {
        counter.m_windowStart = now;
        counter.m_count = 0;
    }
    if (counter.m_count >= kResolveLimitPerWindow)
        return false;
    ++counter.m_count;
    return true;
}

size_t ScreenShareLinkTable::size() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_links.size();
}
