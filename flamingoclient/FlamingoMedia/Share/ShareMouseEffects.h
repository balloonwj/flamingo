/**
 *  屏幕共享与录屏的鼠标效果（第 16 期），ShareMouseEffects.h
 *
 *  docs/屏幕共享与录制方案.md 第 16 期：发起端把鼠标效果叠加进采到的画面后再编码（录屏同样叠加后再写文件），
 *  观看端不用改、也不加协议。三种效果各有开关：
 *    - 鼠标光圈：以光标为圆心的半透明圆，颜色从五种预设色里选；
 *    - 点击效果：每次单击在点击处画一个向外扩散并淡出的圆环，左键红色、右键蓝色；
 *    - 鼠标轨迹：光标最近一小段时间走过的路径，越旧越细越淡，颜色从五种预设色里选。
 *  设置（三个开关与两种颜色）按账号保存在 exe 的配置里，以 SerializeMouseEffectSettings 的文本形式在 exe 与 DLL 之间传递。
 *
 *  本模块不依赖窗口与采集对象，只做计算与 BGRA 像素绘制，便于单元测试，见
 *  flamingoclient/Source/Tests/ShareMouseEffectsTests.cpp。
 *
 *  典型用法（推流线程每轮）：
 *      POINT pt;
 *      const bool inside = ::GetCursorPos(&pt) && MapScreenToFrame(pt, sourceRect, w, h, framePt);
 *      effects.Update(framePt, inside, leftDown, rightDown, nowMs);
 *      effects.Render(bgra, w, h, stride, nowMs);
 *      if (effects.IsAnimating(nowMs)) 继续出帧;
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <windows.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace screenshare {

//鼠标效果的预设颜色（第 16 期）：光圈颜色与轨迹颜色各从中选一种
enum share_mouse_color
{
    share_mouse_color_yellow = 0,   //黄色：光圈的默认色
    share_mouse_color_red    = 1,   //红色：轨迹的默认色
    share_mouse_color_blue   = 2,   //蓝色
    share_mouse_color_green  = 3,   //绿色
    share_mouse_color_purple = 4,   //紫色
};

//预设颜色的个数
const int kShareMouseColorCount = 5;

//光圈半径，单位画面像素
const int kShareMouseHaloRadius = 24;
//光圈的不透明度（0～255）：足够淡，不挡住指针
const int kShareMouseHaloAlpha = 90;
//轨迹点保留的时长，单位毫秒：超过这个时长的点不再画
const int64_t kShareMouseTrailLifeMs = 600;
//轨迹最新一段与最旧一段的线宽，单位画面像素
const int kShareMouseTrailMaxWidth = 6;
const int kShareMouseTrailMinWidth = 1;
//轨迹最新一段的不透明度（0～255），越旧越淡、到时为 0
const int kShareMouseTrailAlpha = 200;
//点击圈从出现到消失的时长，单位毫秒
const int64_t kShareMouseClickLifeMs = 400;
//点击圈起始与结束的半径、圆环线宽，单位画面像素
const int kShareMouseClickStartRadius = 8;
const int kShareMouseClickEndRadius = 32;
const int kShareMouseClickRingWidth = 3;
//点击圈起始的不透明度（0～255），到时为 0
const int kShareMouseClickAlpha = 230;
//左键、右键点击圈的颜色（固定，不随光圈颜色变）
const COLORREF kShareMouseClickLeftColor = RGB(220, 50, 50);
const COLORREF kShareMouseClickRightColor = RGB(45, 108, 223);

//鼠标效果的设置：三个开关与两种颜色
struct ShareMouseEffectSettings
{
    bool m_halo;        //是否画鼠标光圈；默认开
    bool m_click;       //是否画点击效果；默认关
    bool m_trail;       //是否画鼠标轨迹；默认关
    int  m_haloColor;   //光圈颜色，取值见 share_mouse_color；默认黄色
    int  m_trailColor;  //轨迹颜色，取值见 share_mouse_color；默认红色

    ShareMouseEffectSettings();
};

/**
 *  预设颜色的 RGB。
 *  @param color 取值见 share_mouse_color；超出范围时按黄色
 */
COLORREF ShareMouseColorRgb(int color);

/**
 *  预设颜色的名字（菜单里显示），如「黄色」。
 *  @param color 取值见 share_mouse_color；超出范围时按黄色
 */
