# DXGI Desktop Duplication 抓屏改造方案（保留 GDI 降级）

> 被控端抓屏从 GDI `BitBlt` 升级为 DXGI Desktop Duplication，参考 rustdesk
> `libs/scrap`，保留现有 GDI 引擎作为运行期降级。
> balloonwj@qq.com  2026-06-11

---

## 0. 目标与非目标

**目标**
- 被控端抓屏主路径用 DXGI Desktop Duplication（GPU 复制桌面帧，高效、能抓到 GDI 抓不到的硬件加速/全屏内容）。
- 保留现有 GDI 引擎（`CScreenSpy`）作为降级：DXGI 初始化失败、运行期 `ACCESS_LOST`、连续无帧等情况自动回退 GDI，保证不黑屏。
- **协议帧格式与解码端（`RdScreenDecoder`）零改动** —— 走"路径 A"：DXGI 抓出的 BGRA 帧转喂现有 DIFF 差异编码，对端无感。

**非目标（一期不做）**
- 锁屏 / UAC 安全桌面下可见（需 SYSTEM 权限独立进程，已确认不做；安全桌面期间允许画面短暂冻结或降级到 GDISpy 也抓不到时的"无新帧"）。
- DXGI 原生脏矩形增量 / 引入视频编码（VP8/H264）—— 留给将来真要高帧率视频流时的"路径 B"。
- GPU 纹理零拷贝喂硬件编码器（VRAM 路径）。

---

## 1. 总体架构

分三层，抓取与编码解耦：

```
RdControlledStreamer（推流线程，不变）
        │ 要"引擎帧字节"
        ▼
RdScreenCapturer（改造：抓取源可换 + 编码）
        ├── IScreenCapturer*  m_capturer   ← 抓取，产出统一 BGRA32 帧
        │     ├── CDxgiScreenCapturer   （DXGI，主路径）
        │     └── CGdiScreenCapturer    （GDI，降级；包装/复用 CScreenSpy 的抓取）
        ├── CCursorCompositor m_cursor    ← 把光标合成进 BGRA 帧（DXGI 帧不含光标）
        └── CScreenFrameEncoder m_encoder ← BGRA32 →（转 16 位）→ DIFF 差异 → 现有协议帧
                                            （rd_frame_bitmapinfo / first / next）
```

要点：
- **抽象接口只负责"抓取"**，统一产出 BGRA32（top-down，带 stride）。DXGI 与 GDI 两个实现产出格式一致，上层无差别。
- **编码独立**：现有 `CScreenSpy` 的 DIFF 差异 + `ConstructBI` + `Compare` + `WriteRectBuffer` 逻辑剥离成 `CScreenFrameEncoder`，输入改为"外部传入的像素"，输出仍是现有 16 位 DIB + DIFF 的协议帧。**这是路径 A 的核心改动**，保证解码端不动。
- **降级用外层换实现**（`unique_ptr<IScreenCapturer>` 在 DXGI/GDI 间替换），不学 rustdesk 在单个类里用 `Option<gdi>` 共存 —— 各实现独立、可单测、更清晰。

---

## 2. 抽象接口 IScreenCapturer

新文件 `RemoteDesktop/IScreenCapturer.h`：

```cpp
namespace remotedesktop {

// 抓取一帧的结果。区分"无新帧"和"需重建"是工程命门（见 §7）。
enum class CaptureResult
{
    Ok         = 0,   // 抓到新帧，out 有效
    Timeout    = 1,   // 本次无新帧（屏幕没变）—— 不是错误
    AccessLost = 2,   // duplication 失效（锁屏/UAC/切用户/分辨率变/驱动重置）—— 需销毁重建
    Fatal      = 3,   // 不可恢复错误 —— 调用方应降级到 GDI
};

// 一帧像素视图：指向 capturer 内部缓冲，仅在下次 CaptureFrame 前有效（零拷贝）。
struct FrameView
{
    const BYTE* m_data;    // BGRA32 像素，top-down
    int         m_width;   // 像素宽
    int         m_height;  // 像素高
    int         m_stride;  // 行字节数（可能 > width*4，有对齐 padding，逐行必须按它走）
};

// 抓屏抓取器抽象：DXGI / GDI 各一实现，统一产出 BGRA32。
class IScreenCapturer
{
public:
    virtual ~IScreenCapturer() {}

    // 初始化（建 device / duplication 等）。成功 true。
    virtual bool Init() = 0;

    // 抓一帧。timeoutMs：无新帧时最长阻塞毫秒（建议=抓屏周期，既限帧又让出 CPU）。
    // 返回 Ok 时填充 out（内部缓冲视图）。
    virtual CaptureResult CaptureFrame(UINT timeoutMs, FrameView& out) = 0;

    // 主屏宽 / 高（像素）。
    virtual int ScreenWidth()  const = 0;
    virtual int ScreenHeight() const = 0;
};

} // namespace remotedesktop
```

