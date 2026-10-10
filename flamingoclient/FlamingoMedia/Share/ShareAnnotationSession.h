/**
 *  屏幕共享房间里本人的标注输入与收发，ShareAnnotationSession.h
 *
 *  把本人的鼠标操作（归一化坐标）变成标注操作：先应用到本地的 ShareAnnotationModel，再编码成标注轨的帧经房间中转发出；
 *  收到别人的标注帧时解码后应用到同一个模型（docs/屏幕共享与录制方案.md 5.9）。规则：
 *    - 画笔、荧光笔：按下即发 begin，移动时攒点，每 kAnnotAppendIntervalMs 或攒够 kAnnotAppendMaxPoints 个点发一次 append，
 *      松开时把剩下的点发出并发 end；
 *    - 箭头、矩形、椭圆：拖动中只在本地预览（PendingShape），松开时一次发出 begin（两点）与 end；
 *    - 文字：AddText 一次发出；橡皮：按下与拖动经过哪一笔就擦哪一笔；撤销：擦除本人最近的一笔；
 *    - 清空、整笔补发只有发起人能做（模型也只接受发起人发出的这两种操作）。
 *  本类不依赖窗口，单元测试直接驱动；时间由调用方传入。只在 UI 线程使用。
 *
 *  典型用法：
 *      session.Start(&streamer, seat, ::GetTickCount());
 *      session.PointerDown(pt, tolerance, now); session.PointerMove(...); session.PointerUp(...);
 *      if (session.OnRemoteFrame(frame.m_senderSeat, frame.m_data)) 重画;
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "ShareAnnotation.h"
#include "ShareAnnotationObjects.h"
#include "ShareAudioTransport.h"

namespace screenshare {

//界面上的橡皮工具（不是笔迹种类，不出现在线路上）：整笔擦
const int kAnnotEraser = 100;
//粗细的档位（第 19 期）：细、中、粗；中档与第 6 期的默认线宽一致
enum AnnotationWidthLevel
{
    annot_width_thin   = 0,   //细
    annot_width_medium = 1,   //中（默认）
    annot_width_thick  = 2,   //粗
};
//画笔位置的广播间隔（毫秒，第 19 期）：位置变了且距上次发送超过它才发
const int64_t kAnnotPointerIntervalMs = 100;
//各种笔迹的默认线宽 / 字号（换算见 kAnnotWidthScale / kAnnotTextScale）：画笔、荧光笔、图形、文字
const int kAnnotPenWidth = 4;
const int kAnnotHighlighterWidth = 18;
const int kAnnotShapeWidth = 4;
const int kAnnotTextSize = 28;
//画笔攒点后发送的时间间隔（毫秒）与点数上限
const int64_t kAnnotAppendIntervalMs = 40;
const size_t kAnnotAppendMaxPoints = 64;
//默认颜色：红
const uint32_t kAnnotDefaultColor = 0xE53935;
//界面上的选择工具（第 20 期，不是笔迹种类，不出现在线路上）：选中对象后移动、缩放、改样式、删除，双击改文字
const int kAnnotSelect = 101;
//拖动选中的对象时发移动操作的最短间隔（毫秒）
const int64_t kAnnotMoveIntervalMs = 40;
//共享内容默认的宽高比（宽 ÷ 高），界面告诉会话实际值之前用它
const double kAnnotDefaultAspect = 16.0 / 9.0;
//点一下放的图元的默认大小（纵向的归一化单位，约为画面高度的 6%；横向按宽高比换算成正方形）
const int kAnnotIconDefaultSize = 3900;

//改选中对象的哪一项样式（RestyleSelection 的 attribute，第 20 期）；只改用得上这一项的对象
enum AnnotRestyle
{
    annot_restyle_color      = 0,   //颜色：value 为 0x00RRGGBB；全部对象
    annot_restyle_width      = 1,   //粗细档位：value 取 AnnotationWidthLevel；文字、数字标号为字号档位
    annot_restyle_line_style = 2,   //线型：value 取 AnnotationLineStyle；线条、箭头、连接线、形状
    annot_restyle_fill       = 3,   //填充：value 取 AnnotationFill；形状、块状箭头
    annot_restyle_start_cap  = 4,   //起点样式：value 取 AnnotationCap；直线、折线、曲线、箭头、连接线
    annot_restyle_end_cap    = 5,   //终点样式：同上
    annot_restyle_routing    = 6,   //走向：value 取 AnnotationRouting；箭头、连接线
    annot_restyle_text_bold  = 7,   //文字加粗：1 加粗、0 不加粗；文字
    annot_restyle_text_bg    = 8,   //文字背景：value 取 AnnotationTextBg；文字
    annot_restyle_icon       = 9,   //图元种类：value 取 AnnotationIcon；图元
};

//选择工具要画的提示（第 20 期）：选中对象的框、单选时的句柄、正在拖的选择框
struct AnnotSelectionVisual
{
    std::vector<AnnotRect>       m_boxes;            //每个选中对象的外接矩形
    std::vector<AnnotationPoint> m_handles;          //单选时的句柄；多选时为空
    bool                         m_endpointHandles;  //句柄是不是两端（直线、箭头、连接线）
    bool                         m_hasBand;          //是否正在拖选择框
    AnnotRect                    m_band;             //选择框

    AnnotSelectionVisual();
};

//本人的标注输入与收发
class ShareAnnotationSession
{
public:
    ShareAnnotationSession();

    /**
     *  开始：清空笔迹，记下发送通道与本人座位。
     *  @param transport 发送通道（房间中转连接），不拥有，Stop 之前须一直有效；为 NULL 时只在本地生效（测试用）
     *  @param selfSeat  本人座位号；发起人为 kAnnotHostSeat
     *  @param idSeed    笔迹编号的起点（传入当前时刻一类的值）：座位号会被后来的人重用，起点不同可避免与前一个人的编号相同
     */
    void Start(ShareAudioTransport* transport, int selfSeat, uint32_t idSeed);

    //停止：清空笔迹，不再发送
    void Stop();

    bool IsRunning() const;

    //当前工具：AnnotationTool 之一或 kAnnotEraser；切换工具时结束进行中的一笔
    void SetTool(int tool);
    int Tool() const;

    //当前颜色 0x00RRGGBB
    void SetColor(uint32_t color);
    uint32_t Color() const;

    //==== 第 19 期：样式（只影响之后画的笔迹） ====

    //粗细档位，取值见 AnnotationWidthLevel；文字为字号档位、数字标号为大小档位
    void SetWidthLevel(int level);
    int WidthLevel() const;

    //线型，取值见 AnnotationLineStyle；用于直线、折线、曲线、箭头与形状的描边
    void SetLineStyle(int lineStyle);

    /**
     *  箭头两端的样式（箭头工具用），取值见 AnnotationCap。默认起点无、终点实心三角。
     */
    void SetArrowCaps(int startCap, int endCap);

    /**
     *  直线、折线、曲线两端的样式，取值见 AnnotationCap。默认两端都无。
     */
    void SetLineCaps(int startCap, int endCap);

    //箭头走向，取值见 AnnotationRouting
    void SetRouting(int routing);

    //形状与块状箭头的填充，取值见 AnnotationFill
    void SetFill(int fill);

    //块状箭头的样式，取值见 AnnotationBlockArrow
    void SetBlockVariant(int variant);

    /**
     *  数字标号的样式与序号写法。
     *  @param badgeStyle 取值见 AnnotationBadgeStyle
     *  @param sequence   取值见 AnnotationSequence
     */
    void SetBadgeStyle(int badgeStyle, int sequence);

    /**
     *  网络拓扑图元的种类（第 20 期，图元工具用）。
     *  @param kind 取值见 AnnotationIcon
     */
    void SetIconKind(int kind);
    int IconKind() const;

    /**
     *  连接线的走向（第 20 期，连接线工具用），取值见 AnnotationRouting。默认直线。
     */
    void SetConnectorRouting(int routing);

    /**
     *  线宽与字号的缩放（第 21 期，白板用）：白板的世界比可见区域大，按比例缩小线宽与字号，显示上与屏幕标注一致。
     *  各档的线宽乘以它后四舍五入，不小于 1。默认 1。
     *  @param scale 缩放比例，不大于 0 时忽略
     */
    void SetWidthScale(double scale);

    /**
     *  当前画面（第 22 期 22e）：多人同时共享时每块画面各有自己的标注。此后新画的笔迹、画笔位置都带上它，橡皮、撤销、选择、
     *  连接线吸附只作用于这块画面上的笔迹，清空也只清它。换画面时取消正在进行的拖动与选择。
     *  @param canvas 画面在中转里的座位；默认 kAnnotDefaultCanvas（建房间的主持人的画面）
     */
    void SetCanvas(int canvas);

    //当前画面（第 22 期 22e）
    int Canvas() const;

    /**
     *  本地清掉某块画面上的笔迹（第 22 期 22e：那块画面已不在共享，座位以后可能分给别人），不发给别人；当前画面就是它时
     *  同时取消正在进行的拖动与选择。
     *  @return 有笔迹被清掉返回 true
     */
    bool ClearCanvasLocal(int canvas);

    /**
     *  广播一块白板的视野（第 23 期，操作 12，只由白板的建立者发）：背景与本人的可见区域。先应用到本地模型再发出。
     *  @param canvas 白板的画面座位
     *  @param view   背景与可见区域（发送者座位号与时刻不用填）
     *  @return 已发出返回 true；没开始、或视野不合法（宽不大于 0）返回 false
     */
    bool SendBoardView(int canvas, const AnnotationBoardView& view);
    double WidthScale() const;

    //「从头编号」：下一个数字标号从 1 开始
    void ResetBadgeNumber();

    //下一个数字标号的号码
    int NextBadgeNumber() const;

    /**
     *  文字的样式。
     *  @param bold    是否加粗
     *  @param textBg  背景，取值见 AnnotationTextBg
     *  @param bgColor 填充背景的颜色（textBg 为 annot_text_bg_fill 时用）
     */
    void SetTextStyle(bool bold, int textBg, uint32_t bgColor);

    /**
     *  按住 Shift（界面在键盘状态变化时调用）：拖动形状时画成正形，直线、箭头、连接线吸附到 45 度的整数倍（按内容的宽高比
     *  换算，见 AnnotConstrainDrag）。2026-10-07 起拖动中 Shift 变化时，按最近一次的鼠标位置重新算终点，预览随之更新。
     *  @param down 是否按着 Shift
     *  @return 正在拖动形状且终点因此变了（调用方应重画预览）时返回 true
     */
    bool SetShiftDown(bool down);

    /**
     *  折线、曲线：结束正在添加的顶点并发出（界面在双击或按回车时调用）。不到两个顶点时丢弃。
     *  @return 发出了一笔返回 true
     */
    bool FinishPending(int64_t nowMs);

    /**
     *  标注模式下光标移动（没有按着也调用）：位置变了且距上次发送超过 kAnnotPointerIntervalMs 时发一条画笔位置，
     *  房间里的人据此显示本人的画笔与名字。只发送，不进本地模型。
     */
    void PointerHover(const AnnotationPoint& pt, int64_t nowMs);

    //退出标注模式或光标离开画面：发过画笔位置时发一条「画笔离开」
    void PointerLeave();

    /**
     *  定时调用（界面每 100 毫秒左右）：清掉淡完的激光笔与过期的画笔位置。
     *  @return 有变化（需要重画）返回 true
     */
    bool Tick(int64_t nowMs);

    /**
     *  记下某个座位的人的显示名（第 19 期）：画别人的画笔位置时标在圆点旁边。控制器在成员变化时调用。
     *  @param seat 座位号
     *  @param name 显示名；为空时删掉这个座位的记录
     */
    void SetSeatName(int seat, const std::wstring& name);

    //某个座位的人的显示名；不知道时返回空串
    std::wstring SeatName(int seat) const;

    /**
     *  鼠标按下、移动、松开（归一化坐标）。没有标注权限时忽略。
     *  @param tolerance 橡皮的命中容差（归一化单位，由调用方按显示尺寸换算）
     *  @param nowMs     当前时刻（毫秒），决定攒点何时发出
     */
    void PointerDown(const AnnotationPoint& pt, int tolerance, int64_t nowMs);
    void PointerMove(const AnnotationPoint& pt, int tolerance, int64_t nowMs);
    void PointerUp(const AnnotationPoint& pt, int64_t nowMs);

    //结束进行中的一笔（退出标注模式、失去权限时）：画笔照常收尾，未松开的图形丢弃
    void CancelPointer();

    /**
     *  拖动中的图形预览（还没有发出）。
     *  @param out [出参] 预览的一笔
     *  @return 正在拖动图形时返回 true
     */
    bool PendingShape(AnnotationStroke& out) const;

    /**
     *  添加一段文字。
     *  @param pt   文字左上角
     *  @param utf8 文字（UTF-8），为空时不添加，超过 kAnnotMaxTextBytes 时截断
     *  @return 已添加返回 true
     */
    bool AddText(const AnnotationPoint& pt, const std::string& utf8);

    //撤销本人最近的一笔；没有可撤销的返回 false
    bool UndoOwn();

    //清空所有人的笔迹（只有发起人可以）；不是发起人返回 false
    bool ClearAll();

    //把全部笔迹整笔补发一次（只有发起人会发；后加入的人据此得到已有的笔迹）
    void SendSnapshot();

    /**
     *  收到别人的标注帧。
     *  @param senderSeat 帧头的发送者座位号
     *  @param payload    标注载荷
     *  @return 笔迹有变化返回 true
     */
    bool OnRemoteFrame(int senderSeat, const std::string& payload);

    /**
     *  同上，另带本机当前时刻（第 19 期）：激光笔据此淡出、画笔位置据此过期。上面那个版本按时刻 0 处理。
     *  @param nowMs 本机当前时刻（毫秒）
     */
    bool OnRemoteFrame(int senderSeat, const std::string& payload, int64_t nowMs);

    //观看者是否有标注权限（来自 1725 的 annotateallowed）；收回时结束本人进行中的一笔（正画在自己的白板上时不结束）
    void SetViewersAllowed(bool allowed);

    /**
     *  各块白板的建立者（2026-10-07，方案第 14.1 节 ④-1），转交给模型，见 ShareAnnotationModel::SetBoardOwners。
     *  @param owners 白板的画面座位 → 建立者的成员座位
     */
    void SetBoardOwners(const std::map<int, int>& owners);

    /**
     *  当前主持人的成员座位（2026-10-07，方案第 14.3 节第 1 条），转交给模型，见 ShareAnnotationModel::SetHostSeat。
     *  主持人才能清空、补发全部笔迹，观看者没有权限时主持人仍能画。
     *  @param seat 主持人的成员座位
     */
    void SetHostSeat(int seat);

    //本人现在能否在当前画面（SetCanvas）上标注：发起人始终能；观看者看权限；权限关闭时在自己建的白板上仍能（2026-10-07）
    bool CanDraw() const;

    /**
     *  本人能否在指定画面上标注（2026-10-07）：规则同 CanDraw，用于还没切到那块画面时的判断（如进入白板上的标注模式）。
     *  @param canvas 画面座位
     */
    bool CanDrawOn(int canvas) const;

    //只清空本地笔迹（不发送）
    void ClearLocal();

    const ShareAnnotationModel& Model() const;

    /**
     *  共享内容的宽高比（第 20 期，宽 ÷ 高）：文字的近似尺寸、连接线裁边等按它换算。界面在显示尺寸变化时调用。
     *  @param aspect 宽高比，不大于 0 时忽略
     */
    void SetContentAspect(double aspect);
    double ContentAspect() const;

    //选中的对象（第 20 期），按选中的先后
    const std::vector<AnnotationRef>& Selection() const;

    //取消选择
    void ClearSelection();

    /**
     *  删除选中的对象（每个发一条擦除），之后取消选择。
     *  @return 删掉了至少一个返回 true
     */
    bool DeleteSelection();

    //选择工具要画的提示
    AnnotSelectionVisual SelectionVisual() const;

    /**
     *  点下的位置上能改文字的对象（第 20 期，双击时用）：文字、图元（名字）、连接线（线上文字）、数字标号（号码）。
     *  @param pt        点
     *  @param tolerance 容差（归一化单位）
     *  @param ref       [出参] 对象
     *  @param text      [出参] 现在的文字（UTF-8）；数字标号为号码的十进制写法
     *  @return 找到返回 true
     */
    bool EditableAt(const AnnotationPoint& pt, int tolerance, AnnotationRef& ref, std::string& text) const;

    /**
     *  某个位置上有没有可选的对象，或是单选对象的缩放句柄（2026-10-07，选择工具的光标用：有时显示四向移动光标，没有时显示
     *  十字光标，表示可以框选）。只看当前这块画面上的对象。
     *  @param pt        点（归一化坐标）
     *  @param tolerance 容差（归一化单位）
     */
    bool ObjectAt(const AnnotationPoint& pt, int tolerance) const;

    /**
     *  改一个对象的文字并发出修改（第 20 期）：文字对象改为空串时删掉它；数字标号把文字当号码（1~65535，不是数字时不改）。
     *  @return 发出了修改或删除返回 true
     */
    bool SetObjectText(const AnnotationRef& ref, const std::string& utf8);

    /**
     *  改选中对象的一项样式，每个用得上这一项、且确有变化的对象发一条修改（第 20 期）。
     *  @param attribute 取值见 AnnotRestyle
     *  @param value     新值
     *  @return 至少改了一个对象返回 true；没有选中对象时返回 false
     */
    bool RestyleSelection(int attribute, int value);

