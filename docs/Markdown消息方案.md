# Markdown 消息方案与开发计划

> 作者：balloonwj@qq.com　起草日期：2026-10-06
> 用途：记录给 PC 客户端单聊、群聊增加「Markdown 消息」的整体方案、分期路线与开发步骤。后续每步开工前据此细化、收尾后回填实际结果。

---

## 1. 背景与目标

技术讨论、周报、操作步骤这类消息，用纯文字发出来层次不清：标题、列表、代码混在一起，对方读起来费力。本方案让单聊、群聊可以发送 Markdown 格式的文字：发送方在 Markdown 编辑器里写源文本、边写边预览后发送；接收方看到渲染后的标题、列表、表格、代码块等格式。

最终形态（第 11 节调整后）：独立的 Markdown 编辑器窗口负责编辑与发送，聊天窗里的 Markdown 消息显示为卡片，点开在预览窗口里看。全部渲染能力放在可选的 `FlamingoMarkdown.dll` 里，这个 DLL 不存在或加载失败时，客户端不显示任何 Markdown 入口，收到的 Markdown 消息按普通文字显示源文本，主程序的其它功能不受影响。

以下口径已与用户确认（2026-10-06），实现时不再另行请示：

| 决策点 | 取值 | 详见 |
| --- | --- | --- |
| 适用范围 | **只做 PC 客户端**（`flamingoclient/`）；服务端、flamingoAdmin、安卓端都不改 | §2.2、§9、§11.5 |
| 交付形式 | 独立的 `FlamingoMarkdown.dll`；DLL 不存在或加载失败时不显示相关入口，不阻塞主程序 | §5 |
| 消息格式 | 仍是文字消息（`msgtype = 1`），`content` 增加 `"format":"markdown"`，`text` 存 Markdown 源文本 | §4 |
| 解析库 | md4c（MIT 许可，两个 C 文件），只编译进 DLL | §5.1 |
| 单个换行 | **显示为换行**（偏离 CommonMark 标准，与 GitHub 评论区一致） | §3.4 |
| 复制 | 整条复制给 Markdown 源文本；拖选一部分后复制给渲染后的文字 | §11.3 |
| DLL 加载时机 | 进程启动后在后台线程加载，不等登录（按推荐取值） | §6.1 |
| 远程图片 | 不下载，显示成可点击的链接（按推荐取值） | §3.4 |
| 原始 HTML | 不解析，按文字原样显示（按推荐取值）；2026-10-07 起只认 `<u>…</u>`（第 13 节） | §3.4 |

---

## 2. 现状盘点（据实证，均已核对代码位置）

### 2.1 文字消息的格式

- 文字消息的 `content` 是 `{text, font?, at?, quote?}`，`text` 是纯文本加 `/f["id"]` 表情标记，构造在 `flamingoclient/Source/net/MessageProtocolClient.cpp` 的 `buildTextContent`，解析在同文件的 `fillMessageFromObject`。
- `at` 的 `off` / `len` 是 `text` 里的 UTF-8 字节偏移（同文件）。
- 客户端解析时，任何 msgtype 只要 `content.text` 是字符串就按文字气泡显示（`fillMessageFromObject` 的最后一个分支），所以旧客户端会忽略不认识的 `content` 字段，照常显示 `text`。

### 2.2 服务端对 `content` 的处理

- `content` 必须是 JSON 对象，服务端重新序列化后整体存储，不关心其中的字段（`flamingoserver/chatserversrc/MessageProtocol.cpp:128-131`）。
- 敏感词检查读 `content.text`（`MessageProtocol::collectSensitiveTexts`）；**msgtype 不在其分支里时整段正文被跳过**，所以若新开一个 msgtype，敏感词检查会失效，必须改服务端。
- 会话摘要直接取 `content.text`（`MessageProtocol.cpp`），服务端搜索文本也取 `content.text`（`MessageSearchText.cpp:57-58`）。
- 逐条转发原样复制 `content`，只去掉 `at` 与 `quote`（`MessageProtocol.cpp`）；合并转发保留子消息的 `content`。因此 `format` 字段在两种转发中都会保留。
- 单条消息上限 256 KiB（`MessageProtocol.h` 的 `MESSAGE_CONTENT_MAX_BYTES`）。MySQL 列实际只有 64 KiB，见 `bugs.md` BUG-108。

结论：沿用 `msgtype = 1`、只加一个字段，服务端不用改任何代码。

### 2.3 消息列表的绘制

- 单聊、群聊共用自绘控件 `ChatMessageList`（`Source/UI/BalloonChatWnd/ChatMessageList.h:355`），每条消息是一个 `ChatBubble` 子控件（`ChatBubble.h:65`）。
- GDI+ 画形状，GDI `DrawTextW` 画文字（`ChatBubble.cpp:4-6`）。文字排版引擎 `ChatInlineText` 每条消息只用一种字体、统一行高（`ChatInlineText.cpp:452-454`），画不了标题、列表、表格这类块级格式。
- 按 `ChatMessage.kind`（`ChatMockData.h:50-76`）手写分支派发，分支分布在 `ChatBubble` 的 Measure、GetRowHeight、RenderContent、各鼠标处理函数里。
- 选区只在单个气泡内（`ChatBubble.cpp`），链接点击经 `WM_CHAT_OPEN_LINK` 交给宿主窗口按白名单打开（`ChatBubble.cpp`）。
- 气泡最大宽度为内容区的 62%（`ChatBubble.cpp:142`，`kBubbleMaxWidthRatio`）。

### 2.4 输入框与工具栏

- 输入框是 `ChatInputRichEdit`（`ChatInputRichEdit.h:47`），基于 balloonui 的无窗口 RichEdit `DuiRichEdit`；单聊在 `ChatWindowLayout.cpp:167`、群聊在 `GroupChatWindowLayout.cpp:189` 由 XML 创建。
- 发送流程：`DoSend` 枚举输入内容 → `BuildOutgoingParts` 拆成文字段与图片段 → 文字段经 `BuildSendBody` 编码表情后发送（单聊 `BalloonChatWindow.cpp`，群聊 `BalloonGroupChatWindow.cpp`）。
- 工具栏按钮是 `<chat-iconbtn>`，图标由 `ChatIcons` 用 GDI+ 矢量绘制（`ChatIcons.h:22-82`），单聊在 `ChatWindowLayout.cpp:135-150`，群聊在 `GroupChatWindowLayout.cpp:158-172`。

### 2.5 可以照搬的 DLL 接入方式

`FlamingoOcr.dll`（`flamingoclient/FlamingoOcr/FlamingoOcrApi.h`、`Source/Feature/Ocr/OcrModuleHost.h`）：

- 唯一导出函数返回一张 C 函数表，结构以 `m_cbSize` 开头、只在末尾追加字段；
- 两边都静态链接 CRT，接口只用 C 类型，异常不越过模块边界；
- 宿主在后台线程加载，状态单向变化（未开始 → 加载中 → 可用 / 不可用），不可用时入口不显示、不再重试，DLL 加载后不卸载。

### 2.6 正在开发中的相关代码

工作区里有未入库的「代码片段卡片」代码：`Source/UI/BalloonChatWnd/ChatCodeBlock.*`（代码块排版与绘制）与 `Source/Common/Code/CodeLexer.*`（语法着色），2026-10-06 仍在修改，尚未接入协议与气泡。本方案第一版不依赖它们；第 4 期已把 `CodeLexer` 编进 DLL 给代码块着色（第 12 节）。

---

## 3. 交互设计

编辑入口、发送与消息卡片的交互见第 11.3 节（独立的 Markdown 编辑器与消息卡片）。

### 3.3 接收端

- 表格放不下时压缩列宽、单元格内折行；代码块不横向滚动，折行时续行保持原缩进。
- 引用、转发、合并转发、Pin、撤回等菜单项与文字消息相同。
- DLL 不可用时，Markdown 消息就是一条普通文字消息，显示源文本，所有交互与现在一致。

### 3.4 支持的语法

| 语法 | 是否支持 | 说明 |
| --- | --- | --- |
| 标题 `#` ~ `######` | 支持 | 字号按级别递增，一、二级标题下方画细分隔线 |
| 段落、单个换行 | 支持 | **单个换行显示为换行**；空一行另起一段 |
| 加粗、斜体、删除线 | 支持 | 删除线为 GFM 扩展 `~~文字~~` |
| 行内代码 | 支持 | 等宽字体、浅灰底 |
| 代码块（围栏 / 缩进） | 支持 | 等宽字体、浅灰底、右上角「复制」；围栏后写了认得的语言名时语法着色（第 12 节） |
| 引用 `>` | 支持 | 左侧竖条，可嵌套 |
| 有序 / 无序列表 | 支持 | 可嵌套，嵌套层级的项目符号不同 |
| 任务列表 `- [ ]` | 支持 | 只显示勾选状态，点击不改变状态 |
| 表格 | 支持 | 含列对齐 |
| 分隔线 `---` | 支持 | |
| 链接、自动链接 | 支持 | 只打开 http、https、mailto 地址，其余按文字显示 |
| 图片 `![说明](地址)` | 部分 | **不下载远程图片**，显示成可点击的「[图片] 说明」链接，避免被人借此探测谁读了消息 |
| 原始 HTML | 不支持 | 按文字原样显示；只认 `<u>…</u>`（下划线，第 13 节） |
| 数学公式、脚注 | 不支持 | md4c 的公式扩展不开启 |

---

## 4. 协议

只在文字消息的 `content` 里增加一个字段，命令号、错误码、服务端代码都不变：

```json
{
    "clientmsgid": "...",
    "ctype": 1,
    "cid": "100002",
    "msgtype": 1,
    "content": {
        "text": "## 周报\n- **完成** 登录优化",
        "format": "markdown",
        "font": { "size": 14, "bold": 0, "italic": 0, "underline": 0, "strike": 0, "color": "000000" }
    }
}
```

| 字段 | 取值 | 含义 |
| --- | --- | --- |
| `format` | 缺省 | 纯文字（现有行为） |
| `format` | `"markdown"` | `text` 是 Markdown 源文本 |
| `format` | 其它值 | 当作纯文字处理，为以后的其它格式预留 |

- `font` 照常发送；渲染时只采用其中的字号与颜色作为正文基准，加粗、斜体、下划线、删除线由 Markdown 标记决定。
- `at`、`quote` 与文字消息相同，`at` 偏移指向源文本。
- 本字段已写进 `docs/协议设计-v2.md` 的文字消息 `content` 说明。

---

## 5. FlamingoMarkdown.dll

### 5.1 解析库选型

| 候选 | 规模 | 结论 |
| --- | --- | --- |
| **md4c** | `md4c.c` + `md4c.h`，约六七千行（据推测，以引入时的版本为准） | **采用**。MIT 许可，纯 C、没有依赖；SAX 式回调，直接驱动自己的文档模型；支持 CommonMark 与 GFM 表格、删除线、任务列表、自动链接，可以关闭 HTML；Qt 的 Markdown 导入也用它（据推测，引入时核实） |
| cmark / cmark-gfm | 几十个文件 | 体积大一个量级，先建 AST 再遍历，用不上 |
| maddy 等基于正则的实现 | 单个头文件 | 只输出 HTML，不完全合规范，速度慢 |
| WebView2 / MSHTML 显示 HTML | — | 太重，Win7 上不一定有 |

md4c 取官方最新发布版本，原样放进 `third-party/md4c-<版本>/`（保留 LICENSE），不做修改。整个 DLL 预计一两百 KB（据推测）。md4c 自带的 `md4c-html.c` 不用：导出的 HTML 与剪贴板里的 HTML 都由 DLL 从文档模型生成（11.4、12.2 节）。

### 5.2 职责与边界

DLL 负责：解析源文本，生成文档模型；按给定宽度排版；画到宿主给的 HDC 上；命中测试、选区、取文字；提取纯文本。

DLL 不做：不含 balloonui，不含界面文字（因此不需要多语言，「复制」按钮画成图标），不读写文件，不访问网络。日志经宿主回调输出，写英文。

### 5.3 内部结构

| 模块 | 职责 |
| --- | --- |
| `FlamingoMarkdownApi.h` | 接口约定（宿主与 DLL 共用） |
| `MdApiTable.cpp` | 函数表与边界上的参数检查、异常捕获 |
| `Core/MdBuilder`、`Core/MdModel.h` | 由 md4c 回调生成块树（段落、标题、列表、列表项、引用、代码块、表格、分隔线）与行内文字段（文字、强调、代码、链接、换行），每段文字记录它在源文本里的偏移 |
| `Core/MdLayout` | 块级排版：多字体混排的折行、每行各自的行高、列表缩进与项目符号、引用缩进、代码块、表格列宽分配、分隔线 |
| `Core/MdPaint` | 绘制：文字用 GDI，代码块底色、引用竖条、表格线、任务框、复制按钮用 GDI+ |
| `Core/MdSelection` | 命中测试、选区矩形、按范围取文字 |
| `MdModel::m_flatText` | 去掉标记的纯文本（由 `MdBuilder` 生成，`m_toPlainText` 解析后直接返回它） |
| `Core/MdFontCache` | **DLL 范围的字体缓存**，按（字体名、字号、粗细、斜体）复用 HFONT。每个气泡各自建字体会很快耗尽进程的 GDI 句柄配额，所以字体不随文档创建与释放 |

### 5.4 接口要点

规矩与 `FlamingoOcrApi.h` 相同：结构以 `m_cbSize` 开头、只在末尾追加；接口只用 C 类型；DLL 输出的字符串一律写进调用方提供的缓冲区（先查所需长度再取），不跨堆传递所有权；DLL 加载后不卸载。第 1a 步定稿，大致如下：

