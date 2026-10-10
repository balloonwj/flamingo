/**
 *  锁屏动画「火烈鸟漫步」：湖边浅水里的一只火烈鸟：踱步、单脚站立、倒着头喝水，隔一阵子起飞、飞出屏幕再飞回来落下。
 *  天空、湖水、远山的颜色随当前时刻由晨到夜变化；夜里有星星、月亮和萤火虫。水里有火烈鸟的倒影与涟漪。
 *  样式取自样稿 tmp/lockscreen/lock_preview.html（场景二）。火烈鸟按「画面高度 / 1000」为一个单位绘制，
 *  其余装饰尺寸为样稿 CSS 像素乘以缩放比例。两段腿骨的关节由 SolveTwoBoneKnee（SceneModels.cpp）求出。
 *  balloonwj@qq.com   2026-10-06
 */
#include "../Render/LockScene.h"
#include "Scenes.h"
#include "SceneUtil.h"

#include <vector>

namespace flamingolock {

namespace {

// ---- 天空配色关键帧：小时、天顶色、地平线色 ----
struct SkyKey
{
    float    m_hour;      // 时刻（小时，可带小数）
    uint32_t m_zenith;    // 天顶色 0xRRGGBB
    uint32_t m_horizon;   // 地平线色 0xRRGGBB
};
const int kSkyKeyCount = 10;
const SkyKey kSkyKeys[kSkyKeyCount] =
{
    { 0.0f, 0x0A0F24, 0x1D2747 },
    { 4.5f, 0x111A3A, 0x3A3462 },
    { 6.0f, 0x4D5FA3, 0xF2A07B },
    { 7.5f, 0x6FA8DC, 0xFFD2A8 },
    { 10.0f, 0x3F97DA, 0xBFE3FF },
    { 16.0f, 0x4A9FDC, 0xCFEAFF },
    { 18.0f, 0x5B5EA6, 0xFF9E6D },
    { 19.3f, 0x2E2A5E, 0xC2577A },
    { 20.5f, 0x121738, 0x33305E },
    { 24.0f, 0x0A0F24, 0x1D2747 },
};

// ---- 夜色：0 为白天，1 为深夜 ----
const float kNightEnd = 4.5f;             // 这之前是深夜
const float kDayStart = 7.5f;             // 这之后到 kDayEnd 是白天
const float kDayEnd = 17.5f;
const float kNightStart = 20.5f;          // 这之后是深夜
const float kTwilightHours = 3.0f;        // 晨昏渐变的小时数

// ---- 太阳与月亮 ----
const float kSunRise = 6.0f;              // 日出、日落时刻
const float kSunSet = 18.0f;
const float kMoonRise = 18.5f;            // 月亮升起与落下时刻（次日）
const float kMoonSet = 5.5f;
const float kMoonHours = 11.0f;           // 月亮在天上的小时数
const float kHoursPerDay = 24.0f;
const float kArcLeft = 0.08f;             // 日月轨迹的左端与跨度（占画面宽度）
const float kArcSpan = 0.84f;
const float kSunArcHeight = 0.78f;        // 太阳最高点在水面以上的高度（占水面高度）
const float kMoonArcHeight = 0.72f;
const float kArcBelow = 10.0f;            // 日月轨迹的两端比水面低多少（像素）
const uint32_t kSunHigh = 0xFFF6CF;       // 太阳在高处的颜色
const uint32_t kSunLow = 0xFFB064;        // 太阳贴近水面时的颜色
const float kSunGlowR = 160.0f;           // 太阳光晕半径（单位）
const float kSunGlowAlpha = 0.55f;
const float kSunR = 34.0f;                // 太阳半径（单位）
const uint32_t kMoonGlow = 0xF0F0DC;      // 月光
const float kMoonGlowAlpha = 0.25f;
const float kMoonGlowR = 110.0f;          // 月光半径（单位）
const uint32_t kMoonColor = 0xF2EFDC;     // 月牙
const float kMoonR = 26.0f;               // 月盘半径（单位）
const float kMoonCutX = 0.42f;            // 挖掉的圆相对月盘的偏移与半径（以月盘半径计）
const float kMoonCutY = -0.23f;
const float kMoonCutR = 0.88f;

// ---- 场景布局（占画面高度）----
const float kUnitDiv = 1000.0f;           // 一个单位 = 画面高度 / 1000
const float kWaterY = 0.6f;               // 水面
const float kGroundY = 0.84f;             // 火烈鸟脚下的水底
const float kCruiseY = 0.3f;              // 飞行巡航高度
const float kFlyLowY = 180.0f;            // 刚起飞时身体中心离水底的高度（单位）

// ---- 星星、芦苇、萤火虫、远山 ----
const int kStarCount = 160;
const float kStarAreaY = 0.85f;           // 星星分布在水面以上这一比例的高度内
const float kStarMinR = 0.5f;             // 星星边长范围（像素）
const float kStarMaxR = 1.6f;
const float kStarTwinkleFreq = 1.1f;      // 闪烁频率
const float kStarMinAlpha = 0.4f;         // 闪烁最暗与幅度
const float kStarAlphaRange = 0.6f;
const float kStarShowNight = 0.02f;       // 夜色超过这个值才画星星
const uint32_t kStarColor = 0xFFFFF0;
const int kReedCount = 46;
const int kReedLeftCount = 26;            // 前 26 根长在左边，其余在右边
const float kReedLeftEnd = 0.16f;         // 左边芦苇的分布范围（占画面宽度）
const float kReedRightStart = 0.86f;      // 右边芦苇的起始位置
const float kReedEdge = 10.0f;            // 芦苇可以越出画面边缘多少（像素）
const float kReedMinH = 0.18f;            // 芦苇高度范围（占画面高度）
const float kReedMaxH = 0.42f;
const float kReedCatChance = 0.35f;       // 带香蒲穗的比例
const float kReedMinW = 2.0f;             // 芦苇线宽范围（像素）
const float kReedMaxW = 3.6f;
const float kReedSwayA = 14.0f;           // 两种摆动的幅度（像素）与频率
const float kReedSwayFreqA = 0.9f;
const float kReedSwayB = 4.0f;
const float kReedSwayFreqB = 2.1f;
const float kReedBendX = 0.2f;            // 二次曲线控制点：横向取摆动量的 0.2，纵向在高度的 0.55 处
const float kReedBendY = 0.55f;
const float kReedBelow = 10.0f;           // 芦苇根部在画面底边以下多少（像素）
const float kCatOffsetY = 16.0f;          // 香蒲穗中心在梢头以下多少（像素）
const float kCatRx = 5.0f;                // 香蒲穗半径
const float kCatRy = 18.0f;
const float kCatTilt = 0.01f;             // 香蒲穗随摆动倾斜（弧度 / 像素）
const uint32_t kReedDay = 0x1D2B22;       // 白天、夜里的芦苇色
const uint32_t kReedNight = 0x0B1218;
const uint32_t kCatDay = 0x4A2F1D;        // 白天、夜里的香蒲穗色
const uint32_t kCatNight = 0x1A1410;
const float kReedNightSwitch = 0.5f;      // 夜色超过这个值换夜里的配色
const int kFireflyCount = 14;
const float kFireflyMinY = 0.62f;         // 萤火虫分布的纵向范围（占画面高度）
const float kFireflyMaxY = 0.95f;
const float kFireflyMinSpeed = 0.3f;      // 相位速度范围
const float kFireflyMaxSpeed = 0.8f;
const float kFireflyDriftX = 18.0f;       // 漂移速度（像素 / 秒）
const float kFireflyDriftY = 10.0f;
const float kFireflyFreqX = 1.3f;
const float kFireflyFreqY = 0.9f;
const float kFireflyBlinkFreq = 3.0f;
const float kFireflyShowNight = 0.4f;     // 夜色超过这个值才出现
const float kFireflyBase = 0.35f;         // 亮度的基础与幅度
const float kFireflyRange = 0.65f;
const float kFireflyR = 9.0f;             // 光点半径（像素）
const uint32_t kFireflyColor = 0xDCFF8C;
const int kHillLayers = 2;
const float kHillStep = 20.0f;            // 远山轮廓的取点间隔（像素）
const float kHillWave1 = 34.0f;           // 两道正弦起伏的振幅（像素）
const float kHillWave2 = 16.0f;
const float kHillMinH = 4.0f;             // 山高下限（像素）
const float kHillAmp[kHillLayers] = { 1.0f, 0.6f };       // 两层山的整体缩放
const float kHillBase[kHillLayers] = { 70.0f, 34.0f };    // 两层山的基础高度
const float kHillF1[kHillLayers] = { 0.0032f, 0.0055f };  // 两道起伏的频率（每像素弧度）
const float kHillF2[kHillLayers] = { 0.0091f, 0.014f };
const uint32_t kHillTint[kHillLayers] = { 0x2A3A55, 0x182634 };   // 两层山与地平线色混合的颜色
const float kHillMix[kHillLayers] = { 0.55f, 0.75f };             // 白天的混合比例
const float kHillNightMix[kHillLayers] = { 0.25f, 0.2f };         // 夜里额外增加的混合比例

// ---- 湖水 ----
const uint32_t kWaterTint = 0x1B4A6B;     // 水面上沿与地平线色混合的颜色
const float kWaterTintMix = 0.5f;
const uint32_t kWaterDeep = 0x06121F;     // 水底与天顶色混合的颜色
const float kWaterDeepMix = 0.65f;
const int kGlitterRows = 26;              // 日月在水里的光柱分几行
const float kGlitterTop = 6.0f;           // 光柱第一行在水面以下多少（像素）
const float kGlitterBaseW = 30.0f;        // 光柱宽度：第一行 30 单位，每往下一行加 4
const float kGlitterGrowW = 4.0f;
const float kGlitterMinK = 0.6f;          // 宽度随时间闪动的基础与幅度
const float kGlitterRangeK = 0.4f;
const float kGlitterFreq = 1.7f;
const float kGlitterSunAlpha = 0.32f;     // 太阳、月亮光柱的不透明度
const float kGlitterMoonAlpha = 0.22f;
const float kGlitterFadeRows = 30.0f;     // 不透明度随行数递减：第 i 行乘以 (1 - i / 30)
const float kGlitterSwayFreq = 0.8f;      // 每行左右晃动
const float kGlitterSwayPhase = 1.7f;
const float kGlitterSway = 8.0f;          // 晃动幅度（单位）
const float kGlitterH = 2.2f;             // 每行高度（单位）
const uint32_t kSunGlitter = 0xFFDCA0;
const uint32_t kMoonGlitter = 0xEBEBD7;
const float kBandStep = 7.0f;             // 水面横向波纹的间距与高度（像素）
const float kBandH = 2.0f;
const float kBandFreq = 0.05f;            // 波纹左右错动的空间频率与时间频率
const float kBandTimeFreq = 1.2f;
const float kBandShift = 30.0f;           // 错动幅度（像素）
const float kBandOverhang = 40.0f;        // 波纹向两侧多画的长度（像素）
const float kBandAlpha = 0.16f;
const float kRippleWidth = 1.4f;          // 涟漪线宽（像素）
const float kRippleFlat = 0.22f;          // 涟漪椭圆的扁平度（纵向半径 / 横向半径）
const float kRippleAlpha = 0.45f;
const uint32_t kRippleColor = 0xFFFFFF;
const float kReflectAlpha = 0.3f;         // 倒影不透明度（飞得越高越淡）
const float kReflectMaxAlt = 0.5f;        // 高度超过一半时不画倒影

// ---- 火烈鸟（单位）----
const float kL1 = 84.0f;                  // 大腿（髋到「膝」）长度
const float kL2 = 88.0f;                  // 小腿（「膝」到脚）长度
const float kHipY = -168.0f;              // 髋关节高度（脚底为 0，向上为负）
const float kWalkSpeed = 52.0f;           // 踱步速度（单位 / 秒）
const float kGaitRate = 2.6f;             // 步态相位速度（弧度 / 秒）
const float kFlySpeed = 300.0f;           // 飞行速度（单位 / 秒）
const float kFlightEvery = 45.0f;         // 大约每隔多少秒起飞一次
const float kFlapRate = 7.5f;             // 扇翅相位速度（弧度 / 秒）
const float kStandRate = 1.6f;            // 单脚站立过渡速度（每秒）
const float kDrinkRate = 1.1f;            // 低头喝水过渡速度（每秒）
const float kTurnLeft = 0.16f;            // 踱步走到这两处（占画面宽度）就掉头
const float kTurnRight = 0.84f;
const float kWalkRippleEvery = 0.6f;      // 踱步时每隔多少秒泛起一圈涟漪
const float kDrinkRippleEvery = 0.9f;     // 喝水时头点到水面泛起涟漪的间隔
const float kDrinkDeep = 0.9f;            // 低头过渡超过这个值算头点到水面
const float kTakeoffSec = 1.6f;           // 起飞爬升的时长
const float kTakeoffSpeedK = 0.6f;        // 起飞时的水平速度与飞行速度之比
const float kAwayMargin = 260.0f;         // 飞出画面多远算飞走（单位）
const float kAwaySec = 2.5f;              // 在画面外停留多久再飞回来
const float kLandMin = 0.3f;              // 落点范围（占画面宽度）
const float kLandMax = 0.7f;
const float kLandSpeedK = 0.8f;           // 降落时的水平速度与飞行速度之比
const float kLandSlope = 0.45f;           // 离落点这么远（占画面宽度）时还在巡航高度
const float kLandDone = 2.0f;             // 离落点不到这么多像素算落地
const float kWalkMin = 6.0f;              // 各状态的持续时长范围（秒）
const float kWalkMax = 10.0f;
const float kStandMin = 5.0f;
const float kStandMax = 8.0f;
const float kDrinkMin = 5.0f;
const float kDrinkMax = 7.0f;
const float kChanceWalk = 0.4f;           // 踱完一段后：四成继续踱步，三成单脚站立，三成喝水
const float kChanceStand = 0.7f;
const float kStartX = 0.45f;              // 初始位置（占画面宽度）
const float kStartSinceFlight = 10.0f;    // 初始的「距上次落地」秒数
const float kFlyScale = 0.9f;             // 飞行时的大小与站立时之比
const float kFlyBob = 6.0f;               // 飞行时上下起伏（单位）与频率
const float kFlyBobFreq = 2.0f;

// 涟漪大小（单位）与寿命（秒）
const float kRippleWalkR = 26.0f;
const float kRippleWalkLife = 1.4f;
const float kRippleDrinkR = 70.0f;
const float kRippleDrinkLife = 2.2f;
const float kRippleTakeoffR = 60.0f;
const float kRippleTakeoffLife = 1.8f;
const float kRippleLandR = 70.0f;
const float kRippleLandLife = 2.0f;
const float kRippleFootY = 3.0f;          // 涟漪在水底以上多少（单位）
const float kRippleBeakY = 4.0f;
const float kBeakReach = 112.0f;          // 低头喝水时喙尖在身前多远（单位）

// ---- 火烈鸟的颜色 ----
const uint32_t kLegColor = 0xE2728F;
const uint32_t kNeckColor = 0xF48AA8;
const uint32_t kTailColor = 0xE65A85;
const uint32_t kBodyLight = 0xFFC2D1;
const uint32_t kBodyDark = 0xEF6D95;
const uint32_t kFoldWing = 0xE86088;
const float kFoldWingAlpha = 0.85f;
const uint32_t kFeather = 0x1E1E28;
const uint32_t kHeadColor = 0xF48AA8;
const uint32_t kBeakColor = 0xF7DDE2;
const uint32_t kBeakTip = 0x202028;
const uint32_t kEyeColor = 0xF5D76E;
const uint32_t kWingFar = 0xD9577F;
const uint32_t kWingNear = 0xF27AA0;
const float kWingFarAlpha = 0.75f;
const float kWingFarFlap = 0.85f;         // 远侧翅膀的扇动幅度与近侧之比

// ---- 火烈鸟的形状（单位，局部坐标：站立时脚底中点为原点，飞行时身体中心为原点，朝右）----
const float kLegWidth = 4.5f;
const float kKneeR = 3.6f;
const float kStepX = 30.0f;               // 踱步时脚前后摆动的幅度
const float kStepLift = 22.0f;            // 踱步时抬脚的高度
const float kStandFootX = 6.0f;           // 站着时两脚离中线的距离
const float kFoldX = -22.0f;              // 单脚站立时收起的那只脚的位置
const float kFoldY = 52.0f;               // 比髋低多少
const float kBodyX = -4.0f;               // 身体中心相对髋的位置
const float kBodyY = -22.0f;
const float kTiltUp = -0.06f;             // 抬头与低头时身体的倾斜（弧度）
const float kTiltDown = 0.32f;
const float kBodyRx = 60.0f;              // 站立时身体半径
const float kBodyRy = 31.0f;
const float kBodyGlowX = 8.0f;            // 身体渐变的高光偏移与半径
const float kBodyGlowY = -10.0f;
const float kBodyGradR = 64.0f;
const float kWingCx = -12.0f;             // 收拢的翅膀
const float kWingCy = -6.0f;
const float kWingRx = 42.0f;
const float kWingRy = 19.0f;
const float kWingTilt = -0.1f;
const float kWalkBob = 3.0f;              // 踱步与站立时身体的起伏
const float kStandBob = 1.2f;
const float kStandBobFreq = 1.5f;
const float kNeckWidth = 11.0f;
const float kNeckSwayWalk = 4.0f;         // 脖子摆动
const float kNeckSwayStand = 2.0f;
const float kNeckSwayFreq = 0.8f;
const float kDrinkNod = 2.0f;             // 喝水时头的点动幅度与频率
const float kDrinkNodFreq = 4.0f;
const float kHeadTiltUp = 0.25f;          // 抬头与低头时头的旋转（弧度）
const float kHeadTiltDown = 2.25f;
const float kHeadR = 12.0f;
const float kEyeX = 2.0f;
const float kEyeY = -3.0f;
const float kEyeR = 2.6f;
const float kPupilX = 2.6f;
const float kPupilR = 1.2f;
const float kFlyBodyRx = 58.0f;           // 飞行时身体半径
const float kFlyBodyRy = 21.0f;
const float kFlyGlow = 6.0f;              // 飞行时身体渐变的高光偏移与半径
const float kFlyGradR = 60.0f;
const float kFlyLegWidth = 4.0f;
const float kFlyNeckWidth = 10.0f;
const float kFlyNeckBob = 3.0f;
const float kFlyHeadX = 158.0f;
const float kFlyHeadY = -8.0f;
const float kFlyHeadTilt = 0.35f;
const float kWingSpan = 150.0f;           // 翅尖扬起的最大高度

// 一颗星
struct Star
{
    float m_x;    // 横坐标
    float m_y;    // 纵坐标
    float m_r;    // 边长
    float m_tw;   // 闪烁相位
};

// 一根芦苇
struct Reed
{
    float m_x;     // 根部横坐标
    float m_h;     // 高度
    float m_ph;    // 摆动相位
    bool  m_cat;   // 是否带香蒲穗
    float m_w;     // 线宽
};

// 一只萤火虫
struct Firefly
{
    float m_x;    // 横坐标
    float m_y;    // 纵坐标
    float m_ph;   // 相位
    float m_sp;   // 相位速度
};

// 一圈涟漪
struct Ripple
{
    float m_x;      // 中心横坐标
    float m_y;      // 中心纵坐标
    float m_t;      // 已存在的秒数
    float m_maxR;   // 最大半径
    float m_life;   // 寿命（秒）
};

// 火烈鸟的状态
enum BirdState
{
    kBirdWalk = 0,   // 踱步
    kBirdStand,      // 单脚站立
    kBirdDrink,      // 低头喝水
    kBirdTakeoff,    // 起飞爬升
    kBirdFly,        // 巡航，飞出画面
    kBirdAway,       // 在画面外停留
    kBirdLanding,    // 飞回来降落
};

/**
 *  火烈鸟漫步场景。
 */
class CFlamingoScene : public ILockScene
{
public:
    CFlamingoScene()
        : m_w(0.0f)
        , m_h(0.0f)
        , m_scale(1.0f)
        , m_clock(0.0f)
        , m_hour(12.0f)
        , m_paletteMinute(-1)
        , m_night(0.0f)
        , m_lastRipple(0.0f)
        , m_d2d(NULL)
        , m_x(0.0f)
        , m_facing(1.0f)
        , m_state(kBirdWalk)
        , m_stateT(0.0f)
        , m_stateDur(0.0f)
        , m_gait(0.0f)
        , m_standBlend(0.0f)
        , m_drinkBlend(0.0f)
        , m_alt(0.0f)
        , m_flap(0.0f)
        , m_sinceFlight(0.0f)
        , m_landX(0.0f)
    {
        m_zenith = ColorRgb(kSkyKeys[0].m_zenith);
        m_horizon = ColorRgb(kSkyKeys[0].m_horizon);
    }

