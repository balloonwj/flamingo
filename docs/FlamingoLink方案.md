# FlamingoLink 方案：管理控制台长连接与企业在线用户

- 文档创建：2026-10-04
- 适用范围：`flamingoserver/chatserversrc`（管理端口）、`flamingoAdmin/FlamingoLink/`（控制台专用 DLL）、`flamingoAdmin`（加载与嵌入）
- 状态：见第 11 节「进展」

## 1. 背景与目标

管理控制台（flamingoAdmin）与服务器之间原先只有 HTTP 短连接：每个接口一次连接、处理完由服务端立即断开（`HttpSession.cpp` 末尾的 `conn->forceClose()`），没有心跳，也没有推送通道。服务端的数据变化，控制台只能在下一次请求时才看到。

本方案给控制台加一条长连接，用来实时查看**某个企业当前有哪些用户在线、各自的在线状态**：

- 长连接在控制台登录成功后建立，断线后自动重连；
- 整个功能做成控制台专用的 `FlamingoLink.dll`，控制台启动时加载，**加载失败就没有这个功能及其页面，其余功能不受影响**；
- PC 客户端将来也会有一个同名、但完全独立的 `FlamingoLink.dll`（用于数据上报），与本 DLL 不是同一个工程、不是同一个文件。

## 2. 已确定的事项（2026-10-04 与用户逐项确认）

| 编号 | 事项 | 决定 |
| --- | --- | --- |
| D1 | 服务端端口 | chatserver 新开**管理专用端口**（默认 20006），不复用员工客户端的 20000 |
| D2 | 加密 | 沿用 v2 协议的 libsodium 三帧握手；**服务端公钥由 login.do 下发**，安全性跟随 HTTP 通道（开了 TLS 时可防中间人，没开时只能防窃听） |
| D3 | 界面范围 | 只做企业详情页的「在线用户」子页签；不做强制下线、组织架构状态实时刷新、仪表板在线统计卡 |
| D4 | 多设备 | 表格**每台设备一行**，同一个人有几台设备在线就有几行；汇总写「在线 N 人、M 台设备」 |
| D5 | 形态 | 功能做成 `FlamingoLink.dll`，放在 `flamingoAdmin/FlamingoLink/`；控制台与 PC 客户端各有一个同名独立 DLL |
| D6 | 页面位置 | **页面的界面代码也放进 DLL**：DLL 自带 balloonui，自己建子窗口嵌进企业详情页 |

## 3. 现状盘点（据实证）

1. 在线状态只存在内存里，按会话（每台设备一份）记录，取值 0 离线、1 在线、2 离开、3 勿扰（`Protocol.h` 的 `enum presence_type`）。注释写明不入库（`ChatSession::onChangePresence`）。同一账号可以多种客户端类型同时在线；同一类型重复登录时顶掉旧会话（`ChatSession::onLogin` 第 6 步）。
2. 服务端已有按企业范围推给员工的 1109（`cmd_presence_changed_notify`），挂在登录成功、改状态、连接断开三处（`ChatServer::broadcastToEnterprise`）。它有两个缺陷，见 `bugs.md` BUG-92、BUG-93：一台设备断开就推「离线」不看同账号其它设备；被顶号、被踢的会话断开时什么都不推。**管理端推送不复用 1109**，改为由服务端从会话表现算每个用户的全部设备，因此不受这两个缺陷影响。
3. 20000 端口的空闲检测没有启用（`ChatSession` 构造函数里 `enableHearbeatCheck()` 被注释掉）。管理端口单独实现空闲检测，不影响 20000。
4. 帧格式、压缩、加密握手的服务端实现在 `net/FrameCodec`、`net/FramePayload`、`net/FrameCrypto`，`TcpSession` 负责发送方向的组帧与加密，可直接作为管理会话的基类。
5. 控制台的 PC 端可复用文件：`flamingoclient/Source/net/` 下的 `FrameCodec`、`FrameCrypto`、`FramePayload`、`HandshakeRunner` 与 `Common/Util/ZlibUtil` 不依赖客户端专属头文件，`FlamingoFileShare.vcxproj` 已经直接编译过它们。
6. 控制台的叠层弹窗与提示条都画在主窗口自己的控件树里（`Widgets/Toast.h` 的 `RootContainer`），主窗口原先创建时没有 `WS_CLIPCHILDREN`（第 3 期已加上，`flamingoAdmin/main.cpp`）。原生子窗口总是盖在父窗口的绘制内容之上，所以 DLL 的页面窗口需要在叠层弹窗出现时隐藏。
7. 控制台日志器原先不加锁、每次调用都打开文件追加，规定工作线程不得写日志（`Util/HttpWorker.cpp`）。DLL 的连接线程要写日志，所以日志器要加锁（第 3 期起 `Util/Logger.cpp` 加锁）。