std::wstring ShareMouseColorName(int color);

/**
 *  把设置写成一行文本，存进 exe 的账号配置，形如「halo=1;click=0;trail=0;halocolor=0;trailcolor=1」。
 */
std::string SerializeMouseEffectSettings(const ShareMouseEffectSettings& settings);

/**
 *  从 SerializeMouseEffectSettings 的文本读回设置。缺失、取值不认识的项按默认值，整段为空或乱写时全部是默认值。
 */
ShareMouseEffectSettings ParseMouseEffectSettings(const std::string& text);

/**
 *  把光标的屏幕坐标换算成画面坐标。
 *  @param screenPt   光标的屏幕坐标（物理像素）
 *  @param sourceRect 被采集内容在虚拟桌面上的矩形（物理像素，见 IShareCapturer::SourceRect）
 *  @param frameW     画面宽，单位像素
 *  @param frameH     画面高，单位像素
 *  @param out        [出参] 画面坐标（返回 true 时有效）；画面与采集矩形大小不同时按比例换算
 *  @return 光标在采集矩形里（含左上边、不含右下边）且矩形与画面都不为空时返回 true
 */
bool MapScreenToFrame(POINT screenPt, const RECT& sourceRect, int frameW, int frameH, POINT& out);

//控制条与录屏条菜单里的一项（第 16 期）：顶层为三个开关与两个颜色子菜单
struct ShareMouseMenuItem
{
    UINT                            m_id;        //菜单项 id；子菜单本身为 0
    std::wstring                    m_text;      //显示的文字
    bool                            m_checked;   //是否打勾：开关项为开着，颜色项为当前选中的颜色
    int                             m_swatchColor; //颜色项代表的颜色（取值见 share_mouse_color），菜单里这一项的图标是这种颜色的小方块；不是颜色项时为 -1
    std::vector<ShareMouseMenuItem> m_children;  //子菜单项；为空表示这是一个普通项

    ShareMouseMenuItem();
};

/**
 *  生成鼠标效果的菜单：「鼠标光圈」「点击效果」「鼠标轨迹」三个开关，「光圈颜色」「轨迹颜色」两个子菜单（各五种颜色）。
 *  @param settings 当前设置，决定哪些项打勾
 *  @param idBase   菜单项 id 的起点：开关为 idBase+0～+2，光圈颜色为 idBase+10～+14，轨迹颜色为 idBase+20～+24
 */
std::vector<ShareMouseMenuItem> BuildMouseEffectMenu(const ShareMouseEffectSettings& settings, UINT idBase);

/**
 *  按选中的菜单项改设置：开关项取反，颜色项改成对应颜色。
 *  @param settings [入参 / 出参] 设置
 *  @param id       选中的菜单项 id
 *  @param idBase   同 BuildMouseEffectMenu
 *  @return id 是鼠标效果菜单里的项、设置已改返回 true；不认识的 id 返回 false，设置不变
 */
bool ApplyMouseEffectMenuCommand(ShareMouseEffectSettings& settings, UINT id, UINT idBase);

//颜色菜单项图标的边长（像素），与 DuiMenu 画图标的尺寸相同
const int kShareMouseSwatchSize = 16;

/**
 *  画颜色菜单项的图标（第 16 期补充）：透明底上一个该颜色的方块，最外一圈是同色加深的细边；checked 为 true 时
 *  方块里画白色对勾（DuiMenu 的勾选标记与图标共用一列，当前颜色改在方块里标出）。输出为预乘 alpha 的 BGRA，透明处四个分量都是 0。
 *  @param bgra    第一行像素，由调用方分配，至少 stride × size 字节；为 NULL 时什么也不做
 *  @param size    边长，单位像素（一般为 kShareMouseSwatchSize，其它尺寸按 16 像素的设计等比缩放）
 *  @param stride  行跨度，单位字节，至少 size × 4
 *  @param color   方块的颜色
 *  @param checked 是否是当前选中的颜色
 */
void RenderMouseColorSwatch(uint8_t* bgra, int size, int stride, COLORREF color, bool checked);

//鼠标效果的状态与绘制：记下光标轨迹与点击，按当前时刻把效果画到 BGRA 画面上
class ShareMouseEffects
{
public:
    ShareMouseEffects();