    void Init(const SceneEnv& env, float width, float height) override
    {
        m_w = width;
        m_h = height;
        m_scale = env.m_scale;
        m_d2d = env.m_d2d;
        m_clock = 0.0f;
        m_lastRipple = 0.0f;
        const float wy = WaterY();
        m_stars.clear();
        for (int i = 0; i < kStarCount; ++i)
        {
            Star s;
            s.m_x = RandRangeF(0.0f, width);
            s.m_y = RandRangeF(0.0f, wy * kStarAreaY);
            s.m_r = RandRangeF(kStarMinR, kStarMaxR) * m_scale;
            s.m_tw = RandRangeF(0.0f, kSceneTwoPi);
            m_stars.push_back(s);
        }
        m_reeds.clear();
        for (int i = 0; i < kReedCount; ++i)
        {
            Reed r;
            const bool left = (i < kReedLeftCount);
            r.m_x = left ? RandRangeF(-kReedEdge * m_scale, width * kReedLeftEnd)
                         : RandRangeF(width * kReedRightStart, width + kReedEdge * m_scale);
            r.m_h = RandRangeF(kReedMinH, kReedMaxH) * height;
            r.m_ph = RandRangeF(0.0f, kSceneTwoPi);
            r.m_cat = (RandRangeF(0.0f, 1.0f) < kReedCatChance);
            r.m_w = RandRangeF(kReedMinW, kReedMaxW) * m_scale;
            m_reeds.push_back(r);
        }
        m_fireflies.clear();
        for (int i = 0; i < kFireflyCount; ++i)
        {
            Firefly f;
            f.m_x = RandRangeF(0.0f, width);
            f.m_y = RandRangeF(height * kFireflyMinY, height * kFireflyMaxY);
            f.m_ph = RandRangeF(0.0f, kSceneTwoPi);
            f.m_sp = RandRangeF(kFireflyMinSpeed, kFireflyMaxSpeed);
            m_fireflies.push_back(f);
        }
        m_ripples.clear();
        m_x = width * kStartX;
        m_facing = 1.0f;
        m_alt = 0.0f;
        m_standBlend = 0.0f;
        m_drinkBlend = 0.0f;
        m_sinceFlight = kStartSinceFlight;
        SetState(kBirdWalk);
        BuildStaticGeometry();
        m_round = MakeRoundStroke(m_d2d);
        m_paletteMinute = -1;
        DiscardDeviceResources();
    }