## 4. 总体结构

```
flamingoAdmin.exe                              chatserver
┌──────────────────────────────┐              ┌──────────────────────────────────┐
│ AdminFrame                    │  HTTP 12345  │ HttpSession  login.do 返回         │
│   登录成功 → Start(token,...) │◄────────────►│   linkport / linkpubkey            │
│ EnterpriseDetailScreen        │              │                                    │
│   「在线用户」页签 + 占位控件 │              │ AdminLinkServer（端口 20006）      │
│        │ 跟随位置 / 显隐       │              │   AdminLinkSession × N             │
│        ▼                      │  长连接 20006 │   专用推送线程：订阅表、攒批推送   │
│ FlamingoLink.dll              │◄────────────►│        ▲                           │
│   连接线程：握手/鉴权/心跳/重连│              │        │ 用户在线情况变化（五处）  │
│   页面窗口：自带 balloonui    │              │ ChatSession / ChatServer           │
└──────────────────────────────┘              └──────────────────────────────────┘
```

## 5. 服务端

### 5.1 配置（`etc/chatserver.conf`）

| 配置项 | 默认 | 说明 |
| --- | --- | --- |
| `adminlinklistenip` | `0.0.0.0` | 管理端口的监听地址。可只绑内网地址 |
| `adminlinklistenport` | 缺失即不开启 | 管理端口。缺失或为 0 时不监听，login.do 回 `linkport=0`，控制台页面提示「服务器未开启实时连接」 |
| `adminlinkiothreadnum` | 1 | 管理端口的网络 I/O 线程数。管理员连接很少，一条线程足够 |

加密沿用全局的 `encryption` 与 `serverkeyfile`，与 20000 端口用同一把身份密钥。

### 5.2 类

- `AdminLinkProtocol`：报文的解析与组装、权限判定、按用户归并设备、分帧，全部是纯函数，单测覆盖。
- `AdminLinkSession`（继承 `TcpSession`）：一条控制台连接。收帧、加密握手、心跳应答、鉴权；订阅与退订转交推送线程处理。
- `AdminLinkServer`（单例）：监听、会话表、专用推送线程（`EventLoopThread`）。推送线程上维护订阅表与待推送用户集合，并负责空闲检测、未鉴权超时与 token 复查。

### 5.3 连接生命周期

1. 控制台连上后，若服务端开启了加密，先走三帧握手（cmd 1001）。
2. 鉴权（2501）之前只放行握手、心跳（1002）、鉴权三种命令，收到其它命令直接断开。连上后 10 秒内没有鉴权成功也断开。
3. 鉴权用现有的 `AdminManager::checkToken`。成功后会话记下管理员 ID 与管辖企业（0 表示平台超管）。
4. 任何方向 30 秒内收不到对方的数据就认为连接已断：服务端每 5 秒检查一次，超时即断开；控制台每 10 秒发一次心跳。
5. 推送线程每 60 秒对已鉴权的会话复查一次 token；token 已失效（过期、注销、管理员被停用）就断开连接。控制台重连时鉴权会失败，进而回到登录页。

### 5.4 订阅与推送

