/**
 *  LockAnimationChoice.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "LockAnimationChoice.h"

#include <string.h>

namespace flamingolock {

namespace {

//五种动画的配置值，下标即 LockAnimation
const char* const kAnimTokens[kAnimCount] = { "honor", "flamingo", "particle", "bounce", "rain" };
const char* const kRandomToken = "random";   // 随机方式的配置值
const char* const kRotateToken = "rotate";   // 轮流播放的配置值
const int kRandomIndex = kAnimCount;         // 下拉框里「随机」的序号
const int kRotateIndex = kAnimCount + 1;     // 下拉框里「轮流播放」的序号

// 按方式与动画拼一个选择
LockAnimationChoice MakeChoice(LockAnimationMode mode, LockAnimation fixed)
{
    LockAnimationChoice c;
    c.m_mode = mode;
    c.m_fixed = fixed;
    return c;
}

} // namespace

LockAnimationChoice DefaultChoice()
{
    return MakeChoice(kAnimModeFixed, kAnimHonor);
}

const char* ChoiceToToken(const LockAnimationChoice& c)
{
    if (c.m_mode == kAnimModeRandom)
    {
        return kRandomToken;
    }
    if (c.m_mode == kAnimModeRotate)
    {
        return kRotateToken;
    }
    if (c.m_fixed >= 0 && c.m_fixed < kAnimCount)
    {
        return kAnimTokens[c.m_fixed];
    }
    return kAnimTokens[kAnimHonor];
}

LockAnimationChoice ChoiceFromToken(const char* token)
{
    if (token == NULL || token[0] == '\0')
    {
        return DefaultChoice();
    }
    if (strcmp(token, kRandomToken) == 0)
    {
        return MakeChoice(kAnimModeRandom, kAnimHonor);
    }
    if (strcmp(token, kRotateToken) == 0)
    {
        return MakeChoice(kAnimModeRotate, kAnimHonor);
    }
    for (int i = 0; i < kAnimCount; ++i)
    {
        if (strcmp(token, kAnimTokens[i]) == 0)
        {
            return MakeChoice(kAnimModeFixed, static_cast<LockAnimation>(i));
        }
    }
    return DefaultChoice();
}

LockAnimationChoice ChoiceAt(int index)
{
    if (index >= 0 && index < kAnimCount)
    {
        return MakeChoice(kAnimModeFixed, static_cast<LockAnimation>(index));
    }
    if (index == kRandomIndex)
    {
        return MakeChoice(kAnimModeRandom, kAnimHonor);
    }
    if (index == kRotateIndex)
    {
        return MakeChoice(kAnimModeRotate, kAnimHonor);
    }
    return DefaultChoice();
}

int ChoiceIndex(const LockAnimationChoice& c)
{
    if (c.m_mode == kAnimModeRandom)
    {
        return kRandomIndex;
    }
    if (c.m_mode == kAnimModeRotate)
    {
        return kRotateIndex;
    }
    if (c.m_fixed >= 0 && c.m_fixed < kAnimCount)
    {
        return static_cast<int>(c.m_fixed);
    }
    return 0;
}

LockAnimation PickInitialAnimation(const LockAnimationChoice& c, uint32_t randomValue)
{
    if (c.m_mode == kAnimModeRandom)
    {
        return static_cast<LockAnimation>(randomValue % kAnimCount);
    }
    if (c.m_mode == kAnimModeRotate)
    {
        return kAnimHonor;
    }
    if (c.m_fixed >= 0 && c.m_fixed < kAnimCount)
    {
        return c.m_fixed;
    }
    return kAnimHonor;
}

LockAnimation NextAnimation(LockAnimation current)
{
    const int next = (static_cast<int>(current) + 1) % kAnimCount;
    return static_cast<LockAnimation>(next < 0 ? 0 : next);
}

} // namespace flamingolock
