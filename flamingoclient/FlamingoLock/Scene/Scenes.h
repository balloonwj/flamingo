/**
 *  五种锁屏动画场景的创建函数，以及各场景里可单测的纯逻辑（时间线、碰撞、取样、关节求解）。
 *
 *  场景本身（绘制）见同目录的 HonorScene.cpp、FlamingoScene.cpp、ParticleClockScene.cpp、BounceScene.cpp、
 *  GlyphRainScene.cpp，样式取自样稿 tmp/lockscreen/lock_preview.html；纯逻辑部分放在 SceneModels.cpp，
 *  同时编进单元测试工程（docs/锁屏方案.md 第 7 节 AH / AB / AP / AR / AF 各条）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingolock {

class ILockScene;

ILockScene* CreateHonorScene();          // 八荣八耻
ILockScene* CreateFlamingoScene();       // 火烈鸟漫步
ILockScene* CreateParticleClockScene();  // 粒子时钟
ILockScene* CreateBounceScene();         // 弹跳标志与烟花
ILockScene* CreateGlyphRainScene();      // 汉字数字雨

// ===========================================================================
// 八荣八耻：原文与时间线
// ===========================================================================

const int kHonorLineCount = 8;           // 八句
const int kHonorHalfChars = 7;           // 每半句的字数

/**
 *  第 index 句（0~7）的「荣」半句或「耻」半句原文。
 *  @param index 句序，越界时按 0
 *  @param honor true 取「以……为荣」，false 取「以……为耻」
 *  @return 原文（静态字符串）
 */
const wchar_t* HonorLineText(int index, bool honor);

/**
 *  八荣八耻动画的阶段。
 */
enum HonorPhase
{
    kHonorLine = 0,     // 逐句：一句一句写出来
    kHonorSummary,      // 全文总览，盖上「知荣明耻」印章
};

/**
 *  八荣八耻动画在某一时刻的位置。
 */
struct HonorPosition
{
    HonorPhase m_phase;     // 阶段
    int        m_line;      // 逐句阶段时的句序（0~7）；总览阶段为 -1
    float      m_phaseT;    // 当前这一句（或总览）已经播放的秒数
};

const float kHonorCharStep = 0.12f;      // 相邻两个字开始浮现的间隔（秒）
const float kHonorCharDur = 0.6f;        // 一个字从淡到清晰的时长（秒）
const float kHonorHalfGap = 0.5f;        // 「荣」半句写完到「耻」半句开始的间隔（秒）
const float kHonorHold = 3.6f;           // 一整句写完后停留（秒）
const float kHonorFadeOut = 0.9f;        // 一句淡出（秒）
const float kHonorRowStep = 0.3f;        // 总览里相邻两行出现的间隔（秒）
const float kHonorSummaryHold = 10.0f;   // 总览停留（秒，不含逐行出现与淡出）
const float kHonorSealDelay = 0.6f;      // 最后一行出现后多久落印（秒）
const float kHonorSummaryFade = 1.0f;    // 总览淡出（秒）

/**
 *  一句（两个半句）从开始到淡出完毕的总时长（秒）。
 */
float HonorLineDuration();

/**
 *  总览从开始到淡出完毕的总时长（秒）。
 */
float HonorSummaryDuration();

/**
 *  一轮（八句加总览）的总时长（秒）。
 */
float HonorCycleDuration();

/**
 *  从动画开始经过 t 秒时的位置；按一轮循环。
 *  @param t 秒数（不小于 0）
 *  @return 位置
 */
HonorPosition HonorPositionAt(float t);

// ===========================================================================
// 弹跳标志：运动与正中角落的判定
// ===========================================================================

const float kBounceCornerWindow = 0.12f;  // 横、竖两个方向碰边的时间差不超过这么多秒，算正中角落
const float kBounceCornerCooldown = 0.5f; // 两次正中角落之间至少隔多久（秒），避免一次撞角算两次

/**
 *  弹跳标志的运动状态（纯逻辑）。
 */
struct BounceModel
{
    float m_x;            // 标志左上角横坐标
    float m_y;            // 标志左上角纵坐标
    float m_vx;           // 横向速度（像素 / 秒）
    float m_vy;           // 纵向速度（像素 / 秒）
    float m_w;            // 标志宽
    float m_h;            // 标志高
    int   m_colorIndex;   // 当前颜色序号，每碰一次边加 1
    float m_clock;        // 场景时钟（秒）
    float m_lastHitX;     // 上次碰左右边的时刻
    float m_lastHitY;     // 上次碰上下边的时刻
    float m_lastCorner;   // 上次正中角落的时刻
    int   m_corners;      // 累计正中角落的次数
};

/**
 *  把运动状态复位：时钟归零，碰边与撞角的时刻、次数一并清零（AB3：场景重新初始化后不会漏掉撞角）。
 *  @param m 运动状态（位置、速度、尺寸由调用方随后设置）
 */
void ResetBounceModel(BounceModel& m);

/**
 *  一次推进的结果。
 */
