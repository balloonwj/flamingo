# flamingoAdmin

FlamingoIM 的企业管理控制台 —— Windows 桌面应用，基于 `balloonui` DUI 控件库。

与 `flamingoserver` / `flamingoclient` / `flamingoAndroid` 并列，是仓库根目录下的
一个独立组件。当前阶段数据全部 mock。

## 构建

需要 **Visual Studio 2022**（`v143` 工具集）。

```
打开 flamingoAdmin.sln  →  选择 Debug | x64  →  生成
```

`flamingoAdmin.sln` 已把 `balloonui_static`（`../third-party/balloonui/`）列为
依赖项，会先编出 `balloonui_static.lib` 再编 flamingoAdmin。

flamingoAdmin.exe 采用**全静态链接**：balloonui 链接 `balloonui_static.lib`、
CRT 用 `/MT`（Debug `/MTd`），最终 exe 不依赖 `balloonui.dll`，也不依赖 VC++
运行库 DLL，只依赖 Windows 系统 DLL。

- 输出目录：`Bin\x64\`（x64）/ `Bin\`（Win32）。
- 推荐配置：**Debug | x64**。

## 运行

直接运行 `Bin\x64\flamingoAdmin.exe`，无需附带任何 DLL。

## 设计与计划

界面设计与分阶段实施计划见 [`docs/UI设计计划.md`](docs/UI设计计划.md)。
设计稿原件为仓库根的 `design/FlamingoIM Enterprise Admin.html`。

## 目录结构

| 路径 | 说明 |
|---|---|
| `main.cpp` | 程序入口 + 主窗口 `AdminFrame` |
| `Theme/` | 调色板、字体、尺寸常量 |
| `Util/` | 日志器等通用工具 |
| `Mock/` | mock 数据结构与内存数据层 |
| `Shell/` | 侧边栏、顶栏等窗口外壳组件 |
| `Screens/` | 8 个业务界面 |
| `Widgets/` | 复用 UI 控件 |
| `Tests/` | Mock 数据层单元测试 |
| `docs/` | 设计与计划文档 |