| 函数 | 作用 |
| --- | --- |
| `m_init` | 传入宿主信息（日志回调） |
| `m_getVersion` | DLL 版本，写日志用 |
| `m_parse` | UTF-8 源文本 → 文档句柄（不透明指针） |
| `m_layout` | 文档 + HDC + 样式 + 最大宽度 → 排版，输出宽高；同一宽度重复调用时直接返回缓存结果 |
| `m_paint` | 在 (x, y) 处绘制；参数含选区、鼠标悬停的部位、裁剪矩形（长消息只画与裁剪矩形相交的块） |
| `m_hitTest` | 点 → 命中种类（文字 / 链接 / 代码块复制按钮 / 无）、字符位置、链接或代码块序号 |
| `m_getText` | 按渲染后的字符范围取文字，复制选区用 |
| `m_getLinkUrl` | 取第 n 个链接的地址 |
| `m_getCodeBlockText` | 取第 n 个代码块的原文 |
| `m_toPlainText` | 源文本 → 去掉标记的纯文本（不需要先建文档），摘要与搜索用 |
| `m_freeDoc` | 释放文档 |

样式结构由宿主填写：正文字体名（`i18n::UiFontName()`）、基准字号与颜色（来自聊天字体设置）、链接色、代码底色、引用竖条色、表格线色、选区底色与文字色，以及按 DPI 换算好的各项间距。代码字体固定为 Consolas，缺字形的字符改用正文字体（与 `ChatCodeBlock` 的做法相同）。

字符位置一律指渲染后文字的 UTF-16 下标，与现有 `SelLayout` 的口径一致；@ 高亮（第 4 期）需要的「源文本偏移 → 渲染位置」换算由文档模型里记录的偏移完成。

线程约定：`m_parse`、`m_toPlainText` 没有全局状态，可以在任意线程调用；`m_layout`、`m_paint`、`m_hitTest` 在界面线程调用；同一个文档不跨线程共享。

### 5.5 防护

- md4c 只开启需要的扩展（表格、删除线、任务列表、宽松自动链接，以及第 13 节的警告框），设 `MD_FLAG_NOHTMLBLOCKS`（行内 HTML 只认 `<u>`，第 13 节）；不开启下划线扩展（否则 `_文字_` 会变成下划线而不是斜体）与公式扩展。
- 文档模型限制嵌套层数与表格列数，超出部分按文字显示，防止恶意构造的消息拖慢界面。
- md4c 的解析耗时与输入长度成正比（据推测）；病态输入（深层嵌套、大量未闭合标记）的解析耗时由 MD16 检查。

### 5.6 工程

- 目录 `flamingoclient/FlamingoMarkdown/`，加入 `FlamingoClient.sln`；四个配置都定义 `WIN32`、使用静态 CRT；工程文件里的路径一律写相对路径。
- 单测编进 FlamingoUnit（直接编译 DLL 的 `Core/` 源文件与 `md4c.c`），标签 `[mddll]`；守卫用例 `MDG1`（四个配置的宏与 CRT）、`MDG2`（导入表只含 Windows 系统 DLL）。

---

## 6. 客户端（Flamingo.exe）

### 6.1 宿主 `Source/Feature/Markdown/`

- `MarkdownModuleHost`：照搬 `COcrModuleHost` 的状态机与 `CPluginLibrary` 加载方式。进程启动后在后台线程加载（调用点在 `CAppShell::Startup`），加载失败只记一条日志、不再重试。提供 `CanUse()` 给界面线程查询，从不等待 DLL。加载完成后不通知已打开的聊天窗（原因见 10.2 节）。
- `CMarkdownDocument`（`MarkdownDocument`）：给 `ChatBubble` 与预览控件用的 C++ 包装类，以 RAII 管理文档句柄；DLL 不可用时所有方法返回「未渲染」，调用方走纯文字路径。

### 6.2 协议与模型

- `MessageProtocolClient`：`buildTextContent` 增加 `format` 参数；`fillMessageFromObject` 读出 `format` 存进 `net::MessageNotify`。
- `ChatMessage` 增加 `isMarkdown` 标志，`kind` 仍为 `kMsgText`。转发、引用、拖拽、重新编辑、菜单等按 `kind` 判断的地方自动沿用文字消息的逻辑。
- 本地库：`BuildContentJson` 的文字分支写入 `format`；`raw` 列本来就保存完整原始 JSON，`NotifyFromRecord` 恢复时字段不丢。

### 6.4 编辑器与预览控件

聊天输入区不再有 Markdown 模式（11.2 节），下面两件都用在独立的编辑器窗口与预览窗口里（`Source/UI/BalloonMarkdown/MarkdownEditorLayout.cpp`）：

- 格式条：编辑器顶部的一行按钮（11.3 节，横向布局 `ChatMarkdownFormatBar`），图标由 `ChatIcons` 绘制；插入、去掉标记的逻辑放在纯函数模块 `MarkdownEditCommands`（输入：文本与选区；输出：新文本与新选区），用单测覆盖。
- 预览控件 `ChatMarkdownPreview`：`DuiControl` 子类，放在 balloonui `DuiScrollView` 里，编辑器右侧的实时预览与预览窗口的正文都用它，滚动条用统一的悬浮细滑块。它只是一层外壳，绘制与命中全部交给 DLL。按开发约定文档 UI 约定第 2 条说明自绘的理由：balloonui 没有块级富文本渲染；最接近的 `DuiRichEdit` 显示 RTF 的效果与 DLL 的渲染不一致，编辑器里看到的预览会与接收方在预览窗口里看到的样子不同。它的边界只限于「宿主 DLL 渲染结果」，不承担其它绘制，没有归入 balloonui 的计划。
- 新增的界面文字一律用 `TR`，完成后运行 `python flamingoclient/tools/i18n_extract.py`。

---

## 7. 不改服务端带来的限制

这些都只影响显示，不会丢内容：

- 服务端生成的会话摘要直接取 `content.text`，所以登录后拉取的会话列表里，最后一条是 Markdown 消息的会话会显示源文本（带 `**`、`#` 等符号），直到本机收发新消息、由客户端重新生成摘要为止。
- 服务端搜索结果的摘录显示源文本；跨过标记的短语在服务端搜不到（例如消息是 `**重要**通知`，搜「重要通知」搜不到）。
- 旧版客户端、DLL 不可用的客户端看到的都是源文本。

以后如果愿意改服务端，在摘要与搜索文本生成处去掉 Markdown 标记即可，不需要改协议。

---

## 8. 测试

| 标签 | 范围 |
| --- | --- |
| `[mddll]` | 文档模型（各种块与行内元素、`\r` / `\r\n` 换行、单个换行显示为换行、HTML 按文字显示、图片转链接、嵌套与列数上限、病态输入耗时）；纯文本提取；排版（最大宽度、折行、表格列宽压缩、代码块续行缩进）；命中、选区、取文字 |
| `MDG1`、`MDG2` | DLL 工程配置与导入表守卫 |
| `[mdhost]` | 宿主状态机：用假函数表模拟加载成功、DLL 不存在、函数表过旧、初始化失败 |
| `[mdproto]` | `format` 字段的构造与解析、缺省与未知取值按纯文字处理、本地库存取后字段不丢 |
| `[mdbubble]` | MB1 ~ MB7（`Source/Tests/ChatBubbleMarkdownTests.cpp`）：DLL 不可用时 Markdown 消息与同样正文的普通文字消息行高相同、全选取到源文本；DLL 可用时显示为卡片、卡片上没有可选文字；卡片高度随摘要增加、超过三行后不再增高；不带 Markdown 标志的消息不受 DLL 影响；换成普通文字消息后回到普通路径；摘要在 DLL 可用时为「[Markdown] 标题」、不可用时与 `BodyToDigest` 相同；交给 DLL 的源文本里表情换成「[表情]」、转义斜杠还原、网址保留 |
| `[mdedit]` | 格式条各命令对「有选区 / 无选区 / 已有标记」三种情况的结果 |

另需运行 `[i18n-scan]`、`[langpack]`、`[msgid]`、`[filters]` 守卫。

---

## 9. 分期路线

| 期 | 步骤 | 内容 |
| --- | --- | --- |
| 1 DLL | 1a | 引入 md4c；新建 FlamingoMarkdown 工程、接口头文件、函数表空实现，加入 sln；`MDG1`、`MDG2` |
| | 1b | 文档模型与纯文本提取，`[mddll]` |
| | 1c | 块级排版 |
| | 1d | 绘制、命中、选区、取文字；调试入口 `--markdowndemo` 供人工核对效果 |
| 2 接收 | 2a | `MarkdownModuleHost` 与 `MarkdownDocument`，`[mdhost]` |
| | 2b | 协议 `format` 字段、`ChatMessage` 标志、本地库，`[mdproto]`；`docs/协议设计-v2.md` 补字段 |
| | 2c | `ChatBubble` 接入 Markdown 分支，`[mdbubble]`。原为气泡里渲染全文，已被第 5 期（5e）取代：现为卡片分支，显示标题与最多 3 行摘要，单击打开预览窗口（11.3 节） |
| | 2d | 原为气泡里的选区、复制、链接、代码块复制，已随气泡全文渲染一起删除（11.2 节）：这些交互现在在预览控件 `ChatMarkdownPreview` 里（编辑器预览与预览窗口，6.4 节）；卡片的右键菜单为「复制 Markdown 源文本」「在窗口中查看」与转发、引用、撤回等通用项 |
| | 2e | 本地生成的摘要改用纯文本 |
| 4 完善（可选） | — | Markdown 里显示内置表情（DLL 经宿主回调绘制）、@ 高亮；代码块语法着色（复用 `CodeLexer`，等代码片段功能定稿后）；消息记录窗、聊天记录管理器、合并记录查看窗渲染；复制时写 HTML 格式（`md4c-html.c`）；本地搜索文本改用纯文本；列表回车自动续项 |

第 3 期（聊天输入区的 Markdown 模式：开关、格式条、预览、发送）已被第 5 期的独立编辑器取代并删除（第 11.2 节），表中不再列出。第 4 期的实施见第 12 节，第 5、6 期见第 11.5 节，第 7 期见第 13 节。

---

## 10. 实施记录（2026-10-06，第 1 ~ 3 期）

用户认可方案后一次做完第 1 ~ 3 期，测试用例按 `finish-all-then-report` 的约定自行设计。

### 10.1 第三方库

- md4c 取 **0.6.0**（2026-09-17 发布，修复了几处错误路径上的内存管理问题），只保留 `src/` 与 LICENSE、README、CHANGELOG，放在 `third-party/md4c-0.6.0/`。与 `third-party/` 下其它库一样**不入库**（`.gitignore` 的 `/third-party/`），换机器编译要从 <https://github.com/mity/md4c/archive/refs/tags/v0.6.0.tar.gz> 取得。
- 启用 `MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_NOHTMLBLOCKS | MD_FLAG_HARD_SOFT_BREAKS | MD_FLAG_ADMONITIONS`（第 7 期把原来的 `MD_FLAG_NOHTML` 换成只关块级 HTML 的 `MD_FLAG_NOHTMLBLOCKS`，并加上警告框，见第 13 节）。「单个换行就换行」直接由 md4c 的 `MD_FLAG_HARD_SOFT_BREAKS` 实现。
- 据实证（MD8 首次运行失败的输出）：代码块**行首**的 Tab 属于缩进，md4c 按 CommonMark 规则自行展开成空格；行中间的 Tab 原样保留，复制按钮给出的原文里仍是 Tab。
- 实际体积：Release|x64 约 320 KB、Release|Win32 约 256 KB（比方案估计的一两百 KB 大，主要是静态 CRT）。导入表只有 kernel32、user32、gdi32、gdiplus（`MDG2` 守住）。

### 10.2 与方案的出入

| 方案 | 实际 | 原因 |
| --- | --- | --- |
| §6.1 DLL 加载完成后通知已打开的聊天窗重排 | **不做通知** | 加载在 `CAppShell::Startup` 里、登录之前发起，DLL 只有几百 KB、`m_init` 不做耗时的事，登录完成前早已就绪，聊天窗不会比它先打开。万一先打开，开关按钮不出现、消息按纯文字显示，重开窗口即恢复 |
| §6.4 预览放进 `DuiScrollView` | 照做 | 开发约定文档说 balloonui 有 `DuiScrollView`，实际它定义在 `Controls/Window/DuiScrollBar.h` 里，没有单独的头文件 |
| §1d 调试入口 `--markdowndemo` | 改为隐藏用例 `MLX`（标签 `[.mdshot]`） | 把一份覆盖全部语法的示例文档按两种气泡配色画成 PNG（输出到环境变量 `MD_SHOT_DIR` 所指目录），不用启动客户端就能人工核对效果；`FlamingoUnit.exe "[.mdshot]"` 运行 |
| 聊天输入区的格式条一行放下 12 个按钮与两个页签（输入区的 Markdown 模式已删除，见 11.2 节） | 宽度不够时**从末尾往前隐藏格式按钮**（新增 `ChatMarkdownFormatBar`，`DuiHBox` 的子类） | 格式条约 460 像素宽，单聊窗缩到最小宽度时输入区只有约 436 像素；`DuiHBox` 遇到固定宽度超出时不压缩，「预览」页签会越过右边缘被截掉。隐藏的是表格、任务列表等排在后面的按钮，窗口变宽后恢复，这些格式仍可直接输入标记 |

### 10.3 文件清单

DLL（`flamingoclient/FlamingoMarkdown/`）：`FlamingoMarkdownApi.h`、`MdApiTable.*`、`DllMain.cpp`、`FlamingoMarkdown.def`、`Core/` 下的 `MdModel.h`、`MdText`（UTF 转换、实体）、`MdBuilder`（md4c 回调 → 模型）、`MdFontCache`（DLL 范围的字体缓存）、`MdLayout`（两遍排版）、`MdPaint`、`MdSelection`、`MdHostLog`、`MdDocument.h`；`Tests/` 下四个测试文件。已加入 `FlamingoClient.sln`。

