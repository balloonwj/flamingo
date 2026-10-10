/**
 *  LockPassword.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include <windows.h>
#include <bcrypt.h>

#include "LockPassword.h"

#pragma comment(lib, "bcrypt.lib")

namespace flamingolock {

namespace {

const wchar_t kFirstPrintableAscii = 0x20;   // 可打印 ASCII 的第一个字符（空格）
const wchar_t kLastPrintableAscii = 0x7E;    // 可打印 ASCII 的最后一个字符（~）
const wchar_t kMaskChar = 0x25CF;            // 密码框显示的圆点 ●

// CNG 返回值是否表示成功（NTSTATUS 非负）
bool NtOk(NTSTATUS status)
{
    return status >= 0;
}

// 按位异或累积后再判断，比较耗时与两边内容无关
bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, int n)
{
    uint8_t diff = 0;
    for (int i = 0; i < n; ++i)
    {
        diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

} // namespace

bool IsAllowedPasswordChar(wchar_t ch)
{
    return ch >= kFirstPrintableAscii && ch <= kLastPrintableAscii;
}

PasswordCheck CheckNewPassword(const std::wstring& password, const std::wstring& confirm)
{
    if (password.empty())
    {
        return kPasswordEmpty;
    }
    for (size_t i = 0; i < password.size(); ++i)
    {
        if (!IsAllowedPasswordChar(password[i]))
        {
            return kPasswordBadChar;
        }
    }
    if ((int)password.size() < kMinPasswordChars)
    {
        return kPasswordTooShort;
    }
    if ((int)password.size() > kMaxPasswordChars)
    {
        return kPasswordTooLong;
    }
    if (password != confirm)
    {
        return kPasswordMismatch;
    }
    return kPasswordOk;
}

// ---------------------------------------------------------------------------
// CLockPasswordVerifier
// ---------------------------------------------------------------------------

CLockPasswordVerifier::CLockPasswordVerifier()
    : m_set(false)
    , m_failures(0)
    , m_cooldownUntilMs(0)
{
    ::SecureZeroMemory(m_salt, sizeof(m_salt));
    ::SecureZeroMemory(m_digest, sizeof(m_digest));
}

CLockPasswordVerifier::~CLockPasswordVerifier()
{
    Clear();
}

bool CLockPasswordVerifier::ComputeDigest(const std::wstring& password, uint8_t* out) const
{
    BCRYPT_ALG_HANDLE alg = NULL;
    if (!NtOk(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)))
    {
        return false;
    }
    DWORD objLen = 0;
    ULONG got = 0;
    bool ok = NtOk(::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                                       sizeof(objLen), &got, 0));
    std::vector<uint8_t> hashObj(ok ? objLen : 0);
    BCRYPT_HASH_HANDLE hash = NULL;
    if (ok)
    {
        ok = NtOk(::BCryptCreateHash(alg, &hash, hashObj.data(), objLen, NULL, 0, 0));
    }
    if (ok)
    {
        ok = NtOk(::BCryptHashData(hash, const_cast<PUCHAR>(m_salt), kSaltBytes, 0));
    }
    if (ok && !password.empty())
    {
        //密码按 UTF-16LE 字节参与摘要（Windows 上 wchar_t 即 UTF-16LE）
        ok = NtOk(::BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(password.data())),
                                   static_cast<ULONG>(password.size() * sizeof(wchar_t)), 0));
    }
    if (ok)
    {
        ok = NtOk(::BCryptFinishHash(hash, out, kDigestBytes, 0));
    }
    if (hash != NULL)
    {
        ::BCryptDestroyHash(hash);
    }
    if (!hashObj.empty())
    {
        ::SecureZeroMemory(hashObj.data(), hashObj.size());
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool CLockPasswordVerifier::Set(const std::wstring& password)
{
    Clear();
    if (!NtOk(::BCryptGenRandom(NULL, m_salt, kSaltBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        Clear();
        return false;
    }
    if (!ComputeDigest(password, m_digest))
    {
        Clear();
        return false;
    }
    m_set = true;
    return true;
}

VerifyResult CLockPasswordVerifier::Verify(const std::wstring& input, uint64_t nowMs)
{
    if (!m_set)
    {
        return kVerifyNotSet;
    }
    if (CooldownRemainingMs(nowMs) > 0)
    {
        return kVerifyCoolingDown;
    }
    m_cooldownUntilMs = 0;

    uint8_t digest[kDigestBytes];
    bool ok = ComputeDigest(input, digest) && ConstantTimeEqual(digest, m_digest, kDigestBytes);
    ::SecureZeroMemory(digest, sizeof(digest));
    if (ok)
    {
        m_failures = 0;
        return kVerifyOk;
    }
    ++m_failures;
    if (m_failures >= kMaxPasswordFailures)
    {
        m_failures = 0;
        m_cooldownUntilMs = nowMs + kPasswordCooldownMs;
    }
    return kVerifyWrong;
}

uint32_t CLockPasswordVerifier::CooldownRemainingMs(uint64_t nowMs) const
{
    if (m_cooldownUntilMs == 0 || nowMs >= m_cooldownUntilMs)
    {
        return 0;
    }
    return static_cast<uint32_t>(m_cooldownUntilMs - nowMs);
}

int CLockPasswordVerifier::FailureCount() const
{
    return m_failures;
}

void CLockPasswordVerifier::Clear()
{
    ::SecureZeroMemory(m_salt, sizeof(m_salt));
    ::SecureZeroMemory(m_digest, sizeof(m_digest));
    m_set = false;
    m_failures = 0;
    m_cooldownUntilMs = 0;
}

bool CLockPasswordVerifier::IsSet() const
{
    return m_set;
}

void CLockPasswordVerifier::CopyForTest(std::vector<uint8_t>& salt, std::vector<uint8_t>& digest) const
{
    salt.assign(m_salt, m_salt + kSaltBytes);
    digest.assign(m_digest, m_digest + kDigestBytes);
}

// ---------------------------------------------------------------------------
// CLockPasswordBox
// ---------------------------------------------------------------------------

CLockPasswordBox::CLockPasswordBox()
{
    //预留容量，之后追加不会重新分配（重新分配会把旧明文留在已释放的内存里）
    m_text.reserve(kMaxPasswordChars + 1);
}

CLockPasswordBox::~CLockPasswordBox()
{
    Clear();
}

bool CLockPasswordBox::InsertChar(wchar_t ch)
{
    if (!IsAllowedPasswordChar(ch) || (int)m_text.size() >= kMaxPasswordChars)
    {
        return false;
    }
    m_text.push_back(ch);
    return true;
}

bool CLockPasswordBox::Backspace()
{
    if (m_text.empty())
    {
        return false;
    }
    m_text[m_text.size() - 1] = L'\0';
    m_text.pop_back();
    return true;
}

void CLockPasswordBox::Clear()
{
    if (!m_text.empty())
    {
        ::SecureZeroMemory(&m_text[0], m_text.size() * sizeof(wchar_t));
    }
    m_text.clear();
}

const std::wstring& CLockPasswordBox::Text() const
{
    return m_text;
}

int CLockPasswordBox::Length() const
{
    return static_cast<int>(m_text.size());
}

std::wstring CLockPasswordBox::MaskText() const
{
    return std::wstring(m_text.size(), kMaskChar);
}

} // namespace flamingolock