- 每条连接同一时刻最多订阅一个企业；再次订阅即替换。
- 订阅（2502）的权限与 HTTP 接口一致：平台超管可以订阅任何存在的企业；企业管理员只能订阅本企业，越权回 1101；企业不存在回 1102。
- 订阅请求由会话的 I/O 线程校验后交给推送线程：推送线程登记订阅、回 2502 应答，再把当前完整名单以一条或多条 2504 发出（第一条 `reset=1`）。
- 推送的单位是「某个用户当前全部在线设备」，由推送线程从会话表现算；设备列表为空表示此人已离线。控制台收到后整体替换这个人的行。
- 触发点共五处，都只调用 `AdminLinkServer::onUserOnlineChanged(userid)` 把 userid 放进待推送集合（加锁的 `std::set`，不发包）：
  1. `ChatSession::onLogin` 登录成功；
  2. `ChatSession::onChangePresence` 改状态；
  3. `ChatSession::onLogout` 主动注销（会话仍在、但 `isLoggedIn()` 为 false，不再计入在线）；
  4. `ChatSession::makeSessionInvalid` 被顶号、被管理员踢、被本人其它设备踢（在 userid 清零之前记下）；
  5. `ChatServer::onDisconnected` 连接断开。
- 推送线程每 1 秒处理一次待推送集合：按企业分组，只处理有人订阅的企业；每个企业取一次组织架构缓存（账号、昵称、部门）与一次会话表快照，组好报文推给订阅了该企业的连接。快照与变化推送都在这一个线程上计算、发送经 `TcpSession::send` 投递，因此同一连接上不会出现新状态先到、旧状态后到。
- 每帧最多 1000 个用户（单帧包体上限 1 MiB，一个用户约 200~300 字节），超出时分多帧发送。

### 5.5 login.do 应答新增字段

```json
{"code":0,"msg":"ok","token":"…","admin":{…},"linkport":20006,"linkpubkey":"QLcj…ZEg="}
```

- `linkport`：管理端口；未开启时为 0。
- `linkpubkey`：服务端身份公钥的 base64；服务端未开启加密时为空串，控制台据此跳过握手。

## 6. 协议（2500 号段，只在管理端口上收发）

心跳沿用 `cmd_heartbeat`（1002，空包体，服务端原样回应），握手沿用 `cmd_handshake`（1001）。

### 6.1 cmd_adminlink_auth = 2501（控制台 → 服务端）

```json
请求 {"token":"9f2c…（32 位十六进制）","clientversion":"1.0.0"}
应答 {"code":0,"msg":"ok","adminid":"1","enterpriseid":"0","heartbeat":10,"timeout":30}
```

`heartbeat` 是建议的心跳间隔（秒），`timeout` 是空闲断开时间（秒）。失败码：3 请求格式错误；1100 token 无效或已过期（控制台据此回登录页，不再重连）。

### 6.2 cmd_adminlink_subscribe_online = 2502（控制台 → 服务端）

```json
请求 {"enterpriseid":"123"}
应答 {"code":0,"msg":"ok","enterpriseid":"123"}
```

应答之后紧跟一条或多条 2504，第一条带 `"reset":1`。失败码：3 请求格式错误（含企业 ID 为 0）；1101 无权查看该企业；1102 企业不存在。

### 6.3 cmd_adminlink_unsubscribe_online = 2503（控制台 → 服务端）

```json
请求 {}
应答 {"code":0,"msg":"ok"}
```

### 6.4 cmd_adminlink_online_notify = 2504（服务端 → 控制台，推送）

```json
{
  "enterpriseid":"123",
  "reset":1,
  "users":[
    {"userid":"10001","account":"zhangsan","nickname":"张三","depts":["研发部","测试组"],
     "devices":[{"clienttype":1,"presence":1,"logintime":1759550000000,"ip":"192.168.1.10"},
                {"clienttype":2,"presence":2,"logintime":1759551000000,"ip":"10.0.0.8"}]},
    {"userid":"10002","account":"lisi","nickname":"李四","depts":["销售部"],"devices":[]}
  ]
}
```

- `reset=1`：控制台先清空表格再应用本帧；`reset=0` 或缺省：逐个用户整体替换。
- `devices` 为空：此人已离线，删除他的全部行。
- `depts`：主部门在前，其余按组织架构顺序；不在任何部门时为空数组。
- `logintime`：该设备本次登录成功的时间（Unix 毫秒）；`ip`：该设备连接的来源地址（不含端口）。