exe（`flamingoclient/Source/`）：
- `Feature/Markdown/MarkdownModuleHost.*`（后台加载，状态机照 `COcrModuleHost`）、`MarkdownDocument.*`（文档 RAII 包装、纯文本提取、测试替换函数表）；`App/AppShell.cpp` 启动时预加载。
- `net/MessageProtocolClient.*`（`text_format`、`content.format` 收发）、`net/IUSocket.*`（三个文字发送函数加参数）、`UI/BalloonHistory/V2MsgQuery.cpp`（本地库 content 列写 `format`）。
- `UI/BalloonChatWnd/`：`ChatMockData.h`（`isMarkdown`）、`ChatBubble.*`（Markdown 分支）、`ChatMarkdown.*`（源文本、样式、摘要）、`MarkdownEditCommands.*`（格式命令纯函数）、`ChatMarkdownIcons.*`（13 个图标）、`ChatMarkdownPreview.*`（预览控件）、`ChatMarkdownFormatBar.*`（窄宽度下隐藏末尾格式按钮的格式条）、`ChatIconButton.cpp`（禁用色）、`ChatReedit.*`（`m_isMarkdown`）、`ChatSendLimit.*`、`ChatMessageMenu.h`（菜单项「复制 Markdown 源文本」）、`ChatQuoteModel.cpp` / `ChatMergedCard.cpp`（摘要）、两个布局与两个聊天窗。
- `UI/BalloonMain/BalloonMainPanel.cpp`、`UI/BalloonSubtitle/SubtitleModel.cpp`：摘要改用 `DigestForNotify`。
- `tools/gen_media_vcxproj.py`：FlamingoMedia 直接编译 `ChatIcons.cpp`，一并编译 `ChatMarkdownIcons.cpp`。

### 10.4 测试

| 标签 | 用例 | 内容 |
| --- | --- | --- |
| `[mddll]` | MD1 ~ MD18 | 解析器标志、行内样式、单个换行、三种换行符、标题、列表、隐式段落、代码块、引用、表格、HTML 按文字、链接白名单、图片转链接、实体、上限截断、病态输入耗时、非法 UTF-8、链接规范化 |
| | ML1 ~ ML16 | 收缩宽度、折行、长单词断开、代码块撑满与复制按钮、表格压缩、命中测试、取文字、列表标记、标题与引用、空文档与极窄宽度、代理对不拆开、行内代码内边距、绘制与裁剪、长文档耗时、字体缓存去重、代码行缩进 |
| | MA1 ~ MA8 | 函数表、`m_init`、参数检查、缓冲区约定、排版缓存、纯文本提取、文档校验、命中与取文字配合 |
| | MDG1、MDG2 | 工程配置（四个配置、`WIN32`、静态 CRT、`/utf-8`、无绝对路径）、导入表白名单 |
| `[.mdshot]` | MLX | 示例图（默认不运行） |
| `[mdhost]` | MH1 ~ MH6 | 预加载成功、DLL 不存在、不是本模块、函数表过小、`m_init` 失败、只加载一次 |
| `[mdproto]` | MP1 ~ MP5 | 单聊 / 群聊请求写 `format`、解析推送、体积计量、本地库 content 列 |
| `[mdedit]` | ME1 ~ ME16 | 加 / 去行内标记、斜体不误伤加粗、占位文字、行首前缀多行与空行、有序列表重新编号、`\r\n` 换行、代码块包裹与拆除、链接、表格、最小替换区间 |

界面文字已运行 `i18n_extract.py`，新增的 22 条补上了四种语言的译文（标「待审校」）。

### 10.5 留到第 4 期

代码块语法着色、消息记录窗 / 聊天记录管理器 / 合并记录查看窗按卡片显示、复制时写 HTML 格式、列表回车自动续项已于 2026-10-07 完成（第 12 节）。Markdown 里画内置表情（现显示「[表情]」）、@ 高亮、本地搜索文本改用纯文本仍未做。

## 11. 方案调整：独立编辑器与消息卡片（2026-10-06）

第 1 ~ 3 期做完、实机核对时，用户调整了交互方式：不在聊天输入框里编辑 Markdown，改为**单独的 Markdown 编辑器窗口**；聊天窗里的 Markdown 消息不再在气泡里渲染全文，改为**卡片**，点开在**预览窗口**里看。本节记录调整后的设计，与前文冲突之处以本节为准。DLL（解析、排版、绘制、命中、取文字）、协议（`content.format = "markdown"`）、宿主预加载照旧沿用。

### 11.1 用户确认的决定

| 问题 | 决定 |
| --- | --- |
| 编辑入口 | 系统主菜单与托盘菜单打开独立编辑器，**登录前后都能用**；登录后可把文档发送到单聊或群聊 |
| 聊天输入区的 Markdown 模式 | **去掉**：输入框恢复为只发普通文字；工具栏的 M↓ 按钮改为「打开 Markdown 编辑器」，打开时发送对象预先选好当前会话 |
| 聊天窗里的显示 | **卡片**：标题 + 最多 3 行摘要 + 底部「M↓ Markdown 文档」，样式与合并转发卡片一致；自己发的与别人发的都显示卡片 |
| 点击卡片 | 打开预览窗口，支持复制与导出 |
| 编辑器布局 | 左右分栏、实时预览，分隔条可拖，可收起预览；顶部「打开 / 保存 / 导出」与格式条；底部发送对象与「发送」 |
| 导出格式 | .md、.html、.png、.pdf 本期做；**.docx 单独排一期**（11.6 节评估） |
| PDF 做法 | 「Microsoft Print to PDF」静默输出矢量 PDF；这台打印机不存在时（Win7、被删除）改为每页一张图片的 PDF，导出前提示文字不可选中 |
| 预览窗口的复制 | 鼠标选中文字后 Ctrl+C / 右键复制渲染后的文字；代码块保留「复制」按钮；工具栏「复制 ▾」提供复制 Markdown 源文本、复制为纯文字、复制为图片 |
| 聊天窗默认尺寸 | 沿用 flamingo-e6 已改的单聊 760×720、群聊 1000×720、输入区块 212，本方案不再改 |

据实证（2026-10-06 10:14 在不可见桌面里运行的试验脚本，脚本未入库）：`CreateDC("WINSPOOL", "Microsoft Print to PDF")` 后在 `StartDoc` 的 `DOCINFO.lpszOutput` 里给出文件名，2.3 秒打印完毕、**不弹任何对话框**；生成的 PDF 嵌入了字体子集（`/CIDFontType2`、`/FontFile2`）并带 `/ToUnicode`，中文可选中、可搜索；GDI+ 画的色块也是矢量（文件里没有图片对象）。打印机报告 600 DPI、4961×7016（A4）。

### 11.2 去掉的部分

- `CChatMarkdownComposer`、`ChatMarkdownPreview` 在聊天窗里的用法、两份布局里的格式条占位与预览占位（单聊 444 / 445 / 446 / 447 / 450 ~ 461，群聊 539 / 564 / 565 / 566 / 600 ~ 611，作废后不再复用）；
- 输入框的 Ctrl+B / Ctrl+I 钩子（`ChatInputRichEdit::SetFormatShortcutMsg`）与 `WM_APP + 0x85`（作废，开发约定文档的登记改为「已作废」）；
- 气泡里的 Markdown 全文渲染分支（选区、悬停、代码块复制按钮），改为卡片分支；
- 两个聊天窗发送路径里的 `TextFormat()`：聊天输入框只发普通文字（flamingo-e6 的代码片段判断里读 Markdown 模式的两处改为 `false`）。

保留并挪到编辑器里复用的：`MarkdownEditCommands`（格式命令）、`ChatMarkdownFormatBar`（窄宽度下隐藏末尾按钮）、`ChatMarkdownIcons`（13 个图标）、`ChatMarkdownPreview`（预览控件，加上选区）。

### 11.3 界面

**编辑器窗口 `CMarkdownEditorWindow`**（`Source/UI/BalloonMarkdown/`，balloonui `DuiFrameWindow`，可缩放）：

- 顶部一行：「打开」「保存」「导出 ▾」，格式条（12 个按钮，沿用 `ChatMarkdownFormatBar`），右侧「预览」开关（收起时编辑区占满整宽）；
- 中间：`DuiSplitter` 左右分栏，左边 `DuiRichEdit`（纯文字、多行、自动换行，回车就是换行），右边 `DuiScrollView` + 预览控件；输入停止约 300 毫秒后刷新预览；
- 底部一行：「发送给：<对象>」「选择…」「发送」。未登录时这一行显示「登录后可发送」，按钮灰显；登录状态变化时刷新；
- 快捷键：Ctrl+B / Ctrl+I（格式）、Ctrl+O（打开）、Ctrl+S（保存）；
- 标题栏：「Markdown 编辑器 - <文件名或「未命名」>」，有未保存修改时前面加「*」；关闭时有未保存修改则询问「保存 / 不保存 / 取消」；
- 打开 .md：识别 UTF-8（带不带 BOM 均可），否则按系统 ANSI 代码页读取；保存为不带 BOM 的 UTF-8；
- 可以同时打开多个编辑器窗口，由进程级的 `CMarkdownEditorManager`（`CAppShell` 持有）管理：登出不关闭编辑器，只是发送不可用；退出程序时若有未保存的编辑器，统一询问一次。

**入口**：

- 系统主菜单「消息与工具」组，排在「图片文字识别」之后：「Markdown 编辑器」（`kSysMenuMarkdownEditor = 19`），模块不可用时不出现；
- 托盘菜单，登录前与登录后两份都有，排在「锁屏」之后：「Markdown 编辑器」（`kTrayMenuMarkdownEditor = 13`），模块不可用时不出现；
- 聊天窗工具栏的 M↓ 按钮（443 / 538）：打开一个新的编辑器，发送对象预先选好当前会话。

**发送**：「选择…」打开现有的转发目标选择窗 `CForwardTargetDlg`（多选，单聊、群聊均可）。`ForwardRequest` 增加一种请求类型「发送 Markdown 文档」，携带源文本与标题，选择窗右侧预览区显示卡片摘要；主面板的 `OnForwardTargetsConfirmed` 按这种类型逐个目标发出 `format = "markdown"` 的文字消息（`SendChatTextMessage` / `SendGroupTextMessage`），不走 `cmd_forward_message`。发送前按文字消息上限检查（`FindOversizedTextPart`），超出时提示。发出后编辑器不关闭、内容保留，底部显示「已发送给 …」。

**消息卡片**（`ChatBubble` 的卡片分支）：宽度与合并转发卡片相同；第一行标题（取第一个标题，没有标题就取第一行文字，粗体、一行、超长省略），下面最多 3 行去掉标记的摘要，分隔线下是「M↓ Markdown 文档」。单击打开预览窗口（新增 `WM_APP + 0x8A`：聊天消息列表请宿主聊天窗打开 Markdown 预览窗口，定义在 `ChatMessageList.h`；`0x86 ~ 0x89` 已被代码片段功能占用，`0x8B`、`0x8C` 预留给编辑器窗口与预览窗口投给自己的延后操作）；右键菜单保留「复制 Markdown 源文本」与转发、引用、撤回等通用项。会话列表、通知、引用、合并卡片里的摘要改为「[Markdown] 标题」（`DigestForNotify` 里 flamingo-e6 加的代码片段分支保留）。DLL 不可用时仍按普通文字显示源文本。

**预览窗口 `CMarkdownPreviewWindow`**：标题为卡片标题，正文上方一行「<发送者> · <时间>」；工具栏「复制 ▾」「导出 ▾」「在编辑器中打开」（把源文本复制到一个新的编辑器窗口，可修改后再发送）。正文支持鼠标选中、Ctrl+C、右键「复制 / 全选」。同一条消息只开一个预览窗口，再次点击时激活已有窗口。

### 11.4 导出

编辑器的「导出 ▾」与预览窗口的「导出 ▾」共用 `Source/UI/BalloonMarkdown/MarkdownExport`：

| 格式 | 做法 |
| --- | --- |
| .md | 原样写出源文本，UTF-8 不带 BOM |
| .html | DLL 新接口 `m_exportHtml`：从文档模型生成单个文件、样式内嵌（不用 md4c 自带的 HTML 输出模块，以保证图片转链接、HTML 标签按文字显示等处理与卡片和预览一致） |
| .png | 按预览宽度排版，用现有 `m_paint` 画到 32 位位图，GDI+ 编码为 PNG；高度超过 32000 像素时提示改用 PDF |
| .pdf | 按 A4、打印机 DPI 排版；DLL 新接口 `m_paginate` 给出每页的起止（只在行与行之间分页），逐页用 `m_paint` 画到打印机 DC；打印机不存在时改为把每页画成位图、封装为图片式 PDF（zlib 压缩，客户端已带 `Source/third-party/zlib1.2.11/`） |
| .docx | 第 6 期，见 11.6 |

DLL 函数表在末尾追加 `m_getCardText`（标题与摘要）、`m_exportHtml`、`m_paginate`（第 6 期再追加 `m_exportDocx`），`m_cbSize` 随之增大，旧 DLL 缺这些函数时对应功能隐藏。

### 11.5 分期

| 期 | 步 | 内容 |
| --- | --- | --- |
| 5 | 5a | 拆除输入区 Markdown 模式；M↓ 按钮改为打开编辑器（先接空壳窗口）；代码块命令在非空行后先补一个空行（实机核对时发现：紧跟表格插入的代码块被 md4c 并进表格） |
| | 5b | 编辑器窗口：分栏、格式条、实时预览、快捷键、打开 / 保存、未保存提示 |
| | 5c | 入口：系统主菜单、托盘菜单（登录前后）、聊天工具栏预选会话；`CMarkdownEditorManager` |
| | 5d | 发送：选择窗的「发送 Markdown 文档」请求类型、主面板按目标发送、登录状态 |
| | 5e | 消息卡片：`m_getCardText`、气泡卡片分支、摘要「[Markdown] 标题」 |
| | 5f | 预览窗口：选区复制、三种复制、在编辑器中打开 |
| | 5g | 导出 .md / .html / .png |
| | 5h | 导出 .pdf（打印机静默输出、`m_paginate`、图片式 PDF 兜底） |
| 6 | | 导出 .docx |