    void DiscardDeviceResources() override
    {
        m_solid.Release();
        m_sky.Release();
        m_water.Release();
        m_sunGlow.Release();
        m_moonGlow.Release();
        m_fireflyGlow.Release();
        m_body.Release();
        m_layer.Release();
        m_paletteMinute = -1;
    }

    void Update(float dt, uint64_t wallMs) override
    {
        m_clock += dt;
        const SYSTEMTIME st = LocalTimeFromWallMs(wallMs);
        const float minutesPerHour = 60.0f;
        const float secondsPerHour = 3600.0f;
        m_hour = st.wHour + st.wMinute / minutesPerHour + st.wSecond / secondsPerHour;
        const float u = Unit();
        m_stateT += dt;
        m_sinceFlight += dt;
        m_flap += dt * kFlapRate;
        m_standBlend = Clamp01F(m_standBlend + ((m_state == kBirdStand) ? dt : -dt) * kStandRate);
        m_drinkBlend = Clamp01F(m_drinkBlend + ((m_state == kBirdDrink) ? dt : -dt) * kDrinkRate);

        switch (m_state)
        {
        //踱步：走到画面两侧附近就掉头
        case kBirdWalk:
            m_gait += dt * kGaitRate;
            m_x += m_facing * kWalkSpeed * u * dt;
            if ((m_facing > 0.0f && m_x > m_w * kTurnRight) || (m_facing < 0.0f && m_x < m_w * kTurnLeft))
            {
                m_facing = -m_facing;
            }
            if (m_clock - m_lastRipple > kWalkRippleEvery)
            {
                m_lastRipple = m_clock;
                AddRipple(m_x, GroundY() - kRippleFootY * u, kRippleWalkR * u, kRippleWalkLife);
            }
            if (m_stateT > m_stateDur)
            {
                NextGroundState();
            }
            break;

        //单脚站立
        case kBirdStand:
            if (m_stateT > m_stateDur)
            {
                NextGroundState();
            }
            break;

        //喝水：头点到水面时泛起涟漪
        case kBirdDrink:
            if (m_drinkBlend > kDrinkDeep && m_clock - m_lastRipple > kDrinkRippleEvery)
            {
                m_lastRipple = m_clock;
                AddRipple(m_x + m_facing * kBeakReach * u, GroundY() - kRippleBeakY * u, kRippleDrinkR * u,
                          kRippleDrinkLife);
            }
            if (m_stateT > m_stateDur)
            {
                NextGroundState();
            }
            break;

        //起飞：一边向前一边爬升到巡航高度
        case kBirdTakeoff:
            m_x += m_facing * kFlySpeed * kTakeoffSpeedK * u * dt;
            m_alt = EaseInOutSineF(Clamp01F(m_stateT / kTakeoffSec));
            if (m_stateT >= kTakeoffSec)
            {
                SetState(kBirdFly);
            }
            break;

        //巡航：飞出画面
        case kBirdFly:
            m_x += m_facing * kFlySpeed * u * dt;
            if (m_x < -kAwayMargin * u || m_x > m_w + kAwayMargin * u)
            {
                SetState(kBirdAway);
            }
            break;

        //在画面外停留一会儿，掉头飞回来，选一个落点
        case kBirdAway:
            if (m_stateT > kAwaySec)
            {
                m_facing = -m_facing;
                m_landX = RandRangeF(m_w * kLandMin, m_w * kLandMax);
                SetState(kBirdLanding);
            }
            break;

        //降落：离落点越近飞得越低
        case kBirdLanding:
        {
            const float diff = m_landX - m_x;
            const float dir = (diff > 0.0f) ? 1.0f : ((diff < 0.0f) ? -1.0f : m_facing);
            m_facing = dir;
            const float dist = fabsf(diff);
            const float stepLen = kFlySpeed * kLandSpeedK * u * dt;
            m_x += dir * ((dist < stepLen) ? dist : stepLen);
            m_alt = Clamp01F(dist / (m_w * kLandSlope));
            if (dist < kLandDone)
            {
                m_alt = 0.0f;
                m_sinceFlight = 0.0f;
                AddRipple(m_x, GroundY() - kRippleFootY * u, kRippleLandR * u, kRippleLandLife);
                SetState(kBirdStand);
            }
            break;
        }

        //没有其它状态
        default:
            break;
        }

        for (size_t i = 0; i < m_ripples.size();)
        {
            m_ripples[i].m_t += dt;
            if (m_ripples[i].m_t >= m_ripples[i].m_life)
            {
                m_ripples.erase(m_ripples.begin() + i);
                continue;
            }
            ++i;
        }
        for (size_t i = 0; i < m_fireflies.size(); ++i)
        {
            Firefly& f = m_fireflies[i];
            f.m_ph += dt * f.m_sp;
            f.m_x += cosf(f.m_ph * kFireflyFreqX) * kFireflyDriftX * m_scale * dt;
            f.m_y += sinf(f.m_ph * kFireflyFreqY) * kFireflyDriftY * m_scale * dt;
        }
    }