### 6.5 错误码（新号段 1100-1199）

| 值 | 名称 | 含义 |
| --- | --- | --- |
| 1100 | `error_code_adminlink_token_invalid` | token 无效、已过期、已注销，或管理员已被停用 |
| 1101 | `error_code_adminlink_forbidden` | 企业管理员订阅了别的企业 |
| 1102 | `error_code_adminlink_enterprise_not_exist` | 订阅的企业不存在 |

两份 `Protocol.h`（服务端、PC 客户端）同步增加，受 PP 组守卫。

## 7. FlamingoLink.dll

### 7.1 工程

- 位置 `flamingoAdmin/FlamingoLink/`，加入 `flamingoAdmin.sln`，四个配置（Debug / Release × x64 / Win32），产物输出到 `flamingoAdmin.exe` 所在目录。
- 静态 CRT（`/MTd`、`/MT`），定义 `WIN32` 与 `SODIUM_STATIC`，`/utf-8`，不用预编译头；自带 `stdafx.h`。
- 编译进来的外部源码：PC 客户端 `Source/net` 的帧编解码与加密握手、`Common/Util/ZlibUtil`、客户端那份 jsoncpp 与 zlib；控制台的 `Theme/AdminTheme`、`Widgets/ActionTable`、`Widgets/ColumnHeader`、`Widgets/UiHelpers`。链接 `balloonui_static`、libsodium、`ws2_32`、`gdiplus`、`comctl32`。
- 界面文字直接写在 DLL 里（控制台本身没有多语言）。

### 7.2 接口（`FlamingoLinkApi.h`）

- 导出唯一的工厂函数 `FlamingoLink_Create(hostApiVersion)`，返回归 DLL 所有的 `ILinkModule*`。
- `ILinkHost`（控制台实现）：`Log(level, text)` 可能在 DLL 的工作线程上调用；`OnSessionExpired()` 在界面线程上调用，表示重连时 token 已失效。
- `ILinkModule`（DLL 实现）：`Init` / `Shutdown`、`Start(LinkStartParams)` / `Stop`、`CreatePage(父窗口)` / `DestroyPage`、`SetPageBounds`、`ShowPage`、`SetEnterprise(企业 ID, 企业名)`。
- 跨 DLL 只传 POD 结构与 `const char*` / `const wchar_t*`；两边各自静态链接 CRT，不能跨边界释放内存。

### 7.3 连接线程

- 一条工作线程负责全部网络动作：连接（超时 5 秒）→ 握手（有公钥时）→ 鉴权 → 重新订阅当前企业 → 收发循环。
- 收发循环用 `select` 等 200 毫秒；发送请求经加锁队列交给工作线程。距上次发送满 10 秒发心跳；30 秒没收到任何数据即判定断线。
- 断线后按 1、2、4、8、16、30 秒的间隔重试（封顶 30 秒，每次在 ±20% 内随机），连上一次就把间隔重置。鉴权返回 1100 时不再重试，通知控制台会话已过期。
- 连接线程把事件（连接状态、名单）放进加锁队列，再 `PostMessage` 给 DLL 自己的页面窗口；页面窗口不存在时事件只更新 DLL 内的名单。

### 7.4 页面

- 页面窗口是父窗口为控制台主窗口的子窗口（`WS_CHILD | WS_CLIPSIBLINGS`），自带一个 balloonui 宿主。
- 顶部一行：汇总「在线 N 人、M 台设备（PC a · Android b …）」、按姓名 / 账号 / 部门筛选的搜索框、连接状态（「实时连接正常」「正在连接…」「连接已断开，n 秒后第 k 次重连」「服务器未开启实时连接」）。
- 表格每台设备一行，列：姓名、账号、部门、设备、状态（彩色圆点 + 文字）、登录时间、IP 地址。按状态（在线 > 勿扰 > 离开）、姓名、设备类型排序。
- 断线期间保留最后一份数据但以灰色绘制；重连成功后用新名单整体替换。
- 页面显示且已设置企业时订阅，隐藏或换企业时退订 / 改订。

## 8. 控制台接入