---

## 3. DXGI 后端 CDxgiScreenCapturer

新文件 `RemoteDesktop/DxgiScreenCapturer.{h,cpp}`。COM 用 ATL `CComPtr` 管理释放（项目是 WTL/ATL，可用），位深固定 DXGI 默认的 BGRA8。

### 3.1 成员

```cpp
CComPtr<ID3D11Device>           m_device;        // D3D11 设备
CComPtr<ID3D11DeviceContext>    m_context;       // 立即上下文
CComPtr<IDXGIOutputDuplication> m_dupl;          // 桌面复制对象
CComPtr<ID3D11Texture2D>        m_staging;       // CPU 可读 staging（首帧建、尺寸不变复用）
CComPtr<IDXGISurface>           m_stagingSurf;   // m_staging 的 IDXGISurface（Map 用）
bool                            m_holdingFrame;  // 是否持有未 Release 的帧
bool                            m_fastlane;      // DesktopImageInSystemMemory：可直接 MapDesktopSurface
int                             m_width, m_height;
std::vector<BYTE>               m_rotateBuf;     // 旋转时的中转缓冲（非 0° 才用）
DXGI_MODE_ROTATION              m_rotation;      // 屏幕旋转
```

### 3.2 Init() 初始化流程（参考 rustdesk `dxgi/mod.rs` Capturer::new、Displays）

1. `CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory)`
2. `factory->EnumAdapters1(0, &adapter)` 取第 0 个 adapter（多显卡可扩展选择）
3. `adapter->EnumOutputs(targetIndex, &output)` 选目标显示器，`output->GetDesc(&desc)` 拿 `DesktopCoordinates`/`Rotation`/`Monitor`
4. `output->QueryInterface(__uuidof(IDXGIOutput1), &output1)` —— **Duplication 需要 IDXGIOutput1**
5. `D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &m_device, NULL, &m_context)`
   - 给了显式 adapter，driver type **必须** `UNKNOWN`；flags 传 0、feature levels 传 NULL 让系统自选。
6. `output1->DuplicateOutput(m_device, &m_dupl)` —— 拿到 `IDXGIOutputDuplication`
7. `m_dupl->GetDesc(&duplDesc)`，记 `m_fastlane = duplDesc.DesktopImageInSystemMemory`，记 `m_width/m_height`（注意旋转 90/270 时宽高互换）、`m_rotation`
8. **任一步失败 → Init 返回 false**（调用方据此直接用 GDI 后端，见 §6）

### 3.3 CaptureFrame() 抓帧流程（参考 `load_frame` / `ohgodwhat`）