    void Draw(ID2D1RenderTarget* rt) override
    {
        if (!EnsureResources(rt))
        {
            return;
        }
        UpdatePalette(rt);
        Celestial c;
        const bool hasCelestial = ComputeCelestial(c);
        DrawSky(rt);
        if (hasCelestial)
        {
            DrawCelestial(rt, c);
        }
        DrawHills(rt);
        DrawWater(rt, hasCelestial ? &c : NULL);
        //倒影：以脚下的水底为轴上下翻转，半透明；飞得太高时不画
        if (m_alt < kReflectMaxAlt && m_state != kBirdAway)
        {
            D2D1_MATRIX_3X2_F old;
            rt->GetTransform(&old);
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
            const D2D1_RECT_F water = D2D1::RectF(0.0f, WaterY(), m_w, m_h);
            rt->PushAxisAlignedClip(water, D2D1_ANTIALIAS_MODE_ALIASED);
            rt->PushLayer(D2D1::LayerParameters(water, NULL, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::IdentityMatrix(), kReflectAlpha * (1.0f - m_alt * 2.0f)),
                          m_layer);
            DrawBird(rt, D2D1::Matrix3x2F::Scale(1.0f, -1.0f) * D2D1::Matrix3x2F::Translation(0.0f, GroundY() * 2.0f));
            rt->PopLayer();
            rt->PopAxisAlignedClip();
            rt->SetTransform(old);
        }
        DrawWaterBands(rt);
        DrawRipples(rt);
        if (m_state != kBirdAway)
        {
            DrawBird(rt, D2D1::Matrix3x2F::Identity());
        }
        DrawReeds(rt);
        DrawFireflies(rt);
    }

    bool ShowsCornerClock() const override
    {
        return true;
    }

private:
    // 太阳或月亮此刻的位置
    struct Celestial
    {
        float m_x;     // 横坐标
        float m_y;     // 纵坐标
        bool  m_sun;   // true 为太阳，false 为月亮
        float m_p;     // 在轨迹上走过的比例 0~1
    };

    float Unit() const
    {
        return m_h / kUnitDiv;
    }

    float WaterY() const
    {
        return m_h * kWaterY;
    }

    float GroundY() const
    {
        return m_h * kGroundY;
    }

    float CruiseY() const
    {
        return m_h * kCruiseY;
    }

    void SetState(BirdState st)
    {
        m_state = st;
        m_stateT = 0.0f;
        switch (st)
        {
        //踱步一段时间
        case kBirdWalk:
            m_stateDur = RandRangeF(kWalkMin, kWalkMax);
            break;
        //站一会儿
        case kBirdStand:
            m_stateDur = RandRangeF(kStandMin, kStandMax);
            break;
        //喝一会儿水
        case kBirdDrink:
            m_stateDur = RandRangeF(kDrinkMin, kDrinkMax);
            break;
        //起飞蹬水的那一刻泛起一圈大涟漪
        case kBirdTakeoff:
            AddRipple(m_x, GroundY() - kRippleFootY * Unit(), kRippleTakeoffR * Unit(), kRippleTakeoffLife);
            break;
        //其余状态的时长由各自的条件决定
        default:
            break;
        }
    }