    /**
     *  换设置。关掉的效果随之清空（如关掉轨迹时清掉已记的轨迹点）。
     *  @param settings 新设置
     */
    void SetSettings(const ShareMouseEffectSettings& settings);

    //当前设置
    const ShareMouseEffectSettings& Settings() const;

    /**
     *  每轮喂一次光标状态。
     *  @param framePt   光标在画面里的坐标（MapScreenToFrame 的结果）；inside 为 false 时忽略
     *  @param inside    光标是否在画面里：不在时不记轨迹点、不触发点击圈，光圈也不画
     *  @param leftDown  左键当前是否按下：由松开变为按下的那一刻触发左键点击圈
     *  @param rightDown 右键当前是否按下：同上，触发右键点击圈
     *  @param nowMs     本机时刻，单位毫秒（单调递增）
     */
    void Update(POINT framePt, bool inside, bool leftDown, bool rightDown, int64_t nowMs);

    /**
     *  把效果画到 BGRA 画面上，只改效果覆盖的像素。依次画轨迹、光圈、点击圈。
     *  @param bgra   第一行像素；为 NULL 时什么也不做
     *  @param width  画面宽，单位像素
     *  @param height 画面高，单位像素
     *  @param stride 行跨度，单位字节
     *  @param nowMs  本机时刻，单位毫秒；据此算轨迹与点击圈的淡出
     */
    void Render(uint8_t* bgra, int width, int height, int stride, int64_t nowMs) const;

    /**
     *  效果是否还在变化（轨迹还没淡完、点击圈还没消失），推流线程据此在画面不变时继续出帧。
     *  @param nowMs 本机时刻，单位毫秒
     */
    bool IsAnimating(int64_t nowMs) const;

    //还在显示的轨迹点个数（测试用）
    size_t TrailPointCount(int64_t nowMs) const;

    //还在显示的点击圈个数（测试用）
    size_t ActiveClickCount(int64_t nowMs) const;

    //清空轨迹、点击圈与按键状态（开始共享、恢复共享时调用）
    void Reset();

private:
    //轨迹上的一个点
    struct TrailPoint
    {
        POINT   m_pt;     //画面坐标
        int64_t m_time;   //记下的时刻，毫秒
    };

    //一个点击圈
    struct ClickRing
    {
        POINT    m_pt;      //画面坐标
        int64_t  m_time;    //按下的时刻，毫秒
        COLORREF m_color;   //左键红、右键蓝
    };

    ShareMouseEffectSettings m_settings;   //当前设置
    std::deque<TrailPoint>   m_trail;      //轨迹点，按时间先后
    std::vector<ClickRing>   m_clicks;     //点击圈，按时间先后
    POINT                    m_cursor;     //最近一次的光标画面坐标
    bool                     m_inside;     //最近一次光标是否在画面里
    bool                     m_leftDown;   //上一次左键是否按下（用来找按下的那一刻）
    bool                     m_rightDown;  //上一次右键是否按下
    POINT                    m_lastTrailPt;   //上次记轨迹点时的光标画面坐标；光标没动时不重复记点（轨迹点过期后也不再补记）
    bool                     m_hasLastTrailPt; //是否记过轨迹点（光标离开画面、关掉轨迹、Reset 时清掉，回到画面后第一下照常记点）
};


/**
 *  物理按键换算成逻辑按键（第 16 期 16b）。GetAsyncKeyState(VK_LBUTTON) 读的是物理左键；系统设置了「切换主要和次要按钮」
 *  时物理左键是逻辑右键。点击效果按用户感知的左右键取色（左键红、右键蓝），所以要换算。
 *  @param physicalLeft  物理左键是否按下
 *  @param physicalRight 物理右键是否按下
 *  @param swapped       系统是否交换了主次按钮（GetSystemMetrics(SM_SWAPBUTTON) 非 0）
 *  @param left          [出参] 逻辑左键是否按下
 *  @param right         [出参] 逻辑右键是否按下
 */
void LogicalMouseButtons(bool physicalLeft, bool physicalRight, bool swapped, bool& left, bool& right);

