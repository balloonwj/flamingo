/**
 *  屏幕共享标注的数据模型与编解码，ShareAnnotation.h
 *
 *  标注以矢量操作经房间中转的标注轨（screenshare_track_annotation = 5）传给房间里的所有人，不画进视频
 *  （docs/屏幕共享与录制方案.md 5.9）。本文件只有纯逻辑，不依赖窗口：
 *    - 坐标按共享内容归一化：0 为左 / 上边缘，kAnnotCoordMax 为右 / 下边缘，各端按自己显示共享内容的矩形换算；
 *    - 一笔由「作者座位号 + 作者自己分配的笔迹编号」唯一确定，同一操作重复收到不会重复生效；
 *    - 操作的二进制格式见 ShareAnnotationCodec（载荷全部大端），协议文档 docs/协议设计-v2.md 17.8 有同样的说明；
 *    - ShareAnnotationModel 按发送者座位号与标注权限决定接受哪些操作，各端收到同样的操作就得到同样的笔迹。
 *  典型用法（UI 线程）：
 *      AnnotationMessage m;
 *      if (ShareAnnotationCodec::decode(frame.m_data, m) && model.Apply(frame.m_senderSeat, m)) 重画;
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <vector>

namespace screenshare {

//归一化坐标的最大值：0 ~ kAnnotCoordMax 对应共享内容的左 / 上边缘到右 / 下边缘
const int kAnnotCoordMax = 65535;
//一笔最多的点数；画笔超过后不再加点（发送端自动结束这一笔）
const int kAnnotMaxStrokePoints = 4000;
//文字标注的最大字节数（UTF-8）
const int kAnnotMaxTextBytes = 600;
//线宽的换算基准：线宽值 w 表示共享内容宽度的 w / kAnnotWidthScale
const int kAnnotWidthScale = 2000;
//文字字号的换算基准：字号值 s 表示共享内容高度的 s / kAnnotTextScale
const int kAnnotTextScale = 1000;
//发起人的座位号（只有发起人能清空、补发全部笔迹）
const int kAnnotHostSeat = 0;

//笔迹的种类（第 19 期起扩充，取值只增不改：老客户端不认识的种类整条操作丢弃）
enum AnnotationTool
{
    annot_tool_pen         = 1,   //画笔（钢笔）：任意折线，线宽均匀
    annot_tool_highlighter = 2,   //荧光笔：粗的半透明折线
    annot_tool_arrow       = 3,   //箭头：两点，从第一点指向第二点；走向与两端样式见 AnnotationStyle
    annot_tool_rect        = 4,   //矩形：两点为对角
    annot_tool_ellipse     = 5,   //椭圆：两点为外接矩形的对角
    annot_tool_text        = 6,   //文字：一点为文字左上角
    annot_tool_pencil      = 7,   //铅笔（第 19 期）：细的折线，颜色略浅、带一点颗粒感
    annot_tool_marker      = 8,   //马克笔（第 19 期）：粗的不透明折线，端点方头
    annot_tool_brush       = 9,   //毛笔（第 19 期）：折线，线宽随运笔速度变化（快细慢粗）
    annot_tool_laser       = 10,  //激光笔（第 19 期）：发亮的折线，松开后淡出、不保留
    annot_tool_line        = 11,  //直线（第 19 期）：两点
    annot_tool_polyline    = 12,  //折线（第 19 期）：多个顶点，逐个点击添加
    annot_tool_curve       = 13,  //曲线（第 19 期）：多个控制点，画成穿过各点的平滑曲线
    annot_tool_block_arrow = 14,  //块状箭头（第 19 期）：两点为外接矩形的对角，样式见 AnnotationStyle::m_variant
    annot_tool_round_rect  = 15,  //圆角矩形（第 19 期）：两点为对角
    annot_tool_triangle    = 16,  //三角形（第 19 期）：两点为外接矩形的对角，顶点在上边中点
    annot_tool_diamond     = 17,  //菱形（第 19 期）
    annot_tool_pentagon    = 18,  //五边形（第 19 期）
    annot_tool_hexagon     = 19,  //六边形（第 19 期）
    annot_tool_star        = 20,  //五角星（第 19 期）
    annot_tool_parallelogram = 21,//平行四边形（第 19 期）
    annot_tool_bubble      = 22,  //对话气泡（第 19 期）：圆角矩形加左下方的尖角
    annot_tool_cloud       = 23,  //云形（第 19 期）
    annot_tool_badge       = 24,  //数字标号（第 19 期）：一点为中心，样式、序号写法与号码见 AnnotationStyle
    annot_tool_eraser_area = 25,  //局部橡皮（第 19 期）：折线，擦掉它之前画的笔迹在这条路径上的像素
    annot_tool_icon        = 26,  //网络拓扑图元（第 20 期）：两点为外接矩形的对角，图元种类见 AnnotationStyle::m_variant，名字在 m_text
    annot_tool_connector   = 27,  //连接线（第 20 期）：两点；两端可吸附到图元或形状（AnnotationStyle 的吸附字段），线型、走向与两端样式同箭头，线上文字在 m_text
};

//种类的取值上限（校验用）
const int kAnnotToolLast = annot_tool_connector;

//网络拓扑图元的种类（annot_tool_icon 时的 AnnotationStyle::m_variant，第 20 期）
enum AnnotationIcon
{
    annot_icon_computer = 0,    //电脑（显示器加底座）
    annot_icon_laptop   = 1,    //笔记本
    annot_icon_server   = 2,    //服务器（机柜）
    annot_icon_router   = 3,    //路由器
    annot_icon_switch   = 4,    //交换机
    annot_icon_firewall = 5,    //防火墙（砖墙）
    annot_icon_ap       = 6,    //无线 AP
    annot_icon_internet = 7,    //互联网（云）
    annot_icon_database = 8,    //数据库（圆柱）
    annot_icon_phone    = 9,    //手机
    annot_icon_printer  = 10,   //打印机
};

//图元种类的个数
const int kAnnotIconCount = 11;

//线型（AnnotationStyle::m_lineStyle，第 19 期）
enum AnnotationLineStyle
{
    annot_line_solid    = 0,   //实线
    annot_line_dash     = 1,   //虚线
    annot_line_dot      = 2,   //点线
    annot_line_dash_dot = 3,   //点划线
};

//线条两端的样式（AnnotationStyle::m_startCap / m_endCap，第 19 期），大小随线宽
enum AnnotationCap
{
    annot_cap_none            = 0,   //无
    annot_cap_triangle        = 1,   //实心三角
    annot_cap_triangle_hollow = 2,   //空心三角
    annot_cap_open            = 3,   //开口 V 形（老版本箭头的样子）
    annot_cap_swallowtail     = 4,   //燕尾
    annot_cap_circle          = 5,   //实心圆
    annot_cap_circle_hollow   = 6,   //空心圆
    annot_cap_diamond         = 7,   //菱形
    annot_cap_bar             = 8,   //竖线
};

//箭头的走向（AnnotationStyle::m_routing，第 19 期）
enum AnnotationRouting
{
    annot_route_straight = 0,   //直线
    annot_route_elbow    = 1,   //直角折线：先横后竖
    annot_route_arc      = 2,   //弧形：向线段左侧拱起
};

//形状的填充（AnnotationStyle::m_fill，第 19 期）
enum AnnotationFill
{
    annot_fill_none        = 0,   //空心
    annot_fill_solid       = 1,   //实心
    annot_fill_translucent = 2,   //半透明
};

//块状箭头的样式（annot_tool_block_arrow 时的 AnnotationStyle::m_variant，第 19 期）
enum AnnotationBlockArrow
{
    annot_block_right    = 0,   //右箭头
    annot_block_double   = 1,   //左右双向
    annot_block_up_down  = 2,   //上下双向
    annot_block_bent     = 3,   //弯折（先向上再向右）
    annot_block_u_turn   = 4,   //U 形掉头
    annot_block_circular = 5,   //环形循环
};

//数字标号的样式（annot_tool_badge 时的 AnnotationStyle::m_variant，第 19 期）
enum AnnotationBadgeStyle
{
    annot_badge_circle        = 0,   //实心圆
    annot_badge_circle_hollow = 1,   //空心圆
    annot_badge_square        = 2,   //方块
    annot_badge_pin           = 3,   //定位针
};

//数字标号的序号写法（AnnotationStyle::m_sequence，第 19 期）
enum AnnotationSequence
{
    annot_seq_digit   = 0,   //1 2 3
    annot_seq_letter  = 1,   //A B C（26 之后为 AA、AB）
    annot_seq_chinese = 2,   //一 二 三
};

//文字的背景（AnnotationStyle::m_textBg，第 19 期）
enum AnnotationTextBg
{
    annot_text_bg_none  = 0,   //无背景
    annot_text_bg_white = 1,   //白底
    annot_text_bg_fill  = 2,   //填充色（AnnotationStyle::m_bgColor）
};

//样式块的字节数（第 19 期）：begin / text / full 在原有字段之后追加 [长度 1B][样式块]，老版本发的操作没有样式块；
//第 19 期的样式块为 16 字节，收到的样式块不得短于它
const size_t kAnnotStyleBytes = 16;
//第 20 期的样式块字节数：在第 19 期之后追加两端的吸附对象，各为 [作者座位号 1B][编号 4B]，座位号 kAnnotNoAttachSeat 表示没有吸附
const size_t kAnnotStyleBytesV2 = 26;
//第 22 期 22e 的样式块字节数：在第 20 期之后追加 [画面座位 1B]，表示这一笔画在哪块画面上
const size_t kAnnotStyleBytesV3 = 27;
//吸附对象的座位号取这个值表示没有吸附
const int kAnnotNoAttachSeat = 0xFF;
//默认画面（第 22 期 22e）：建房间的主持人的画面，座位号为 0；老版本发的笔迹、画笔位置都按它
const int kAnnotDefaultCanvas = 0;
//清空操作不带画面座位（老版本）时表示清空全部画面
const int kAnnotAllCanvases = -1;
//一次移动最多带的对象个数（第 20 期）
const int kAnnotMaxMoveTargets = 64;
//激光笔松开后淡出的时长（毫秒）；淡出完从模型里删掉，不进整笔补发
const int64_t kAnnotLaserFadeMs = 1500;
//别人的画笔位置多久没有更新就不再显示（毫秒）
const int64_t kAnnotPointerStaleMs = 3000;

//标注操作（载荷第 1 字节）
enum AnnotationOp
{
    annot_op_begin  = 1,   //开始一笔：带种类、颜色、线宽与最初的若干点；作者为帧头的发送者座位号
    annot_op_append = 2,   //给作者自己正在画的一笔追加点
    annot_op_end    = 3,   //作者画完这一笔
    annot_op_text   = 4,   //添加一段文字（一次完成）
    annot_op_erase  = 5,   //擦除指定作者的指定一笔（橡皮与撤销都用它）
    annot_op_clear  = 6,   //清空一块画面上所有人的笔迹（第 22 期起带画面座位，老版本不带时为全部画面）；只接受发起人发出的
    annot_op_full   = 7,   //整笔补发：带作者座位号，已有则覆盖、没有则添加；只接受发起人发出的，用于后加入的人
    annot_op_pointer = 8,  //画笔位置（第 19 期）：标注模式下发送者的光标位置、当前工具与颜色，别人据此显示他的画笔与名字；
                           //第 22 期起末尾带画面座位
    annot_op_pointer_leave = 9,   //画笔离开（第 19 期）：发送者退出标注模式，别人不再显示他的画笔
    annot_op_update = 10,  //修改一个已画完的对象（第 20 期）：带作者座位号与编号，颜色、线宽、点、文字、样式整体替换，种类不变；
                           //有标注权限的人都能修改任何人的对象（与擦除相同）
    annot_op_move   = 11,  //移动若干已画完的对象（第 20 期）：带对象列表与位移；每个对象各自把位移限制在画面以内
    annot_op_board  = 12,  //白板视野（第 23 期）：白板的建立者广播背景与自己的可见区域（按世界归一化）；不进笔迹，
                           //别人据此画背景，打开「跟随创建者」时跟着看同一块
};

//一个对象的引用：作者座位号加作者分配的编号（第 20 期，连接线的吸附对象、移动的目标）
struct AnnotationRef
{
    int      m_author;   //作者座位号；kAnnotNoAttachSeat 表示空引用
    uint32_t m_id;       //编号

    //空引用
    AnnotationRef();
    AnnotationRef(int author, uint32_t id);

    //是否指向某个对象
    bool IsSet() const;

    bool operator==(const AnnotationRef& rhs) const;
    bool operator<(const AnnotationRef& rhs) const;
};

//一条线、一个形状、一段文字的样式（第 19 期）；各字段按种类取用，不用的保持默认值
struct AnnotationStyle
{
    int      m_lineStyle;   //线型，取值见 AnnotationLineStyle
    int      m_startCap;    //起点样式，取值见 AnnotationCap
    int      m_endCap;      //终点样式，取值见 AnnotationCap
    int      m_routing;     //箭头走向，取值见 AnnotationRouting
    int      m_fill;        //填充，取值见 AnnotationFill
    int      m_rotation;    //旋转角度（度，0~359，顺时针），块状箭头用
    int      m_variant;     //块状箭头样式（AnnotationBlockArrow）或数字标号样式（AnnotationBadgeStyle）
    int      m_sequence;    //数字标号的序号写法，取值见 AnnotationSequence
    int      m_number;      //数字标号的号码（1~65535）
    bool     m_bold;        //文字是否加粗
    int      m_textBg;      //文字背景，取值见 AnnotationTextBg
    uint32_t m_bgColor;     //文字填充背景的颜色 0x00RRGGBB（m_textBg 为 annot_text_bg_fill 时用）
    AnnotationRef m_attachStart;   //连接线起点吸附的对象（第 20 期）；没有吸附为空引用
    AnnotationRef m_attachEnd;     //连接线终点吸附的对象（第 20 期）；没有吸附为空引用
    int      m_canvas;      //画在哪块画面上（第 22 期 22e）：那块画面在中转里的座位；老版本的笔迹为 kAnnotDefaultCanvas

    AnnotationStyle();

    //两份样式是否完全相同
    bool operator==(const AnnotationStyle& rhs) const;
};

//归一化坐标的一个点
struct AnnotationPoint
{
    uint16_t m_x;   //横坐标，0 ~ kAnnotCoordMax
    uint16_t m_y;   //纵坐标，0 ~ kAnnotCoordMax

    AnnotationPoint();
    AnnotationPoint(uint16_t x, uint16_t y);
};

//一笔标注
struct AnnotationStroke
{
    int                          m_author;     //作者座位号
    uint32_t                     m_id;         //作者分配的笔迹编号
    int                          m_tool;       //种类，取值见 AnnotationTool
    uint32_t                     m_color;      //颜色 0x00RRGGBB
    int                          m_width;      //线宽（文字为字号），换算见 kAnnotWidthScale / kAnnotTextScale
    std::vector<AnnotationPoint> m_points;     //点
    std::string                  m_text;       //文字（UTF-8），只有文字标注有
    bool                         m_finished;   //作者是否已画完
    AnnotationStyle              m_style;      //样式（第 19 期）；老版本发的笔迹为默认值（老箭头的终点为开口 V 形）
    int64_t                      m_finishedMs; //本机收到「画完」的时刻（毫秒，只在本机用，不上线路）；激光笔据此淡出

    AnnotationStroke();
};

//房间里另一个人的画笔位置（第 19 期，只在本机显示，不进笔迹）
struct AnnotationPointer
{
    int             m_seat;        //这个人的座位号
    AnnotationPoint m_pt;          //光标位置（归一化坐标）
    int             m_tool;        //当前工具：AnnotationTool 之一，或会话类定义的界面工具（如整笔橡皮 kAnnotEraser）
    uint32_t        m_color;       //当前颜色
    int64_t         m_updatedMs;   //本机最近收到的时刻（毫秒）
    int             m_canvas;      //光标在哪块画面上（第 22 期 22e）；老版本发的为 kAnnotDefaultCanvas

    AnnotationPointer();
};

//一块白板的视野（第 23 期，操作 12 的内容）
struct AnnotationBoardView
{
    int     m_background;   //背景，取值见 WhiteboardBackground（ShareWhiteboard.h）
    int     m_left;         //可见区域左边在世界里的位置，归一化 0 ~ kAnnotCoordMax
    int     m_top;          //可见区域上边，同上
    int     m_width;        //可见区域的宽，归一化 1 ~ kAnnotCoordMax
    int     m_senderSeat;   //发送者座位号（模型收到时记下，不上线路）；接收方据此认出是不是白板的建立者发的
    int64_t m_updatedMs;    //本机收到的时刻（毫秒，只在本机用，不上线路）

    AnnotationBoardView();
};

//解码后的一条操作；各字段是否有意义取决于 m_op
struct AnnotationMessage
{
    int                          m_op;         //操作，取值见 AnnotationOp
    int                          m_author;     //作者座位号：erase / full 带在载荷里，其余操作由接收方取帧头的发送者座位号
    uint32_t                     m_id;         //笔迹编号
    int                          m_tool;       //种类（begin / full）
    uint32_t                     m_color;      //颜色（begin / text / full）
    int                          m_width;      //线宽或字号（begin / text / full）
    std::vector<AnnotationPoint> m_points;     //点（begin / append / full；text 为一个点）
    std::string                  m_text;       //文字（text / full）
    bool                         m_finished;   //是否已画完（full）
    AnnotationStyle              m_style;      //样式（begin / text / full / update，第 19 期）
    bool                         m_hasStyle;   //解码时载荷里是否带了样式块；编码时忽略（新版本总是带）
    std::vector<AnnotationRef>   m_targets;    //移动的对象（move，第 20 期），1 ~ kAnnotMaxMoveTargets 个
    int                          m_dx;         //横向位移（move，归一化单位，-32768 ~ 32767）
    int                          m_dy;         //纵向位移（move，同上）
    int                          m_canvas;     //画面座位（第 22 期 22e）：pointer 为光标所在的画面，clear 为要清空的画面
                                               //（kAnnotAllCanvases 表示全部）；其余操作的画面在 m_style.m_canvas 里；
                                               //board 为白板的画面座位
    AnnotationBoardView          m_board;      //白板视野（board，第 23 期）

    AnnotationMessage();
};

//标注操作的编解码；全部为静态方法
class ShareAnnotationCodec
{
public:
    /**
     *  把一条操作编成标注轨的载荷。
     *  @param m 操作；点数超过 kAnnotMaxStrokePoints、文字超过 kAnnotMaxTextBytes 时截断
     *  @return 载荷字节串；m_op 非法时返回空串
     */
    static std::string encode(const AnnotationMessage& m);

    /**
     *  解码标注轨的载荷。
     *  @param payload 载荷
     *  @param m       [出参] 操作
     *  @return 格式正确返回 true；长度不符、操作或种类非法、点数或文字超限返回 false
     */
    static bool decode(const std::string& payload, AnnotationMessage& m);
};