    void AddRipple(float x, float y, float maxR, float life)
    {
        Ripple r;
        r.m_x = x;
        r.m_y = y;
        r.m_t = 0.0f;
        r.m_maxR = maxR;
        r.m_life = life;
        m_ripples.push_back(r);
    }

    // 地面上的状态切换：隔了足够久就起飞，否则在踱步、站立、喝水之间随机挑
    void NextGroundState()
    {
        if (m_sinceFlight > kFlightEvery)
        {
            SetState(kBirdTakeoff);
            return;
        }
        const float r = RandRangeF(0.0f, 1.0f);
        if (m_state != kBirdWalk || r < kChanceWalk)
        {
            SetState(kBirdWalk);
        }
        else if (r < kChanceStand)
        {
            SetState(kBirdStand);
        }
        else
        {
            SetState(kBirdDrink);
        }
    }

    // 夜色浓度：0 为白天，1 为深夜
    float NightFactor() const
    {
        if (m_hour < kNightEnd || m_hour > kNightStart)
        {
            return 1.0f;
        }
        if (m_hour > kDayStart && m_hour < kDayEnd)
        {
            return 0.0f;
        }
        if (m_hour <= kDayStart)
        {
            return Clamp01F((kDayStart - m_hour) / kTwilightHours);
        }
        return Clamp01F((m_hour - kDayEnd) / kTwilightHours);
    }

    // 太阳 6 点升起 18 点落下；月亮 18:30 升起次日 5:30 落下；都不在天上时返回 false
    bool ComputeCelestial(Celestial& c) const
    {
        const float wy = WaterY();
        if (m_hour >= kSunRise && m_hour <= kSunSet)
        {
            c.m_p = (m_hour - kSunRise) / (kSunSet - kSunRise);
            c.m_x = m_w * (kArcLeft + kArcSpan * c.m_p);
            c.m_y = wy - sinf(c.m_p * kScenePi) * wy * kSunArcHeight + kArcBelow * m_scale;
            c.m_sun = true;
            return true;
        }
        float hh = -1.0f;
        if (m_hour >= kMoonRise)
        {
            hh = m_hour - kMoonRise;
        }
        else if (m_hour <= kMoonSet)
        {
            hh = m_hour + (kHoursPerDay - kMoonRise);
        }
        if (hh < 0.0f)
        {
            return false;
        }
        c.m_p = hh / kMoonHours;
        c.m_x = m_w * (kArcLeft + kArcSpan * c.m_p);
        c.m_y = wy - sinf(c.m_p * kScenePi) * wy * kMoonArcHeight + kArcBelow * m_scale;
        c.m_sun = false;
        return true;
    }