测试按步补充：`[mdedit]`（代码块补空行）、`[mdeditor]`（编辑器的未保存状态、读写编码、标题、发送可用性）、`[mdcard]`（标题与摘要提取、卡片尺寸、点击投递）、`[mdpreview]`（选区与复制）、`[mdexport]`（HTML 结构、PNG 尺寸、分页、打印机缺失时的选择；真实打印放隐藏用例在不可见桌面里跑）、系统菜单与托盘菜单的已有用例同步更新；第 6 期 `[mddocx]`。原 `[mdcomposer]` 改写为编辑器的布局用例，`[mdbubble]` 改写为卡片用例。

### 11.6 Word（.docx）导出评估

.docx 是 ZIP 包里的几份 XML（`[Content_Types].xml`、`_rels/.rels`、`word/document.xml`、`word/styles.xml`、`word/numbering.xml`、`word/_rels/document.xml.rels`），从 DLL 的文档模型直接生成，不引入新库。据实证（`FlamingoMarkdown/Core/MdModel.h:88-131`，`MdBlock` 与 `MdLink`），模型已有标题级别、有序列表起始编号、任务状态、代码块语言、单元格对齐、表头行、链接地址与是否由图片转来，生成所需的结构信息齐全。

| Markdown | Word |
| --- | --- |
| 标题 1 ~ 6 | 内置「标题 1 ~ 6」样式（导航窗格、目录可用） |
| 加粗 / 斜体 / 删除线 | 字符格式 |
| 行内代码 | Consolas + 浅灰底纹 |
| 链接 | 超链接（外部关系） |
| 有序 / 无序列表 | `numbering.xml` 的真正列表编号，支持嵌套与起始编号 |
| 任务列表 | 行首 ☐ / ☑ |
| 引用 | 左缩进 + 左边框 |
| 代码块 | 等宽字体、灰色底纹、保留缩进 |
| 表格 | Word 表格，表头行底色，列对齐 |
| 分隔线 | 段落下边框 |
| 图片 | 链接文字（与卡片、预览一致，不下载图片） |

ZIP 用不压缩方式手写（约 150 行，CRC32 自算），或把客户端已带的 zlib 编进 DLL。据推测 Word 接受不压缩的条目（OPC 规范允许 stored 与 deflate），以实际打开为准。局限：Word 自行排版，字体、行距与预览不会完全一致，但结构完整、可继续编辑。风险：XML 稍有差错 Word 就报「文件已损坏」——单测解包逐份检查 XML 合法与关系、内容类型齐全，再用 Word / WPS 打开覆盖全部语法的样例核对（据推测本机装有 Office 与 WPS：`Get-Printer` 输出里有 OneNote 与 WPS 的虚拟打印机；核对前再确认 Word 是否可用）。工作量约 900 ~ 1200 行（含单测）。不采用的替代：HTML 改扩展名为 .doc（新版 Word 提示格式不匹配）、RTF（标题样式、列表编号、表格同样繁琐）、调用本机 Word 自动化（要求装了 Word）。

### 11.7 实施记录（2026-10-06，第 5、6 期）

用户确认「5、6 两期一口气做完」后一次做完，测试用例自行设计。

**与方案的出入**

| 方案 | 实际 | 原因 |
| --- | --- | --- |
| 11.3 `CMarkdownEditorManager` 由 `CAppShell` 持有 | 进程级单例（`CMarkdownEditorManager::GetInstance()`）；`CAppShell::Shutdown` 调 `CloseAll()`，主面板「退出」先调 `ConfirmCloseAllForExit` 对所有未保存的编辑器统一询问一次 | 系统菜单、托盘菜单、聊天窗工具栏、预览窗口四处都要打开编辑器，单例省去逐层传指针；进程退出时仍由 `CAppShell` 收尾 |
| 11.3 选择窗确认后由主面板的 `OnForwardTargetsConfirmed` 逐个目标发出 | 选择窗只**选对象**（`ForwardRequest::m_markdownPick`，确认按钮为「确定」，多选），结果经 `IMarkdownTargetSink` 交回编辑器；点编辑器的「发送」才调宿主接口 `IMarkdownEditorHost::SendMarkdownDocument`，由主面板打开（或复用）每个目标的聊天窗，交给聊天窗的 `SendMarkdownDocument` 发送 | 与底部「发送给：…」「选择…」「发送」三个控件的分工一致：改过文档后不必重选对象就能再发一次。走聊天窗的发送路径，消息立刻以卡片出现在聊天窗里，发送失败、重发、发送计数都与普通文字消息相同；代价是发送时会打开目标会话的聊天窗（与「发送到会话」发本地文件相同） |
| 11.3 编辑器顶部「预览」开关收起预览区 | `DuiSplitter` 不能隐藏一侧，收起时把右侧最小宽度设为 0、分隔位置设到最右（`SetMinSizes(最小宽度, 0)` + `SetSplitPx`），展开时恢复 | 不改 balloonui |
| 11.3 卡片点击消息号 `WM_APP + 0x8A`，`0x8B`、`0x8C` 预留 | `0x8B` 为编辑器窗口投给自己的延后操作（打开文件、导出菜单、选对象等，`MarkdownEditorAction` 1 ~ 5），`0x8C` 为预览窗口投给自己的复制菜单、导出菜单与「在编辑器中打开」；开发约定文档已登记 | — |
| 11.4 PDF 逐页画到打印机 DC | 照做。导出前先删掉同名旧文件，`EndDoc` 之后等文件出现且大小非零（最多约 10 秒）才报告成功；排版字号与缩放按打印机 DPI 换算（字号 × DPI / 96，`m_scalePercent` = DPI × 100 / 96） | 打印服务在后台写文件，`EndDoc` 返回不代表文件已写完 |
| 11.6 ZIP 不压缩手写，或把 zlib 编进 DLL | 不压缩（stored），CRC32 在 `MdDocx.cpp` 里自算 | DLL 不增加依赖，`MDG2` 守住的导入表不变；Word 与 WPS 都能打开不压缩的包（见下文人工核对） |
| 11.5 测试标签 `[mdcard]`、`[mdpreview]`、`[mdexport]`、`[mddocx]` | 卡片并入改写后的 `[mdbubble]`；编辑器模型、两个窗口的布局、导出与图片式 PDF 都在 `[mdeditor]`；DLL 新接口在 `[mddll]`（MX1 ~ MX10）；原 `[mdcomposer]` 改为 `[mdtoolbar]`；菜单入口带 `[mdentry]`；真实打印与样例 .docx 是隐藏用例 `[.mdpdf]`、`[.mddocx]` | 按被测代码所在的文件归类，标签更少、更好记 |
| — | 单聊、群聊的 M↓ 按钮编号沿用 443 / 538；群聊窗最小宽度现为 700（flamingo-e6 后来又改过），MT3 在最小宽度下检查工具栏不越界 | — |
| — | 代码块命令改为在非空行后先补一个空行；表格命令原本就这样做，两者改为共用 `MarkdownEditCommands.cpp` 的 `BlankLineBefore()` | 实机核对时发现紧跟表格插入的代码块被 md4c 当成表格的行（据实证：ME17 的对照断言，围栏紧跟表格行时解析结果里没有代码块）；ME12 随之更新、ME17 新增 |

**DLL（`FlamingoMarkdown/`，第 5、6 期结束时为 1.2.0，实机核对后加撑满宽度升为 1.3.0）**：函数表第二版（`kMdApiV2Size`）追加 `m_getCardText`、`m_exportHtml`、`m_paginate`，第三版（`kMdApiV3Size`）追加 `m_exportDocx`；新增 `Core/MdOutline`（卡片标题与摘要：标题取第一个标题块，没有就取第一个文字块的第一行；摘要为其余文字压成一行，最多 400 字）、`Core/MdHtml`（从模型生成 HTML，样式内嵌，链接只保留 http / https / mailto）、`Core/MdPaginate`（合并行区间后二分查找，只在行间分页，单行超过一页时硬切）、`Core/MdDocx`（WordprocessingML：标题 1 ~ 6 带大纲级别、引用、代码块、超链接、表格样式；无序列表共用一个编号，每个有序列表各自一个编号并带起始值）。

**exe**：

- `Source/UI/BalloonMarkdown/`（新目录）：`MarkdownEditorManager`（窗口管理、退出前询问、预览窗口按消息复用）、`MarkdownEditorHost.h`（`IMarkdownEditorHost` / `IMarkdownTargetSink`，主面板实现）、`MarkdownEditorModel`（编码识别与保存、编辑框文字与源文本互转、标题、发送对象文字、发送前检查、预览窗口的「发送者 · 时间」）、`MarkdownEditorLayout`（两个窗口的控件树与尺寸）、`MarkdownEditorWindow`、`MarkdownPreviewWindow`、`MarkdownExport`（五种导出、三种复制、按预览宽度画位图）、`MarkdownPdf`（打印机静默输出与图片式 PDF）。
- `Feature/Markdown/MarkdownDocument`：包装四个新接口，`CanExportDocx()` 按 `m_cbSize` 判断 DLL 是否为第三版。
- `UI/BalloonChatWnd/`：`ChatBubble` 卡片分支（标题、最多 3 行摘要、分隔线、M↓ 图标与「Markdown 文档」，点击与右键「在窗口中查看」投递 `MarkdownPreviewOpenRequest`）、`ChatMessageList.h`（`WM_CHAT_OPEN_MARKDOWN_PREVIEW`）、`ChatMessageMenu.h`（`kChatMsgMenuViewMarkdown = 18`）、`ChatMarkdown`（`ApplyMarkdownEditorButton`、摘要改为「[Markdown] 标题」、删去不再使用的气泡样式函数）、`ChatMarkdownPreview`（选区、链接、双击全选、右键菜单、Ctrl+C / Ctrl+A）、`ChatReedit`（「重新编辑」Markdown 消息时打开编辑器）、两个聊天窗与两份布局（拆除 Markdown 模式，删除 `ChatMarkdownComposer.*` 与它的测试文件）。
- `UI/BalloonMain/`：系统菜单（`kSysMenuMarkdownEditor = 19`）、托盘菜单（`kTrayMenuMarkdownEditor = 13`）、`MainPanelIcons`（`kIconMarkdown = 38`）、主面板实现宿主接口（登录成功时 `SetHost`，`ResetForLogout` 里清空并关闭预览窗口）、`ForwardRequest` / `ForwardTargetModel`（选对象模式的标题、按钮与预览文字）。

**测试**：

| 标签 | 用例 | 内容 |
| --- | --- | --- |
| `[mddll]` | MX1 ~ MX6 | 函数表第二版、卡片标题与摘要、HTML 的标签与结构、HTML 里的链接白名单与转义、分页只在行间（超高单行硬切）、新接口的参数检查与缓冲区约定 |
| | MX7 ~ MX10 | .docx 的 ZIP 包（部件齐全、CRC 与大小一致）、`document.xml` 里各种语法的写法、每份 XML 都合法、`m_exportDocx` 的接口约定 |
| `[.mddocx]` | MXS | 生成覆盖全部语法的样例 .docx（默认不运行） |
| `[mdeditor]` | ED1 ~ ED15、ED17 | 读文件识别编码、保存为无 BOM 的 UTF-8 与 CRLF、编辑框文字与源文本互转、源文本与聊天正文互转、标题、窗口标题、发送对象文字、发送前检查、编辑器控件树在默认与最小尺寸下排得下、导出 .md 与文件读写、选会话窗口的选对象模式、预览窗口的「发送者 · 时间」、预览窗口控件树、导出 .html 与 .png、图片式 PDF 的结构与交叉引用表偏移、导出 .docx |
| | ED18 ~ ED21（实机核对后补） | 先按大尺寸排过版再缩到设计尺寸后左右两栏各占一半（附不处理时的对照）、选择窗的标题与摘要按卡片规则取且摘要不含标题、首屏渲染前发出的文档在何时重新拉取历史、预览与导出的样式撑满宽度 |
| `[mddll]` | ML17（实机核对后补） | `md_layout_fill_width`：撑满时文档宽度等于排版宽度、不设时收缩、旧结构大小时不读该字段、两种样式不混用排版缓存 |
| `[.mdpdf]` | ED16 | 经系统 PDF 打印机静默导出（在不可见桌面里单独运行） |
| `[.mdpng]` | ED22 | 把示例文档按预览窗口的样式导出成 PNG（`MD_SHOT_DIR`），人工核对撑满宽度等效果；2026-10-07 已用它核对，标题细线、代码块底色、分隔线铺满整个宽度 |
| `[mdbubble]` | MB1 ~ MB7（改写） | DLL 不可用时与普通文字相同、可用时为卡片且没有可选文字、卡片高度超过 3 行摘要后不再增高、普通消息不受影响、换消息复位、摘要「[Markdown] 标题」、源文本转换 |
| `[mdtoolbar]` | MT1 ~ MT3（取代 `[mdcomposer]`） | DLL 不可用时按钮隐藏且工具栏不留空位、可用时按钮在「字体」右边、最小窗口宽度下工具栏不越界 |
| `[mdedit]` | ME12（更新）、ME17 | 代码块紧跟表格时先空一行 |
| `[mdentry]` | TRAY22、TRAY23、SYSMD1、SYSMD2 | 托盘菜单三种形态与系统菜单的「Markdown 编辑器」位置、命令 id、模块不可用时不出现 |

