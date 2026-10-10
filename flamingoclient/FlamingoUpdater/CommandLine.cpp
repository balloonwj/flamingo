/**
 *  更新器命令行解析的实现，CommandLine.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "CommandLine.h"
#include "../Source/Common/I18n/I18n.h"   // TR / TRF：提示文字的多语言查找

namespace iupdate {

namespace {

//四个带值参数与那个开关参数的字面量，集中在此，免得散在判断里写错。
const wchar_t* const kOptZip    = L"--zip";
const wchar_t* const kOptTarget = L"--target";
const wchar_t* const kOptExec   = L"--exec";
const wchar_t* const kOptPid    = L"--pid";
const wchar_t* const kOptSilent = L"--silent";
//界面语言（多语言第 3c 步），可选的带值参数
const wchar_t* const kOptLang   = L"--lang";

//进程 id 的十进制位数上限。Windows 的进程 id 是 32 位无符号数，十进制最多 10 位；
//超过这个长度的输入一律判为非法，顺带挡住溢出。
const size_t kMaxPidDigits = 10;

//两个宽字符串是否相等。不引入 <cstring>，也避开各编译器对 wcscmp 的差异。
bool EqualsW(const wchar_t* a, const wchar_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }
    while (*a != L'\0' && *b != L'\0')
    {
        if (*a != *b)
        {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

/**
 *  把十进制数字串转成无符号整数。
 *    text： 待转换的字符串
 *    out：  [出参] 转换结果
 *  @return 全部字符都是数字、长度在 1~kMaxPidDigits 之间时返回 true。
 *          空串、含非数字字符、超长一律返回 false —— 与其把 "12ab" 当成 12，
 *          不如判为非法，免得等错进程。
 */
bool ParseUnsigned(const wchar_t* text, unsigned long& out)
{
    if (text == NULL || *text == L'\0')
    {
        return false;
    }

    unsigned long value = 0;
    size_t digits = 0;
    for (const wchar_t* p = text; *p != L'\0'; ++p)
    {
        if (*p < L'0' || *p > L'9')
        {
            return false;
        }
        ++digits;
        if (digits > kMaxPidDigits)
        {
            return false;
        }
        value = value * 10 + static_cast<unsigned long>(*p - L'0');
    }

    out = value;
    return true;
}

} // namespace

bool ParseCommandLine(int argc, wchar_t* const* argv,
                      UpdateArgs& out, std::wstring& errmsg)
{
    out = UpdateArgs();
    errmsg.clear();

    if (argc <= 0 || argv == NULL)
    {
        errmsg = TR(L"命令行参数为空。");
        return false;
    }

    bool pidSeen = false;

    //从 1 开始：argv[0] 是程序自身路径
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* opt = argv[i];
        if (opt == NULL)
        {
            continue;
        }

        //--silent 不带值，先单独处理，免得走到下面的"取下一个参数当值"
        if (EqualsW(opt, kOptSilent))
        {
            out.m_silent = true;
            continue;
        }

        const bool isZip    = EqualsW(opt, kOptZip);
        const bool isTarget = EqualsW(opt, kOptTarget);
        const bool isExec   = EqualsW(opt, kOptExec);
        const bool isPid    = EqualsW(opt, kOptPid);
        const bool isLang   = EqualsW(opt, kOptLang);

        if (!isZip && !isTarget && !isExec && !isPid && !isLang)
        {
            //未知参数：忽略而不报错。将来加新开关时，旧版更新器碰到它不至于罢工。
            continue;
        }

        //带值参数后面必须还有一个参数
        if (i + 1 >= argc || argv[i + 1] == NULL)
        {
            errmsg = TRF(L"参数 {0} 后面缺少取值。", opt);
            return false;
        }

        const wchar_t* value = argv[i + 1];
        ++i;

        if (isZip)
        {
            out.m_zipPath = value;
        }
        else if (isTarget)
        {
            out.m_targetDir = value;
        }
        else if (isExec)
        {
            out.m_execPath = value;
        }
        else if (isLang)
        {
            out.m_language = value;
        }
        else
        {
            if (!ParseUnsigned(value, out.m_pid) || out.m_pid == 0)
            {
                errmsg = TR(L"参数 --pid 必须是正整数。");
                return false;
            }
            pidSeen = true;
        }
    }

    //四个必填项逐个检查，缺哪个就报哪个，方便从日志里直接看出调用方哪里写错了
    if (out.m_zipPath.empty())
    {
        errmsg = TR(L"缺少参数 --zip（升级包路径）。");
        return false;
    }
    if (out.m_targetDir.empty())
    {
        errmsg = TR(L"缺少参数 --target（安装目录）。");
        return false;
    }
    if (out.m_execPath.empty())
    {
        errmsg = TR(L"缺少参数 --exec（要重启的程序）。");
        return false;
    }
    if (!pidSeen)
    {
        errmsg = TR(L"缺少参数 --pid（主进程 id）。");
        return false;
    }

    return true;
}

std::wstring PeekLanguageArg(int argc, wchar_t* const* argv)
{
    if (argc <= 0 || argv == NULL)
    {
        return std::wstring();
    }
    //从 1 开始：argv[0] 是程序自身路径
    for (int i = 1; i < argc; ++i)
    {
        if (EqualsW(argv[i], kOptLang))
        {
            return (i + 1 < argc && argv[i + 1] != NULL) ? std::wstring(argv[i + 1]) : std::wstring();
        }
    }
    return std::wstring();
}

} // namespace iupdate