//标注轨帧的接收方：推流端、收流端在中转接收线程上把标注帧交给它，实现方须自行转到 UI 线程
class ShareAnnotationSink
{
public:
    virtual ~ShareAnnotationSink()
    {
    }

    /**
     *  收到一帧标注（中转接收线程上调用）。
     *  @param senderSeat 帧头的发送者座位号
     *  @param payload    标注载荷
     */
    virtual void OnAnnotationFrame(int senderSeat, const std::string& payload) = 0;
};

//房间里全部笔迹的状态；只在 UI 线程使用
class ShareAnnotationModel
{
public:
    ShareAnnotationModel();

    /**
     *  应用一条操作。
     *  @param senderSeat 帧头的发送者座位号（服务端已改写，可信）
     *  @param m          操作
     *  @return 笔迹有变化返回 true；被拒绝（没有权限、非发起人清空或补发）、重复或无效的操作返回 false
     */
    bool Apply(int senderSeat, const AnnotationMessage& m);

    /**
     *  同上，另带本机当前时刻（第 19 期）：激光笔「画完」时记下时刻以便淡出，画笔位置记下更新时刻以便过期。
     *  @param nowMs 本机当前时刻（毫秒，单调递增即可）
     */
    bool Apply(int senderSeat, const AnnotationMessage& m, int64_t nowMs);