结果（2026-10-06 16:59 隔离编译的 `FlamingoUnit_md.exe`，在不可见桌面里运行）：`[mddll]`、`[mdeditor]`、`[mdbubble]`、`[mdtoolbar]`、`[mdedit]`、`[mdhost]`、`[mdproto]`、`[mdentry]`、`[traymenu]`、`[sysmenu]` 共 152 个用例全部通过，ED16 通过。实机核对后的改动（撑满宽度、首屏前发送、选择窗摘要等）完成后，2026-10-07 04:3x 重跑全量（`~[live]`，4965 个用例，隔离编译不再排除任何文件）：只有 MDL-L4 ~ L6 与 FSD5 四个已知的、与本功能无关的失败，守卫用例全部通过。

界面文字新增 56 条，已补上四种语言的译文（标「待审校」）；`FlamingoLang.dll` 的 Debug x64 / Win32 已于 2026-10-07 重编，带上了这些译文。

**人工核对**：样例 .docx（`FlamingoUnit.exe "[.mddocx]"` 生成）在不可见桌面里分别用 Word 与 WPS 的 COM 接口以只读方式打开，均正常打开、没有「文件已损坏」提示。Word 读到 42 个段落、2 个表格、3 个超链接、4 个列表，第一段样式为「标题 1」、大纲级别 1，第一个表格 2 行 2 列；WPS 读到的列表数为 2（与 Word 不同，据推测 WPS 把同一种编号的列表合并计数，未进一步核实）。PDF 打印机路径由隐藏用例 ED16 在不可见桌面里实际导出验证。

**实机核对（2026-10-06 晚，经用户同意上屏）**：另建一套独立环境 `tmp/md/live/`（`setup_mdlive.py`：服务器为 `tmp/calltest/server` 的副本，数据库用 SQLite 在线备份，端口 27000 ~ 27005；客户端 A、B 分别自动登录 MY0127、MY0308，只放 `Flamingo_md.exe` 与 `FlamingoMarkdown.dll`），不动别的会话的测试环境。据实证（两端日志）走通了：系统菜单打开编辑器（没有 OCR 模块时紧跟「备忘与提醒」）、输入后实时预览、「选择…」多选发送对象、发送（`format=1`）、B 收到后点卡片打开预览窗口、预览窗口「在编辑器中打开」、聊天窗工具栏打开编辑器并预选当前会话、经系统 PDF 打印机导出 PDF（600 DPI，无对话框）、导出 Word。核对中发现并已修正五个问题：

| 现象 | 原因 | 修正 |
| --- | --- | --- |
| 编辑器与预览窗口标题栏没有程序图标 | 窗口晚于开发约定文档 UI 约定第 10 条（当天新增）编写 | 两个窗口用 `balloonmain::ProgramWindowIcons` 设标题栏与任务栏图标，底色取根控件底色 `kMdWindowBackColor` |
| 编辑区约 700 像素宽、预览区只有约 265 像素，不是左右各半 | 据实证（`DuiSplitter.cpp`）：比例只在第一次排版时换算成像素，之后保持像素；窗口建好时先按建窗的默认尺寸排过一次，`ResizeClient` 之后左右比例就不对了 | 新增 `ResetMarkdownEditorSplit`，窗口在 `ResizeClient` 之后调用（`SetSplitFraction` 在控件排过版之后会立即按当前矩形换算）；ED18 |
| 预览区上面一条白、下面浅灰 | 据实证（截图像素 255 与 252）：预览控件底色为白，内容不满一屏时露出 `DuiScrollView` 的底色 `RGB(252,252,252)` | 预览控件底色改为与滚动视图相同 |
| 选择窗「文档」区里标题出现两次 | 摘要用的是整篇纯文本，第一行就是标题，而第一行又单独写了「[Markdown] 标题」 | 新增 `MarkdownCardTextFromSource`，标题与摘要按卡片的规则取；ED19 |
| A 发出后，自己的聊天窗里看不到这张卡片（B 正常收到） | 据实证（A 的日志）：主面板先打开聊天窗再立即发送，聊天窗打开时请求的首屏历史早于这条消息，首屏渲染时清空列表、按快照重建，乐观插入的卡片被清掉 | 新增 `MarkdownEarlySendTracker`：首屏渲染前发出的文档都确认入库、且首屏已渲染后，聊天窗调用 `ReloadLatestMessages()` 重新拉一次最新一页；单聊窗、群聊窗都接入；ED20 |

2026-10-07 按用户要求在同一环境里完整自测一遍（据实证：截图 `tmp/md/live/shots/t*.png` 与两端日志）：系统菜单、托盘菜单、聊天窗工具栏三个入口都能打开编辑器（工具栏打开时预选当前会话）；格式按钮插入占位文字并选中、输入时替换；表格后插入代码块先空一行；「预览」开关收起与展开；选择窗摘要不重复标题；发送后 A 自己的聊天窗里卡片都在；B 端会话摘要、卡片、预览窗口；三种复制（剪贴板测完已恢复）；五种导出（.md 与原文一致，.html 结构完整，.pdf 带 `/ToUnicode`，.docx 用 Word 打开正常）；预览窗口「在编辑器中打开」；未修改时直接关闭、修改后关闭弹出「是否保存」并可选「否」；「保存」后标题变为文件名，「打开」读入带 BOM 的 UTF-8 文件。两端日志里没有 Markdown 相关的错误。自测中唯一的异常是测试脚本投递的 End 键没有移动光标（真实键盘不受影响），与产品无关。

另有一处按用户的决定改为撑满：DLL 排版时文档宽度收缩到最宽的一行（为聊天气泡设计，ML1），在宽的预览区里标题下的细线、代码块底色只画到最宽那一行为止，看起来偏窄。`MdStyle` 末尾追加 `m_layoutFlags`（`kMdStyleV2Size`，DLL 升为 1.3.0），设了 `md_layout_fill_width` 时文档宽度等于排版宽度；`m_cbSize` 不含该字段的旧宿主照旧收缩，1.3.0 之前的 DLL 不认这个字段、同样照旧收缩。宿主的 `MakePreviewMarkdownStyle`（编辑器预览、预览窗口、导出 PNG / PDF 共用）设上该选项，聊天卡片不经过排版，不受影响。用例 ML17（DLL 侧：撑满、收缩、旧结构大小、样式比较）与 ED21（宿主侧）。

**已知局限**：PDF 与 PNG 导出在界面线程上同步进行，长文档导出期间窗口不响应（据实证：`MarkdownPdf.cpp` 的 `WaitForFile` 在调用线程上轮询，最多约 10 秒）；经系统 PDF 打印机导出时，任务栏通知区域会出现 Windows 的打印队列图标，打印任务结束后还会保留约一分钟才自动消失（据实证，2026-10-06 21:36 实测：图标属于 explorer.exe，窗口类 `ASYNCUI_NOTIFYICON_WINDOW_CLASS`，提示「某某的 1 个文档被挂起」；任务约 1 秒完成后提示变为「0 个文档被挂起」，打印进程退出后图标仍在，约 65 秒后消失）。这是走打印机这条路附带的系统行为，用户确认保持现状；要避免只能不经过打印机（图片式 PDF，或自行生成矢量 PDF）。

### 11.8 交接说明（2026-10-07）

供其他会话接手。实现过程与依据见第 10 节与 11.7 节，本节只列现状、改动位置、必须保持的约定、编译测试方法与未提交的文件。

**现状**

- 第 1 ~ 3 期（DLL、协议字段、宿主预加载）、第 5 期（独立编辑器、消息卡片、预览窗口、导出 .md / .html / .png / .pdf）、第 6 期（导出 .docx）全部完成；第 1 ~ 3 期里输入区的 Markdown 模式与气泡全文渲染已被第 5 期取代并删除（11.2 节）。实机核对与完整自测都已通过（11.7 节）。第 4 期（第 12 节）与第 7 期（第 13 节）随后也已完成。
- DLL 版本 1.5.0：函数表到第五版（`kMdApiV5Size`，末尾是 `m_setAlertTitles`），`MdStyle` 到第二版（`kMdStyleV2Size`，末尾是 `m_layoutFlags`）。
- 协议与服务端没有改：Markdown 消息就是普通文字消息，`content.format = "markdown"`（客户端常量 `net::text_format_markdown`）。旧客户端显示源文本。
- 全部改动都没有提交（见本节末尾）。

**改什么去哪里**

| 要改的内容 | 位置 |
| --- | --- |
| 解析、排版、绘制、命中测试、选区 | `FlamingoMarkdown/Core/MdBuilder`、`MdLayout`、`MdPaint`、`MdSelection`、`MdText` |
| 卡片标题与摘要 | `FlamingoMarkdown/Core/MdOutline` |
| HTML 导出、分页、Word 导出 | `FlamingoMarkdown/Core/MdHtml`、`MdPaginate`、`MdDocx` |
| 函数表、结构定义、参数检查 | `FlamingoMarkdown/FlamingoMarkdownApi.h`、`MdApiTable.cpp` |
| exe 加载 DLL、文档包装 | `Source/Feature/Markdown/MarkdownModuleHost`、`MarkdownDocument` |
| 编辑器窗口、预览窗口、窗口管理 | `Source/UI/BalloonMarkdown/MarkdownEditorWindow`、`MarkdownPreviewWindow`、`MarkdownEditorManager` |
| 两个窗口的控件树、尺寸、分隔条比例 | `Source/UI/BalloonMarkdown/MarkdownEditorLayout` |
| 编码识别、标题、发送前检查等纯逻辑 | `Source/UI/BalloonMarkdown/MarkdownEditorModel` |
| 导出与复制、打印机 PDF 与图片式 PDF | `Source/UI/BalloonMarkdown/MarkdownExport`、`MarkdownPdf` |
| 格式按钮的命令、格式条、按钮图标 | `Source/UI/BalloonChatWnd/MarkdownEditCommands`、`ChatMarkdownFormatBar`、`ChatMarkdownIcons` |
| 预览控件（选区、链接、右键菜单） | `Source/UI/BalloonChatWnd/ChatMarkdownPreview` |
| 聊天窗里的卡片 | `Source/UI/BalloonChatWnd/ChatBubble`（Markdown 卡片分支） |
| 摘要、排版样式、卡片文字、首屏前发送的追踪 | `Source/UI/BalloonChatWnd/ChatMarkdown`（`MarkdownDigestFromBody`、`MakePreviewMarkdownStyle`、`MarkdownCardTextFromSource`、`MarkdownEarlySendTracker`） |
| 发送 | 两个聊天窗的 `SendMarkdownDocument`；主面板 `BalloonMainPanelForward.cpp`（实现 `IMarkdownEditorHost`） |
| 入口 | `MainPanelSystemMenu`、`MainPanelTrayMenu`、聊天窗工具栏 M↓ 按钮（单聊 443、群聊 538） |
| 选发送对象 | `ForwardRequest::m_markdownPick`、`ForwardTargetModel` |

**必须保持的约定**

- 函数表与所有带 `m_cbSize` 的结构只在末尾追加；DLL 读取新字段之前先比较 `m_cbSize`（参照 `ResolveMdStyle` 对 `m_layoutFlags` 的处理）；改了接口要升 `kMdVersion`，并同步 MX1 里的版本断言。
- 消息号：`WM_APP + 0x8A`（卡片请宿主聊天窗打开预览窗口）、`0x8B`（编辑器窗口的延后操作）、`0x8C`（预览窗口的延后操作）；`0x85` 已作废。命令 id：`kSysMenuMarkdownEditor = 19`、`kTrayMenuMarkdownEditor = 13`、`kChatMsgMenuViewMarkdown = 18`；图标 `kIconMarkdown = 38`。作废的控件编号（单聊 444 ~ 447、450 ~ 461，群聊 539、564 ~ 566、600 ~ 611）不得复用。
- 编辑器的分隔条：窗口在 `ResizeClient` 之后调用 `ResetMarkdownEditorSplit`，否则左右比例停在建窗时默认尺寸换算出的像素上（ED18）。
- 预览窗口、编辑器预览与导出 PNG / PDF 统一用 `MakePreviewMarkdownStyle`（撑满宽度）；预览控件底色与 `DuiScrollView` 的底色 `RGB(252,252,252)` 保持一致。
- 从编辑器发送时主面板先打开目标聊天窗再立即发送，聊天窗的首屏历史会清空列表。修改聊天窗的首屏渲染、`ReloadLatestMessages` 或发送确认处理时，保留 `MarkdownEarlySendTracker` 的五处调用：发送后 `OnSent`、确认成功与失败各一次 `OnAck`、首屏渲染完 `OnFirstPageRendered`、重新请求最新一页时 `OnReloadRequested`（ED20）。
- 界面文字一律用 `TR` 系列，改完运行 `python flamingoclient/tools/i18n_extract.py`，新增译文标「待审校」；新窗口在标题栏显示程序图标（开发约定文档 UI 约定第 8、10 条）。

**已知局限与未做的事**

- 第 4 期内容（10.5 节）：代码块着色、消息记录窗与聊天记录管理器按 Markdown 渲染、复制时写 HTML 格式、列表回车自动续项已于 2026-10-07 完成（第 12 节）；Markdown 里画表情、@ 高亮仍未做。
- PDF / PNG 导出在界面线程上同步进行，长文档导出期间窗口最多约 10 秒不响应。
- 经打印机导出 PDF 时托盘的打印队列图标约一分钟后才消失（系统行为，用户确认保持现状）。
- 据推测：「发送到会话」往刚打开的聊天窗发本地文件可能有同样的首屏清空问题，未验证，也不在本功能范围内。
- WPS 读到的列表数与 Word 不同，原因未核实（不影响打开）。

**编译与测试**

