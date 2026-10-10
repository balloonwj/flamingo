/**
 *  锁屏临时密码的规则、校验与锁屏界面上密码框的编辑逻辑（纯逻辑，不依赖窗口，可单测）。
 *
 *  · CheckNewPassword：设置临时密码时的规则（4~32 个可打印 ASCII 字符，两次一致）。
 *  · CLockPasswordVerifier：只以「随机盐 + SHA-256 摘要」保存密码，解锁时比对；连续输错 5 次后冷却 30 秒。
 *  · CLockPasswordBox：锁屏界面上自绘密码框的内容（只接受可打印 ASCII，上限 32 个字符，显示为圆点）。
 *
 *  典型用法：
 *      if (CheckNewPassword(pw, confirm) == kPasswordOk) verifier.Set(pw);
 *      ...
 *      box.InsertChar(ch); ...
 *      if (verifier.Verify(box.Text(), nowMs) == kVerifyOk) 解锁;
 *
 *  方案见 docs/锁屏方案.md 第 4.6 节。
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingolock {

const int kMinPasswordChars = 4;         // 临时密码最少字符数
const int kMaxPasswordChars = 32;        // 临时密码最多字符数，也是锁屏密码框的容量
const int kMaxPasswordFailures = 5;      // 连续输错多少次后进入冷却
const uint32_t kPasswordCooldownMs = 30000;   // 冷却时长（毫秒）

/**
 *  设置临时密码时的检查结果。
 */
enum PasswordCheck
{
    kPasswordOk = 0,        // 合规，可以锁屏
    kPasswordEmpty,         // 第一个密码框为空
    kPasswordTooShort,      // 少于 kMinPasswordChars 个字符
    kPasswordTooLong,       // 多于 kMaxPasswordChars 个字符
    kPasswordBadChar,       // 含有可打印 ASCII 以外的字符（中文、全角、控制字符等）
    kPasswordMismatch,      // 两次输入不一致
};

/**
 *  判断一个字符能否出现在临时密码里：可打印 ASCII（0x20 空格 ~ 0x7E）。
 *  锁屏界面的密码框关闭了输入法，只能按键盘输入，所以设置时也只允许这些字符，避免设了打不出来。
 *  @param ch 字符
 *  @return 允许返回 true
 */
bool IsAllowedPasswordChar(wchar_t ch);

/**
 *  检查设置临时密码时两个输入框的内容。先查第一个框（空、字符、长度），再查两次是否一致。
 *  @param password 第一个密码框的内容
 *  @param confirm  第二个密码框（再输一次）的内容
 *  @return 检查结果
 */
PasswordCheck CheckNewPassword(const std::wstring& password, const std::wstring& confirm);

/**
 *  解锁时的比对结果。
 */
enum VerifyResult
{
    kVerifyOk = 0,          // 密码正确，应当解锁
    kVerifyWrong,           // 密码错误（未达到冷却次数）
    kVerifyCoolingDown,     // 处于冷却期，本次没有比对
    kVerifyNotSet,          // 还没有设置密码（或已清除），任何输入都不通过
};

/**
 *  临时密码的保存与比对。只在内存里保存 16 字节随机盐与 32 字节 SHA-256 摘要（对「盐 + 密码的 UTF-16LE 字节」
 *  计算），不保存明文；摘要用 Windows CNG（bcrypt.dll）计算。非线程安全，只在锁屏线程上使用。
 */
class CLockPasswordVerifier
{
public:
    static const int kSaltBytes = 16;     // 随机盐的字节数
    static const int kDigestBytes = 32;   // SHA-256 摘要的字节数

    CLockPasswordVerifier();
    ~CLockPasswordVerifier();

    /**
     *  设置密码：生成新的随机盐并计算摘要，清零错误计数与冷却。
     *  @param password 密码明文（调用方用完后自行清零）
     *  @return 成功返回 true；生成随机数或计算摘要失败时返回 false，并处于「未设置」状态
     */
    bool Set(const std::wstring& password);

    /**
     *  比对一次输入。冷却期内不比对；输错累计到 kMaxPasswordFailures 次时进入 kPasswordCooldownMs 的冷却并清零计数；
     *  输对时清零计数。
     *  @param input 用户输入的明文
     *  @param nowMs 当前时刻（毫秒，单调递增即可，通常是 GetTickCount64）
     *  @return 比对结果
     */
    VerifyResult Verify(const std::wstring& input, uint64_t nowMs);

    /**
     *  冷却还剩多久。
     *  @param nowMs 当前时刻（毫秒，与 Verify 用同一时钟）
     *  @return 剩余毫秒数；不在冷却期返回 0
     */
    uint32_t CooldownRemainingMs(uint64_t nowMs) const;

    /**
     *  当前连续输错的次数（冷却开始时归零）。
     */
    int FailureCount() const;

    /**
     *  清除盐、摘要与计数（解锁后调用），之后 Verify 一律返回 kVerifyNotSet。
     */
    void Clear();

    /**
     *  是否已设置密码。
     */
    bool IsSet() const;

    /**
     *  取出盐与摘要的副本，只供单元测试检查「同一密码两次设置的盐与摘要不同」。
     *  @param salt   接收盐（kSaltBytes 字节）
     *  @param digest 接收摘要（kDigestBytes 字节）
     */
    void CopyForTest(std::vector<uint8_t>& salt, std::vector<uint8_t>& digest) const;

private:
    // 对「m_salt + password 的 UTF-16LE 字节」计算 SHA-256，写入 out（kDigestBytes 字节）；失败返回 false
    bool ComputeDigest(const std::wstring& password, uint8_t* out) const;

private:
    uint8_t  m_salt[kSaltBytes];       // 随机盐，Set 时生成，Clear 时清零
    uint8_t  m_digest[kDigestBytes];   // 摘要，Set 时计算，Clear 时清零
    bool     m_set;                    // 是否已设置密码
    int      m_failures;               // 连续输错次数（进入冷却时归零）
    uint64_t m_cooldownUntilMs;        // 冷却结束的时刻（毫秒）；0 表示不在冷却
};

/**
 *  锁屏界面上自绘密码框的内容。只接受可打印 ASCII，最多 kMaxPasswordChars 个字符；显示时每个字符画一个圆点。
 *  清空与析构时把内容清零，避免明文残留在内存里。
 */
class CLockPasswordBox
{
public:
    CLockPasswordBox();
    ~CLockPasswordBox();

    /**
     *  在末尾追加一个字符（来自 WM_CHAR）。
     *  @param ch 字符
     *  @return 追加了返回 true；字符不允许或已满时返回 false
     */
    bool InsertChar(wchar_t ch);

    /**
     *  删除最后一个字符（退格键）。
     *  @return 删除了返回 true；已经为空时返回 false
     */
    bool Backspace();

    /**
     *  清空内容并把原有字符清零。
     */
    void Clear();

    /**
     *  当前内容（明文），只用于交给 CLockPasswordVerifier 比对。
     */
    const std::wstring& Text() const;

    /**
     *  当前字符数。
     */
    int Length() const;

    /**
     *  显示用的文字：与字符数相同个数的圆点（U+25CF）。
     */
    std::wstring MaskText() const;

private:
    std::wstring m_text;   // 已输入的明文；容量预留 kMaxPasswordChars，追加时不重新分配，清空时逐字清零
};

} // namespace flamingolock
