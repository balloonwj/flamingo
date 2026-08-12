/**
 *  flamingoAdmin —— 本地口令保护实现（Windows DPAPI）。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Util/DpapiSecret.h"

#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "Crypt32.lib")

namespace flamingoadmin {

namespace {

//写进 ini 的描述串。DPAPI 会把它连同密文一起存下来，解密时原样带回。
//这里填一个能一眼看出来源的名字，方便日后排查 ini 里这串东西是谁写的。
const wchar_t* const kSecretDescription = L"flamingoAdmin login password";

//Base64 编码格式：不带首尾的 -----BEGIN----- 包装、不插换行。
//ini 的一个键必须是单行，插了换行读回来就是断的。
const DWORD kBase64Flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;

//把宽字符明文转成 UTF-8 字节。DPAPI 加密的是字节流，先定死编码，
//免得将来换了系统区域设置，同一串密码编出不同字节、解出乱码。
bool WideToUtf8Bytes(const CString& text, std::vector<BYTE>& out)
{
    out.clear();
    if (text.IsEmpty())
    {
        return true;
    }

    const int need = ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0)
    {
        return false;
    }

    out.resize(static_cast<size_t>(need));
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(),
                                              reinterpret_cast<char*>(&out[0]), need,
                                              nullptr, nullptr);
    return written == need;
}

//UTF-8 字节转回宽字符明文。
bool Utf8BytesToWide(const BYTE* data, DWORD len, CString& out)
{
    out.Empty();
    if (data == nullptr || len == 0)
    {
        return true;
    }

    const int need = ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data),
                                           static_cast<int>(len), nullptr, 0);
    if (need <= 0)
    {
        return false;
    }

    //GetBufferSetLength 给出可写缓冲；写完必须 ReleaseBuffer 交还长度，
    //否则 CString 的内部长度还是旧值。
    wchar_t* buf = out.GetBufferSetLength(need);
    if (buf == nullptr)
    {
        return false;
    }
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data),
                                              static_cast<int>(len), buf, need);
    out.ReleaseBuffer(written > 0 ? written : 0);
    return written == need;
}

} // anonymous namespace

bool ProtectSecret(const CString& plain, CString& outCipher)
{
    outCipher.Empty();

    //没有密码要存是正常情形（用户没勾"记住密码"，或把密码清空了），不算失败。
    if (plain.IsEmpty())
    {
        return true;
    }

    std::vector<BYTE> plainBytes;
    if (!WideToUtf8Bytes(plain, plainBytes) || plainBytes.empty())
    {
        return false;
    }

    DATA_BLOB in;
    in.pbData = &plainBytes[0];
    in.cbData = static_cast<DWORD>(plainBytes.size());

    DATA_BLOB out = {};
    //不设 entropy、不弹 UI：密钥完全由当前 Windows 账户派生，换用户 / 换机器解不开。
    if (!::CryptProtectData(&in, kSecretDescription, nullptr, nullptr, nullptr, 0, &out))
    {
        return false;
    }

    //密文是二进制，必须 Base64 之后才能写进 ini。先问需要多长，再实际编码。
    bool ok = false;
    DWORD chars = 0;
    if (::CryptBinaryToStringW(out.pbData, out.cbData, kBase64Flags, nullptr, &chars)
        && chars > 0)
    {
        //chars 含结尾的 '\0'，GetBufferSetLength 要的是不含结尾符的长度。
        wchar_t* buf = outCipher.GetBufferSetLength(static_cast<int>(chars));
        if (buf != nullptr)
        {
            DWORD written = chars;
            if (::CryptBinaryToStringW(out.pbData, out.cbData, kBase64Flags, buf, &written))
            {
                outCipher.ReleaseBuffer(static_cast<int>(written));
                ok = true;
            }
            else
            {
                outCipher.ReleaseBuffer(0);
            }
        }
    }

    //CryptProtectData 的输出缓冲由系统分配，无论成败都要还回去。
    if (out.pbData != nullptr)
    {
        ::LocalFree(out.pbData);
    }

    if (!ok)
    {
        outCipher.Empty();
    }
    return ok;
}

bool UnprotectSecret(const CString& cipher, CString& outPlain)
{
    outPlain.Empty();

    //ini 里没存过密码：正常情形，交给调用方按"没记过"处理。
    if (cipher.IsEmpty())
    {
        return true;
    }

    //先把 Base64 解回二进制。串被手工改过 / 截断过，这一步就会失败。
    DWORD bytes = 0;
    if (!::CryptStringToBinaryW(cipher.GetString(), static_cast<DWORD>(cipher.GetLength()),
                                CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr)
        || bytes == 0)
    {
        return false;
    }

    std::vector<BYTE> cipherBytes(static_cast<size_t>(bytes));
    if (!::CryptStringToBinaryW(cipher.GetString(), static_cast<DWORD>(cipher.GetLength()),
                                CRYPT_STRING_BASE64, &cipherBytes[0], &bytes, nullptr, nullptr))
    {
        return false;
    }

    DATA_BLOB in;
    in.pbData = &cipherBytes[0];
    in.cbData = bytes;

    DATA_BLOB out = {};
    //解不开的情形：密文被篡改、换了机器、换了 Windows 用户。一律返回 false，
    //由调用方当成"没记过密码"，让用户重新输入。
    if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
    {
        return false;
    }

    const bool ok = Utf8BytesToWide(out.pbData, out.cbData, outPlain);

    if (out.pbData != nullptr)
    {
        //明文在内存里待过一趟，还回系统之前先抹掉，别留在堆上。
        ::SecureZeroMemory(out.pbData, out.cbData);
        ::LocalFree(out.pbData);
    }

    if (!ok)
    {
        outPlain.Empty();
    }
    return ok;
}

} // namespace flamingoadmin