- `FlamingoMarkdown.vcxproj` 已加入 `FlamingoClient.sln`。FlamingoUnit 直接编进 DLL 的 `Core/` 源文件与 `MdApiTable.cpp`，exe 侧用例用 `markdown::SetMarkdownApiOverrideForTest(true, flamingomd::GetMdApiTable())` 代替真实 DLL，用完要恢复（`SetMarkdownApiOverrideForTest(false, NULL)`）。
- 标签：`[mddll]`、`[mdhost]`、`[mdproto]`、`[mdbubble]`、`[mdedit]`、`[mdeditor]`、`[mdtoolbar]`、`[mdentry]`；隐藏用例 `[.mdpdf]`（真实打印）、`[.mddocx]`、`[.mdpng]`、`[.mdshot]`（后三个输出到环境变量 `MD_SHOT_DIR` 所指目录）。一次跑完：`FlamingoUnit.exe "[mddll],[mdeditor],[mdbubble],[mdtoolbar],[mdedit],[mdhost],[mdproto],[mdentry],[traymenu],[sysmenu]"`，2026-10-07 为 162 个用例全部通过。
- 会加载新编 DLL 或真实打印的运行放在不可见桌面里跑，避免调试版 CRT 的对话框弹到用户桌面。
- 本机辅助脚本都在 `tmp/` 下，`tmp/` 不入库，换机器需要自行准备：`tmp/md/live/`（双账号上屏核对环境：`setup_mdlive.py` 搭建或换 exe，`start_server.ps1`、`start_client.ps1` 启动，`ui.ps1`、`typelines.ps1`、`dlgsave.ps1`、`msgbox.ps1` 操作界面）。
- md4c 0.6.0 放在 `third-party/md4c-0.6.0/`，与 `third-party/` 下其它库一样不入库，换机器从 <https://github.com/mity/md4c/archive/refs/tags/v0.6.0.tar.gz> 取得。

**未提交的文件**

工作区里同时有别的会话的改动，提交时按下面的清单逐个核对，不要把别人的改动一并带上。

- 新增（未跟踪）：
  - `flamingoclient/FlamingoMarkdown/` 整个工程：`Core/` 下 24 个文件、`MdApiTable.*`、`DllMain.cpp`、`FlamingoMarkdown.def`、`FlamingoMarkdown.vcxproj`、`FlamingoMarkdownApi.h`、`Tests/` 下 6 个测试文件。编译产物已由 `.gitignore` 排除。
  - `flamingoclient/Source/Feature/Markdown/`、`flamingoclient/Source/UI/BalloonMarkdown/`。
  - `Source/UI/BalloonChatWnd/` 下的 `ChatMarkdown*`、`MarkdownEditCommands*`。
  - `Source/Tests/` 下的 `ChatBubbleMarkdownTests`、`ChatMarkdownToolbarTests`、`MarkdownEditCommandsTests`、`MarkdownEditorTests`、`MarkdownHostTests`。
  - 本文档。
- 修改（已跟踪）：
  - 聊天窗一侧：两个聊天窗与两份布局、`ChatBubble`、`ChatMessageList.h`、`ChatMessageMenu.h`、`ChatReedit`、`ChatInputRichEdit`、`ChatIconButton`、`ChatMockData.h`、`ChatSendLimit`、`ChatQuoteModel`、`ChatMergedCard`、`VoiceBarLayoutTests`。
  - 主面板一侧：`BalloonMainPanel`、`BalloonMainPanelForward`、`MainPanelSessionController`、`MainPanelTrayController`、`MainPanelSystemMenu`、`MainPanelTrayMenu`、`MainPanelIcons`、`ForwardRequest`、`ForwardTargetModel`，以及对应的菜单测试。
  - 其它代码：`App/AppShell.cpp`、`net/MessageProtocolClient`、`net/IUSocket`、`UI/BalloonHistory/V2MsgQuery.cpp`、`UI/BalloonSubtitle/SubtitleModel.cpp`、`tools/gen_media_vcxproj.py`。
  - 工程、文本与配置：`Flamingo.vcxproj`、`Tests/FlamingoUnit.vcxproj` 与两个 `.filters`、`FlamingoClient.sln`、FlamingoMedia 工程（生成脚本加入了 `ChatMarkdownIcons.cpp`）、四个 `.po`、开发约定文档、`.gitignore`（新增 FlamingoMarkdown 编译产物的忽略规则）。

## 12. 第 4 期：代码块着色、历史记录里的卡片、复制带格式、列表回车续项（2026-10-07）

用户选定第 4 期里的四项一起做：代码块语法着色、消息记录窗与聊天记录管理器（以及聊天记录查看窗）按卡片显示 Markdown 消息、复制时写 HTML 格式、编辑器里列表回车自动续项。Markdown 里画表情、@ 高亮不在本期。

### 12.1 用户确认的决定

| 问题 | 决定 |
| --- | --- |
| 代码着色做到哪些输出上 | 全部：编辑器预览、预览窗口、PNG / PDF、导出的 HTML 与 Word 都着色；只在围栏后写了认得的语言名时着色，不自动识别语言 |
| 预览窗口「复制 ▾」菜单 | 新增「复制全文（带格式）」（纯文字加 HTML）；「复制为纯文字」保持只放纯文字。预览控件里选中后 Ctrl+C / 右键「复制」一律同时放纯文字与 HTML |
| 历史记录里的摘要文字 | 左栏会话摘要、「与我有关」页的预览改为「[Markdown] 标题」；搜索命中片段、导出 txt / HTML、右键复制仍用源文本 |
| 测试用例与实机核对 | 用例按计划认可；实机核对可以上屏（剪贴板事先备份、事后恢复） |

### 12.2 实施

**代码块语法着色（DLL）**

- 新增 `FlamingoMarkdown/Core/MdCodeColor`：把围栏后的语言名交给客户端的词法分析器 `Source/Common/Code/CodeLexer`（纯标准 C++，直接编进 DLL，导入表不变，`MDG2` 守住），算出每个字符的着色种类；颜色表与聊天窗代码片段（`ChatCodeBlock.cpp` 的 `kTokenColors`）逐项相同，由 ML19 核对。颜色由 DLL 自带，不往 `MdStyle` 加字段（宿主先把结构清零，新字段在旧宿主那里是 0，分不出「没设置」与「黑色」）。
- 排版仍按「一行代码一个文字段」折行，排好后在 `EmitFragment` 里按着色种类把片段切开，各段的横坐标取自原片段的 `m_caret`，所以折行、行高、命中测试与选区都与不着色时相同（ML20、ML21）。`MdFragment` 追加 `m_tokenKind`，绘制时代码块里着色的片段按种类取色。
- HTML 导出把着色的段包进 `<span style="color:#RRGGBB">`（行内样式，复制到 Word 等程序时颜色不依赖样式表）；Word 导出给着色的 run 加 `<w:color>`。切段统一用 `MdCodeColorRunEnd`，不把代理对拆成两段。

**复制时写 HTML 格式**

- DLL 第四版函数表追加 `m_exportHtmlRange`（`kMdApiV4Size`，DLL 升为 1.4.0）：按渲染后文字的下标导出一段，输出完整的 HTML 文档，头部与样式表与 `m_exportHtml` 相同，正文只含与选区相交的块（段落里只留选中的文字、列表只含相交的项且有序列表的起始编号随之调整、表格只含相交的行且列数不变、代码块只含选中的部分），`<div class="md">` 前后夹着 `<!--StartFragment-->` 与 `<!--EndFragment-->`。为此 `MdBlock` 追加 `m_flatBegin` / `m_flatEnd`（块在渲染后文字里的起止，`FlatWriter` 填写）。整篇导出与按选区导出共用一套生成函数，整篇导出走「全部」分支，输出与改动前逐字节相同（MX3、MX4 照旧通过）。
- exe 新增 `Source/UI/BalloonMarkdown/MarkdownClipboard`：`WrapCfHtml` 加上 CF_HTML 头部（去掉文档类型声明，StartHTML 指向 `<html>`，四个偏移按 UTF-8 字节计）；`BuildMarkdownSelectionClipboard` / `BuildMarkdownWholeClipboard` 组装纯文字与 HTML；`PutMarkdownClipboard` 一次打开剪贴板写入两种格式。旧版 DLL 没有第四版接口时只写纯文字。`ChatMarkdownPreview::CopySelection`（预览窗口正文与编辑器右侧预览）与预览窗口菜单改用它。

**历史记录里的卡片**

- 卡片的宽度、标题与摘要的取法、高度计算与内容绘制从 `ChatBubble.cpp` 抽到 `Source/UI/BalloonChatWnd/ChatMarkdownCard`，聊天气泡与新的独立卡片控件 `ChatMarkdownCardControl` 共用（MB1 ~ MB7 照旧通过）。控件自己画与「别人发的消息」气泡相同的底色与圆角，单击投递 `WM_CHAT_OPEN_MARKDOWN_PREVIEW`（沿用 `0x8A`，载荷 `MarkdownPreviewOpenRequest`），双击不再传给所在的行。
- 消息记录窗与聊天记录管理器（`CBalloonHistoryWindow`）：`BuildViewData` 按本地库 content 列的 `format` 认出 Markdown 消息（`IsMarkdownContentJson`），`MakeMarkdownCardInfo` 生成卡片内容（DLL 不可用或解析失败时返回 false，这一行照旧按文字显示）；`HistoryRowView` 追加 `m_isMarkdown` 与 `m_markdownCard`；`HistoryWindowLayout` 的行高与建行加上 Markdown 分支，卡片编号段 `kIdHistoryMarkdownCardBase = 11200`（容量 200，排在导出窗会话勾选段 9000 ~ 10999 之后，V2L5 核对不相交）；窗口处理 `WM_CHAT_OPEN_MARKDOWN_PREVIEW`，本人发的显示为「我」。
- 聊天记录查看窗（`CMergedRecordWindow`）：`MergedRecordRows` 按子消息的 `m_textFormat` 生成卡片行；合并记录里不区分本人，预览窗口显示原发送者的名字。据实证（`flamingoserver/chatserversrc/MessageProtocol.cpp:232-261`，`messageBodyValue`），服务端组装合并转发时原样带上每条子消息的 content，`format` 不会丢（MK2）。
- 摘要：`HistoryView` 新增 `DigestFromContent`（Markdown 消息为「[Markdown] 标题」，与主面板会话列表同一个函数 `MarkdownDigestFromUtf8Body`），只在左栏会话摘要（`HistoryConversationList.cpp`）与「与我有关」（`BalloonHistoryWindowLibrary.cpp`）两处使用；`PreviewFromContent` 不变，导出与搜索仍是源文本。

**列表回车续项**

- `MarkdownEditCommands` 新增纯函数 `ContinueMarkdownListOnEnter`：`- ` / `* ` / `+ `、`1. ` / `1) `（编号加一，位数不少于原来）、任务框（新项一律未完成）、引用（可嵌套，引用里的列表连同引用符一起续）；缩进与标记后的空白照原样；光标后的文字带到新行；空项上回车去掉列表标记、不换行（引用里的空列表项保留引用符）。有选区、光标在标记之前、光标在全文之外（`DuiRichEdit::GetText` 去掉了末尾的空行）、不是列表或引用（含分隔线 `* * *`）、在围栏代码块里时不处理。
- 编辑器窗口 `OnKeyDownMsg` 处理回车：Shift+回车直接插入一个 `\r`（不依赖 RichEdit 对 Shift+回车的处理），不带修饰键时先尝试续项；处理了的回车借用 `m_swallowCtrlChar` 丢掉随后的 `'\r'` 字符消息；输入法组字期间（`WM_IME_STARTCOMPOSITION` / `ENDCOMPOSITION` 旁听记录）不处理。续项是一次可撤销的替换。

### 12.3 与计划的出入