    /**
     *  清掉过期的临时内容（第 19 期，界面定时调用）：画完超过 kAnnotLaserFadeMs 的激光笔，以及超过 kAnnotPointerStaleMs
     *  没有更新的画笔位置。
     *  @return 笔迹或画笔位置有变化返回 true
     */
    bool ExpireTransient(int64_t nowMs);

    //别人的画笔位置（第 19 期），按座位号升序
    std::vector<AnnotationPointer> Pointers() const;

    //观看者是否有标注权限（发起人始终有）；收回后不再接受观看者的新操作，已有笔迹保留。
    //例外（2026-10-07，方案第 14.1 节 ④-1）：白板建立者落在自己白板上的操作照常接受，见 SetBoardOwners
    void SetViewersAllowed(bool allowed);
    bool ViewersAllowed() const;

    /**
     *  各块白板的建立者（2026-10-07）：控制器按服务端的房间快照交来，不以发送者自称为准。观看者的标注权限关闭时，
     *  建立者落在自己白板上的操作（开始一笔、追加、画完、文字、擦除、修改、移动、画笔位置、白板视野）照常接受。
     *  @param owners 白板的画面座位 → 建立者的成员座位；整表替换，传空表即清掉
     */
    void SetBoardOwners(const std::map<int, int>& owners);