    // 预先建好与时刻无关的几何：远山、月牙、火烈鸟的头、尾羽、飞羽、飞行时拖在后面的腿
    void BuildStaticGeometry()
    {
        const float wy = WaterY();
        const float u = Unit();
        for (int li = 0; li < kHillLayers; ++li)
        {
            const float seed1 = RandRangeF(0.0f, kSceneTwoPi);
            const float seed2 = RandRangeF(0.0f, kSceneTwoPi);
            CPathBuilder pb;
            m_hills[li].Release();
            if (!pb.Begin(m_d2d))
            {
                continue;
            }
            pb.MoveTo(0.0f, wy + 1.0f, true);
            const float step = kHillStep * m_scale;
            for (float x = 0.0f; x <= m_w + step; x += step)
            {
                const float xc = x / m_scale;   //样稿里的频率以 CSS 像素计
                float hgt = (kHillBase[li] + kHillWave1 * sinf(xc * kHillF1[li] + seed1) +
                             kHillWave2 * sinf(xc * kHillF2[li] + seed2)) * kHillAmp[li] * u;
                if (hgt < kHillMinH * m_scale)
                {
                    hgt = kHillMinH * m_scale;
                }
                pb.LineTo(x, wy - hgt);
            }
            pb.LineTo(m_w + step, wy + 1.0f);
            pb.Close();
            m_hills[li] = pb.Finish();
        }

        //月牙：月盘减去偏移的圆
        m_crescent.Release();
        const float r = kMoonR * u;
        CComPtr<ID2D1EllipseGeometry> disk;
        CComPtr<ID2D1EllipseGeometry> cut;
        CComPtr<ID2D1PathGeometry> crescent;
        if (SUCCEEDED(m_d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(0.0f, 0.0f), r, r), &disk)) &&
            SUCCEEDED(m_d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(r * kMoonCutX, r * kMoonCutY),
                                                                 r * kMoonCutR, r * kMoonCutR),
                                                   &cut)) &&
            SUCCEEDED(m_d2d->CreatePathGeometry(&crescent)))
        {
            CComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(crescent->Open(&sink)))
            {
                disk->CombineWithGeometry(cut, D2D1_COMBINE_MODE_EXCLUDE, NULL, sink);
                if (SUCCEEDED(sink->Close()))
                {
                    m_crescent = crescent;
                }
            }
        }

        CPathBuilder pb;
        m_beak.Release();
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(7.0f, -8.0f, true);
            pb.QuadTo(22.0f, -9.0f, 27.0f, -2.0f);
            pb.QuadTo(32.0f, 6.0f, 28.0f, 17.0f);
            pb.QuadTo(24.0f, 10.0f, 18.0f, 6.0f);
            pb.QuadTo(12.0f, 4.0f, 7.0f, 6.0f);
            pb.Close();
            m_beak = pb.Finish();
        }
        m_beakTip.Release();
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(26.0f, 1.0f, true);
            pb.QuadTo(32.0f, 6.0f, 28.0f, 17.0f);
            pb.QuadTo(25.0f, 11.0f, 22.0f, 8.0f);
            pb.Close();
            m_beakTip = pb.Finish();
        }
        m_tail.Release();
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(-50.0f, -6.0f, true);
            pb.QuadTo(-78.0f, 2.0f, -84.0f, 14.0f);
            pb.QuadTo(-66.0f, 10.0f, -46.0f, 12.0f);
            pb.Close();
            m_tail = pb.Finish();
        }
        m_foldFeather.Release();
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(-40.0f, -2.0f, true);
            pb.QuadTo(-66.0f, 2.0f, -74.0f, 10.0f);
            pb.QuadTo(-56.0f, 14.0f, -38.0f, 10.0f);
            pb.Close();
            m_foldFeather = pb.Finish();
        }
        m_flyLegs.Release();
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(-40.0f, 6.0f, false);
            pb.LineTo(-160.0f, 12.0f);
            pb.MoveTo(-40.0f, 2.0f, false);
            pb.LineTo(-158.0f, 4.0f);
            m_flyLegs = pb.Finish();
        }
    }

    bool EnsureResources(ID2D1RenderTarget* rt)
    {
        if (m_solid != NULL)
        {
            return true;
        }
        if (FAILED(rt->CreateSolidColorBrush(ColorRgb(kStarColor), &m_solid)))
        {
            return false;
        }
        m_moonGlow = MakeRadialBrush(rt, ColorRgb(kMoonGlow, kMoonGlowAlpha), ColorRgb(kMoonGlow, 0.0f));
        m_fireflyGlow = MakeRadialBrush(rt, ColorRgb(kFireflyColor, 1.0f), ColorRgb(kFireflyColor, 0.0f));
        m_body = MakeRadialBrush(rt, ColorRgb(kBodyLight), ColorRgb(kBodyDark));
        rt->CreateLayer(NULL, &m_layer);
        return m_moonGlow != NULL && m_fireflyGlow != NULL && m_body != NULL && m_layer != NULL;
    }

    ID2D1SolidColorBrush* Solid(uint32_t rgb, float alpha)
    {
        m_solid->SetColor(ColorRgb(rgb, alpha));
        return m_solid;
    }

    ID2D1SolidColorBrush* Solid(const D2D1_COLOR_F& color)
    {
        m_solid->SetColor(color);
        return m_solid;
    }

    // 随时刻变化的颜色：天空、湖水的渐变与太阳的颜色每分钟重算一次（渐变画刷的颜色不能修改，只能重建）
    void UpdatePalette(ID2D1RenderTarget* rt)
    {
        const float minutesPerHour = 60.0f;
        const int minute = static_cast<int>(m_hour * minutesPerHour);
        if (minute == m_paletteMinute && m_sky != NULL)
        {
            return;
        }
        m_paletteMinute = minute;
        m_night = NightFactor();
        m_zenith = ColorRgb(kSkyKeys[0].m_zenith);
        m_horizon = ColorRgb(kSkyKeys[0].m_horizon);
        for (int i = 0; i < kSkyKeyCount - 1; ++i)
        {
            const SkyKey& a = kSkyKeys[i];
            const SkyKey& b = kSkyKeys[i + 1];
            if (m_hour >= a.m_hour && m_hour <= b.m_hour)
            {
                const float t = (m_hour - a.m_hour) / (b.m_hour - a.m_hour);
                m_zenith = MixColor(ColorRgb(a.m_zenith), ColorRgb(b.m_zenith), t);
                m_horizon = MixColor(ColorRgb(a.m_horizon), ColorRgb(b.m_horizon), t);
                break;
            }
        }
        m_sky = MakeLinearBrush(rt, m_zenith, m_horizon);
        m_water = MakeLinearBrush(rt, MixColor(m_horizon, ColorRgb(kWaterTint), kWaterTintMix),
                                  MixColor(m_zenith, ColorRgb(kWaterDeep), kWaterDeepMix));
        Celestial c;
        m_sunGlow.Release();
        if (ComputeCelestial(c) && c.m_sun)
        {
            const float low = 1.0f - sinf(c.m_p * kScenePi);
            m_sunCore = MixColor(ColorRgb(kSunHigh), ColorRgb(kSunLow), low);
            D2D1_COLOR_F inner = m_sunCore;
            inner.a = kSunGlowAlpha;
            D2D1_COLOR_F outer = m_sunCore;
            outer.a = 0.0f;
            m_sunGlow = MakeRadialBrush(rt, inner, outer);
        }
    }

    void DrawSky(ID2D1RenderTarget* rt)
    {
        const float wy = WaterY();
        if (m_sky != NULL)
        {
            m_sky->SetStartPoint(D2D1::Point2F(0.0f, 0.0f));
            m_sky->SetEndPoint(D2D1::Point2F(0.0f, wy));
            rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, wy + 1.0f), m_sky);
        }
        if (m_night > kStarShowNight)
        {
            for (size_t i = 0; i < m_stars.size(); ++i)
            {
                const Star& s = m_stars[i];
                const float a = m_night * (kStarMinAlpha + kStarAlphaRange * fabsf(sinf(s.m_tw + m_clock * kStarTwinkleFreq)));
                rt->FillRectangle(D2D1::RectF(s.m_x, s.m_y, s.m_x + s.m_r, s.m_y + s.m_r), Solid(kStarColor, a));
            }
        }
    }

    void DrawCelestial(ID2D1RenderTarget* rt, const Celestial& c)
    {
        const float u = Unit();
        if (c.m_sun)
        {
            DrawGlowAt(rt, m_sunGlow, c.m_x, c.m_y, kSunGlowR * u, kSunGlowR * u, 1.0f);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.m_x, c.m_y), kSunR * u, kSunR * u), Solid(m_sunCore));
            return;
        }
        DrawGlowAt(rt, m_moonGlow, c.m_x, c.m_y, kMoonGlowR * u, kMoonGlowR * u, 1.0f);
        if (m_crescent != NULL)
        {
            D2D1_MATRIX_3X2_F old;
            rt->GetTransform(&old);
            rt->SetTransform(D2D1::Matrix3x2F::Translation(c.m_x, c.m_y) * old);
            rt->FillGeometry(m_crescent, Solid(kMoonColor, 1.0f));
            rt->SetTransform(old);
        }
    }

    void DrawHills(ID2D1RenderTarget* rt)
    {
        for (int li = 0; li < kHillLayers; ++li)
        {
            if (m_hills[li] == NULL)
            {
                continue;
            }
            const float mix = Clamp01F(kHillMix[li] + m_night * kHillNightMix[li]);
            rt->FillGeometry(m_hills[li], Solid(MixColor(m_horizon, ColorRgb(kHillTint[li]), mix)));
        }
    }

    void DrawWater(ID2D1RenderTarget* rt, const Celestial* c)
    {
        const float wy = WaterY();
        if (m_water != NULL)
        {
            m_water->SetStartPoint(D2D1::Point2F(0.0f, wy));
            m_water->SetEndPoint(D2D1::Point2F(0.0f, m_h));
            rt->FillRectangle(D2D1::RectF(0.0f, wy, m_w, m_h), m_water);
        }
        if (c == NULL)
        {
            return;
        }
        //太阳或月亮在水里的光柱
        const float u = Unit();
        for (int i = 0; i < kGlitterRows; ++i)
        {
            const float y = wy + kGlitterTop * m_scale + i * (m_h - wy) / kGlitterRows;
            const float wv = (kGlitterBaseW + i * kGlitterGrowW) * u *
                             (kGlitterMinK + kGlitterRangeK * fabsf(sinf(m_clock * kGlitterFreq + i)));
            const float a = (c->m_sun ? kGlitterSunAlpha : kGlitterMoonAlpha) * (1.0f - i / kGlitterFadeRows);
            const float x = c->m_x - wv / 2.0f + sinf(m_clock * kGlitterSwayFreq + i * kGlitterSwayPhase) * kGlitterSway * u;
            rt->FillRectangle(D2D1::RectF(x, y, x + wv, y + kGlitterH * u),
                              Solid(c->m_sun ? kSunGlitter : kMoonGlitter, a));
        }
    }

    // 水面的横向波纹：画在倒影之上，把倒影切碎
    void DrawWaterBands(ID2D1RenderTarget* rt)
    {
        D2D1_COLOR_F col = MixColor(m_horizon, ColorRgb(kWaterTint), kWaterTintMix);
        col.a = kBandAlpha;
        ID2D1SolidColorBrush* brush = Solid(col);
        const float step = kBandStep * m_scale;
        for (float y = WaterY() + 2.0f * m_scale; y < m_h; y += step)
        {
            const float off = sinf(y / m_scale * kBandFreq + m_clock * kBandTimeFreq) * kBandShift * m_scale;
            const float over = kBandOverhang * m_scale;
            rt->FillRectangle(D2D1::RectF(off - over, y, off + m_w + over, y + kBandH * m_scale), brush);
        }
    }

    void DrawRipples(ID2D1RenderTarget* rt)
    {
        for (size_t i = 0; i < m_ripples.size(); ++i)
        {
            const Ripple& r = m_ripples[i];
            const float p = r.m_t / r.m_life;
            const float rad = r.m_maxR * EaseOutCubicF(p);
            if (rad <= 0.0f)
            {
                continue;
            }
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(r.m_x, r.m_y), rad, rad * kRippleFlat),
                            Solid(kRippleColor, kRippleAlpha * (1.0f - p)), kRippleWidth * m_scale);
        }
    }

    void DrawReeds(ID2D1RenderTarget* rt)
    {
        const bool night = (m_night > kReedNightSwitch);
        for (size_t i = 0; i < m_reeds.size(); ++i)
        {
            const Reed& r = m_reeds[i];
            const float swayCss = sinf(m_clock * kReedSwayFreqA + r.m_ph) * kReedSwayA +
                                  sinf(m_clock * kReedSwayFreqB + r.m_ph * 2.0f) * kReedSwayB;
            const float sway = swayCss * m_scale;
            const float bx = r.m_x;
            const float by = m_h + kReedBelow * m_scale;
            const float tx = bx + sway;
            const float ty = by - r.m_h;
            CPathBuilder pb;
            if (pb.Begin(m_d2d))
            {
                pb.MoveTo(bx, by, false);
                pb.QuadTo(bx + sway * kReedBendX, by - r.m_h * kReedBendY, tx, ty);
                CComPtr<ID2D1PathGeometry> geo = pb.Finish();
                if (geo != NULL)
                {
                    rt->DrawGeometry(geo, Solid(night ? kReedNight : kReedDay, 1.0f), r.m_w, m_round);
                }
            }
            if (r.m_cat)
            {
                //香蒲穗
                const float cy = ty + kCatOffsetY * m_scale;
                D2D1_MATRIX_3X2_F old;
                rt->GetTransform(&old);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(swayCss * kCatTilt * kSceneRadToDeg, D2D1::Point2F(tx, cy)) * old);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, cy), kCatRx * m_scale, kCatRy * m_scale),
                                Solid(night ? kCatNight : kCatDay, 1.0f));
                rt->SetTransform(old);
            }
        }
    }

    void DrawFireflies(ID2D1RenderTarget* rt)
    {
        if (m_night < kFireflyShowNight)
        {
            return;
        }
        for (size_t i = 0; i < m_fireflies.size(); ++i)
        {
            const Firefly& f = m_fireflies[i];
            const float blink = sinf(f.m_ph * kFireflyBlinkFreq);
            const float a = (m_night - kFireflyShowNight) / (1.0f - kFireflyShowNight) *
                            (kFireflyBase + kFireflyRange * ((blink > 0.0f) ? blink : 0.0f));
            DrawGlowAt(rt, m_fireflyGlow, f.m_x, f.m_y, kFireflyR * m_scale, kFireflyR * m_scale, a);
        }
    }

    // ---- 火烈鸟 ----

    // 一条腿：髋 → 关节 → 脚，再加一笔脚趾
    void DrawLeg(ID2D1RenderTarget* rt, float hipX, float hipY, float footX, float footY)
    {
        float kneeX = 0.0f;
        float kneeY = 0.0f;
        SolveTwoBoneKnee(hipX, hipY, footX, footY, kL1, kL2, kneeX, kneeY);
        CPathBuilder pb;
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(hipX, hipY, false);
            pb.LineTo(kneeX, kneeY);
            pb.LineTo(footX, footY);
            const float toeBack = 4.0f;
            const float toeFront = 13.0f;
            const float toeDrop = 1.0f;
            pb.MoveTo(footX - toeBack, footY, false);
            pb.LineTo(footX + toeFront, footY + toeDrop);
            CComPtr<ID2D1PathGeometry> geo = pb.Finish();
            if (geo != NULL)
            {
                rt->DrawGeometry(geo, Solid(kLegColor, 1.0f), kLegWidth, m_round);
            }
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kneeX, kneeY), kKneeR, kKneeR), Solid(kLegColor, 1.0f));
    }

    // 头部（局部坐标，朝右）：粉色头、弯折的喙、黑色喙尖、黄眼睛
    void DrawHead(ID2D1RenderTarget* rt)
    {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0.0f, 0.0f), kHeadR, kHeadR), Solid(kHeadColor, 1.0f));
        if (m_beak != NULL)
        {
            rt->FillGeometry(m_beak, Solid(kBeakColor, 1.0f));
        }
        if (m_beakTip != NULL)
        {
            rt->FillGeometry(m_beakTip, Solid(kBeakTip, 1.0f));
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kEyeX, kEyeY), kEyeR, kEyeR), Solid(kEyeColor, 1.0f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kPupilX, kEyeY), kPupilR, kPupilR), Solid(kBeakTip, 1.0f));
    }

    // 站在水里的火烈鸟（局部坐标：脚底中点为原点，朝右，向上为负）；m 为局部到画面的变换
    void DrawStanding(ID2D1RenderTarget* rt, const D2D1_MATRIX_3X2_F& m)
    {
        const float sb = m_standBlend;
        const float db = m_drinkBlend;
        const bool walking = (m_state == kBirdWalk);
        const float ph = m_gait;
        const float bob = walking ? cosf(ph * 2.0f) * kWalkBob : sinf(m_clock * kStandBobFreq) * kStandBob;
        const float hipX = 0.0f;
        const float hipY = kHipY + bob;
        rt->SetTransform(m);

        //两条腿：踱步时交替抬起；单脚站立时后腿收到腹下
        float faX = kStandFootX;
        float faY = 0.0f;
        float fbX = -kStandFootX;
        float fbY = 0.0f;
        if (walking)
        {
            const float liftA = cosf(ph);
            const float liftB = cosf(ph + kScenePi);
            faX = kStepX * sinf(ph);
            faY = -((liftA > 0.0f) ? liftA : 0.0f) * kStepLift;
            fbX = kStepX * sinf(ph + kScenePi);
            fbY = -((liftB > 0.0f) ? liftB : 0.0f) * kStepLift;
        }
        fbX = LerpF(fbX, kFoldX, sb);
        fbY = LerpF(fbY, kHipY + kFoldY, sb);
        DrawLeg(rt, hipX, hipY, fbX, fbY);
        DrawLeg(rt, hipX, hipY, faX, faY);

        //身体随低头前倾
        const float tilt = kTiltUp + db * kTiltDown;
        const D2D1_MATRIX_3X2_F body = D2D1::Matrix3x2F::Rotation(tilt * kSceneRadToDeg) *
                                       D2D1::Matrix3x2F::Translation(hipX + kBodyX, hipY + kBodyY) * m;
        rt->SetTransform(body);
        if (m_tail != NULL)
        {
            rt->FillGeometry(m_tail, Solid(kTailColor, 1.0f));
        }
        m_body->SetCenter(D2D1::Point2F(0.0f, 0.0f));
        m_body->SetGradientOriginOffset(D2D1::Point2F(kBodyGlowX, kBodyGlowY));
        m_body->SetRadiusX(kBodyGradR);
        m_body->SetRadiusY(kBodyGradR);
        m_body->SetOpacity(1.0f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0.0f, 0.0f), kBodyRx, kBodyRy), m_body);
        //收拢的翅膀与黑色飞羽
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(kWingTilt * kSceneRadToDeg, D2D1::Point2F(kWingCx, kWingCy)) * body);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kWingCx, kWingCy), kWingRx, kWingRy), Solid(kFoldWing, kFoldWingAlpha));
        rt->SetTransform(body);
        if (m_foldFeather != NULL)
        {
            rt->FillGeometry(m_foldFeather, Solid(kFeather, 1.0f));
        }

        //脖子：抬头与低头两套三次曲线控制点之间插值
        rt->SetTransform(m);
        const float sway = walking ? sinf(ph) * kNeckSwayWalk : sinf(m_clock * kNeckSwayFreq) * kNeckSwayStand;
        const float p0x = 40.0f;
        const float p0y = kHipY - 38.0f + bob + db * 14.0f;
        const float c1x = LerpF(80.0f, 112.0f, db) + sway;
        const float c1y = LerpF(kHipY - 70.0f, kHipY - 40.0f, db) + bob;
        const float c2x = LerpF(8.0f, 126.0f, db);
        const float c2y = LerpF(kHipY - 112.0f, -92.0f, db) + bob * (1.0f - db);
        const float hdx = LerpF(48.0f, 114.0f, db) + sway * 0.5f;
        const float nod = (db > kDrinkDeep) ? sinf(m_clock * kDrinkNodFreq) * kDrinkNod : 0.0f;
        const float hdy = LerpF(kHipY - 152.0f, -8.0f, db) + bob * (1.0f - db) + nod;
        CPathBuilder pb;
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(p0x, p0y, false);
            pb.CubicTo(c1x, c1y, c2x, c2y, hdx, hdy);
            CComPtr<ID2D1PathGeometry> neck = pb.Finish();
            if (neck != NULL)
            {
                rt->DrawGeometry(neck, Solid(kNeckColor, 1.0f), kNeckWidth, m_round);
            }
        }
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(LerpF(kHeadTiltUp, kHeadTiltDown, db) * kSceneRadToDeg) *
                         D2D1::Matrix3x2F::Translation(hdx, hdy) * m);
        DrawHead(rt);
    }

    // 一只翅膀：up 为 -1~1，正数向上扬；farSide 为远侧（颜色略深、半透明）
    void DrawWing(ID2D1RenderTarget* rt, float up, bool farSide)
    {
        const float tipY = -kWingSpan * up;
        const float alpha = farSide ? kWingFarAlpha : 1.0f;
        CPathBuilder pb;
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(26.0f, -4.0f, true);
            pb.QuadTo(10.0f, tipY * 0.55f, -12.0f, tipY * 0.6f);
            pb.LineTo(-34.0f, tipY * 0.58f);
            pb.QuadTo(-24.0f, tipY * 0.2f, -22.0f, -2.0f);
            pb.Close();
            CComPtr<ID2D1PathGeometry> wing = pb.Finish();
            if (wing != NULL)
            {
                rt->FillGeometry(wing, Solid(farSide ? kWingFar : kWingNear, alpha));
            }
        }
        //外半段的黑色飞羽
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(-12.0f, tipY * 0.6f, true);
            pb.QuadTo(-30.0f, tipY, -48.0f, tipY * 1.02f);
            pb.LineTo(-34.0f, tipY * 0.58f);
            pb.Close();
            CComPtr<ID2D1PathGeometry> tip = pb.Finish();
            if (tip != NULL)
            {
                rt->FillGeometry(tip, Solid(kFeather, alpha));
            }
        }
    }

    // 飞行中的火烈鸟（局部坐标：身体中心为原点，朝右）；m 为局部到画面的变换
    void DrawFlying(ID2D1RenderTarget* rt, const D2D1_MATRIX_3X2_F& m)
    {
        rt->SetTransform(m);
        const float flapA = sinf(m_flap);
        DrawWing(rt, flapA * kWingFarFlap, true);
        if (m_flyLegs != NULL)
        {
            rt->DrawGeometry(m_flyLegs, Solid(kLegColor, 1.0f), kFlyLegWidth, m_round);
        }
        m_body->SetCenter(D2D1::Point2F(0.0f, 0.0f));
        m_body->SetGradientOriginOffset(D2D1::Point2F(kFlyGlow, -kFlyGlow));
        m_body->SetRadiusX(kFlyGradR);
        m_body->SetRadiusY(kFlyGradR);
        m_body->SetOpacity(1.0f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0.0f, 0.0f), kFlyBodyRx, kFlyBodyRy), m_body);
        CPathBuilder pb;
        if (pb.Begin(m_d2d))
        {
            pb.MoveTo(44.0f, -4.0f, false);
            pb.QuadTo(100.0f, -10.0f + flapA * kFlyNeckBob, 150.0f, -8.0f);
            CComPtr<ID2D1PathGeometry> neck = pb.Finish();
            if (neck != NULL)
            {
                rt->DrawGeometry(neck, Solid(kNeckColor, 1.0f), kFlyNeckWidth, m_round);
            }
        }
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(kFlyHeadTilt * kSceneRadToDeg) *
                         D2D1::Matrix3x2F::Translation(kFlyHeadX, kFlyHeadY) * m);
        DrawHead(rt);
        rt->SetTransform(m);
        DrawWing(rt, flapA, false);
    }

    // 按当前状态把火烈鸟画进画面；world 为额外的变换（画倒影时上下翻转）
    void DrawBird(ID2D1RenderTarget* rt, const D2D1_MATRIX_3X2_F& world)
    {
        const float u = Unit();
        const bool flying = (m_state == kBirdTakeoff || m_state == kBirdFly || m_state == kBirdLanding ||
                             m_state == kBirdAway);
        D2D1_MATRIX_3X2_F old;
        rt->GetTransform(&old);
        if (flying)
        {
            const float y = LerpF(GroundY() - kFlyLowY * u, CruiseY(), m_alt) + sinf(m_clock * kFlyBobFreq) * kFlyBob * u;
            DrawFlying(rt, D2D1::Matrix3x2F::Scale(m_facing * u * kFlyScale, u * kFlyScale) *
                               D2D1::Matrix3x2F::Translation(m_x, y) * world * old);
        }
        else
        {
            DrawStanding(rt, D2D1::Matrix3x2F::Scale(m_facing * u, u) * D2D1::Matrix3x2F::Translation(m_x, GroundY()) *
                                 world * old);
        }
        rt->SetTransform(old);
    }