| 计划 | 实际 | 原因 |
| --- | --- | --- |
| MX7 的 Word 断言要随着色更新 | 改的是 MX8：按整行比对代码段落的文字（`CodeParagraphTexts`），不再按 run 的切法逐字断言 | 断言 `">int main()</w:t>"` 的是 MX8；着色只影响 run 的切法，着色本身由 MX12 检查 |
| 卡片文字由 `MarkdownCardTextFromBody` 取 | 合进 `MakeMarkdownCardInfo`，源文本只转换一次 | 前者只剩这一处调用 |
| `IsMarkdownContentJson` 放在 `ChatMarkdownCard.h` | 声明移到新的 `ChatMarkdownContent.h`（不依赖 ATL），`MarkdownDigestFromUtf8Body` 的声明也移过去；`ChatMarkdown.h`、`ChatMarkdownCard.h` 包含它 | `HistoryView.cpp`、`HistoryConversationList.cpp` 刻意不使用预编译头、不引入 ATL，不能包含带 `CString` 声明的头文件 |
| — | 隐藏用例 ED22 的示例文档里代码块写上 ` ```cpp ` | 人工核对 PNG 时顺便看到着色效果 |

### 12.4 测试

| 标签 | 用例 | 内容 |
| --- | --- | --- |
| `[mdedit]` | ME18 ~ ME26 | 三种无序列表符号、有序列表编号加一（含进位与补零）、任务框、引用与引用里的列表、缩进、空项结束列表、光标在行中间、不续项的各种情况（含围栏代码块与光标在全文之外）、`\r\n` 与 `\n` |
| `[mddll]` | ML18 ~ ML22 | 着色片段的种类与位置、不着色的情况与语言名别名、颜色表与聊天窗一致、着色不改变折行、命中与取文字不变、绘制出的像素颜色（不着色时没有这两种颜色作对照） |
| | MX11、MX12 | HTML 与 Word 导出的着色；切段不拆开代理对 |
| | MX13 ~ MX16 | 按选区导出 HTML：半句、跨块与列表起始编号、表格的行与代码块的一段、第四版接口约定（全选与整篇导出的正文相同、顺序不限、越界截断、起止相同为空、参数检查） |
| | MX1（更新）、MX3（更新）、MX8（更新） | 版本 1.4；代码块着色后的 HTML；按整行比对 Word 代码段落 |
| `[mdeditor]` | ED23、ED24 | CF_HTML 头部的偏移；预览复制的内容（选区、旧版 DLL、全文带格式） |
| `[mdhistory]`（新） | MK1 ~ MK5 | 识别本地库 content、合并转发的子消息、消息记录窗列表里的卡片、卡片控件的尺寸与单击投递、一行摘要 |
| `[history]` | V2L5（更新） | Markdown 卡片编号段与已有各段不相交 |

结果（2026-10-07 16:22，隔离编译的 `FlamingoUnit_md.exe`，其他会话暂停编译期间）：`[mdhistory]`、`[mdbubble]`、`[mdeditor]`、`[mdtoolbar]`、`[mdedit]`、`[mdhost]`、`[mdproto]`、`[history]`、`[forward]`、`[codedigest]`、`[codehistory]` 共 140 个用例全部通过；`[mddll]` 在此之前单独运行全部通过，只有 MD16（病态输入的解析耗时）在机器满载时超时，据实证与本期改动无关：不含本期改动的 `FlamingoUnit_cn.exe`（14:17 编译）在同一负载下运行 MD16 三轮都超时（3.8 ~ 5.4 秒，上限 3 秒）。

全量回归（`~[live]`，在不可见桌面里运行）：16:44 那次只剩 MDL-L4 ~ L6 三个已知失败，其余全部通过（含 I18G3、`[msgid]`、`[filters]`、`[i18n-scan]`，MD16 在机器不满载时通过）。DLL 四个配置、`Flamingo_md.exe`（Debug x64）、`FlamingoLang.dll`（Debug x64 / Win32）在其他会话暂停编译期间编完；`MDG1`、`MDG2` 通过（导入表仍只有 kernel32、user32、gdi32、gdiplus）。

### 12.5 实机核对（2026-10-07 16:3x，经用户同意上屏）

在 `tmp/md/live` 双账号环境（客户端 A 张勇、B 韩晓燕888，`setup_mdlive.py client` 换上新编的 exe 与 DLL）里核对，截图在 `tmp/md/live/shots4/`：

- 列表续项（`e_typed.png`）：在编辑器里逐行投递文字与回车，「- 甲」「1. 一」「- [x] 完成」「> 引用」之后各行的标记都由回车续上，新的任务项为 `[ ]`；每组之后两次回车先去掉空项的标记、再留出空行；` ```cpp ` 围栏里「- inside fence」之后回车没有续项。
- 代码着色：编辑器右侧预览与预览窗口（`a_preview.png`）里 `int`、`return`、`42`、`// done` 分别为类型、关键字、数字、注释的颜色。
- 聊天窗卡片（`a_chat.png`）：卡片绘制抽到 `ChatMarkdownCard` 之后，新卡片与改动前发出的卡片外观一致。
- 复制带格式：预览窗口「复制 ▾」里有「复制全文（带格式）」；复制后剪贴板里有 `HTML Format` 与纯文字两种格式，纯文字已去掉标记；据实证（直接读剪贴板原始字节，`p4_cfhtml_raw.bin`）StartHTML 处为 `<html>`、EndHTML 等于总长、片段从 `<div class="md">` 到 `</div>`。粘贴进不可见的 Word（COM，`p4_word_paste.ps1` / `.txt`）：第一段为内置「标题 1」、大纲级别 1；3 个列表；代码段落为「HTML 预设格式」、Consolas；`return` 的颜色值 16711680（蓝 0000FF）、`done` 为 32768（绿 008000）。核对前备份、核对后恢复了用户剪贴板里的文字。
- 历史记录里的卡片（`a_history.png`）：聊天记录管理器里本次与前一天发出的 Markdown 消息都显示为卡片；左栏会话摘要为「[Markdown] 第 4 期…」；点卡片打开了对应标题的预览窗口。

未在实机上核对的：Shift+回车只换行、Ctrl+Z 一步撤回续项、输入法组字时回车不续项。这三项需要真实的修饰键或输入法，投递窗口消息模拟不出来；注入真实键盘输入会落到当时的前台窗口，可能打进用户正在使用的程序，所以没有做。撤回依赖 `ReplaceSel(…, true)` 一次替换（与格式按钮相同的写法）。

### 12.6 已知局限

- `HistoryLibraryLayout.h` 的 `kIdLibCopyBase` 与 `HistoryWindowLayout.h` 的 `kIdHistoryCodeCardBase` 都是 8200（本期之前就是这样）。据推测两者分属互相替换显示的两棵控件树，没有冲突，未核实，本期未改动。

### 12.7 本期改动的文件（均未提交）

与 11.8 节相同，工作区里同时有别的会话的改动，提交时按下面的清单与 11.8 节的清单逐个核对。

- 新增：`FlamingoMarkdown/Core/MdCodeColor.*`；`Source/UI/BalloonMarkdown/MarkdownClipboard.*`；`Source/UI/BalloonChatWnd/ChatMarkdownCard.*`、`ChatMarkdownCardControl.*`、`ChatMarkdownContent.h`；`Source/Tests/MarkdownHistoryTests.cpp`。
- 修改，DLL：`FlamingoMarkdownApi.h`、`MdApiTable.cpp`、`FlamingoMarkdown.vcxproj`、`Core/MdModel.h`、`MdBuilder.cpp`、`MdLayout.*`、`MdPaint.cpp`、`MdHtml.*`、`MdDocx.cpp`，测试 `Tests/MdLayoutTests.cpp`、`MdExportTests.cpp`、`MdDocxTests.cpp`。
- 修改，exe：`UI/BalloonChatWnd/MarkdownEditCommands.*`、`ChatBubble.cpp`、`ChatMarkdown.h`、`ChatMarkdownPreview.*`；`UI/BalloonMarkdown/MarkdownEditorWindow.*`、`MarkdownPreviewWindow.cpp`；`Feature/Markdown/MarkdownDocument.*`；`UI/BalloonHistory/HistoryWindowLayout.*`、`BalloonHistoryWindow.*`、`HistoryView.*`、`HistoryConversationList.cpp`、`BalloonHistoryWindowLibrary.cpp`、`MergedRecordRows.cpp`、`MergedRecordWindow.*`；测试 `Tests/MarkdownEditCommandsTests.cpp`、`MarkdownEditorTests.cpp`、`HistoryWindowLayoutTests.cpp`。
- 工程、文本与配置：`Source/Flamingo.vcxproj`、`Source/Tests/FlamingoUnit.vcxproj` 与两个 `.filters`、四个 `.po`（`i18n_extract.py` 重新提取时也加进了别的会话新加、尚未翻译的条目）、本文档、开发约定文档。
- 本机辅助文件（`tmp/` 不入库）：`tmp/md/live/p4_*`、`tmp/md/live/shots4/`。

## 13. 第 7 期：标题级别、格式快捷键、下划线、分割线、警告框、查找替换（2026-10-07）

用户提出编辑器里标题只有一级、缺少警告框与水平分割线语法、没有查找替换，并给出两张截图（`snapshots/吗.png`、`吗2.png`）指定格式快捷键。本期按 7a ~ 7i 九步实施。

### 13.1 用户确认的决定