/**
 *  推流线程里的鼠标效果一环（第 16 期 16b）：每轮喂一次光标状态，判断要不要为效果多出一帧；要编码时把效果画进画面的副本。
 *  采集器只在画面内容变化时出新帧，光标移动、轨迹与点击圈淡出时画面可能并没有变，所以由本类告诉推流线程「效果要求出帧」；
 *  光标不动、效果不变时不要求，静止时不多占带宽。原始画面不改：光标在画面没变时移动，下一轮还要从原始画面重新合成。
 *
 *  线程：SetSettings / Settings 任意线程；其余只在推流线程上调用。
 *
 *  典型用法（推流线程每轮）：
 *      const bool fxNeedsFrame = m_mouseFx.Tick(cursor, left, right, sourceRect, w, h, now);
 *      ...决定编码后...
 *      int stride = 0;
 *      const uint8_t* pixels = m_mouseFx.Compose(raw, w, h, rawStride, now, stride);
 *      ...编码 pixels...
 *      m_mouseFx.OnEncoded();
 */
class ShareMouseFxStage
{
public:
    ShareMouseFxStage();

    /**
     *  换设置（任意线程）。下一轮 Tick 返回 true，让画面随之更新（如关掉光圈后把画面上的光圈擦掉）。
     *  @param settings 新设置
     */
    void SetSettings(const ShareMouseEffectSettings& settings);

    //当前设置（任意线程）：最近一次 SetSettings 设的值
    ShareMouseEffectSettings Settings() const;

    /**
     *  每轮调用一次（推流线程）：把光标换算到画面坐标交给效果状态，并判断这一轮要不要为效果多出一帧。
     *  @param screenPt   光标的屏幕坐标（物理像素）
     *  @param leftDown   逻辑左键是否按下（见 LogicalMouseButtons）
     *  @param rightDown  逻辑右键是否按下
     *  @param sourceRect 被采集内容在虚拟桌面上的矩形（物理像素）
     *  @param frameW     画面宽，单位像素；还没有画面时传 0
     *  @param frameH     画面高，单位像素
     *  @param nowMs      本机时刻，单位毫秒（单调递增）
     *  @return 要求出帧时返回 true：设置变了；光圈开着且光标的位置、或「在不在画面里」与上次编码时不同；
     *          轨迹或点击圈还在淡出；淡出刚结束（再出一帧把残影擦掉）
     */
    bool Tick(POINT screenPt, bool leftDown, bool rightDown, const RECT& sourceRect, int frameW, int frameH,
              int64_t nowMs);

    /**
     *  要编码时调用（推流线程）：效果有可见内容时，把画面拷进内部缓冲、画上效果并返回缓冲；否则原样返回 raw。
     *  @param raw       原始画面第一行像素（BGRA），本函数不修改它
     *  @param width     画面宽，单位像素
     *  @param height    画面高，单位像素
     *  @param stride    原始画面的行跨度，单位字节
     *  @param nowMs     本机时刻，单位毫秒
     *  @param outStride [出参] 返回画面的行跨度，单位字节
     *  @return 要编码的画面第一行像素：缓冲（归本对象所有，到下一次 Compose 或 Reset 之前有效）或 raw 本身
     */
    const uint8_t* Compose(const uint8_t* raw, int width, int height, int stride, int64_t nowMs, int& outStride);

    //一帧已编码（推流线程）：记下这一帧里的光标与效果状态，之后光标不动、效果不变就不再要求出帧
    void OnEncoded();

    //清空效果状态与「上次编码时」的记录（推流开始、切换共享内容时，推流线程）；设置保留
    void Reset();

private:
    mutable std::mutex       m_settingsMutex;    //保护以下两项
    ShareMouseEffectSettings m_settings;         //最近一次 SetSettings 设的值
    bool                     m_settingsChanged;  //设置变了、推流线程还没取走

    ShareMouseEffects    m_effects;          //效果状态（推流线程）
    bool                 m_inside;           //最近一轮光标是否在画面里
    POINT                m_framePt;          //最近一轮光标的画面坐标（m_inside 为 true 时有效）
    bool                 m_animating;        //最近一轮效果是否还在变化（轨迹或点击圈在淡出）
    bool                 m_dirty;            //设置变了、还没有编码过一帧
    bool                 m_encodedValid;     //是否编码过至少一帧（以下三项有效）
    bool                 m_encodedInside;    //上次编码时光标是否在画面里
    POINT                m_encodedPt;        //上次编码时光标的画面坐标
    bool                 m_encodedAnimating; //上次编码时效果是否还在变化
    std::vector<uint8_t> m_buffer;           //画上效果的画面（BGRA，行跨度 = 宽 × 4）
};

} // namespace screenshare