private:
    float                             m_w;              // 画面宽
    float                             m_h;              // 画面高
    float                             m_scale;          // 缩放比例
    float                             m_clock;          // 场景时钟（秒）
    float                             m_hour;           // 当前本地时刻（小时，带小数）
    int                               m_paletteMinute;  // 天空等颜色按哪一分钟算的；-1 表示还没算
    float                             m_night;          // 夜色浓度 0~1（随颜色每分钟重算）
    float                             m_lastRipple;     // 上次泛起涟漪的场景时刻
    D2D1_COLOR_F                      m_zenith;         // 当前天顶色
    D2D1_COLOR_F                      m_horizon;        // 当前地平线色
    D2D1_COLOR_F                      m_sunCore;        // 当前太阳颜色
    ID2D1Factory*                     m_d2d;            // Direct2D 工厂（借用），每帧构建曲线用
    std::vector<Star>                 m_stars;          // 星星
    std::vector<Reed>                 m_reeds;          // 芦苇
    std::vector<Firefly>              m_fireflies;      // 萤火虫
    std::vector<Ripple>               m_ripples;        // 涟漪
    float                             m_x;              // 火烈鸟横坐标
    float                             m_facing;         // 朝向：1 朝右，-1 朝左
    BirdState                         m_state;          // 当前状态
    float                             m_stateT;         // 进入当前状态后的秒数
    float                             m_stateDur;       // 当前状态计划持续的秒数（踱步、站立、喝水）
    float                             m_gait;           // 步态相位（弧度）
    float                             m_standBlend;     // 0 为两脚着地，1 为单脚站立
    float                             m_drinkBlend;     // 0 为抬头，1 为低头喝水
    float                             m_alt;            // 0 为在水里，1 为巡航高度
    float                             m_flap;           // 扇翅相位（弧度）
    float                             m_sinceFlight;    // 距上次落地的秒数
    float                             m_landX;          // 降落的落点横坐标
    CComPtr<ID2D1PathGeometry>        m_hills[kHillLayers];  // 两层远山
    CComPtr<ID2D1PathGeometry>        m_crescent;       // 月牙（以月盘中心为原点）
    CComPtr<ID2D1PathGeometry>        m_beak;           // 喙
    CComPtr<ID2D1PathGeometry>        m_beakTip;        // 黑色喙尖
    CComPtr<ID2D1PathGeometry>        m_tail;           // 尾羽
    CComPtr<ID2D1PathGeometry>        m_foldFeather;    // 收拢翅膀上的黑色飞羽
    CComPtr<ID2D1PathGeometry>        m_flyLegs;        // 飞行时拖在后面的两条腿
    CComPtr<ID2D1StrokeStyle>         m_round;          // 圆头描边
    CComPtr<ID2D1SolidColorBrush>     m_solid;          // 复用的纯色画刷
    CComPtr<ID2D1LinearGradientBrush> m_sky;            // 天空渐变（每分钟重建）
    CComPtr<ID2D1LinearGradientBrush> m_water;          // 湖水渐变（每分钟重建）
    CComPtr<ID2D1RadialGradientBrush> m_sunGlow;        // 太阳光晕（每分钟重建；太阳不在天上时为空）
    CComPtr<ID2D1RadialGradientBrush> m_moonGlow;       // 月光
    CComPtr<ID2D1RadialGradientBrush> m_fireflyGlow;    // 萤火虫光点
    CComPtr<ID2D1RadialGradientBrush> m_body;           // 身体的径向渐变（站立与飞行共用，绘制前调整参数）
    CComPtr<ID2D1Layer>               m_layer;          // 画半透明倒影用的图层
};

} // namespace

ILockScene* CreateFlamingoScene()
{
    return new CFlamingoScene();
}

} // namespace flamingolock
