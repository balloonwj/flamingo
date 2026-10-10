/**
 *  图片文字识别的执行端：在后台线程解码图片、调用 FlamingoOcr.dll 识别、把结果拷成 exe 自己的结构，
 *  再用 PostMessage 投给发起识别的窗口（docs/图片文字识别方案.md 第 5.3、8.2 节）。
 *
 *  分三层，便于单元测试（OCRH7、OCRH8）：
 *    - DecodeImageFileToBgra / BitmapToBgra：把图片文件或位图转成 32 位 BGRA；
 *    - RunOcrRequest：同步执行一次识别（解码 → m_recognize → 拷贝结果 → m_freeResult）；
 *    - DeliverOcrResult：投递结果，目标窗口已关闭导致投递失败时由本函数释放结果；
 *  COcrRecognizer 把这三步放进一个常驻的识别线程，按提交顺序逐个执行，同一时刻最多一个 m_recognize 调用。
 *
 *  典型用法（界面线程）：
 *
 *      ocr::OcrRequest req;
 *      req.m_filePath = imagePath;
 *      req.m_target = m_hWnd;
 *      req.m_message = kMsgOcrRecognized;
 *      m_taskId = ocr::COcrRecognizer::GetInstance().Submit(req);
 *      ...
 *      //收到 kMsgOcrRecognized：lParam 是 OcrTextResult*，用完 delete
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

#include "../../../FlamingoOcr/FlamingoOcrApi.h"

namespace ocr {

//识别任务在 DLL 返回码之外的结果码（与 flamingoocr::OcrResultCode 不重叠）
enum OcrTaskCode
{
    ocr_task_decode_failed = 100,   //图片文件打不开或无法解码
    ocr_task_no_engine     = 101,   //识别模块不可用（宿主没有可用的函数表）
};

/**
 *  识别出的一个词（坐标为原图像素）。
 */
struct OcrTextWord
{
    std::wstring m_text;   //词的文字
    float        m_x;      //左边
    float        m_y;      //上边
    float        m_w;      //宽
    float        m_h;      //高

    OcrTextWord();
};

/**
 *  识别出的一行（坐标为原图像素）。
 */
struct OcrTextLine
{
    std::wstring             m_text;    //整行文字（DLL 已做拼行整理）
    float                    m_x;       //左边
    float                    m_y;       //上边
    float                    m_w;       //宽
    float                    m_h;       //高
    std::vector<OcrTextWord> m_words;   //本行的词

    OcrTextLine();
};

/**
 *  一次识别的结果，投给窗口的消息里 lParam 指向它，由收件窗口 delete。
 */
class OcrTextResult
{
public:
    OcrTextResult();
    ~OcrTextResult();

    /**
     *  各行文字以换行符（\r\n）连成的一段文字，供结果窗口显示与复制。
     */
    std::wstring JoinedText() const;

    /**
     *  当前仍未释放的结果个数，供单元测试检查泄漏。
     */
    static long LiveCount();

    uint32_t                 m_taskId;         //任务号，与 Submit 的返回值一致
    int                      m_code;           //结果码：flamingoocr::OcrResultCode 或 OcrTaskCode
    std::vector<OcrTextLine> m_lines;          //各行；失败时为空
    std::wstring             m_languageTag;    //实际使用的识别语言
    float                    m_textAngle;      //文字倾斜角（度）
    uint32_t                 m_scalePercent;   //识别前的缩放百分比
    uint32_t                 m_imageWidth;     //识别所用图像的宽（像素，已按旋转方向转正）
    uint32_t                 m_imageHeight;    //识别所用图像的高（像素）
    DWORD                    m_elapsedMs;      //从解码开始到拿到结果的耗时（毫秒）

private:
    OcrTextResult(const OcrTextResult&);
    OcrTextResult& operator=(const OcrTextResult&);
};

/**
 *  一张紧凑排列的 32 位 BGRA 图像（行跨度为宽 × 4）。
 */
struct OcrBgraImage
{
    std::vector<uint8_t> m_pixels;   //像素
    uint32_t             m_width;    //宽（像素）
    uint32_t             m_height;   //高（像素）

    OcrBgraImage();
};

/**
 *  一次识别请求。图片来源二选一：m_filePath 非空时解码该文件，否则使用 m_image。
 */
struct OcrRequest
{
    std::wstring m_filePath;       //图片文件的完整路径
    int          m_quarterTurns;   //解码后顺时针旋转的 90° 次数（看图窗的旋转状态），取值 0 ~ 3，其它值按 4 取模
    OcrBgraImage m_image;          //内存中的图像（截图选区）；m_filePath 非空时忽略
    std::wstring m_languageTag;    //识别语言代码；为空表示默认语言
    HWND         m_target;         //接收结果的窗口
    UINT         m_message;        //投递结果所用的消息号，lParam 为 OcrTextResult*

    OcrRequest();
};