```
// 1) 先释放上一帧（DXGI 同时只能持一帧；Map 期间不能 Release）
if (m_holdingFrame) { Unmap(); m_dupl->ReleaseFrame(); m_holdingFrame = false; }

// 2) 取下一帧
HRESULT hr = m_dupl->AcquireNextFrame(timeoutMs, &info, &desktopResource);
if (hr == DXGI_ERROR_WAIT_TIMEOUT) return Timeout;                 // 无新帧，不是错误
if (hr == DXGI_ERROR_ACCESS_LOST)  return AccessLost;              // 需重建
if (FAILED(hr))                    return Fatal;                   // 降级 GDI
m_holdingFrame = true;

// 3) info.LastPresentTime.QuadPart == 0 → 本次无实际更新
if (info.LastPresentTime.QuadPart == 0) { /* 可当 Timeout 处理 */ return Timeout; }

// 4) 读像素：fastlane 零拷贝，否则 staging
if (m_fastlane) {
    m_dupl->MapDesktopSurface(&mappedRect);        // pBits / Pitch
} else {
    desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), &frameTex);
    // 首帧或尺寸变化时建 staging（USAGE_STAGING + CPU_ACCESS_READ + BindFlags=0），
    // 之后复用 m_staging；SetEvictionPriority(MAXIMUM) 防驱逐。
    EnsureStaging(frameTex);
    m_context->CopyResource(m_staging, frameTex);  // GPU 内拷进 staging
    m_stagingSurf->Map(&mappedRect, DXGI_MAP_READ);// pBits / Pitch
}

// 5) 处理旋转（非 0° 用 m_rotateBuf 转正，宽高互换），否则直接：
out.m_data   = (BYTE*)mappedRect.pBits;
out.m_stride = mappedRect.Pitch;                   // ★ 必须用 Pitch 逐行，可能有 padding
out.m_width  = m_width;
out.m_height = m_height;
return Ok;
// Unmap/ReleaseFrame 延到下次 CaptureFrame 开头（见步骤 1）
```

**复用 staging（相对 rustdesk 的免费优化）**：rustdesk 每帧都 `CreateTexture2D` 新建 staging，我们首帧建一次缓存，尺寸不变只 `CopyResource`+`Map`/`Unmap`。

---

## 4. GDI 后端 CGdiScreenCapturer

新文件 `RemoteDesktop/GdiScreenCapturer.{h,cpp}`，实现 `IScreenCapturer`，产出 **BGRA32**（与 DXGI 一致）：

- `GetDC(NULL)` 桌面 DC + `CreateCompatibleDC` + `CreateDIBSection`（32 位 top-down）
- `BitBlt(..., SRCCOPY | CAPTUREBLT)`（`CAPTUREBLT` 能抓 layered window，注意它会让光标闪，所以光标统一交给 §8 合成、抓取时不画系统光标）
- `CaptureFrame` 每次 `BitBlt` 后 `out` 指向 DIB 内存，`m_stride = width*4`，永远返回 `Ok`（GDI 没有"无新帧"概念，靠上层编码器的 DIFF 去重）

> 备选：直接复用现有 `CScreenSpy` 的抓取部分。但 `CScreenSpy` 抓 16 位且与 DIFF 耦合，统一成 BGRA32 反而要拆它；GDI 是降级路径、性能不敏感，**新写一个干净的 32 位 BitBlt 抓取器更简单**，现有 `CScreenSpy` 的价值集中在它的 DIFF 编码（§5 复用那部分）。

---

## 5. 帧编码对接（路径 A 核心）

把现有 `CScreenSpy` 的"差异编码"从"GDI 抓取"里剥离，成 `CScreenFrameEncoder`（可原地改 `CScreenSpy`，加一个"喂外部像素"的入口；或新建类移植其 DIFF 逻辑）：

```cpp
class CScreenFrameEncoder
{
public:
    explicit CScreenFrameEncoder(int outBitCount = 16);  // 输出位深，沿用 16 位
    // 喂一帧 BGRA32（来自 IScreenCapturer），内部转 outBitCount 位 DIB。
    void Feed(const FrameView& bgra);
    // 产出现有协议帧（与今天字节完全一致，解码端不用改）：
    void GetBitmapInfo(std::vector<BYTE>& out);  // rd_frame_bitmapinfo
    void GetFirstFrame(std::vector<BYTE>& out);  // rd_frame_first（整屏）
    void GetNextFrame(std::vector<BYTE>& out);   // rd_frame_next（DIFF 增量）
};
```

实现要点：
- `Feed` 把外部 BGRA32 按 stride 逐行转成内部 16 位 DIB（复用 `CScreenSpy::ConstructBI` 的 BI 构造、位深转换走 `SetDIBitsToDevice`/`StretchDIBits` 到 16 位内存 DC，或直接做 32→16 像素转换）。
- `GetFirstFrame` / `GetNextFrame` 完全复用 `CScreenSpy` 现有的 `getFirstScreen` / `getNextScreen`（DIFF `Compare` + `WriteRectBuffer` + `ScanChangedRect`），**只是输入源从"自己 BitBlt"换成"Feed 进来的 DIB"**。
- 输出帧首字节仍是 `rd_frame_bitmapinfo` / `rd_frame_first` / `rd_frame_next`，对端 `RdScreenDecoder` 零改。