private:
    void send(const AnnotationMessage& m);
    void flushAppend(int64_t nowMs, bool force);
    void eraseAt(const AnnotationPoint& pt, int tolerance);
    //当前画面上的笔迹（第 22 期 22e，副本）：橡皮、选择、吸附据此命中
    std::vector<AnnotationStroke> canvasStrokes() const;
    void finishStroke(int64_t nowMs);
    //按当前粗细档位取某种工具的线宽（文字、数字标号为字号）
    int  widthForTool(int tool) const;
    //给新的一笔分配编号（跳过 0）
    uint32_t allocId();
    //按当前设置组出某种工具的样式（第 19 期）
    AnnotationStyle styleForTool(int tool) const;
    //是否自由线条（按下即发、边画边发）的种类
    static bool isFreehandTool(int tool);
    //是否拖动两点、松开时一次发出的种类（直线、箭头、形状、块状箭头）
    static bool isTwoPointTool(int tool);
    //是否逐点添加、「完成」时发出的种类（折线、曲线）
    static bool isMultiPointTool(int tool);
    //按 Shift 调整拖动的终点：形状画成正形，直线与箭头吸附到 45 度的整数倍
    AnnotationPoint constrainedEnd(const AnnotationPoint& from, const AnnotationPoint& to) const;
    //在 pt 放一个数字标号（一次发出 begin 与 end），号码自动加一
    void placeBadge(const AnnotationPoint& pt, int64_t nowMs);
    /**
     *  点一下放的图元的外接矩形（第 20 期）：以 center 为中心、默认大小，靠边时整体挪进画面。
     *  @param a、b [出参] 左上、右下两点
     */
    void defaultIconRect(const AnnotationPoint& center, AnnotationPoint& a, AnnotationPoint& b) const;
    //按某个粗细档位取某种工具的线宽（文字、数字标号为字号）
    static int widthForLevel(int tool, int level);
    //同上，再乘以线宽缩放（第 21 期）
    int scaledWidth(int tool, int level) const;
    //选择工具（第 20 期，实现在 ShareAnnotationSessionSelect.cpp）：按下、拖动、松开
    void selectDown(const AnnotationPoint& pt, int tolerance, int64_t nowMs);
    void selectMove(const AnnotationPoint& pt, int64_t nowMs);
    void selectUp(const AnnotationPoint& pt, int64_t nowMs);
    //结束进行中的选择拖动：移动发出剩下的位移，缩放与拖端点发出最终的修改
    void finishSelectDrag(int64_t nowMs);
    //拖动选中的对象：把攒下的位移发出去（不到间隔且不强制时先不发）
    void flushMove(int64_t nowMs, bool force);
    //去掉已经不存在的选中对象
    void pruneSelection();
    /**
     *  把一个对象的新状态作为修改应用到本地并发出。
     *  @param s 新状态（作者、编号指明是哪个对象）
     *  @return 有变化并已发出返回 true
     */
    bool sendUpdate(const AnnotationStroke& s);
    //一个对象当前状态对应的修改操作
    static AnnotationMessage updateMessageFor(const AnnotationStroke& s);
    //改一项样式之后的对象：attribute 用不上时返回 false
    bool restyled(const AnnotationStroke& s, int attribute, int value, AnnotationStroke& out) const;
    //选择拖动的种类
    enum SelectDrag
    {
        select_drag_none     = 0,   //没有在拖
        select_drag_move     = 1,   //移动选中的对象
        select_drag_resize   = 2,   //拖外接矩形的句柄缩放
        select_drag_endpoint = 3,   //拖直线、箭头、连接线的一端
        select_drag_band     = 4,   //拖选择框
    };

    ShareAnnotationModel         m_model;          //全部笔迹
    ShareAudioTransport*         m_transport;      //发送通道（不拥有）
    int                          m_selfSeat;       //本人座位号
    int                          m_canvas;         //当前画面（第 22 期 22e），见 SetCanvas
    bool                         m_running;        //是否已开始
    uint32_t                     m_nextId;         //下一笔的编号
    int                          m_tool;           //当前工具
    uint32_t                     m_color;          //当前颜色
    bool                         m_drawing;        //是否按着鼠标在画
    uint32_t                     m_strokeId;       //正在画的画笔 / 荧光笔的编号；0 表示没有
    std::vector<AnnotationPoint> m_pendingPoints;  //攒着还没发出的点
    int64_t                      m_lastFlushMs;    //上次发出攒点的时刻
    bool                         m_shapeActive;    //是否正在拖动图形
    AnnotationPoint              m_shapeFrom;      //图形起点
    AnnotationPoint              m_shapeTo;        //图形当前终点
    AnnotationPoint              m_shapeRaw;       //拖动图形时最近一次的鼠标位置（未经 Shift 约束），Shift 变化时据此重算终点
    //第 19 期
    int                          m_widthLevel;     //粗细档位，取值见 AnnotationWidthLevel
    int                          m_lineStyle;      //线型
    int                          m_arrowStartCap;  //箭头起点样式
    int                          m_arrowEndCap;    //箭头终点样式
    int                          m_lineStartCap;   //直线、折线、曲线起点样式
    int                          m_lineEndCap;     //直线、折线、曲线终点样式
    int                          m_routing;        //箭头走向
    int                          m_fill;           //形状与块状箭头的填充
    int                          m_blockVariant;   //块状箭头样式
    int                          m_badgeStyle;     //数字标号样式
    int                          m_badgeSequence;  //数字标号的序号写法
    int                          m_badgeNext;      //下一个数字标号的号码（1 起）
    bool                         m_textBold;       //文字是否加粗
    int                          m_textBg;         //文字背景
    uint32_t                     m_textBgColor;    //文字填充背景的颜色
    bool                         m_shiftDown;      //是否按着 Shift
    std::vector<AnnotationPoint> m_multiPoints;    //折线、曲线已经确定的顶点；为空表示没有在添加顶点
    AnnotationPoint              m_multiFloat;     //折线、曲线跟着光标走的最后一个点（预览用）
    bool                         m_pointerSent;    //发过画笔位置、还没发「画笔离开」
    AnnotationPoint              m_lastPointer;    //上次发出的画笔位置
    int64_t                      m_lastPointerMs;  //上次发出画笔位置的时刻（毫秒）
    std::map<int, std::wstring>  m_seatNames;      //座位号 → 显示名（第 19 期，画别人的画笔位置时用）
    //第 20 期：选择工具
    double                       m_aspect;         //共享内容的宽高比（宽 ÷ 高）
    std::vector<AnnotationRef>   m_selection;      //选中的对象
    int                          m_iconKind;       //图元工具当前的图元种类，取值见 AnnotationIcon
    int                          m_connRouting;    //连接线工具当前的走向
    double                       m_widthScale;     //线宽与字号的缩放（第 21 期，白板用），默认 1
    int                          m_selectDrag;     //选择拖动的种类，取值见 SelectDrag
    AnnotationPoint              m_dragOrigin;     //拖动起点
    int                          m_moveWantDx;     //移动：从起点算起想要的总位移（横向，归一化单位）
    int                          m_moveWantDy;     //移动：同上（纵向）
    int                          m_moveSentDx;     //移动：已经发出的总位移（横向）
    int                          m_moveSentDy;     //移动：已经发出的总位移（纵向）
    int64_t                      m_lastMoveMs;     //移动：上次发出移动的时刻（毫秒）
    int                          m_dragHandle;     //缩放、拖端点：拖的是哪个句柄，取值见 AnnotHandle
    AnnotRect                    m_dragFromRect;   //缩放：开始拖时对象的外接矩形
    AnnotationStroke             m_dragOriginal;   //缩放、拖端点：开始拖时对象的状态
    bool                         m_dragChanged;    //缩放、拖端点：本地是否已经改过（松开时据此发出修改）
    AnnotationPoint              m_bandTo;         //选择框的另一角（起点为 m_dragOrigin）
    int                          m_dragTolerance;  //按下时的命中容差（归一化单位），松开时找吸附对象用
};

} // namespace screenshare