    /**
     *  某块画面的白板建立者。
     *  @param canvas 画面座位
     *  @return 建立者的成员座位；不是已知的白板返回 -1
     */
    int BoardOwnerOf(int canvas) const;

    /**
     *  当前主持人的成员座位（2026-10-07，方案第 14.3 节第 1 条）。默认 kAnnotHostSeat（发起人）；主持人移交后，控制器按房间
     *  快照改为新主持人的座位：清空与整笔补发只接受主持人发出的，观看者没有权限时主持人的操作照常接受。
     *  @param seat 主持人的成员座位
     */
    void SetHostSeat(int seat);

    //当前主持人的成员座位
    int HostSeat() const;

    //清空全部笔迹（本地，例如切换了共享内容或离开房间）
    void Clear();

    //全部笔迹，按添加的先后（后添加的画在上面）
    const std::vector<AnnotationStroke>& Strokes() const;

    /**
     *  某作者最近添加、还在的一笔（撤销用）。
     *  @param id [出参] 笔迹编号
     *  @return 有返回 true
     */
    bool LastStrokeOf(int author, uint32_t& id) const;

    /**
     *  某作者在某块画面上最近添加、还在的一笔（第 22 期 22e，撤销只撤当前画面上的）。
     *  @param canvas 画面座位
     *  @param id     [出参] 笔迹编号
     *  @return 有返回 true
     */
    bool LastStrokeOf(int author, int canvas, uint32_t& id) const;