**这一步是改动量和单测的重心**：单测喂已知 BGRA（含带 padding 的 stride）→ 验证输出帧字节、验证 DIFF 在两帧间只发变化区域。

---

## 6. 运行期降级控制

放在 `RdScreenCapturer`（改造后）或新的 `CCapturePipeline`：

```cpp
// 伪逻辑
if (m_capturer == NULL) m_capturer = MakeDxgi();      // 优先 DXGI
if (!m_capturer->Init()) m_capturer = MakeGdi();      // DXGI 初始化失败 → GDI

CaptureResult r = m_capturer->CaptureFrame(timeoutMs, view);
switch (r) {
case Ok:        m_cursor.Composite(view); m_encoder.Feed(view); /* 产帧 */ break;
case Timeout:   /* 无新帧：本轮不产增量，或重发上帧；正常 */                break;
case AccessLost:
    // 锁屏/UAC/切用户/分辨率变：销毁重建 DXGI；重建失败计数；连续失败超阈值切 GDI
    if (!RebuildDxgi()) FallbackToGdi();
    break;
case Fatal:     FallbackToGdi(); break;               // 不可恢复 → 永久降级本会话
}
```

降级策略（一期从简）：
- DXGI `Init` 失败 → 直接 GDI，本会话不再试 DXGI。
- 运行期 `AccessLost` → 重建 DXGI（重新 `DuplicateOutput`）；重建连续失败 N 次（如 3）→ 切 GDI。
- `Fatal` → 切 GDI，本会话保持。
- （可选，二期）切 GDI 后定时尝试回切 DXGI（rustdesk 的 `cancel_gdi`）；一期不做。

---

## 7. 边界与错误处理清单（缺一个就在真机崩/黑屏）

| 场景 | 触发 | 处理 |
| --- | --- | --- |
| 屏幕没变 | `DXGI_ERROR_WAIT_TIMEOUT` / `LastPresentTime==0` | 当 `Timeout`，不报错；本轮不产增量或重发上帧 |
| 锁屏 / Ctrl+Alt+Del / UAC / 切用户 / 驱动重置 / RDP 会话变化 | `DXGI_ERROR_ACCESS_LOST` | 销毁 duplication + 资源，重新枚举 output + `DuplicateOutput`；重建期间可临时 GDI 顶住；重建连续失败 → 降级 GDI |
| 分辨率 / 显示器拓扑变化 | staging 尺寸不匹配 / `AccessLost` | 重建 duplication + 重建 staging |
| 行对齐 padding | `Pitch > width*4` | **逐行按 Pitch 拷**，不能假设 width*4 |
| 持帧时序 | DXGI 同时只能持一帧 | `ReleaseFrame` 延到下次 `AcquireNextFrame` 前；`Map` 期间不 `Release` |
| 屏幕旋转 | `desc.Rotation != IDENTITY` | CPU 端旋转回正（宽高互换），一期用软件旋转即可 |
| 权限不足 / 被独占 | `E_ACCESSDENIED` / `Init` 失败 | 降级 GDI |
| 安全桌面普通进程抓不到 | （非目标） | 期间 `Timeout`/`AccessLost` 循环，画面冻结，安全桌面退出后自动恢复 |

---

## 8. 光标处理

DXGI 帧 **不含光标**（GDI 走 `CAPTUREBLT` 时为了不闪也不画系统光标）。统一在抓取后、编码前合成：

- 复用现有 `CCursorInfo`（`CScreenSpy` 已经用它）：`GetCursorInfo` 拿位置 + `hCursor`，把光标位图 alpha-blend 到 BGRA 帧的对应位置。
- 封装成 `CCursorCompositor::Composite(FrameView&)`，DXGI / GDI 两条路共用。
- （rustdesk 是光标独立发、客户端合成，体验更好但要改协议；一期沿用"被控端把光标画进帧"，与现状一致、解码端不变。）

---

## 9. 多显示器 / 旋转（一期范围）