struct BounceStep
{
    bool  m_hitX;         // 本步碰到左边或右边
    bool  m_hitY;         // 本步碰到上边或下边
    bool  m_corner;       // 本步判定为正中角落
    float m_cornerX;      // 正中的角落横坐标（0 或画面宽）
    float m_cornerY;      // 正中的角落纵坐标（0 或画面高）
};

/**
 *  推进 dt 秒：移动、碰边反向、换色，判断是否正中角落。
 *  @param m      运动状态
 *  @param dt     秒数
 *  @param width  画面宽
 *  @param height 画面高
 *  @param colorCount 颜色总数（换色时取模）
 *  @return 本步结果
 */
BounceStep StepBounce(BounceModel& m, float dt, float width, float height, int colorCount);

/**
 *  按当前速度方向，把标志挪到恰好 t 秒后同时碰到两条边的位置（演示与用例用）。
 *  @param m      运动状态
 *  @param width  画面宽
 *  @param height 画面高
 *  @param t      期望的秒数；会被限制在两个方向都来得及的范围内
 */
void AimBounceAtCorner(BounceModel& m, float width, float height, float t);

/**
 *  烟花上升的初速度：按「y = y0 + v·t + a·t²/2」反推，使烟花在引信燃尽时恰好到达目标高度。
 *  @param y0      起点纵坐标
 *  @param targetY 炸开的纵坐标
 *  @param fuse    引信时长（秒，大于 0）
 *  @param accel   竖直加速度（像素 / 秒²，向下为正）
 *  @return 初速度（像素 / 秒，向上为负）
 */
float RocketLaunchVelocity(float y0, float targetY, float fuse, float accel);

// ===========================================================================
// 粒子时钟：把文字取样成目标点
// ===========================================================================

/**
 *  一个取样点。
 */
struct SamplePoint
{
    float m_x;   // 横坐标
    float m_y;   // 纵坐标
};

/**
 *  从一张 8 位灰度蒙版（按行存放，0 为透明）上取样：先按 fineStep 数出不透明像素数，
 *  再反推取样间隔，使取到的点数接近且不超过 maxPoints。
 *  @param mask      蒙版像素，长度 width * height
 *  @param width     蒙版宽
 *  @param height    蒙版高
 *  @param maxPoints 点数上限（大于 0）
 *  @param fineStep  细数时的间隔（像素，至少 1）
 *  @param points    接收取样点（蒙版坐标）
 *  @param stepOut   接收最终采用的取样间隔
 */
void SampleMaskPoints(const std::vector<uint8_t>& mask, int width, int height, int maxPoints, int fineStep,
                      std::vector<SamplePoint>& points, int& stepOut);

// ===========================================================================
// 汉字数字雨：定格句子的时间安排
// ===========================================================================

const float kRainPhraseEvery = 8.0f;      // 每隔多少秒定格一句
const float kRainFirstPhrase = 1.5f;      // 第一句在第几秒开始
const float kRainCharStep = 0.13f;        // 定格时相邻两个字落下的间隔（秒）
const float kRainStreakSec = 0.28f;       // 每个字落下前那道亮光的时长（秒）
const float kRainHalfGap = 0.3f;          // 「荣」半句落完到「耻」半句开始的额外间隔（秒）
const float kRainHold = 4.2f;             // 整句定格后停留（秒）
const float kRainFade = 1.2f;             // 淡出（秒）

/**
 *  一句（14 个字）中第 i 个字落定的时刻，相对这一句开始（秒）。前 7 个是「荣」半句，后 7 个是「耻」半句。
 *  @param i 字序 0~13
 *  @return 秒数
 */
float RainCharLandAt(int i);

/**
 *  一句从开始到淡出完毕的总时长（秒）。
 */
float RainPhraseDuration();

/**
 *  第 k 句（0 起）开始的时刻与对应的原文句序。
 *  @param k 第几次定格
 *  @return 开始时刻（秒）
 */
float RainPhraseStartAt(int k);

/**
 *  第 k 次定格对应八荣八耻的第几句（循环）。
 */
int RainPhraseLine(int k);

/**
 *  八荣八耻全文里出现过的汉字（去重），汉字数字雨从中随机取字。
 */
const std::wstring& RainGlyphSet();

// ===========================================================================
// 火烈鸟：两段骨骼求关节
// ===========================================================================

/**
 *  已知髋、脚的位置与两段骨长，求「膝」（实为踝）的位置，关节凸向身后（局部坐标 x 为负的一侧）。
 *  髋脚距离超出可达范围时按最近的可达距离计算。
 *  @param hipX  髋横坐标
 *  @param hipY  髋纵坐标
 *  @param footX 脚横坐标
 *  @param footY 脚纵坐标
 *  @param len1  大腿长
 *  @param len2  小腿长
 *  @param kneeX 接收关节横坐标
 *  @param kneeY 接收关节纵坐标
 */
void SolveTwoBoneKnee(float hipX, float hipY, float footX, float footY, float len1, float len2,
                      float& kneeX, float& kneeY);

} // namespace flamingolock