    /**
     *  本地清掉某块画面上的全部笔迹（第 22 期 22e：那块画面已不在共享，座位以后可能分给别人），不发给别人；
     *  第 23 期起连同这块画面的白板视野一起清掉。
     *  @return 有笔迹（或白板视野）被清掉返回 true
     */
    bool ClearCanvas(int canvas);

    /**
     *  某块白板最近收到的视野（第 23 期，操作 12）。
     *  @param canvas 白板的画面座位
     *  @param out    [出参] 视野（含发送者座位号与收到的时刻）
     *  @return 收到过返回 true
     */
    bool BoardView(int canvas, AnnotationBoardView& out) const;

    //全部笔迹的整笔补发操作（annot_op_full），按添加的先后
    std::vector<AnnotationMessage> Snapshot() const;

    /**
     *  找点到最近、且在容差以内的最上面一笔（橡皮用）。文字标注按其左上角点计。
     *  @param x、y      归一化坐标
     *  @param tolerance 容差，归一化单位
     *  @param author    [出参] 作者座位号
     *  @param id        [出参] 笔迹编号
     *  @return 找到返回 true
     */
    bool HitTest(int x, int y, int tolerance, int& author, uint32_t& id) const;

    //每次笔迹变化加一，界面据此判断要不要重画
    uint64_t Version() const;