- **一期只抓主显示器**（与现状一致）：`EnumOutputs` 选 `DesktopCoordinates.left==0 && top==0` 的那个，或固定 index 0。
- 多显示器选择 / 切换留二期（接口已支持，加个 targetIndex 即可）。
- 旋转：§7 已列，CPU 软件旋转回正。

---

## 10. 文件组织与工程改动

新增（挂到 `Flamingo.vcxproj` + `.filters` 的 RemoteDesktop 筛选器）：
- `RemoteDesktop/IScreenCapturer.h`
- `RemoteDesktop/DxgiScreenCapturer.{h,cpp}`
- `RemoteDesktop/GdiScreenCapturer.{h,cpp}`
- `RemoteDesktop/ScreenFrameEncoder.{h,cpp}`（或原地改 `CScreenSpy`）
- `RemoteDesktop/CursorCompositor.{h,cpp}`（抽 `CCursorInfo` 合成逻辑）

改造：
- `RdScreenCapturer.{h,cpp}`：从"封装 CScreenSpy"改为"持有 `IScreenCapturer` + `CScreenFrameEncoder` + `CCursorCompositor` + 降级控制"。对上层 `RdControlledStreamer` 的接口（`captureBitmapInfo/First/Next` + `screenWidth/Height`）**保持不变**，改动不外溢。

链接：DXGI/D3D11 需 `d3d11.lib`、`dxgi.lib`（加到工程链接器输入或 `#pragma comment(lib, ...)`）。

不变：`RdControlledStreamer`、协议帧（`RdEngineDef.h` 的 `rd_frame_*`）、中转通道、解码端 `RdScreenDecoder`、控制端观看窗。

---

## 11. 分步实施计划（每步可独立验证、随时停）

1. **抽象层重构（纯重构，行为不变）**：抽 `IScreenCapturer`，把现有 GDI 抓取实现成 `CGdiScreenCapturer`（先产 16 位也行，过渡），`RdScreenCapturer` 改持有它。**先写"现有行为钉子"单测固化输出帧字节，再动。** 跑通 = 现状不回归。
2. **DXGI 抓帧 happy path**：实现 `CDxgiScreenCapturer::Init/CaptureFrame`，加调试入口（如 `--dxgicap`）抓一帧存 bmp，肉眼确认在你的显卡上能抓出图。
3. **统一 BGRA32 + 编码对接**：`CGdiScreenCapturer` 也产 BGRA32；实现 `CScreenFrameEncoder`（BGRA→16位→现有 DIFF）；`CCursorCompositor` 合成光标。端到端：DXGI 抓 → 编码 → 对端观看窗看到。单测覆盖编码器 + stride。
4. **降级与边界**：实现 §6 降级控制 + §7 边界（`AccessLost` 重建、`Timeout`、连续失败切 GDI）。把降级状态机抽纯函数单测（不依赖真实 DXGI）。
5. **真机覆盖测**：多显示器、旋转、切用户、UAC 弹窗（画面冻结后自动恢复）、独显/集显/虚拟机/RDP。

---

## 12. 测试点（Catch2，客户端现成框架）

- **现有行为钉子**（增量 1 前）：固化 `RdScreenCapturer` 当前输出帧字节，重构后比对不变。
- **编码器**（增量 3）：喂构造的 BGRA（含 `stride > width*4` 的 padding）→ 验证输出帧格式、验证两帧间 DIFF 只发变化区域、验证 32→16 位转换正确。
- **降级状态机**（增量 4）：用假的 `IScreenCapturer`（可编程返回 Ok/Timeout/AccessLost/Fatal）驱动降级控制器，验证：AccessLost→重建、重建连续失败 N 次→切 GDI、Fatal→切 GDI、Ok→正常产帧 的状态转移。
- **COM 抓帧本身**：不单测，靠增量 2 调试入口 + 增量 5 真机。

---

## 13. 实现期再定的小点

- staging 复用的尺寸变化判定（分辨率变时重建）。
- 旋转用纯软件还是后续 `ID3D11VideoProcessor`（一期软件）。
- 切 GDI 后是否定时回切 DXGI（一期不做）。
- `timeoutMs` 取值（建议 = 抓屏周期 200ms，或后续做自适应帧率时动态调）。
