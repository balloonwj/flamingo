/**
 *  锁屏画面的选择：五种动画，以及「随机」「轮流播放」两种方式（纯逻辑，可单测）。
 *
 *  选择保存在 config\flamingo.ini 的 [lock] animation（ASCII 配置值），由宿主代为读写。
 *  设置临时密码的窗口里的下拉框按 kChoiceCount 个选项列出，顺序见 ChoiceAt。
 *
 *  典型用法：
 *      LockAnimationChoice c = ChoiceFromToken(token);       // 读配置
 *      LockAnimation a = PickInitialAnimation(c, rand());    // 锁屏开始时播哪一种
 *      if (c.m_mode == kAnimModeRotate && 满 60 秒) a = NextAnimation(a);
 *
 *  方案见 docs/锁屏方案.md 第 6 节第 2 项。
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

namespace flamingolock {

/**
 *  五种锁屏动画。
 */
enum LockAnimation
{
    kAnimHonor = 0,         // 八荣八耻
    kAnimFlamingo,          // 火烈鸟漫步
    kAnimParticle,          // 粒子时钟
    kAnimBounce,            // 弹跳标志与烟花
    kAnimRain,              // 汉字数字雨
    kAnimCount,             // 动画种数，不是一种动画
};

/**
 *  选择方式。
 */
enum LockAnimationMode
{
    kAnimModeFixed = 0,     // 固定播一种
    kAnimModeRandom,        // 每次锁屏随机一种
    kAnimModeRotate,        // 每分钟换下一种，循环播放
};

/**
 *  一个选择：方式，加上固定方式下的那种动画。
 */
struct LockAnimationChoice
{
    LockAnimationMode m_mode;    // 选择方式
    LockAnimation     m_fixed;   // 固定方式下播的动画；其它方式下无意义（取 kAnimHonor）
};

const int kChoiceCount = kAnimCount + 2;      // 下拉框的选项数：五种动画 + 随机 + 轮流播放
const uint32_t kRotateIntervalMs = 60000;     // 轮流播放时每种动画播多久（毫秒）

/**
 *  缺省选择：固定播八荣八耻。
 */
LockAnimationChoice DefaultChoice();

/**
 *  把选择写成配置值："honor" / "flamingo" / "particle" / "bounce" / "rain" / "random" / "rotate"。
 *  @param c 选择
 *  @return 配置值（静态字符串）
 */
const char* ChoiceToToken(const LockAnimationChoice& c);

/**
 *  从配置值读出选择。大小写敏感；NULL、空串与不认识的值一律退回缺省选择。
 *  @param token 配置值
 *  @return 选择
 */
LockAnimationChoice ChoiceFromToken(const char* token);

/**
 *  下拉框第 index 项对应的选择：0~4 依次为五种动画，5 为随机，6 为轮流播放。
 *  @param index 下拉框序号；越界时返回缺省选择
 *  @return 选择
 */
LockAnimationChoice ChoiceAt(int index);

/**
 *  选择在下拉框里的序号（ChoiceAt 的逆运算）。
 *  @param c 选择
 *  @return 序号 0~kChoiceCount-1
 */
int ChoiceIndex(const LockAnimationChoice& c);

/**
 *  锁屏开始时播哪一种动画。
 *  @param c           选择
 *  @param randomValue 随机数（任意 32 位值，随机方式下取模决定）
 *  @return 动画；轮流播放从八荣八耻开始
 */
LockAnimation PickInitialAnimation(const LockAnimationChoice& c, uint32_t randomValue);

/**
 *  轮流播放时的下一种动画（循环）。
 *  @param current 当前动画
 *  @return 下一种
 */
LockAnimation NextAnimation(LockAnimation current);

} // namespace flamingolock