    /**
     *  按引用找对象（第 20 期）。
     *  @return 找到返回指向模型内部的指针（模型下次变化前有效），否则返回 NULL
     */
    const AnnotationStroke* Find(const AnnotationRef& ref) const;

private:
    AnnotationStroke* find(int author, uint32_t id);
    /**
     *  观看者的标注权限关闭时，这条操作是否只落在发送者自己建的白板上（是则照常接受，2026-10-07）。
     *  @param senderSeat 发送者座位号
     *  @param m          操作
     *  @return 只落在发送者自己的白板上返回 true；没有已知的建立者、或涉及别的画面返回 false
     */
    bool onOwnBoardOnly(int senderSeat, const AnnotationMessage& m);
    //擦除一个对象之前：吸附在它上面的连接线改为不吸附，那一端停在它的中心（第 20 期）
    void detachConnectorsFrom(const AnnotationStroke& target);
    //移动若干对象（第 20 期）；有对象动了返回 true
    bool applyMove(const AnnotationMessage& m);
    //修改一个对象（第 20 期）；有变化返回 true
    bool applyUpdate(const AnnotationMessage& m);
    /**
     *  移动连接线时处理它的一端（第 20 期）：吸附的对象不在这一批里就脱开，这一端停在那个对象的中心加上位移。
     *  @param connector 连接线
     *  @param attach    [入出参] 这一端的吸附对象
     *  @param start     是否起点
     *  @param batch     这一批移动的对象
     *  @param dx、dy    这条连接线实际的位移
     */
    void detachMovedEnd(AnnotationStroke& connector, AnnotationRef& attach, bool start,
                        const std::vector<AnnotationRef>& batch, int dx, int dy);
    void touch();