| 问题 | 决定 |
| --- | --- |
| 标题级别怎么选 | 「H」按钮改为下拉菜单：「正文」「一级标题」~「六级标题」，当前行的级别打勾；快捷键 Ctrl+1 ~ Ctrl+6，「正文」不配快捷键（不加 Ctrl+0） |
| 格式快捷键 | 只做截图里的：Ctrl+B 加粗、Ctrl+I 斜体、Ctrl+U 下划线、Ctrl+Shift+` 行内代码、Alt+Shift+5 删除线、Ctrl+K 链接、Ctrl+\ 清除样式，加上标题的 Ctrl+1 ~ Ctrl+6；截图以外的不加 |
| 菜单里的快捷键怎么显示 | 靠右单独一列（灰色），为此修改 balloonui 的 `DuiMenu`（用户批准） |
| 下划线 | 支持 `<u>…</u>`（Markdown 没有下划线语法，按 Typora 等的做法写 HTML 标签） |
| 警告框、分割线、查找替换 | 按方案做：GitHub 写法的五种警告框 `> [!NOTE]` 等；格式条加「分割线」「警告框」按钮；Ctrl+F / Ctrl+H、F3 / Shift+F3、Esc |

### 13.2 实施

**7a 标题级别与菜单的快捷键列（balloonui 与编辑器）**

- `DuiMenu`（经用户批准修改 balloonui）：菜单项文字可以写成「文字\t快捷键」，`\t` 之后的部分以灰色靠右画在单独一列，菜单宽度按「最宽的文字 + 间距 + 最宽的快捷键」计算；助记符只认 `\t` 之前的部分。新增静态函数 `SplitShortcut`，宽度计算抽成 `MenuBodyWidth`，`MeasureBody` 与 `MeasureSize` 共用。
- 编辑器格式条的「H」按钮改为弹出标题级别菜单（延后操作 `md_editor_action_heading_menu`，等按钮的点击通知处理完再弹）。纯函数 `SetMarkdownHeadingLevel`（设为指定级别，0 为正文；设为同一级别时不改动）与 `MarkdownHeadingLevelAt`（取当前行的级别，用于打勾）。

**7b 下划线（DLL 1.5.0）**

- md4c 打开行内 HTML（去掉 `MD_FLAG_NOHTMLSPANS`），`MdBuilder` 只认 `<u>` / `</u>`（大小写不限）：用嵌套深度 `m_underlineDepth` 给中间的文字加样式位 `md_style_underline`，没闭合的 `<u>` 到段落结束，没有对应 `<u>` 的 `</u>` 丢掉；其它行内标签仍按文字显示，代码里的 `<u>` 不处理。
- 排版时下划线与链接一样用带下划线的字体；HTML 导出写 `<u>`，Word 导出给 run 加 `<w:u w:val="single"/>`（排在 `w:strike` 之后、`w:shd` 之前）。编辑器的格式条在斜体之后插入「下划线」按钮（`MarkdownEditCommand` 其后的取值依次加一）。

**7c 其余快捷键与清除样式**

- 纯函数 `MarkdownShortcutFor` 把按键与修饰键对应到快捷键（多按或少按修饰键都不算）。编辑器在 `WM_KEYDOWN` 里处理，带 Alt 的 Alt+Shift+5 另在 `WM_SYSKEYDOWN` 里处理；快捷键随后产生的控制字符（Ctrl+I 是制表符、Ctrl+K 是 0x0B 等）用 `PeekMessage` 从队列里取走，Alt 组合产生的 `WM_SYSCHAR` 同样取走，否则系统按菜单助记符处理并发出提示音。
- 纯函数 `ClearMarkdownInlineStyle`（Ctrl+\）：去掉选区里的 `**`、`__`、`~~`、行内代码的反引号（代码文字保留）、`*`、作强调用的 `_`（单词中间的不算）、`<u>` 与 `</u>`；链接与图片只留下文字；每行行首的缩进、引用符、列表符号、任务框与标题前缀不动；选区正好被成对的标记包着时外侧标记一起去掉。
- 格式按钮的悬停提示写上快捷键（「标题（Ctrl+1 ~ Ctrl+6）」等）。

**7d 分割线**

- 格式条末尾加「分割线」按钮：在选区终点之后另起一段插入 `---`，与上面的文字隔一个空行（否则上一行会变成二级标题），后面还有文字时另起一行。

**7e 警告框（DLL 函数表第五版）**

- md4c 打开 `MD_FLAG_ADMONITIONS`：引用块第一行正好是「[!类型]」（类型不区分大小写）时整块成为 `MD_BLOCK_ADMONITION`，类型行不进正文（据实证，`md4c.c` 7087 ~ 7106 行）。模型里警告框是 `m_alertType` 不为 0 的引用块。新增 `FlamingoMarkdown/Core/MdAlert`：类型、主色与底色（参照 GitHub 浅色主题）、标题。
- 排版（`LayoutAlert`）：整块浅色底（`md_deco_alert_back`，插在框内装饰之前，绘制时先画，不会盖住框里的代码块底色）、左侧彩色竖条、标题行（五种图标形状不同的 `md_deco_alert_icon` 加一个加粗的标题片段；标题片段不可选中、不在任何行里、不进渲染后文字）；正文按引用缩进、用正文颜色（普通引用块是次要文字颜色），右边留内边距。
- HTML 导出写成与 GitHub 相同结构的 `<div class="markdown-alert markdown-alert-note">` 加标题段，颜色写在元素的 `style` 上；Word 导出先写一段加粗的彩色标题，框内段落带彩色左边框与底色、不用引用样式，代码段落只加边框（保留自己的底色）。
- 标题是界面文字，DLL 不自带：函数表第五版追加 `m_setAlertTitles`（`kMdApiV5Size`），宿主 `CMarkdownModuleHost` 在模块初始化之后传入五个译文（上下文「Markdown 警告框」）；没有设置时显示类型名本身（NOTE 等）。
- 编辑器格式条末尾加「警告框」按钮，弹出类型菜单（类型名靠右显示在快捷键列里）。纯函数 `InsertMarkdownAlert`：光标或选区起点在类型行上时只换类型；有选区时把各行放进警告框；没有选区时另起一段插入类型行与占位文字并选中占位文字；上下紧挨着文字时各空一行（否则会并进相邻的引用块，或下一行作为延续行并进警告框）。

**7f、7g 查找与替换**

- 纯逻辑 `Source/UI/BalloonMarkdown/MarkdownFindReplace`：`FindMarkdownText`（向后从选区终点、向前从选区起点，到末尾或开头后回绕）、`CountMarkdownMatches`、`ReplaceAllMarkdownText`（结果是一个替换区间，一次 `ReplaceSel` 写回、一步撤销；替换进去的文字不再参与查找）、`ReplaceCurrentMarkdownText`（选区正好是匹配时替换并找下一个，否则只找下一个）、`MarkdownFindKeyFor`（按键对应）。不区分大小写时用 `CharLowerBuffW` 逐个码元转小写，下标与原文一致。
- 编辑器在顶部一行与编辑区之间加两行（默认隐藏）：查找行是查找框、「上一个」「下一个」「区分大小写」、匹配个数（「共 N 处」「无结果」）、关闭按钮；替换行是替换框、「替换」「全部替换」。Ctrl+F 只显示查找行，Ctrl+H 两行都显示；编辑框里选中单行文字时填进查找框。F3 / Shift+F3 查找下一个 / 上一个（查找框为空时打开查找条），查找框里回车查找下一个、Shift+回车上一个，替换框里回车替换当前；Esc 或关闭按钮收起查找条（延后操作 `md_editor_action_close_find`，等按钮的点击通知处理完再隐藏它所在的行）。显隐由 `ShowMarkdownFindBar` 设置并对根容器 `ForceLayout`（UI 约定第 5 条）。焦点在查找框或替换框里时，格式快捷键不作用于编辑框。找到的匹配先 `EM_SCROLLCARET` 滚到可见再选中；回绕时右上角状态文字提示「已从开头继续查找」。

### 13.3 与计划的出入

| 计划 | 实际 | 原因 |
| --- | --- | --- |
| 警告框标题经 `MdStyle` 第三版字段传入（MA9 按此设计） | 改为函数表第五版的 `m_setAlertTitles`，宿主加载模块后调用一次；MA9 按新接口检查 | 标题与排版样式无关，导出 HTML / Word 时也要用到，而导出不经过 `MdStyle`；放在函数表里，旧宿主不调用时自然显示类型名 |
| 没有对应 `<u>` 的 `</u>` | 丢掉，不显示 | 按文字显示会在正文里留下孤立的标签，用户多半是删开标签时漏删了闭标签 |
| 只有新快捷键丢掉随后的控制字符 | Ctrl+B、Ctrl+I 也改为统一用 `DropPendingControlChar` | 与新快捷键同一套处理，Ctrl+I 产生的制表符原先由 `m_swallowCtrlChar` 丢掉，效果相同 |
| ED25 只查查找条的布局 | 另加按键对应（`MarkdownFindKeyFor`）的断言 | 按键对应是纯函数，放在同一个用例里检查成本很低 |
| ML24、MX18 | ML24 的示例里加了列表与代码块；MX18 加了纯文字与卡片摘要里没有类型行的断言 | 按 13.1 确认的用例内容补齐 |

### 13.4 测试

| 标签 | 用例 | 内容 |
| --- | --- | --- |
| balloonui（DuiGallery） | `DuiMenuTests` 的 SplitShortcut、ShortcutWidthCounted、ShortcutNoAccelerator | 「文字\t快捷键」拆成两段；菜单宽度计入最宽的快捷键；助记符只认 `\t` 之前的部分 |
| `[mdedit]` | ME27 ~ ME36 | 标题级别的设置与读取、快捷键对应、下划线命令、清除样式、分割线、插入警告框 |
| | FR1 ~ FR3（新文件 `MarkdownFindReplaceTests.cpp`） | 查找下一个 / 上一个与回绕、区分大小写、匹配个数；全部替换；替换当前 |
| `[mddll]` | MD19、MD20 | `<u>` 的解析；警告框的解析（五种类型、大小写、只有类型行、列表里的警告框、仍是普通引用的三种写法） |
| | ML23、ML24 | 下划线画出横线；警告框的装饰顺序与范围、标题片段、正文颜色、框里的列表与代码块、宿主设置的标题、底色与竖条的像素颜色 |
| | MX17、MX18 | 下划线与警告框的 HTML / Word 导出（含按选区导出、纯文字与卡片摘要、译文标题） |
| | MA9 | 第五版函数表：`m_setAlertTitles` 的标题用于导出，NULL 项与多余项、清除 |
| | MD1、MX1（更新） | 解析选项打开行内 HTML 与警告框；版本 1.5 |
| `[mdeditor]` | ED25 | 查找条默认隐藏；显示查找行、两行都显示、隐藏、隐藏后再显示，按默认尺寸与最小尺寸各行排好、底部「发送」排得下；查找按键的对应 |

结果（2026-10-07，其他会话暂停编译期间）：

- balloonui（19:20，`DuiGallery.exe` 在不可见桌面里运行）：`DuiMenuTests` 25 个全部通过（含新增的 3 个）。失败的有三个：`DuiSplitterTests/HitTestBarRegion`、`DuiFrameWindowTests/Defaults` 是开发约定文档记录的已有失败；`DuiRichEditCaretTests/SystemCaretLeavesNoPixelsOnScreen` 报「screen capture failed」，据推测是不可见桌面里截不到屏幕所致，与本期改动无关。
- ED25 第一次运行时查出最小窗口宽度（640）下查找行放不下：关闭按钮右边到了 677，行的右边是 628。查找框原定宽 260，加上其余控件共需约 664 像素，而可用宽度只有 616。改为查找框 200、匹配个数占去剩余宽度之后通过。
- `FlamingoUnit_md.exe` 全量回归（`~[live]`，19:3x，不可见桌面）：5213 个用例，5209 个通过，3 个失败，1 个跳过。失败的是 MDL-L4 ~ L6（FlamingoMedia 测试程序与插件的版本不符，已知），跳过的是截图热键用例（它自己判定当前环境不适合运行）；`[mddll]` 里的 MDG1、MDG2（DLL 的工程配置与导入表）、I18G3、`[msgid]`、`[filters]` 都通过。
- FlamingoMarkdown.dll 四个配置、`Flamingo_md.exe`（Debug x64）、`FlamingoLang.dll`（Debug x64 / Win32）都编译通过，没有新的警告。
- 实机核对之后改了编辑器的焦点判断与勾选框宽度（见 13.5 节），20:00 重跑 Markdown 相关的一组（`[mddll]`、`[mdeditor]`、`[mdedit]` 等 282 个用例）：只有 I18G3 失败，缺的五条文字（「{0}：{1}」「赞」「＋ 表情」等）不是本期的，已告知相关会话；ED25 照旧通过。

### 13.5 实机核对（2026-10-07 19:3x ~ 19:5x）

在 `tmp/md/live` 双账号环境里核对（客户端 A 张勇、B 韩晓燕888，`setup_mdlive.py client` 换上 19:28 的 `Flamingo_md.exe` 与 19:21 的 DLL），截图在 `tmp/md/live/shots7/`。操作都是投递窗口消息，截图用 PrintWindow。带 Ctrl / Alt 的快捷键用新写的 `tmp/md/live/modkey.ps1` 模拟：它用 AttachThreadInput 挂到客户端的界面线程上，只在两个线程共享的按键状态里把修饰键设为按下，投递按键之后恢复原状态并解除挂接，不向前台窗口注入真实按键。

- 格式条（`e_empty.png`）：15 个按钮齐全。标题菜单（`e_headingmenu.png`）：「正文」打勾，「一级标题」~「六级标题」的快捷键 Ctrl+1 ~ Ctrl+6 灰色靠右成一列；选「二级标题」后当前行加上 `## `。警告框菜单（`e_alertmenu.png`）：五种类型，类型名靠右；选「注意」后在文末另起一段插入 `> [!CAUTION]` 与选中的占位文字。
- 渲染（`e_typed.png`，编辑器右侧预览；`a_preview.png`，预览窗口）：二级标题与细线、`<u>` 下划线、加粗、行内代码、分割线正确；三个警告框的底色、彩色竖条、图标（警告三角形、技巧灯泡、注意八边形）与标题（宿主经 `m_setAlertTitles` 传入，日志 `setalerttitles: 5 title(s) set.`）正确。
- 快捷键（`k_*.png`）：Ctrl+B、Alt+Shift+5（没有多出「%」）、Ctrl+U、Ctrl+Shift+`、Ctrl+K（选中地址占位）、Ctrl+I、Ctrl+3 都按预期改写；Ctrl+\ 去掉正好包着选区的成对标记。
- 查找替换：F3 在查找框为空时打开查找行（`e_find.png`），输入 abc 后显示「共 3 处」（不区分大小写），回车与 F3 依次选中各处，焦点在查找框里时编辑框的选区仍然显示（据实证，`e_find.png`、`e_find2.png`）；Esc 收起查找条，编辑区收回高度（`e_findclosed.png`）；Ctrl+H 两行都显示，选中的文字填进查找框并全选，随后的退格符被丢掉（`e_replace.png`）；「全部替换」把三处换成 XYZ、匹配个数变为「无结果」（`e_replaceall.png`），Ctrl+Z 一步撤回（`e_undo.png`）。
- Word：隐藏用例 `[.mddocx]` 生成的样例（含警告框）用 Word 打开无错误；警告框标题段加粗、颜色 9A6700，框内段落左边框 9A6700、底色 FCF6E8、样式为「正文」，代码段落保留 Code Block 样式与自己的底色。
- 发送：选发送对象窗口里的摘要与聊天窗卡片（`pick.png`、`a_chat.png`）都没有类型行与标题；点卡片打开的预览窗口与编辑器预览一致。

核对中发现两个问题：

1. **用过标题菜单或警告框菜单之后，回车不再续列表与引用。** 投递的回车和直接 SendMessage 的回车都只插入了普通换行；排除了输入法组字标志与修饰键状态（用 AttachThreadInput 读到的界面线程按键状态里 Shift、Ctrl、Alt 都没有按下）之后，给窗口补发一条 WM_SETFOCUS，同样的回车就续上了「> 」（`e_imeend.png` 与 `e_setfocus.png` 对照）。原因：DuiMenu 弹出时调用 `SetForegroundWindow` 与 `SetFocus` 拿走窗口的 Win32 焦点（`DuiMenu.cpp`），DuiHost 收到 WM_KILLFOCUS 后把编辑框的 `IsFocused` 置为 false；菜单关闭后宿主的焦点控件仍是编辑框，`SetDuiFocus(编辑框)` 遇到同一个控件直接返回，标志不会恢复，而按键仍然照常交给编辑框。改为以宿主的焦点控件（`GetDuiFocus`）为准（`EditorHasKeyboardFocus`，`FindBarHasFocus` 同样改写）。真实用户在前台操作时，菜单关闭后窗口重新激活会补发 WM_SETFOCUS，据推测不一定遇到；改动对两种情况都正确。
2. **「区分大小写」勾选框的文字被截成「区分大小」**：宽度原按 13 像素的按钮字体量，勾选框实际用默认字体。改为设计宽度 110，再用 `i18n::GrowFixedWidthToText` 按默认字体只增不减地加宽（与「发送代码」窗口的「行号」勾选框相同）。

两处修改后（19:58 重编 `Flamingo_md.exe` 与 `FlamingoUnit_md.exe`）复核：先用标题菜单设二级标题，再投递输入引用与列表，回车依次续上「> 」与「> - 」，空项上回车先去掉列表标记、再去掉引用符，警告框里有了正文与列表（`f_continue.png`）；「区分大小写」五个字完整显示（`f_findbar_crop.png`）；焦点在查找框里时按 Ctrl+B，编辑框与查找框的文字都不变（`f_ctrlb_crop.png`）。

### 13.6 已知局限

- 警告框的颜色是固定的浅色主题配色，不随宿主传入的 `MdStyle` 颜色变化。
- 警告框标题片段不在任何行里：在标题上按下鼠标拖选时，选区从最近的一行开始；标题不进渲染后文字，复制得到的纯文字里没有标题（HTML 里有）。
- 警告框作为列表项的第一个子块时，有序列表的编号对齐到框里正文的第一行，而不是标题行；Word 导出里这一项不带编号。
- 查找只认原样的文字，不支持正则表达式与全字匹配。
- 新增的 33 条译文都标为「待审校」；`FlamingoLang.dll` 已重新编译。

### 13.7 本期改动的文件（均未提交）

与 11.8、12.7 节相同，工作区里同时有别的会话的改动，提交时按清单逐个核对。

- balloonui（经用户批准）：`balloonui/Controls/List/DuiMenu.h`、`DuiMenu.cpp`，测试 `balloonui/Tests/DuiMenuTests.cpp`。
- 新增：`FlamingoMarkdown/Core/MdAlert.*`；`Source/UI/BalloonMarkdown/MarkdownFindReplace.*`；`Source/Tests/MarkdownFindReplaceTests.cpp`。
- 修改，DLL：`FlamingoMarkdownApi.h`、`MdApiTable.cpp`、`FlamingoMarkdown.vcxproj`、`Core/MdModel.h`、`MdBuilder.cpp`、`MdLayout.*`、`MdPaint.cpp`、`MdHtml.cpp`、`MdDocx.cpp`，测试 `Tests/MdModelTests.cpp`、`MdLayoutTests.cpp`、`MdDocxTests.cpp`、`MdApiTests.cpp`、`MdExportTests.cpp`。
- 修改，exe：`UI/BalloonChatWnd/MarkdownEditCommands.*`、`ChatIcons.h`、`ChatMarkdownIcons.*`；`UI/BalloonMarkdown/MarkdownEditorWindow.*`、`MarkdownEditorLayout.*`；`Feature/Markdown/MarkdownModuleHost.cpp`；测试 `Tests/MarkdownEditCommandsTests.cpp`、`MarkdownEditorTests.cpp`。
- 工程、文本与配置：`Source/Flamingo.vcxproj`、`Source/Tests/FlamingoUnit.vcxproj` 与两个 `.filters`、四个 `.po`、本文档、开发约定文档。
- 本机辅助文件（`tmp/` 不入库）：`tmp/md/live/modkey.ps1`（实机核对时模拟带修饰键的按键）、`tmp/md/live/shots7/`。