- 启动时按完整路径加载 exe 同目录的 `FlamingoLink.dll`（`Util/PluginLibrary`）。文件不存在、位数不符、导出函数缺失、接口版本不符时只记日志，企业详情页不出现「在线用户」页签。
- 登录成功后 `Start`（主机取 HTTP 的主机，端口与公钥取 login.do 应答，token 取登录结果）；退出登录、会话过期时 `Stop`；程序退出时依次 `DestroyPage`、`Stop`、`Shutdown`、卸载。
- 企业详情页加第三个子页签「在线用户」，内容区是一个占位控件（子类化现成的空容器控件，只覆写排版与显隐），它把位置与显隐转交给 DLL。
- 主窗口加 `WS_CLIPCHILDREN`；叠层弹窗显示时隐藏 DLL 页面，关闭后恢复。
- 日志器加锁，允许工作线程调用。

## 9. 测试用例

### 9.1 服务端（doctest，套件 ADL，`tests/test_adminlink.cpp`）

| 编号 | 内容 |
| --- | --- |
| ADL1 | 鉴权请求解析：正常；缺 token、token 非字符串、非 JSON 均失败 |
| ADL2 | 鉴权应答组装：字段齐全，ID 为字符串 |
| ADL3 | 订阅请求解析：企业 ID 为字符串；缺失、非字符串、为 0 均失败 |
| ADL4 | 订阅权限：超管任意存在的企业通过；企业管理员本企业通过、别的企业 1101；企业不存在 1102 |
| ADL5 | 按用户归并设备：同一用户 PC + Android 合成一个用户两台设备并按类型排序；已注销会话、userid 为 0 的会话不计；无任何会话的用户得到空设备列表 |
| ADL6 | 用户资料：账号、昵称、部门取自组织架构，主部门在前；不在组织架构里的用户用后备资料 |
| ADL7 | 分帧：2500 人按每帧 1000 人分 3 帧，只有第一帧 `reset=1`；空名单快照也发一帧 `reset=1` |
| ADL8 | 来源地址去端口：`1.2.3.4:5678` → `1.2.3.4`，无冒号原样返回 |
| ADL9 | 超时判定：空闲超时、未鉴权超时的边界值 |
| ADL10 | login.do 的 `linkport` / `linkpubkey` 组装：未开启时端口 0、未加密时公钥为空 |
| PP 组 | 两份 `Protocol.h` 新增的 cmd 与错误码一致且落在号段内 |

### 9.2 DLL 与控制台（flamingoAdminTests）

| 编号 | 内容 |
| --- | --- |
| FLK1 | 重连间隔：1、2、4、8、16、30、30 秒，各在 ±20% 内；成功后重置 |
| FLK2 | 鉴权应答解析：成功取心跳与超时；1100 归为会话过期；其它失败归为可重试 |
| FLK3 | 2504 解析：用户、设备、reset；非法 JSON 失败 |
| FLK4 | 名单合并：reset 清空；按用户整体替换；设备为空即删除；每台设备一行 |
| FLK5 | 筛选与排序：按姓名 / 账号 / 部门不区分大小写；状态顺序与同名排序 |
| FLK6 | 汇总：人数、设备数、各设备类型台数 |
| FLK7 | 订阅意图：显示且有企业才订阅；隐藏退订；换企业改订；重连后按当前企业重订 |
| FLK8 | 本地假服务端（明文）：鉴权成功进入已连接；服务端断开后按间隔重连并重订；服务端不应答心跳时判定断线；鉴权 1100 只报一次会话过期且不再重连 |
| FLK9 | 工程守卫：`FlamingoLink.vcxproj` 四个配置都定义 `WIN32`、`SODIUM_STATIC` 并使用静态 CRT |
| FLK10 | login.do 新字段解析：有值时取到；缺省时端口 0、公钥为空 |
| FLK11 | DLL 页面显隐判定：页签选中、页面所在屏幕可见、没有叠层弹窗三者同时满足才显示 |
| FLK12 | 加载器：路径不存在返回「找不到」，导出函数缺失返回对应结果 |

## 10. 分期