/**
 *  请求里有没有图片来源：图片文件路径非空，或内存图像宽高都大于 0 且有像素。
 *  结果窗口据此决定「重新识别」能不能点（从系统菜单打开的空窗口没有图片）。
 *  @param request 识别请求
 *  @return 有图片来源时返回 true
 */
bool HasImageSource(const OcrRequest& request);

/**
 *  用 GDI+ 把图片文件解码成 BGRA（GIF 取第一帧），再按 quarterTurns 顺时针旋转。
 *  @param path         图片文件的完整路径
 *  @param quarterTurns 顺时针旋转的 90° 次数
 *  @param out          [出参] 解码后的图像
 *  @return 成功返回 true
 */
bool DecodeImageFileToBgra(const std::wstring& path, int quarterTurns, OcrBgraImage& out);

/**
 *  把位图（DDB 或 DIB 段）的指定区域转成 BGRA，透明度一律置为 255。
 *  @param bitmap 位图句柄，调用方保留所有权
 *  @param crop   要取的区域（位图坐标）；为 NULL 时取整张位图；超出位图的部分被裁掉
 *  @param out    [出参] 转换后的图像
 *  @return 成功返回 true；区域为空时返回 false
 */
bool BitmapToBgra(HBITMAP bitmap, const RECT* crop, OcrBgraImage& out);

/**
 *  同步执行一次识别。
 *  @param api        已初始化的函数表；为 NULL 时返回结果码 ocr_task_no_engine
 *  @param request    识别请求
 *  @param cancelFlag 取消标志，可为 NULL
 *  @param taskId     写进结果的任务号
 *  @return 新分配的结果（永不为 NULL），所有权交给调用方
 */
OcrTextResult* RunOcrRequest(const flamingoocr::OcrApi* api, const OcrRequest& request, volatile long* cancelFlag,
                             uint32_t taskId);

/**
 *  把结果投给窗口。投递失败（窗口已关闭、消息队列已满）时由本函数释放结果。
 *  @param target  接收窗口
 *  @param message 消息号
 *  @param result  结果，所有权在投递成功后归收件窗口
 *  @return 投递成功返回 true
 */
bool DeliverOcrResult(HWND target, UINT message, OcrTextResult* result);

/**
 *  测试用的函数表来源：返回 NULL 时使用识别模块宿主的函数表。
 */
typedef const flamingoocr::OcrApi* (*OcrRecognizerApiFn)();

/**
 *  识别线程。进程内用 GetInstance() 取唯一实例；单元测试可直接构造独立实例。所有方法都是线程安全的。
 */
class COcrRecognizer
{
public:
    /**
     *  @param apiSource 函数表来源；为 NULL 时使用 COcrModuleHost::GetInstance().GetApi()
     */
    explicit COcrRecognizer(OcrRecognizerApiFn apiSource = NULL);

    //停止识别线程（等待当前任务结束，最多 kStopWaitMs），未执行的任务按已取消投递
    ~COcrRecognizer();

    //进程内唯一的实例（堆上分配，进程退出时不析构）
    static COcrRecognizer& GetInstance();

    /**
     *  提交一个识别任务，立即返回。识别线程在第一次提交时创建。
     *  @param request 识别请求（复制一份保存）
     *  @return 任务号（大于 0）；识别线程无法创建时返回 0，此时不会投递任何结果
     */
    uint32_t Submit(const OcrRequest& request);

    /**
     *  取消一个任务：尚未开始的直接按已取消投递，正在执行的置位取消标志。任务号不存在时什么也不做。
     */
    void Cancel(uint32_t taskId);

    /**
     *  等待队列清空、当前任务结束（测试用；界面线程不得调用）。
     *  @return 在 timeoutMs 内空闲时返回 true
     */
    bool WaitIdle(DWORD timeoutMs);

    //析构时等待识别线程结束的最长时间（毫秒）
    static const DWORD kStopWaitMs = 5000;

private:
    COcrRecognizer(const COcrRecognizer&);
    COcrRecognizer& operator=(const COcrRecognizer&);

    //队列里的一个任务
    struct Task
    {
        uint32_t      m_id;          //任务号
        OcrRequest    m_request;     //请求
        volatile long m_cancel;      //取消标志
    };

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
    const flamingoocr::OcrApi* CurrentApi() const;

    OcrRecognizerApiFn m_apiSource;    //函数表来源；NULL 表示使用识别模块宿主
    SRWLOCK            m_lock;         //保护以下字段
    std::deque<Task*>  m_queue;        //待执行的任务（归本对象所有）
    Task*              m_running;      //正在执行的任务；没有时为 NULL
    uint32_t           m_nextId;       //下一个任务号
    bool               m_stopping;     //析构中，线程应尽快退出
    HANDLE             m_thread;       //识别线程句柄；尚未创建时为 NULL
    HANDLE             m_wakeEvent;    //有新任务或要退出时置位（自动复位）
};

} // namespace ocr