    std::vector<AnnotationStroke> m_strokes;          //全部笔迹，后添加的在后面
    bool                          m_viewersAllowed;   //观看者是否有标注权限
    uint64_t                      m_version;          //变化计数
    std::vector<AnnotationPointer> m_pointers;        //别人的画笔位置（第 19 期），按座位号升序，每个座位至多一条
    std::map<int, AnnotationBoardView> m_boards;      //各块白板最近收到的视野（第 23 期）：画面座位 → 视野
    std::map<int, int>            m_boardOwners;      //各块白板的建立者（2026-10-07）：画面座位 → 建立者的成员座位
    int                           m_hostSeat;         //当前主持人的成员座位（2026-10-07），默认 kAnnotHostSeat，移交后改为新主持人的
};

/**
 *  某块画面上的笔迹（第 22 期 22e），保持原来的先后。
 *  @param all    全部笔迹
 *  @param canvas 画面座位
 */
std::vector<AnnotationStroke> AnnotStrokesOnCanvas(const std::vector<AnnotationStroke>& all, int canvas);

/**
 *  光标在某块画面上的别人的画笔位置（第 22 期 22e），保持原来的顺序。
 */
std::vector<AnnotationPointer> AnnotPointersOnCanvas(const std::vector<AnnotationPointer>& all, int canvas);

/**
 *  激光笔在某时刻的不透明度（第 19 期）：还没画完时为 255；画完后在 kAnnotLaserFadeMs 内线性降到 0。
 *  @param s     一笔激光笔（其余种类恒为 255）
 *  @param nowMs 本机当前时刻（毫秒）
 *  @return 0~255
 */
int AnnotLaserAlpha(const AnnotationStroke& s, int64_t nowMs);

} // namespace screenshare