| 期 | 内容 |
| --- | --- |
| 0 | 本文档、协议文档、bugs.md 记录 1109 的两个缺陷 |
| 1 | 服务端：1a 端口、会话、握手、心跳、空闲断开与 login.do 字段；1b 鉴权；1c 订阅与名单；1d 五个触发点与攒批推送；1e 接口联调 |
| 2 | DLL 连接部分：2a 工程骨架与守卫；2b 连接线程与重连；2c 订阅与名单 |
| 3 | 控制台接入：3a 加载、日志器加锁、`WS_CLIPCHILDREN`；3b 页签与占位控件（先嵌空白页验证）；3c 四个时机与 login.do 字段 |
| 4 | 页面内容 |
| 5 | 联调与收尾：真实 chatserver + PC 客户端核对上线、改状态、下线、多设备、顶号、踢人、断网重连、服务端重启；更新开发约定文档与 Admin 控制台接口文档 |

## 11. 进展

- 2026-10-04：方案定稿，开始实施。
- 2026-10-04：第 0~5 期全部完成（均未提交）。
  - 第 0 步：本文档；协议文档第 7.15、8、22 节；`bugs.md` BUG-92、BUG-93。
  - 第 1 期：`chatserversrc/AdminLinkProtocol`、`AdminLinkSession`、`AdminLinkServer`；`ChatSession` 的登录、注销、改状态、置为失效与 `ChatServer::onDisconnected` 五处通知；`login.do` 的 `linkport` / `linkpubkey`；`etc/chatserver.conf` 的 `adminlink*` 三项；两份 `Protocol.h` 的 2501~2504 与 1100~1102。服务端单测 ADL1~ADL10 与 PP 组通过，Windows 全量 1116 条、WSL 独立目录全量构建均通过。
  - 第 2 期：`flamingoAdmin/FlamingoLink/`（`Core/` 纯函数、`Net/` 连接线程、`Ui/` 页面、`LinkModule`），加入 `flamingoAdmin.sln`。
  - 第 3 期：`Util/PluginLibrary`、`Util/LinkBridge`、`Util/LinkPageState`、`Widgets/LinkPageSlot`；企业详情屏第三个子 tab；`RootContainer` 的显示层变化回调；日志器加锁；主窗口 `WS_CLIPCHILDREN`。
  - 第 4 期：页面内容（汇总、连接状态与倒计时、搜索、每台设备一行的表格、断线置灰）。
  - 测试：flamingoAdminTests 的 FLK1~FLK12 全部通过（FLK8 加载 DLL 对本地假服务端）；真实服务端联调：明文一轮 32 项（鉴权、未鉴权超时、越权 1101 / 1102、名单内容、多设备排序、改状态、注销、断开、管理员强制注销、退订、空闲断开）与加密一轮 7 项（握手、服务端重启后自动重连并重订、token 注销后报告会话过期且不再重连）全部通过；外观截图 `snapshots/flamingolink_online_page*.png`。
  - 联调中修正的问题：页面快速失活又激活时 DLL 的「已订阅企业」被提前清零（改为连接线程真正退订时才清零）；企业换走又换回时连接线程不重新订阅（加订阅代次，换企业即要求重订）。
- 2026-10-05：经用户同意在共用桌面上做了主界面核对（隔离服务端 + 三台员工设备 + 控制台副本，
  只用 PostMessage 驱动、按窗口句柄截图，截图 `snapshots/flamingolink_ui_1~7_*.png`）。企业管理员登录后进入「本企业」，
  第三个子 tab「在线用户」显示正常；打开叠层弹窗时 DLL 页面隐藏、关闭后恢复；切回组织架构与退出登录时页面隐藏。核对中发现并修正三处：
  汇总文字被截断（状态文字与搜索框各收窄到 220）；「在线用户」tab 下主体区四周的边距没有底色（企业详情屏补画 `kInk25`）；
  组织树搜索框从边距里露出半截，且关闭叠层弹窗时 `AdminFrame::SetEditsVisible` 会把它重新设为可见（新增
  `EnterpriseDetailScreen::ApplySubTabEditVisibility`，切 tab 与恢复输入框时都按当前 tab 纠正）。FLK13 覆盖了后两处的逻辑。
