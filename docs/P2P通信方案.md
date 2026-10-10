# P2P 通信方案与开发计划

> 作者：balloonwj@qq.com　起草日期：2026-10-03
> 用途：记录 PC 客户端新建 `FlamingoP2P.dll`、在两个在线客户端之间建立点对点链路的需求结论、现状盘点、协议设计、客户端与服务端设计、业务接入方式与分期开发步骤。后续每步开工前据此细化、收尾后回填实际结果。
> 体例沿用 `docs/单聊文件夹传输方案.md`：结论一律标注来源，「据实证」必附文件:行号，「据推测」表示尚未验证。

---

## 1. 背景与目标

现在客户端之间的所有大流量——远程桌面画面、一对一通话的音视频、屏幕共享、单聊文件夹传输、文件共享的传输——都经服务器 TCP 中转。中转服务器承担全部带宽；两个人坐在同一间办公室，数据也要绕服务器一圈；中转只做逐跳加密，服务端能看到明文。

本方案新建工程 `flamingoclient/FlamingoP2P/`，产出 `FlamingoP2P.dll`，提供一项通用能力：在两个在线客户端之间建立一条点对点链路，链路上可以开多条通道；打洞不通时自动回落到服务器中转，业务方看到的接口不变。之后文件夹传输、文件共享、一对一通话、远程桌面、单聊里的屏幕共享都改用它。

预期收益：

- **省服务器带宽**：打通 P2P 的会话不再经服务器转发数据。
- **局域网内提速、降延迟**：同一内网或同一 VPN 内的两端直接互连。
- **端到端加密**：无论走直连还是回落中转，服务端都只能看到密文。

不在本方案范围内：多人场景的网状连接（群里的屏幕共享房间继续走服务端房间中转）、Android 客户端。

---

## 2. 已确定的事项

2026-10-03 与用户逐条确认：

| # | 事项 | 结论 |
| --- | --- | --- |
| 1 | 网络场景 | 局域网 / VPN 内直连与跨公网打洞**都做**，打不通回落中转。imgserver 新开 UDP 地址探测端口 |
| 2 | 建链策略 | **先打洞、超时回落**：双方同时打洞，超时未通即改走中转；路径选定后整个会话不再切换 |
| 3 | UDP 上的可靠传输层 | 引入 **KCP**（`ikcp.c` / `ikcp.h`，MIT 许可），源码放在 `flamingoclient/FlamingoP2P/third-party/kcp/` 随仓库提交（仓库根的 `third-party/` 被 `.gitignore` 忽略，不能放那里） |
| 4 | 首个接入的业务 | **单聊文件夹传输**（第 5 期），其余业务第 6 期逐个接入 |
| 5 | 多人场景 | **只有一对一场景用 P2P**。单聊里的屏幕共享（发起人加一个观看者）可以用；群里的共享房间、房间语音一律继续走服务端房间中转 |
| 6 | 启用开关 | **按企业开关**，接入现有的 `FeatureSwitchManager`，由 flamingoAdmin 后台设置 |
| 7 | 开关默认值 | **默认开启**（新企业与已有企业都是），有合规要求的企业由管理员手动关闭 |
| 8 | 回落中转由谁负责 | **DLL 统一回落**：DLL 自己连 imgserver 的一对一中转（新增角色 11/12），业务方始终只面对一种链路；中转上跑的同样是端到端密文。所有业务接入后，四个旧中转客户端可以逐步删除 |

以下几项是方案自带的设计选择（第 4～9 节），没有单独提问，用户有不同意见时随时调整：

- 整个进程只有一份 P2P 服务实例，由 exe 加载，经宿主接口交给另外两个 DLL；DLL 之间不互相加载。
- 端到端加密的公钥经已加密的 chatserver 连接交换，信任模型与现在相同（信任 chatserver 转交的公钥）。
- v1 只做 IPv4。
- 企业开关只在服务端建会话时检查：P2P 对用户不可见、没有界面入口。登录应答与 1013 的 `features` 对象里会带上 `p2p`，改 P2P 开关时也推 1013（2d 时经用户确认，取代原先「客户端不接收 1013」的设计）：第 3 期客户端可以在 `p2p` 由关变开时重新发 2301，拿到地址探测服务器。后台开关的权限划分沿用文件共享开关（平台管理员与企业管理员都能设置）。

---

## 3. 现状盘点（据实证）

本节记录 2026-10-03 开工前的状态；之后各期的改动见第 14 节。所提代码已改动，只保留文件与符号名。

### 3.1 大流量全部走 TCP 中转

| 业务 | 握手 / 数据 cmd | 承载进程与配对方式 |
| --- | --- | --- |
| 远程桌面 | 1853 / 1854 | imgserver `relaylistenport`（默认 20003），一对一 `RelayHub` |
| 一对一通话 | 1855 / 1856 | 同上 |
| 屏幕共享（含房间语音） | 1859 / 1860 | imgserver 同一端口，一对多 `RelayRoomHubT` |
| 单聊文件夹传输、文件共享 | 1857 / 1858 | fileserver `listenport`（默认 20001），按（会话号, 通道号）配对 |

依据：`flamingoserver/chatserversrc/Protocol.h`，`flamingoserver/imgserversrc/RelayHub.cpp`，`RelayRoomHub.h`，`fileserversrc/FolderRelayHub.h`，`etc/imgserver.conf`。

- 中转参数分别由 1716（远程桌面）、1707（通话）、1726（屏幕共享）、1816（文件夹传输）、2222（文件共享）下发，都带 `relayip` / `relayport` / `token`（`ChatSession.cpp`）。
- 中转服务器没有任何限速，一对一中转与 fileserver 中转转发时也不限制输出缓冲（`RelaySession.cpp`，`FileSession.cpp`）；只有屏幕共享房间对慢观看者有 1 MiB 的缓冲上限（`RoomForwardPolicy.cpp`）。
- 中转不做配对等待超时：先到的一端立即收到报到成功，对端到达之前发来的数据被直接丢弃（`RelaySession.cpp`）。

### 3.2 中转只是逐跳加密

所有中转链路都先做 `cmd_handshake`（1001）三帧握手，帧体用 XChaCha20-Poly1305 secretstream 加密；中转服务端用发送方的密钥解密，再用接收方的密钥重新加密转发（`docs/协议加密方案.md`，`FolderRelayClient.h`）。屏幕共享的 1860 还要求不压缩，因为服务端要在解密后读帧头（`ShareRelayClient.cpp`）。也就是说，当时服务端能看到所有中转内容的明文。

### 3.3 客户端的中转代码是四份复制

`RdRelayClient`、`CallRelayClient`、`ShareRelayClient`、`FolderRelayClient` 没有公共基类，各自的源文件里都注明照前一个写成（`CallRelayClient.h`，`ShareRelayClient.cpp`，`FolderRelayClient.cpp`）。连接、握手、报到、收包线程、关闭这几段几乎逐字相同，合计约 1200 行重复代码。四者的线程模型一致：阻塞 socket，一条连接一个收包线程（500 ms 接收超时用来检查退出标志），发送在调用方线程上、持发送锁完成加密与发送。断开后都不在 socket 层重连。

### 3.4 客户端与服务端都没有 UDP 代码

- 对 `SOCK_DGRAM` / `IPPROTO_UDP` / `sendto` / `recvfrom` / `WSARecvFrom` 的搜索在 `flamingoclient/` 与 `flamingoserver/` 下都没有结果；客户端也没有任何处理 Windows 防火墙的代码（搜索 `INetFwPolicy` / `netsh` / `firewall` 无结果），且没有安装程序。
- 服务端 `Sockets.cpp` 只创建 TCP socket，`InetAddress` 只支持 IPv4（`InetAddress.h`）。
- 但服务端 reactor 的 `Channel` 可以挂任意 socket（`net/Channel.h`），Windows 用 `SelectPoller`、Linux 用 `EPollPoller`，所以加 UDP 不需要改 reactor 本身，只需补少量 UDP 辅助函数。

### 3.5 早先的承诺一直没有兑现

- `docs/音视频通话方案.md` 写明「第一版先纯中转，预留 P2P 接口；第四期经 1707 交换候选地址做打洞，失败回落中转」。第四期没有做：1707 当时只有服务端发给客户端这一个方向，`ChatSession` 没有它的接收分支。`Protocol.h` 与 `协议设计-v2.md` 把 1707 描述成双向的「SDP/ICE 透传」，与实现不符。
- `docs/文件共享方案.md` 当时明确写了「字节通道经 fileserver 中转……不做 P2P」。

### 3.6 现有 DLL 的接口惯例

- FlamingoMedia 与 FlamingoFileShare 都只导出一个工厂函数 `Xxx_Create(apiVersion)`，版本必须完全相同；模块对象提供 `Init(host, params)` / `Shutdown()`（`FlamingoMedia/Api/FlamingoMediaApi.h`，`FlamingoFileShare/FileShareApi.h`）。
- 每个模块各自静态链接 CRT（`/MTd`、`/MT`），各有自己的堆，所以接口只用纯虚类和基本类型，不跨模块传 STL 对象，内存谁分配谁释放（`FlamingoMediaApi.h`）。
- DLL 的日志经宿主写进 exe 的日志文件，格式化代码共用 `FlamingoMedia/Host/LogFormat.cpp`。
- 文件共享的信令通道是更通用的写法：`SendShareCommand` 返回 seq 便于匹配应答，收帧经带互斥的路由器转交 DLL，`Detach()` 之后保证不会再有回调（`Source/Feature/FileShare/ShareChannel.h`）。
- 两个 DLL 都静态链接了 libsodium，但当时都没有调用 `sodium_init`（已修复）。

### 3.7 可参考的现成做法

`references/rustdesk/分析-rustdesk-信令与中转协议.md` 第 6～9 节：信令服务器负责交换双方地址，客户端用 UDP 打洞直连，打通后在 UDP 上跑 KCP 提供可靠有序的字节流，失败则回落到只做字节透传的 TCP 中转；业务层在三种路径上用同一份代码。本方案的整体思路与它一致。

---

## 4. 总体结构

### 4.1 模块划分

```
  业务方：exe（文件夹传输） / FlamingoMedia（通话、远程桌面、单聊屏幕共享） / FlamingoFileShare
                     │  IP2PService / IP2PLink（纯虚接口）
  ┌──────────────────▼──────────────── FlamingoP2P.dll ───────────────────────┐
  │ 链路管理 │ 候选地址收集与连通性检查 │ 端到端加密 │ 通道复用（可靠 / 数据报） │
  │ 一个 IO 线程：UDP 收发 + 回落中转的 TCP 连接 + 定时器                       │
  └────────────────────────────────────────────────────────────────────────────┘
        │ 信令：经 exe 的 chatserver 主连接，2300 段        │ UDP 直连；不通则 TCP 中转
   chatserver：P2P 会话的授权、按企业开关、把信令转给对端那条连接
   imgserver：UDP 地址探测（新增）；一对一中转新增角色 11/12、cmd 1861/1862
```

### 4.2 DLL 形态与加载

- 接口形态照 FlamingoFileShare：导出 `FlamingoP2P_Create(apiVersion)`，模块对象提供 `Init` / `Shutdown`；`Init` 里调用一次 `sodium_init`。
- 宿主接口 `IP2PHost` 提供：写日志、发 2300 段命令（返回 seq）、取服务器公钥、取本人 uid 与 deviceId。
- exe 在启动时加载，排在 FlamingoMedia 之前，以便之后把服务对象交给它；登录成功后由 DLL 向服务端报到（第 5.2 节 2301）。
- 整个进程只有一份实例。另外两个 DLL 通过各自宿主接口新增的 `GetP2PService()` 取得服务对象（产品尚未发布，接口版本号不变，exe 与 DLL 一起编译）；这一步推迟到第 6 期真正接入这两个 DLL 的业务时再做，避免与并行开发中的接口改动冲突。
- 工程文件仿照 `tools/gen_media_vcxproj.py` 用脚本生成，四种配置（Win32 / x64 × Debug / Release）的输出目录与 exe 相同。

### 4.3 连接模型：先有业务会话，再建 P2P 链路

授权仍由业务决定，P2P 层不单独做「谁能连谁」的判断：

1. 业务会话先按原流程建立（通话接通、文件夹传输被对方接收……）。
2. 服务端的业务管理器（如 `FolderTransferManager`）调用新增的 `P2PSessionManager::tryCreate`。双方连接都已报到具备 P2P 能力、且所在企业开启了 P2P 时，创建一个 P2P 会话，并用 2302 分别推给双方**正在进行这次业务的那条连接**（用户可能多端同时登录，所以必须推到具体连接，文件共享的 2222 已有先例）。不满足条件时返回失败，业务照旧走原来的中转。
3. 业务自己的中转参数消息（如 1816）增加可选字段 `p2psessionid`。客户端看到这个字段就等 DLL 交来链路；没有这个字段就照旧连原来的中转。这样新旧客户端可以混用。
4. DLL 收到 2302 后立即开始建链，链路就绪（或回落中转成功）后按「用途 + 业务会话号」交给已注册的业务方。

---

## 5. 协议设计（chatserver 信令）

### 5.1 号段

- cmd 取 **2300 段**（目前空闲：两份 `Protocol.h` 里最大的值是 2230）。
- 错误码取 **900 段**（目前空闲；一致性用例 PP4 要求两份头文件里都有 `//==== <名称> 900-999 ====` 分段注释，见 `tests/test_protocolparity.cpp:16-17`）。
- `RelayToken` 新增角色 **11**（P2P 发起方）、**12**（P2P 应答方），当前最大是 10（`utils/RelayToken.h:81`）。
- 中转新增 **1861**（报到）/ **1862**（数据），沿用 185x 这一组的排列，承载在 imgserver 的一对一 `RelayHub` 上。
- 改动两份 `Protocol.h`、`docs/协议设计-v2.md`，并由一致性用例 PP1～PP4 把关。

### 5.2 cmd 列表（草案，1a 步定稿）

| cmd | 名称 | 方向 | 用途 |
| --- | --- | --- | --- |
| 2301 | `cmd_p2p_register` | 客户端 ⇄ 服务端 | 登录后 DLL 报到，声明 P2P 协议版本与已登记业务方的用途（第 5 期起，登记或注销业务方后重新报到）。应答带本企业是否开启、地址探测服务器地址列表。服务端据此把这条连接标记为具备 P2P 能力，并只为双方都报到了的用途建会话 |
| 2302 | `cmd_p2p_session_notify` | 服务端 → 客户端 | 推送一个新的 P2P 会话：会话号、本方角色、用途、业务会话号、对端 uid 与 deviceId、回落中转参数（地址与令牌）、各项超时 |
| 2303 | `cmd_p2p_signal` | 客户端 → 服务端 → 对端 | 交换候选地址与临时公钥。服务端只核对发送者是该会话成员并检查长度，然后原样转给对端那条连接，不解析内容 |
| 2304 | `cmd_p2p_result` | 客户端 → 服务端 | 上报建链结果：最终路径（局域网直连 / 公网打洞 / 中转）、耗时、NAT 判断结果。只用于日志与统计 |
| 2305 | `cmd_p2p_close` | 双向 | 结束 P2P 会话（业务结束、建链失败或一方断开） |
| 2306 | `cmd_p2p_debug_open` | 客户端 → 服务端 | 仅供联调：不经业务直接与指定用户建一个用途为 `test` 的会话。服务端配置项 `p2pdebugopen=1` 时才受理，默认关闭 |

2302 的 body 示例（字段名在 1a 步定稿）：

```json
{
    "p2psessionid": "p2p-10001-10002-1790990000000-7",
    "role": 1,
    "purpose": "folder",
    "featuresessionid": "10001-10002-1790990000000-3",
    "peeruid": 10002,
    "peerdeviceid": "…",
    "relayip": "…", "relayport": 20003, "token": "…",
    "connecttimeoutms": 3000
}
```

### 5.3 错误码（草案）

会话不存在、不是会话成员、对端已离线、本人同时进行的 P2P 会话过多、信令过长、企业未开启 P2P（只出现在 2306 的应答里；业务路径上企业未开启时服务端不建会话，不返回错误）。具体编号在 1a 步定。

---

## 6. 建链过程

### 6.1 候选地址收集

每条链路新建一个 UDP socket，绑定 0.0.0.0 的随机端口。**不得绑定固定端口**：第 0a 步实测，绑定固定端口会让 Windows 弹出防火墙提示框，绑定随机端口则不会（第 14.2 节）。

- **本机地址**：`GetAdaptersAddresses` 枚举处于连接状态的网卡的 IPv4 地址，排除回环与链路本地地址；明显的虚拟网卡（如 Hyper-V、VMware 虚拟交换机）降低优先级而不是排除，因为有的企业 VPN 也表现为虚拟网卡。
- **公网映射地址**：向 imgserver 的两个地址探测端口各发一个请求，对方回报「看到的来源 IP 与端口」。两次回报的端口不同，说明本方是对称型 NAT。这个判断只用于调整超时与记日志，不据此放弃打洞——对端是锥形 NAT 时仍可能打通。

### 6.2 地址探测服务

imgserver 新增两个 UDP 端口，报文兼容 RFC 5389 的 Binding 请求与应答（只实现这一种消息和 `XOR-MAPPED-ADDRESS` 属性）。选择兼容标准格式，是为了能用现成的工具验证服务端，也便于以后改用其它标准探测服务器。服务端无状态。应答固定 32 字节（20 字节头加 12 字节的 `XOR-MAPPED-ADDRESS`），而最小的 Binding 请求只有 20 字节；为保证应答不大于请求、不会被利用来放大流量，只应答长度不小于 32 字节的请求（2e 时经用户确认），客户端用一个可忽略的属性把请求补足 32 字节。只发 20 字节请求的现成 STUN 工具因此得不到应答，验证服务端要用自己的客户端。另按来源 IP 限频，以控制负载。

### 6.3 连通性检查（打洞）

1. 双方收集完候选地址后，经 2303 把候选地址与 X25519 临时公钥一起发给对方。
2. 收到对方的 2303 后，双方各自推导出会话密钥，按优先级同时向各候选地址对发探测包：同网段的本机地址 > 其它本机地址 > 公网映射地址。双方都先向对方发包，各自的 NAT 与状态防火墙才会放行对方的包，这就是「打洞」。
3. 探测包用会话密钥加密认证，来历不明或认证失败的包一律丢弃。
4. 每个地址对上的探测包要在超时之前**持续重发**（间隔暂定 50～100 毫秒），不能只发一次：第 0a 步实测，Windows 防火墙会静默丢弃对端主动发来的包，只有在本方已向该地址发过包之后，对端的包才能进来。所以先到的探测包可能被对方的防火墙丢弃，要靠对方也向本方发过包之后的重发来打通。
5. 发起方从先连通的地址对里选定一对，发「选定」报文；应答方确认后链路就绪。
6. 发起方在 `connecttimeoutms` 内没有选定任何地址对，就改走中转（第 6.4 节）。

### 6.4 回落中转

DLL 用 2302 里的中转参数连 imgserver 的中转端口：先做 1001 握手（逐跳加密，保护令牌），再发 1861 报到，之后在 1862 里承载与 UDP 路径相同的端到端加密报文。中转服务器只做配对与透传，看到的是密文。

现有一对一中转在对端报到之前会丢弃数据（第 3.1 节），所以中转路径上同样要做「选定 / 确认」的交换，并在收到对方第一个报文之前定时重发，做法与文件共享的 `ResendHelloUntilHeard` 相同。

第 3e 步的实现（见 14.18 节）：打洞失败且 2302 带了中转参数时改走中转；连接、握手与报到合计最多 5 秒，之后发起方每 200 毫秒经中转重发「选定」，应答方收到后回「选定确认」，双方就绪。改走中转起 10 秒内没有完成选定、中转连不上或中转连接断开，都按建链失败处理。对方的 2303 一直没来时没有对方公钥，无法端到端加密，不改走中转。

### 6.5 参数

以下参数都由服务端经 2302 下发，取值在第 0 期实测后再与用户确认：

| 参数 | 暂定值 | 说明 |
| --- | --- | --- |
| 打洞超时 | 3 秒 | 超时后改走中转 |
| 保活间隔 | 5 秒 | 链路空闲时发保活包，同时维持 NAT 映射 |
| 断开判定 | 15 秒 | 这么长时间收不到对方任何报文即判定链路断开 |
| 每人同时进行的 P2P 会话上限 | 16 | 超出时服务端不建会话，业务走原来的中转 |

单个 UDP 报文的负载上限取 1200 字节。依据：QUIC（RFC 9000 第 14 节）把 1200 字节作为任何路径都必须支持的最小值，WebRTC 的实现也普遍取这个量级，可以避开 IP 分片。

---

## 7. 链路报文、加密与通道

### 7.1 报文格式（草案）

```
[类型 1B][保留 1B][会话标签 4B][计数器 8B][密文 … ][认证标签 16B]
```

- 类型：探测、探测应答、选定、确认、数据、保活、关闭。首字节的取值与 STUN 报文不重叠，同一个 socket 上可以直接区分两者。
- 会话标签由会话密钥派生，用于快速丢弃无关报文。
- 数据报文解密后的内容：`[通道号 2B][通道类型 1B][负载]`，负载是 KCP 分段或数据报分片。
- 中转路径上，同样的报文整个放进 1862 的帧体。

### 7.2 加密

- 两端各生成临时 X25519 密钥对，公钥经 2303 交换，用 `crypto_kx` 按角色推导出收、发两个方向的密钥。
- 每个报文用 ChaCha20-Poly1305（IETF）加密认证，nonce 由发送计数器生成；收、发方向各用一把密钥，计数器不会重复。
- 接收端维护重放窗口（暂定 1024 个报文），重复或过旧的报文丢弃。
- 信任模型：信任 chatserver 转交的对方公钥。chatserver 连接本身经服务器身份密钥认证并加密（`docs/协议加密方案.md` 第 4 节），因此网络上的第三方与中转服务器都读不到内容。

### 7.3 通道

一条链路上可以开多条通道，通道号由双方业务约定。通道分两种：

| 类型 | 用途 | UDP 路径上的实现 | 中转路径上的实现 |
| --- | --- | --- | --- |
| 可靠有序 | 文件传输、控制信令 | 每条通道一个 KCP 会话（带本项目的修正与自研拥塞控制，见第 7.4 节） | 直接透传（中转本身是 TCP，已经可靠有序），不经 KCP |
| 数据报 | 音视频 | 超过单报文上限的数据由 DLL 分片，任何一片丢失就整块丢弃，由业务方请求关键帧补救 | 直接透传，不会丢失 |

因为路径选定后不再切换（第 2 节第 2 条），两种路径各用一套实现即可，不需要在会话中途迁移 KCP 状态。

### 7.4 统计与流控

- 链路对外提供统计：当前路径类型、往返时延、丢包率、发送队列积压字节数、近期吞吐。
- `Send` 在发送队列超过上限时返回「稍后重试」，队列降下来后回调 `OnWritable`。文件夹传输自带的 `FolderSendFlowWindow` 可以照常工作。
- 屏幕共享目前用 `send` 的阻塞时长判断拥塞（`Share/ShareBitrateController.h:4-9`），接入 P2P 后可以改用发送队列积压这个信号。
- KCP 的用法按第 0b 步的实测结论（第 14.1 节）确定，原则是既要跑得满带宽，又不能在公网上塞满路由器队列、挤占同一网络里其它连接：
  - **不用 KCP 自带的拥塞控制，也不能关闭拥塞控制。** 前者在有随机丢包的公网上只能用到 5%～9% 的带宽；后者虽能用到 55%～80%，却会让瓶颈队列多出 50～60 毫秒的排队延迟，并浪费 30%～50% 的线路带宽。
  - **经 `ikcp_setcc` 挂入自研的 BBR 类拥塞控制**：按「瓶颈带宽 × 最小往返时延」决定发送速率与在途数据量，不因随机丢包降速。原型在模拟网络里各条件下的带宽利用率为 68%～98%，平均排队延迟多在 6 毫秒以内。
  - **KCP 源码带一处修正**：原版在发送额度不足时，会把分段标记为「已发送」后才跳出循环，导致分段等到超时才真正发出（第 14.1 节有复现数据）。修正后的源码放在 `third-party/kcp/`，附说明文件写明改动，并考虑向上游提交。
  - **往返时延样本取自 `on_rtt` 回调**（按顺序到达时 `on_pkt_acked` 拿不到往返时延）；超时时间在同一回调里放宽到「平均往返时延 + max(4 × 偏差, 平均往返时延的四分之一, 10 毫秒)」，否则平稳路径上会频繁误判超时。
  - 暂定参数：`nodelay=1`、快速重传阈值 2、刷新间隔 2 毫秒（直接写 `kcp->interval`，`ikcp_nodelay` 会把它抬到 10 毫秒）、收发窗口各 4096 个分段、流模式。

### 7.5 保活与断开

链路空闲时每隔保活间隔发一个保活包；超过断开判定时间收不到任何报文，就通知业务方链路断开，并发 2305。v1 不做断开后转中转继续，业务方的处理与现在中转断开时相同（现有各业务都不在 socket 层重连）。

---

## 8. 客户端 DLL 设计

### 8.1 目录结构（草案）

```
flamingoclient/FlamingoP2P/
    FlamingoP2PApi.h          对外接口（exe 与另外两个 DLL 都包含它）
    DllMain.cpp / FlamingoP2P.def / stdafx.h
    Host/                     模块对象、日志桥
    Core/                     报文编解码、加密、重放窗口、通道复用、分片
    Ice/                      候选地址收集、地址探测客户端、连通性检查状态机
    Link/                     信令编解码、一个 P2P 会话的状态机（第 3d 步起）
    Io/                       IO 线程、UDP socket、网卡枚举、回落中转的 TCP 连接
    Chan/                     通道复用、数据报分片、可靠通道、BBR 类拥塞控制（第 4 期起）
    third-party/kcp/          ikcp.c / ikcp.h（带本项目的修正）、许可证与说明文件
    Tests/                    单测源文件（编进 FlamingoUnit，与文件共享的做法相同）
```

`Core/` 与 `Ice/` 里的类只依赖抽象的时钟与收发接口，不直接碰 socket，以便在模拟网络里做确定性测试（第 11 节）。

### 8.2 线程模型

- 整个 DLL 只有一个 IO 线程，所有链路共用。它负责 UDP socket、回落中转的 TCP socket、定时器以及 KCP 的定时驱动。等待机制第 3d 步定为 `select` 加一个绑定 127.0.0.1 随机端口的 UDP 唤醒 socket（投递任务后向它发 1 字节），一次最长等 100 毫秒或到最近的定时器；`P2PEngine.cpp` 在包含 winsock 头文件之前把 `FD_SETSIZE` 调到 1024（见 14.17 节）。
- 业务方可以在任何线程调用接口，调用经队列投递给 IO 线程处理。
- 回调都发生在 IO 线程上。回调里给出的数据指针只在回调期间有效，业务方要拷贝后自行投递，不能在回调里阻塞。
- **发送要成批交给系统。** 第 0b 步实测，Windows 上一包一次 `send` 的单线程上限约 6.3 万包 / 秒（72 MB/s），跑不满千兆局域网；用 UDP 发送分段卸载（`UDP_SEND_MSG_SIZE`）一次交出 8 个等长报文，回环上可到约 220 MB/s。所以 IO 层把一次刷新产生的等长报文攒起来一次发出，系统不支持该选项时退回逐包发送。

### 8.3 对外接口（草案）

```cpp
// 一条通道的接收端，由业务方实现
class IP2PChannelSink
{
public:
    // 收到一块数据：data 只在本次调用期间有效
    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) = 0;
    // 发送队列已降到上限以下，可以继续发送
    virtual void OnWritable(uint16_t channelId) = 0;
};

// 一条已建立的链路，引用计数管理
class IP2PLink
{
public:
    virtual int  OpenChannel(uint16_t channelId, int kind, IP2PChannelSink* sink) = 0;
    // 返回 成功 / 队列已满稍后重试 / 超长 / 链路已断开
    virtual int  Send(uint16_t channelId, const uint8_t* data, uint32_t len) = 0;
    virtual void GetStats(P2PLinkStats* out) = 0;
    virtual void Close(int reason) = 0;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
};

// 业务方按「用途」注册，接收链路的建立结果与断开通知
class IP2PLinkHandler
{
public:
    virtual void OnLinkReady(const char* featureSessionId, IP2PLink* link) = 0;
    virtual void OnLinkFailed(const char* featureSessionId, int code) = 0;
    virtual void OnLinkClosed(const char* featureSessionId, int code) = 0;
};

class IP2PService
{
public:
    virtual bool RegisterHandler(const char* purpose, IP2PLinkHandler* handler) = 0;
    virtual void UnregisterHandler(const char* purpose) = 0;
};
```

正式代码按代码规范补齐每个方法与参数的中文注释。

### 8.4 exe 侧改动

- `Source/Feature/P2P/P2PHost`：实现 `IP2PHost`，负责加载、`Init`、登录后通知、退出时 `Detach` → `Shutdown` → `Unload`，复用 `Source/App/PluginModuleLoader.h`。
- `CIUSocket::SendP2PCommand` 与配套的策略类（只放行 2300 段、非空 body、已连接），照 `SendShareCommand` 的写法。
- `CIUSocket::HandleV2Frame` 在 `switch` 之前把 2300 段转给 P2P 的路由器，照文件共享的做法。
- DLL 若使用 `WM_APP` 私有消息，按开发约定文档第 6 条登记。

---

## 9. 服务端设计

### 9.1 chatserver

- `P2PProtocol`：2301～2306 的编解码（纯函数，便于单测）。
- `P2PSessionManager`（单例）：
  - 记录哪些连接已报到具备 P2P 能力；
  - `tryCreate(purpose, featureSessionId, 双方连接)`：检查双方能力、双方所在企业的开关、每人会话上限，签发两枚 `RelayToken`（角色 11/12），推 2302；
  - 会话表、按连接定向转发 2303、建链超时清理、连接断开时清理并通知对端 2305。
- `ChatSession` 接入 2301～2306 的分发，以及掉线清理。
- 各业务管理器在第 5、6 期接入时调用 `tryCreate`。

### 9.2 imgserver

- `net/` 补少量 UDP 辅助函数（创建、绑定、收发），用现有 `Channel` 挂进 `EventLoop`；Linux 与 Windows 两套源文件清单都要登记。
- 新增 `P2PAddressProbe`：两个 UDP 端口上的 Binding 应答与按来源 IP 限频。
- `RelaySession` 接入 1861/1862，复用 `RelayHub` 的一对一配对，只接受角色 11/12。动这里时顺带用用例验证调查中发现的一个疑点：`onHello` 只判断了 `m_paired`、没判断 `m_inRoom`（`RelaySession.cpp` 的 `onHello`），据推测房间连接还能再加入 `RelayHub`、断开时留下残留表项。（2f 已用联调用例 P2PR5 复现并修正，见第 14.13 节。）

### 9.3 配置项（草案）

| 进程 | 配置项 | 默认值 | 说明 |
| --- | --- | --- | --- |
| imgserver | `p2pprobeport1` / `p2pprobeport2` | 20004 / 20005（暂定） | 两个 UDP 地址探测端口；服务器在 NAT 后面时须配置端口转发 |
| chatserver | `p2pprobehost` | 同 `imgserverhost` | 下发给客户端的地址探测服务器地址 |
| chatserver | `p2pprobeport1` / `p2pprobeport2` | 20004 / 20005（暂定） | 下发给客户端的两个探测端口，须与 imgserver 一致；配 0 表示不下发这个端口（2c-2a 补充） |
| chatserver | `p2pconnecttimeoutms` 等 | 见第 6.5 节 | 经 2302 下发的各项参数 |
| chatserver | `p2pmaxperuser` | 16 | 每人同时进行的 P2P 会话上限 |
| chatserver | `p2pdebugopen` | 0 | 是否受理联调用的 2306 |

### 9.4 按企业开关

- `FeatureSwitchManager` 新增功能名 `p2p`、设置项 `p2p.enabled`，默认开启（第 2 节第 6、7 条）。
- 只在 `P2PSessionManager::tryCreate` 里检查；关闭后已经建立的 P2P 会话不强制断开，新会话改走原来的中转。
- 后台接口沿用文件共享开关的那组读写设置接口。写库之后按设置项分流：`fileshare.enabled` 照旧推 1013 并收尾文件共享的访问会话；`p2p.enabled` 只推 1013。

### 9.5 flamingoAdmin

在现有的功能开关面板（`FeatureSwitchPanel`）里加一行「客户端点对点直连」，平台管理员在企业设置弹窗里设置，企业管理员在平台设置页里设置，与文件共享开关相同。（2g 已实现，见第 14.14 节。）

---

## 10. 业务接入

### 10.1 单聊文件夹传输（第 5 期）

- 服务端：`FolderTransferManager` 在下发 1816 之前调用 `tryCreate`，成功时 1816 带 `p2psessionid`。1817 重试时同样处理。只有两端的 2301 都报到了用途 `folder` 才建会话（5d 联调后补充，见第 14.21 节）。
- 客户端：在 `Source/Feature/FolderTransfer/` 下引入通道传输抽象（暂名 `IFolderLaneTransport`），接口取 `FolderRelayClient` 现有的那组：连接、发应用帧、收应用帧回调、断开回调。
  - 中转实现：包装现有的 `FolderRelayClient`，行为不变。
  - P2P 实现：一条 P2P 链路上开 1 + N 条可靠通道，0 号通道对应控制通道，1～N 号对应数据通道。
  - `FolderSendSession` 与 `FolderRecvLane` 改为只依赖这个抽象。
- 这些文件也编进 FlamingoFileShare.dll，新增代码同样不能包含 exe 专用头文件（守卫用例 FDG1）。
- exe 侧的 P2P 实现直接从 `P2PHost` 取服务对象；FlamingoFileShare 的接入放到第 6 期。

### 10.2 其余业务（第 6 期，顺序届时再定）

| 业务 | 要点 |
| --- | --- |
| 文件共享的传输 | 复用 10.1 的文件夹传输抽象；FlamingoFileShare 宿主接口新增 `GetP2PService()`。第 6 期第一项已做完，见第 14.22 节 |
| 一对一通话 | 在响铃阶段就开始建链，接通时多半已经打通；音视频改走数据报通道，视频重组要能处理整帧丢失并请求关键帧。第 6 期第三项已按这个思路做完，见第 14.24 节 |
| 远程桌面 | 可先走可靠通道原样迁移，再视情况改用数据报通道。第 6 期第二项已按这个思路做完（可靠通道上的帧通道，见第 14.23 节） |
| 单聊屏幕共享 | 只有发起人与一个观看者时用 P2P；群里的共享房间不变。第 6 期第四项已做完（可靠通道整体搬迁，发起人接管座位号改写、关键帧请求与慢观看者丢帧），见第 14.25 节 |

所有业务接入后，评估删除四个旧中转客户端与对应的服务端中转代码。

---

## 11. 测试

框架都是现成的：客户端 Catch2（DLL 源文件编进 FlamingoUnit，与文件共享相同），服务端 doctest。每期动手前先把该期的用例清单交给用户确认。

- **客户端单测**
  - 报文编解码、加解密、重放窗口、会话标签。
  - 候选地址的过滤与优先级排序（网卡列表用假数据）。
  - **模拟网络下的连通性检查**：假时钟加一个可配置的虚拟网络，模拟完全锥形、受限锥形、端口受限锥形、对称型四种 NAT，以及丢包、延迟、乱序，确定性地验证各种组合下能否打通、何时回落中转。
  - 可靠通道在丢包、乱序链路上的数据完整性；数据报分片在丢片时整块丢弃。
  - 通道复用、发送队列上限与 `OnWritable`。
  - DLL 加载、版本不符、信令路由（照 FSD / PML 系列用例）。
- **服务端单测**：`P2PProtocol` 编解码；`P2PSessionManager` 的能力登记、企业开关、会话上限、定向转发、超时与掉线清理；Binding 报文编解码与限频；`RelayHub` 对角色 11/12 的配对。
- **联调（`[.live][p2p]`，隔离环境）**
  - 同一台机器上两个账号走局域网直连。
  - 用 Debug 环境变量强制走中转。
  - 大文件吞吐，并与现有 fileserver 中转对比。
  - 建链过程中一方退出、链路建立后一方断网。
- **人工核对（需要用户配合）**：在两个不同网络（例如一台机器接手机热点、另一台在公司网络）之间验证公网打洞与回落。

---

## 12. 分期计划

按「每一小步改动集中在一两个文件、能单独编译或验证」的粒度推进，每步完成后汇报。

| 期 | 步骤 |
| --- | --- |
| **0 技术验证**（不写产品代码） | 0a 本机绑定 UDP 端口时 Windows 防火墙的行为（是否弹出提示、点「拒绝」后打洞是否仍能成功；会在桌面上弹框，动手前先告知用户）；0b KCP 在回环与模拟丢包、延迟下的吞吐，与现有 TCP 中转对比，据此定 KCP 参数；0c 跨网络的 NAT 类型与打洞成功率（需要一台两个网络都能访问的服务器和用户配合，届时再商量） |
| **1 契约与骨架** | 1a 协议定契约（两份 `Protocol.h`、协议文档、一致性用例、`RelayToken` 角色 11/12）；1b DLL 工程、导出、模块对象、日志桥、`sodium_init`；1c exe 侧加载、`P2PHost`、`SendP2PCommand` 与收帧路由 |
| **2 服务端** | 2a `P2PProtocol`；2b `P2PSessionManager`；2c `ChatSession` 接入与掉线清理；2d 企业开关 `p2p.enabled`；2e `net/` 的 UDP 辅助与 imgserver 地址探测；2f `RelaySession` 接入 1861/1862；2g flamingoAdmin 开关界面 |
| **3 客户端建链** | 3a 报文编解码与加密；3b 候选地址收集与地址探测客户端；3c 连通性检查状态机（模拟网络）；3d IO 线程接真实 UDP；3e 回落中转；3f 同机联调（2306 建会话，局域网直连与强制中转两条路径） |
| **4 通道** | 4a 通道复用与数据报通道；4b 可靠通道（引入带修正的 KCP）；4c BBR 类拥塞控制模块（复用第 0b 步的模拟网络做确定性单测，验收标准取第 14.1 节原型的结果）；4d 统计与发送队列上限；4e IO 层成批发送（UDP 发送分段卸载）；4f 吞吐与丢包联调，与现有中转对比 |
| **5 单聊文件夹传输** | 5a 服务端 `tryCreate` 与 1816 新字段；5b 客户端传输抽象与中转实现（行为不变，回归现有用例）；5c P2P 实现；5d 联调（直连、强制中转、续传、与旧客户端混用） |
| **6 其余业务** | 逐个业务各成一小期，见第 10.2 节。2026-10-04 四项都已做完：文件共享传输（第 14.22 节）、远程桌面（第 14.23 节）、一对一通话（第 14.24 节）、单聊屏幕共享（第 14.25 节）；界面联调 2026-10-04 完成，修了通话挂断与远程桌面画面两处问题（第 14.26 节） |

---

## 13. 主要风险

- **打洞失败**：两端都是对称型 NAT，或者企业网封锁 UDP，就打不通，只能回落中转。这种情况没有收益，但也不比现在差。第 2304 的上报可以统计实际打通率。
- **Windows 防火墙**：第 0a 步实测（第 14.2 节），绑定随机端口不会弹出提示框；即使程序有明确的入站阻止规则，本方先发过包的地址发回的包仍能进来，所以打洞不受防火墙影响。剩余风险是第三方安全软件的行为可能不同，第 0c 步与人工核对时留意。
- **网络切换**：链路建立后切换 Wi-Fi 或网线，P2P 链路会断开。v1 的处理与现在中转断开时相同；以后可以加「断开后转中转继续」，报文格式里预留路径编号即可。
- **拥塞控制是自研的**：第 0b 步实测 KCP 自带的拥塞控制不能用（第 14.1 节），需要自己实现 BBR 类算法。原型已在模拟网络里达标，但模拟没有覆盖与 TCP 等其它连接争用同一瓶颈时的公平性，也没有覆盖真实网络里确认包聚集、无线链路抖动等情况；4c 步补公平性的模拟用例，第 0c 步与 4f 步在真实网络上核对。
- **局域网地址暴露**：交换本机候选地址后，对端能看到本方的局域网 IP。这是局域网直连的必要代价，在企业内一般可以接受；关闭企业开关即可避免。

---

## 14. 回填记录

### 14.1 第 0b 步：KCP 与逐包加密的本机实测（2026-10-03）

探针与脚本（未入库）：`p2p_probe.cpp`（四种模式：`crypto` / `udp` / `tcp` / `sim`，另有 `rawudp` / `rawuso`）、`build.bat`（原版 KCP）、`build_patched.bat` 与 `make_patched_kcp.py`（修正版 KCP）。x64 Release、静态 CRT。KCP 取自 skywind3000/kcp 提交 `b1a7a21`（2026-06-23）。下面的「回环」都是本机 127.0.0.1 上两个线程互发，「模拟」是虚拟时钟下的单向链路模型（瓶颈带宽、尾部丢弃队列、随机丢包、传播时延），不涉及真实网卡。

**1. 逐包加密的速度（单核，加密与解密合计，按明文计）**

| 算法 | 未调用 `sodium_init` | 调用之后 |
| --- | --- | --- |
| ChaCha20-Poly1305（IETF），每包 1170 字节（本方案） | 119.2 MB/s | 216.2 MB/s |
| secretstream XChaCha20-Poly1305，每块 512 KiB（现有中转） | 134.6 MB/s | 310.5 MB/s |

逐包加密不是瓶颈。未初始化时慢一倍多，这就是 DLL 没有调用 `sodium_init` 时的实际影响。

**2. 回环吞吐（500 MB）**

| 测试 | 吞吐 | 发送端 CPU | 接收端 CPU |
| --- | --- | --- | --- |
| 纯 UDP，一包一次 `send`，不经 KCP、不加密 | 72.3 MB/s（6.3 万包 / 秒） | 97% | 60% |
| 纯 UDP，发送分段卸载，一次 8 个包 | 约 220 MB/s | 94%（多为发送缓冲满时的空转） | 38% |
| KCP + 逐包加密，刷新间隔 1 毫秒 | 44～52 MB/s（两次运行） | 89%～90% | 57%～62% |
| KCP + 逐包加密，BBR 类原型 | 43.5 MB/s | 98% | 66% |
| 对照：TCP + 现有 512 KiB 帧加密 | 280.7 MB/s | 98% | 54% |

- 瓶颈是 Windows 上每个 UDP 包一次系统调用的开销，KCP 与加密只占小头。由此得出第 8.2 节「成批发送」的设计。
- 本机 Win10 19045 上 `UDP_SEND_MSG_SIZE` 与 `UDP_RECV_MAX_COALESCED_SIZE` 都能设置成功；接收合并在回环上没有生效（接收端仍一次收一个包），真实网卡上的表现待第 4f 步核对。
- KCP 默认参数（窗口 32、刷新间隔 100 毫秒）只有 0.1 MB/s，必须调参。

**3. 模拟网络下各组参数的带宽利用率与平均排队延迟**

| 网络条件 | KCP 自带拥塞控制（最好一组） | 关闭拥塞控制（最好一组） | BBR 类原型 |
| --- | --- | --- | --- |
| 千兆局域网，RTT 1 毫秒，无丢包，队列 4 毫秒 | 47%，0.3 毫秒 | 83%，1.9 毫秒，溢出丢包 5.6 万 | **98%，1.3 毫秒** |
| 200M Wi-Fi，RTT 5 毫秒，丢包 0.5%，队列 20 毫秒 | 6%，0.2 毫秒 | 79%，11.8 毫秒 | **81%，1.3 毫秒** |
| 100M 宽带，RTT 30 毫秒，无丢包，队列 100 毫秒 | — | — | **91%，27.7 毫秒** |
| 100M 宽带，RTT 30 毫秒，丢包 0.1%，队列 100 毫秒 | 6%，0.4 毫秒 | 66%，60.9 毫秒，线路浪费 45% | **89%，4.0 毫秒** |
| 20M 宽带，RTT 60 毫秒，丢包 1%，队列 100 毫秒 | 9%，0.6 毫秒 | 59%，53.9 毫秒 | **84%，5.4 毫秒** |
| 8M 差网络，RTT 120 毫秒，丢包 5%，队列 100 毫秒 | 5%，1.3 毫秒 | 5%，44.9 毫秒 | **68%，6.3 毫秒** |

- KCP 自带的拥塞控制把慢启动阈值写死为 2（`IKCP_THRESH_INIT`），窗口几乎只能线性增长，遇到随机丢包又减半，在公网上不可用。
- 关闭拥塞控制会持续塞满瓶颈队列，不符合第 7.4 节的原则。
- 无丢包的 100M 条件下，BBR 类原型的排队延迟约等于一个往返时延，这是 BBR 第一版的已知特点；正式实现时参考 BBRv2 的在途数据上限压低。

**4. 原型调试过程中实测确认的 KCP 行为**（正式实现必须照此处理）

- **发送额度判断顺序的缺陷**：固定速率（链路带宽的一半）在无丢包的 100M、RTT 30 毫秒链路上，原版 KCP 出现 7986 次「重传」，修正后为 0；千兆局域网上分别是 1 次和 0 次。修正内容是先判断分段是否需要发送、再检查额度，额度不足时直接跳出、不改分段状态（`make_patched_kcp.py`）。
- **按顺序到达时 `on_pkt_acked` 不带往返时延**：`ikcp_input` 先按累计确认号删分段（往返时延传 -1），逐分段确认时已找不到它们。原型最初从这里取样，在无丢包链路上一直拿不到最小往返时延、卡死在启动阶段；改用 `on_rtt` 后正常。
- **KCP 的超时余量过小**：超时 = 平均往返时延 + max(刷新间隔, 4 × 偏差)，在 RTT 30 毫秒的平稳路径上只多出 2 毫秒，每 200 毫秒误判约 300 次超时；按第 7.4 节放宽后，100M 宽带（丢包 0.1%）的利用率从 22% 升到 89%。
- **`IKCP_FASTACK_CONSERVE` 在该版本里默认已定义**（`ikcp.c` 第 20 行），快速重传只统计在丢失分段之后发出的分段的确认，不需要另外处理。
- **只要 `on_ack` / `on_fast_retransmit` / `on_timeout` 中有一个为空，KCP 仍会对窗口执行自带的增减逻辑**，自研拥塞控制要把三者都显式给出（可以是空实现）。

**5. BBR 类原型的要点**（供 4c 步参考，代码在 `p2p_probe.cpp` 的 `BbrCc`）：送达速率按「一轮内累计确认字节 / 时长」取样，一轮至少 10 毫秒，瓶颈带宽取最近 10 轮最大值；启动阶段 2.885 倍，带宽连续 3 轮增长不足 25% 或一轮内丢失超过 2% 即结束；排空阶段最多 2 轮；带宽探测阶段按 1.25 / 0.75 / 1 × 6 循环；10 秒没有刷新最小往返时延就把在途数据压到 4 个分段持续 200 毫秒；在途数据上限 = 2 × 带宽 × 最小往返时延 + 2 × 带宽 × 刷新间隔。

**对方案的更正**：第 7.4 节改写了 KCP 的用法；第 8.2 节新增成批发送；第 12 节第 4 期由 4 步拆成 6 步（新增 4c 拥塞控制模块、4e 成批发送）；第 13 节更新了拥塞控制的风险说明。

### 14.2 第 0a 步：Windows 防火墙对 UDP 的处理（2026-10-03）

探针（未入库）：`fw_probe.cpp`（Windows 端，`listen` 只收不发、`punch` 每 100 毫秒向对端发包并接收、`hold` 只绑定不收）、`peer.py`（在 WSL2 里运行，每 50 毫秒向 Windows 发包并接收）。本机 Win10 19045，三种网络类型的防火墙都开启、「程序监听时提示」打开；WLAN 属于专用网络。本机内部的回环流量不经过防火墙，所以用 WSL2 扮演对端：WSL2 发给 Windows 的包从 `vEthernet (WSL)` 网卡进来，这块网卡属于公用网络（下面第二轮的结果可以印证）。防火墙提示框是否出现由用户在桌面上观察后告知。

**1. 防火墙对入站包的处理**（Windows 端绑定 0.0.0.0:45123，WSL 端每轮发约 100 个包）

| 情形 | 第一轮：首次运行，提示框弹出后未做选择，没有任何规则 | 第二轮：已有该程序的公用网络入站阻止规则 |
| --- | --- | --- |
| 只收不发：对端主动发来 | 收到 0 / 100 | 收到 0 / 100 |
| 双方同时互发：本方也向对端发包 | 收到 103 / 103 | 收到 103 / 103 |

- 用户在第一轮之后点了「允许访问」。提示框默认只勾选「专用网络」，系统随之生成 4 条入站规则：专用网络的 TCP 与 UDP 为允许，公用网络的 TCP 与 UDP 为**阻止**（`netsh advfirewall firewall show rule name=fw_probe`）。WSL 的流量走公用网络，因此第二轮测到的正是「明确阻止」的情况。
- 结论：对端单方面发来的包一律被拦；只要本方已向某个地址发过包，从该地址发回的包就能进来，即使程序有明确的入站阻止规则也一样。打洞本来就是双方先互发，所以不受防火墙影响；但先到达的探测包可能被丢弃，探测包必须持续重发（第 6.3 节第 4 条）。

**2. 什么情况下会弹出提示框**（每次换一个新的程序名，保证没有既有规则）

| 程序 | 绑定方式 | 期间的入站包 | 是否弹框 |
| --- | --- | --- | --- |
| `fw_probe` | 0.0.0.0 的固定端口 45123 | 有，被拦 | 弹出 |
| `fw_probe_a2` | 0.0.0.0 的随机端口 | 没有 | 未弹出 |
| `fw_probe_a3` | 0.0.0.0 的随机端口 | 有，WSL 主动发了 100 个，全部被拦 | 未弹出 |

- 结论：绑定随机端口不会触发提示框，即使有被拦下的入站包也一样，所以产品**不得绑定固定 UDP 端口**（第 6.1 节）。
- 中途另测过 `fw_probe_any`（0.0.0.0 随机端口）与 `fw_probe_ip`（192.168.10.2 随机端口），用户当时没有留意是否弹框；事后查询两者都没有生成规则，与「随机端口不弹框」一致，但不作为证据。
- 第 1 条里固定端口那次既有固定端口、又有入站包，没有单独区分是哪一项触发了提示；对产品来说，只要用随机端口就不需要区分。

**3. 留在本机的痕迹**：上述 `fw_probe` 的 4 条规则仍在系统防火墙里，只作用于当时的测试程序 `fw_probe.exe`（它所在的测试目录已删除）。删除需要管理员权限：`netsh advfirewall firewall delete rule name=fw_probe`。

**对方案的更正**：第 6.1 节写明不得绑定固定端口；第 6.3 节新增「探测包持续重发」；第 13 节更新了防火墙的风险说明。

### 14.3 第 0c 步：本机网络的 NAT 类型与对公网主机的打洞（2026-10-03）

探针（未入库）：服务器端 `p2p0c_server.py`（Python 3.6 起，只用标准库）、本机端 `p2p0c_client.cpp`，以及经 SSH 部署和启动的 `deploy.sh` / `start_server.sh` / `rsh.sh`（密码只经环境变量 `P2P_PW` 传入，不落盘）。服务器是用户提供的阿里云 ECS（CentOS 8，公网 IP 一对一映射到实例，系统防火墙未开启；实例上另有 RustDesk 的 `sfe-hbbs` 占用 UDP 21116，测试未触碰），测试脚本放在服务器的 `/root/p2p_0c/`，每次运行 60 秒后自动退出。服务器端开三个 UDP 端口：34780（回报来源地址、登记）、34781（第二个回报端口）、34790（扮演对端，登记后先静默 2 秒、再每 50 毫秒发一个打洞包）。

**结果**（本机在家用宽带的 WLAN 后面，本地端口 54429；数据取自本机探针输出与服务器日志 `run1.log`）

| 项目 | 结果 | 结论 |
| --- | --- | --- |
| 端口 34780 看到的映射 | 183.192.103.49:16725 | NAT 不保留本地端口 |
| 端口 34781 看到的映射 | 同上；发往 34790 时服务器看到的也是同一个映射 | **映射与目的地址无关**，不是对称型 NAT |
| 请服务器从本机从未联系过的 34781 回包 | 服务器发了 3 次，本机一次都没收到 | **过滤与目的地址和端口都相关**（端口受限锥形 NAT） |
| 服务器静默的 2 秒内 | 34790 收到本机主动发来的 32 个包 | 阿里云安全组对这些 UDP 端口放行未经请求的入站包（安全组的具体配置没有查看） |
| 打洞 | 服务器开始发包后 18 毫秒内双方互通，本机在登记后 2005 毫秒收到首个对端包 | 打通 |
| 打通后的往返时延 | 20 / 20，16.4～17.9 毫秒 | |

**局限与遗留**

- 这次只测了「一端在 NAT 后面、另一端是公网主机」，服务器这一侧没有过滤，打通是预期之中的。**两端都在 NAT 后面**（尤其是手机热点等运营商级 NAT，常见对称型映射）的情况没有测到，需要第二个网络环境，留到第 3f 步之后用产品代码与用户一起做人工核对。
- 同一个 NAT 后面两台机器经公网映射地址互通（NAT 回流）没有测。
- KCP 与 BBR 类原型在真实公网上的吞吐没有测（需要把探针移植到 Linux），留到第 4f 步用产品代码做。
- 服务器的 `/root/p2p_0c/` 保留，供第 4f 步复用。

**对方案的影响**：家用宽带最常见的端口受限锥形 NAT 对打洞友好，方案第 6 节的设计不需要修改；「映射与目的地址无关」也说明经地址探测服务拿到的公网映射地址可以直接给对端用。

### 14.4 第 1 期 1a：协议定契约（2026-10-03）

**改动**：两份 `Protocol.h`（服务端 `chatserversrc/`、客户端 `Source/net/`）同步加入 cmd 1861 / 1862、2300 段 2301～2306、错误码 900 段 900～907、业务枚举 `p2p_role` / `p2p_path` / `p2p_close_reason`，并更新两处号段说明；`utils/RelayToken.h` 加角色 11 `role_p2p_controlling`、12 `role_p2p_controlled`；`docs/协议设计-v2.md` 新增 7.13、9.12、第 18 节，补 7.9、第 8 节与 8.1 节号段表。常量由脚本按锚点插入，三份文件都保持 UTF-8 BOM 与 LF。用例按 2026-10-03 与用户确认的清单：PP5 的守护清单加入三个 P2P 枚举，新增 RT10。

**测试**（服务端在独立目录编译 `Tests_p2p.exe` 并运行，避开其它会话的编译目录与 SQLite 临时库）

| 阶段 | 结果 |
| --- | --- |
| 基线（16:45 的源码，改动前） | 排除 `NM*` 后 865 例，1 例失败：PP3，客户端协议头有 `cmd_set_starred = 1216` 而服务端没有，属于并行进行的消息提醒模式项目改到一半的状态 |
| 测试先行 | 加回 RT10 与 PP5 清单后编译失败：`role_p2p_controlling` / `role_p2p_controlled` 不是 `RelayToken` 的成员 |
| 加常量后 `PP*,RT*` | 15 例全部通过 |
| 加常量后全量（排除 `NM*`） | 856 例全部通过；比基线少的 14 例是并行会话删掉的 STM1～7、STP1～7，多出的是 RT10 与并行会话新增的 SSRM1、1b、1c、2，与本步改动无关 |
| 客户端协议头 | 单独编译一个只包含它的文件（x64 与 x86，`/W4 /WX`）通过，`static_assert` 核对了新常量的取值；新名字在客户端、服务端与 balloonui 里此前都不存在。FlamingoUnit（Win32 Debug，独立目录）首次全量编译时，只有 `ConversationsProtocolTests.cpp` 报 9 处 C2039（消息提醒模式项目正把 `m_mute` 改名为 `m_notifyMode`，编译时恰好读到改了一半的源码）；对方 17:36 改完后增量重编，完整编译并链接通过（`FlamingoUnit_p2p.exe`，17:53） |

**事后发现并修复的遗漏**：1a 只在 Windows（MSVC）上验证过。品牌名称会话随后在 WSL 下用 g++ 链接服务端测试时报 `undefined reference to RelayToken::role_p2p_controlling / role_p2p_controlled`：RT10 的 `CHECK` 按引用捕获这两个类内静态常量，构成 ODR 使用，g++ 要求类外定义，MSVC 不要求。已在 `utils/RelayToken.cpp` 补上两行定义（该处注释改为「新增角色时这里也要加一行」）；用 g++ `-std=c++0x` 单独编译 `test_main` + `test_relaytoken` + `RelayToken` + `MD5` 并运行 `RT*`，10 例全部通过。以后改服务端代码，Windows 与 WSL 两条构建路径都要验证。

**基线中排除 `NM*` 的原因**：16:45 的源码里，`test_notifymode.cpp` 的 NMP7 失败，紧随其后的 NMP8 不结束、进程内存持续上涨到 35 GB 以上，测试被迫中止。该文件属于并行进行的消息提醒模式项目；据该项目的会话告知，原因是 `MessageManager::getNotifyModes` 的查询循环漏了 `nextRow()`，16:50 左右已修。加常量后（17:19 编译）补跑 `NM*`，18 例全部通过，所以当前服务端全量为 874 例全部通过。

### 14.5 第 1 期 1b：DLL 工程骨架（2026-10-03）

**改动**：

- 新建 `flamingoclient/FlamingoP2P/`：接口头 `FlamingoP2PApi.h`（第 1 版：`IP2PHost`、`IP2PModule`、`IP2PService`、`IP2PLinkHandler`，`IP2PLink` 只做前置声明）、`DllMain.cpp`、`FlamingoP2P.def`、`stdafx.h`（不含 ATL / WTL），`Host/` 下的模块对象 `CP2PModule`、业务方登记表 `CP2PService`、日志出口 `P2PLog` 与日志桥 `HostLogBridge.cpp`（复用 `FlamingoMedia/Host/LogFormat.cpp`，前缀 `[p2p] `）。
- `Init` 里调用 `sodium_init`（FlamingoMedia 与 FlamingoFileShare 曾经都没有调用它），初始化日志报告其返回值；`OnServerFrame` 只做号段与包体检查并记日志，交给 IO 线程处理在第 3 期接入。
- 工程文件由新脚本 `flamingoclient/tools/gen_p2p_vcxproj.py` 生成；加入 `FlamingoClient.sln`，`gen_vcxproj_filters.py` 的清单加了一行；FlamingoUnit 引用该工程并在编译后把 DLL 复制到测试目录（`CopyFlamingoP2PDll`）；`.gitignore` 忽略新工程的四个中间目录与输出的 `FlamingoP2P.dll`；开发约定文档的工程表加了一行。
- 共用文件（FlamingoUnit 工程、解决方案、filters 脚本）改动前后都与并行的消息提醒模式会话打过招呼，用脚本按字节插入、插完用 XML 解析校验。

**测试**（FlamingoUnit 在独立目录编译为 `FlamingoUnit_p2p.exe`）

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 登记两个测试文件后编译运行 `[p2pdll]`：8 例全部失败（P2PD1～6 找不到 DLL，P2PG1～2 找不到工程文件） |
| 实现之后 | `[p2pdll]` 8 例全部通过，132 个断言 |
| 四种配置 | FlamingoP2P 的 Debug / Release × Win32 / x64 全部编译通过，0 错误 0 警告 |
| filters | `gen_vcxproj_filters.py --check` 五个工程全部一致 |

**过程中的两点说明**：其一，实现之后那次 FlamingoUnit 重编时，文件共享会话正在给宿主接口加头像方法，`FileShareModuleTests.cpp` 暂时编不过，编译没有走到复制 DLL 那一步；因为 18:03 编出的测试程序已经包含全部 P2P 用例、用例代码之后未改，所以手动把 DLL 复制到测试目录后直接重跑，结果即上表。其二，之前一次超时被转到后台的编译留下了一个孤儿 `cl.exe`（输出指向上述独立目录），占住了别的会话要改的文件，已结束。

### 14.6 第 1 期 1c：exe 侧加载与信令收发（2026-10-03）

**改动**：

- 新增 `Source/Feature/P2P/`：`P2PChannel`（发送放行规则 `P2PChannelPolicy` 与收帧路由 `CP2PFrameRouter`，照文件共享的 `ShareChannel`）、`P2PHost`（宿主 `CP2PHost`：加载、初始化、登录与退出通知、关闭与卸载、收帧转交；发命令与取公钥经注入的 `IP2PNetwork`，因此不依赖 `CIUSocket`、能编进 FlamingoUnit 用真实的 DLL 测试）、`P2PSocketNetwork`（`IP2PNetwork` 的 exe 实现，转给 `CIUSocket`，只编进 exe）。
- `CIUSocket` 增加 `SendP2PCommand`（按 `P2PChannelPolicy` 校验），`HandleV2Frame` 在 `switch` 之前把 2300 段的帧原样交给 `CP2PHost`。
- `CAppShell` 在启动时 `LoadP2PModule`（排在 FlamingoMedia 之前），退出时在文件共享模块之后最后一个 `Shutdown`；`MainPanelSessionController` 在登录成功与退出登录时通知 `CP2PHost`。
- `Flamingo.vcxproj` 登记三组新文件、接口头与一个不链接的 FlamingoP2P 工程引用；FlamingoUnit 登记两个测试文件与 `P2PChannel.cpp`、`P2PHost.cpp`；filters 重新生成。改共用工程文件前后都知会了并行的消息提醒模式与文件共享会话。

**测试**（FlamingoUnit 在独立目录编译；exe 用隔离配置编为 `Flamingo_p2p.exe`，不碰 `Flamingo.exe`）

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 先写头文件与一律返回失败的占位实现（让工程能编过，免得卡住并行会话），`[p2phost]` 4 例全部失败（28 个断言中 10 个失败） |
| 实现之后 | `[p2phost]` 4 例与 `[p2pdll]` 8 例全部通过，共 202 个断言 |
| 相邻用例 | `[pluginloader]`、`[fileshare-channel]`、`[msgid]` 共 13 例全部通过 |
| exe | 完整编译、链接成功（第一次链接时失败于别的会话尚未写完的消息记录窗与群文件搜索的 5 个符号，与 P2P 无关；半小时后重编通过） |

**过程中的问题与处理**：

- 第一版 P2PC2 的「解除挂接会等转交结束」一节，等送帧线程进入模块时没有超时；占位实现下路由不调模块，测试进程卡了 17 分钟。已改为最多等 2 秒、超时直接判失败，并给测试运行加了 180 秒的整体超时。
- 隔离目录里一个 17:36 编出的旧 obj（`GroupChatWindowLayoutTests.obj`）没有随屏幕共享会话 19:04 改过的头文件重编，链接报找不到新签名的函数；只删掉自己目录里的这个 obj 后重编解决。

**真实 exe 的启动与退出核对**（经用户同意，2026-10-03 20:17 在当时的运行目录下启动 `Flamingo_p2p.exe` 一次，停在登录窗、未登录）：

- 20:17:06.136 DLL 写出 `FlamingoP2P initialized, api version: 1, libsodium ready (sodium_init returned 0)`，宿主写出 `p2p module loaded`；20:17:06.157 才是 `media module loaded`，加载顺序符合设计。
- 向进程的顶层窗口投 `FMG_MSG_EXIT_FOR_UPDATE` 后，20:17:14 依次为 `FlamingoMedia shutdown` → `media module unloaded` → `FlamingoP2P shutting down` → `p2p module unloaded`，卸载顺序符合设计。
- 之后进程没有退出：cdb 非侵入式附加看到主线程在 `~CAppShell → ~CBalloonMainPanel → ~CCreateGroupDlg` 里析构一个 `std::map` 时崩溃，随后崩溃处理函数写日志与日志线程互相等待而挂住（已记为 `bugs.md` BUG-76）。崩溃原因是隔离编译期间别的会话在 20:09:10 改了 `CreateGroupDlg.h/.cpp`，而 `AppShell.obj` 是 20:07:40 按旧布局编出的，与新编的 `CreateGroupDlg.obj` 布局不一致；与 P2P 无关，P2P 模块在崩溃之前已正常卸载。挂住的测试进程已结束。

### 14.7 第 2 期 2a：协议编解码 P2PProtocol（2026-10-03）

**改动**：新增 `flamingoserver/chatserversrc/P2PProtocol.h/.cpp`（纯逻辑，只依赖 jsoncpp：解析 2301 / 2303 / 2304 / 2305 / 2306，构造 2301 / 2306 应答与 2302 / 2303 转发 / 2305 推送）与 `tests/test_p2pprotocol.cpp`（Suite P2PP，6 例）；登记进 `CMakeLists.txt`（chatserver 源文件与测试清单）、`chatserver.vcxproj`、`Tests.vcxproj` 及两份 filters；新文件为 UTF-8 BOM + LF。几个上限：会话号 96 字节、2303 的 data 序列化后 4096 字节（超过回 905）、2304 的 pair 128 字节与 nat 512 字节，依据写在头文件注释里。`docs/协议设计-v2.md` 第 18 节随之更正：`peeruid` / `fromuid` 按 v2 惯例写成十进制字符串，各应答带 `code` 与 `msg`，2304 失败时不带 `path`。

**测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 占位实现下用 g++ 单独编译 `test_main` + `test_p2pprotocol` + `P2PProtocol` + jsoncpp：6 例全部失败 |
| 实现之后（g++ 单独编译） | 6 例全部通过，131 个断言，`-Wall` 无警告 |
| Windows 全量 | `Tests_p2p.exe` 903 例全部通过 |
| WSL 全量 | g++ 在 WSL 的临时构建目录里构建默认目标（含 chatserver）成功，`flamingo_tests` 903 例全部通过 |

两次全量编译期间都按其它会话的请求协调过：多语言会话构建时只写代码、不编译；聊天记录管理器会话请求暂停时，本步的编译已结束。

### 14.8 第 2 期 2b：会话管理 P2PSessionManager（2026-10-03）

**改动**：新增 `chatserversrc/P2PSessionManager.h/.cpp`（纯逻辑，时间由调用方传入；能力登记、建会话与签令牌、信令路由、成员核对、结束与断线收尾、联调建会话）与 `tests/test_p2psessionmanager.cpp`（Suite P2PS，6 例）；登记进与 2a 相同的四份清单和两份 filters。企业开关由调用方查好后作为参数传入，本类不依赖 `FeatureSwitchManager`。连接用不透明整数（会话对象地址）标识，与文件共享的 `ShareConnKey` 做法相同。类内静态常量在 .cpp 里补了类外定义（吸取 1a 的教训）。

**测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 占位实现下用 g++ 单独编译运行：6 例全部失败 |
| 实现之后（g++ 单独编译） | 6 例全部通过，131 个断言，`-Wall` 无警告 |
| Windows 全量 | 909 例全部通过 |
| WSL 全量 | g++ 构建默认目标成功，909 例全部通过 |

### 14.9 第 2 期 2c-1：信令处理 P2PSignalService（2026-10-03）

2c 拆成两步，中间插入 2d：2c-1 纯逻辑 → 2d 企业开关 → 2c-2 接入 `ChatSession` 与端到端联调。

**改动**：新增 `chatserversrc/P2PSignalService.h/.cpp`（纯逻辑：一帧 2300 段请求进来，产出要发出的若干帧 `P2POutFrame`；连接断开时产出给各对端的 2305）与 `tests/test_p2psignalservice.cpp`（Suite P2PSV，6 例）；登记进与 2a 相同的四份清单和两份 filters。企业开关经接口 `IP2PEnterpriseGate` 查询，测试里注入假的实现。几条行为约定：

- 2301 无论企业开关如何都登记连接（开关可能在运行中打开），只有开启时应答才带 `enabled=1` 与地址探测服务器。
- 2303 转给对端（seq 为 0），同时回请求者一帧应答（seq 原样带回）；2305 把请求里的原因与错误码原样推给对端。
- 2306 先回应答，再给双方各推一份 2302，客户端先拿到会话号，随后的推送就能对上。
- 2302 只由服务端推送，客户端发来 2302 或 2300 段其余未定义的命令时回报文非法，不断开连接。
- 连接断开时给每个仍在线的对端推 2305，原因 `p2p_close_peer_offline`、错误码 903。

**测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 占位实现下用 g++ 单独编译运行：6 例全部失败 |
| 实现之后（g++ 单独编译） | 6 例全部通过，196 个断言 |
| Windows 全量 | 915 例全部通过 |
| WSL 全量 | g++ 构建默认目标成功，915 例全部通过 |

### 14.10 第 2 期 2d：企业开关 p2p.enabled（2026-10-03）

**改动**：

- `FeatureSwitchManager` 的功能表加入 `p2p` / `p2p.enabled`（默认开启），每一项增加中文显示名；新增 `featureOfSetting`（设置项名对应的功能名）与 `describeSettingChange`（管理员操作日志的说明文字，如「关闭客户端点对点直连」）。
- 用户确认：登录应答与 1013 的 `features` 对象带上 `p2p`，改 P2P 开关也推 1013（第 2 节的说明已随之修改）。
- 修正后台改设置的分流：`fileshareadmin::SetSetting` 原先不看设置项、一律调用 `ChatSession::applyFileShareSwitch`，加入 `p2p.enabled` 之后会把关闭 P2P 当作关闭文件共享处理、结束该企业全部的文件共享访问会话。现在按功能名分流：文件共享照旧；P2P 只调用新抽出的 `ChatSession::pushFeaturesNotify` 推 1013。`HttpSession::onAdminSettingSet` 写管理员操作日志时原先固定写「打开 / 关闭文件共享」，改为由 `describeSettingChange` 生成。
- 「用户 → 企业 → 开关」的查询适配器（实现 `IP2PEnterpriseGate`）属于接线，放到 2c-2。
- 同步修改：`AdminApiCodes.h`、`HttpSession.h`、`MysqlManager.cpp`、`table_mysql.sql` 里「本期只有 fileshare.enabled」的注释（重跑 `tools/gen_table_sqlite.py`，生成的两份文件逐字节不变）；`协议设计-v2.md` 16.6 节；`Admin控制台接口文档.md` 12.1、12.2 节。

**测试**（Suite FSW 新增 3 例）：FSW6 P2P 开关的内存表（取值校验、默认开启、与文件共享开关互不影响、features 对象与读设置接口带两项），FSW7 P2P 开关读写数据库，FSW8 设置项对应的功能名与操作日志文字。

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 常量与两个函数为占位实现时，FSW6～8 全部失败（18 个断言不通过），FSW1～5 通过 |
| 实现之后 | FSW1～8 全部通过，97 个断言 |
| Windows chatserver | 用隔离的编译配置编出 `chatserver_p2p.exe`，无编译警告（中途应消息提醒会话的请求暂停过一次构建） |
| Windows 全量 | 918 例全部通过 |
| WSL 全量 | g++ 构建默认目标（含 chatserver）成功，918 例全部通过 |

后台入口按功能名分流的那几行要经 `ChatServer` 推送，单元测试覆盖不到，留到 2c-2 端到端联调时用后台接口关闭 P2P 来验证。

### 14.11 第 2 期 2c-2：接入 ChatSession 与端到端联调（2026-10-03）

分三小步：2c-2a 配置读取与开关查询（纯逻辑）、2c-2b 接入 `ChatSession` 与 `main.cpp`、2c-2c 隔离环境里的端到端联调。

**2c-2a 改动**：新增 `chatserversrc/P2PServerSetup.h/.cpp` 与 `tests/test_p2pserversetup.cpp`（Suite P2PCF，4 例），登记进四份清单和两份 filters。

- `P2PServerSetup::loadConfig` 从 `chatserver.conf` 生成会话管理器配置。回落中转沿用 `relaytokenkey` / `imgserverhost` / `relayport`；`p2pprobehost` 不配时取 `imgserverhost`；chatserver 也要配 `p2pprobeport1` / `p2pprobeport2`（默认 20004 / 20005，须与 imgserver 一致，配 0 表示不下发这个端口），第 9.3 节原先只给 imgserver 列了这两项；三个时间、会话上限、`p2pdebugopen` 见第 9.3 节。数值项为非数字、小于等于 0、端口超过 65535 时回落默认值；探测主机为空时不下发探测地址。
- `P2PFeatureGate` 实现 `IP2PEnterpriseGate`：「用户 → 企业」的查询以函数指针注入（chatserver 里接 `DepartmentManager::getUserEnterpriseId`），再查该企业的 `p2p` 开关。

**2c-2a 测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 占位实现下 P2PCF1～4 全部失败（13 个断言不通过） |
| 实现之后 | 4 例全部通过，56 个断言 |
| Windows chatserver | 隔离编译通过，无编译警告 |
| Windows 全量 | 922 例全部通过 |
| WSL 全量 | g++ 构建默认目标（含 chatserver）成功，922 例全部通过 |

**2c-2b 改动**（接线，不新增文件）：

- `ChatSession`：已登录分支分发 2301～2306（2302 只由服务端推送，客户端发来时交给信令处理按报文非法应答）；`onP2PCommand` 组装请求上下文，用 `P2PFeatureGate` 加 `DepartmentManager::getUserEnterpriseId` 查企业开关，交给 `P2PSignalService`；`sendP2PFrames` 把产出的帧发出，发给本连接的直接发送，发给对端的按 `p2pConnKey`（会话对象地址）找到会话再发送，找不到时记日志丢弃；`cleanupP2POnOffline` 在 `onLogout`（客户端可以只退出登录、不断开连接）与 `ChatServer` 的断线收尾两处调用，重复调用没有副作用。P2P 用毫秒级时间，不复用按秒取整的 `shareNowMs`。
- `ChatSession.h` 只前置声明 `P2POutFrame`、连接标识写成 `uint64_t`，不包含 P2P 的头文件：`P2PProtocol.h` 包含了 `Protocol.h`，而 `HttpSession.cpp` 经 `ChatServer.h` 包含 `ChatSession.h`，它用的是 `AdminApiCodes.h` 里同名、取值不同的 `error_code`。
- `main.cpp` 在屏幕共享配置之后用 `P2PServerSetup::loadConfig` 初始化 `P2PSessionManager`，并记一条包含全部取值的日志；仓库里的 `etc/chatserver.conf` 补上带注释的 P2P 配置项（取值都是默认值）。

**2c-2b 测试**：Windows chatserver 隔离编译无警告；Windows 全量 922 例全部通过；WSL g++ 构建默认目标成功、922 例全部通过。

**2c-2c 端到端联调**：

- 环境：隔离的联调服务端（由脚本建环境、起服务端），端口 29900～29903、HTTP 29945、监控 29989；chatserver 用本期的 `chatserver_p2p.exe`，配置追加 `p2pdebugopen=1`、探测端口 29904 / 29905 与非默认的三个时间（2500 / 4000 / 20000），以便区分「按配置下发」和「用了默认值」。数据库是性能测试库的在线备份，只在副本里给平台管理员重设了已知密码，供用例调后台接口。启动日志 `p2p config: relay=127.0.0.1:29903, probes=2 (first 127.0.0.1:29904), connecttimeoutms=2500, keepalivems=4000, linktimeoutms=20000, maxperuser=8, debugopen=1` 与配置一致。
- 用例：`flamingoclient/Source/Tests/P2PSignalLiveTests.cpp`（Suite P2PL，标签 `[.live][p2plive]`，7 例），由 `LiveChatClient` 登录 MY0127 与 MY0308 扮演两端；P2PL7 用 Winsock 直接调后台的 `/admin/login.do` 与 `/admin/setting/set.do`，用例结束时（包括中途失败）由析构对象把开关恢复为开启。新文件先经注入用的 props 在隔离构建里编译通过，再登记进 `FlamingoUnit.vcxproj`，filters 由 `gen_vcxproj_filters.py` 重新生成。
- 运行：由脚本跑用例，再核对服务端日志与管理员操作日志。

| 核对项 | 结果 |
| --- | --- |
| P2PL1～7 | 全部通过，211 个断言 |
| 服务端日志 | 有信令分发、转发给对端、下线收尾、两次 1013 推送；没有任何 `file share switch` 记录，说明改 P2P 开关没有触发文件共享的收尾（2d 的分流在真实环境里生效） |
| 管理员操作日志 | `setting.set` 的说明文字为「关闭客户端点对点直连」「打开客户端点对点直连」 |
| 被丢弃的帧 | 2 帧，都是用例结束时 A、B 同时断开、下线收尾给对端推的 2305：接收方的会话已从会话表移除，没有接收方可发，属于正常情况；用例正文里的 2302 / 2303 没有丢弃。核对脚本最初把「有丢弃」判为失败，查明原因后改为只允许这一种 |

联调过程中的两次构建受阻都与本期改动无关：别的会话的 `AvatarAntialiasTests.cpp` 用 `far` 作变量名（`windows.h` 的宏），由其负责会话修复；隔离目录里的 `ContactModel.obj` 编在源文件被别的会话修改之前的同一分钟，增量构建没有重编，删掉这个 obj 后链接通过。

2c 至此完成。下一步是 2e（`net/` 的 UDP 辅助与 imgserver 地址探测）。

### 14.12 第 2 期 2e：UDP 辅助与 imgserver 地址探测（2026-10-04）

分三小步：2e-1 `net/` 的 UDP 辅助函数、2e-2 地址探测的纯逻辑、2e-3 imgserver 接入与联调。动手前经用户确认「严格不放大」：应答固定 32 字节，只应答长度不小于 32 字节的请求（6.2 节已改写）。

**2e-1 改动**：`net/Sockets.h/.cpp` 新增 `createUdpNonblocking`（Windows 上关闭 `SIO_UDP_CONNRESET`，失败返回 `INVALID_SOCKET` 而不终止进程）、`bindUdp`、`recvFrom`（报文长于缓冲时两个平台都截断到缓冲大小：Windows 的 `WSAEMSGSIZE` 按截断处理）、`sendTo`、`lastErrorIsWouldBlock`；`tests/test_udpsockets.cpp`（Suite UDP，3 例，只用本机回环）。测试里一个变量起初叫 `small`，与 Windows `rpcndr.h` 的同名宏冲突，已改名。

**2e-2 改动**：新增 `imgserversrc/P2PProbeCore.h/.cpp`（登记进 imgserver 与测试的清单）。`P2PStunCodec` 判断并解析 Binding 请求（不足 32 字节、首字节最高两位不为 0、类型不是 0x0001、魔数不对、长度字段不是 4 的倍数或与实际不符时不应答，属性不解析），构造只带 `XOR-MAPPED-ADDRESS` 的成功应答；`P2PProbeRateLimiter` 按来源 IP 的一秒固定窗口限频，表项数有上限，表满时先清理过期表项、仍满则拒绝新来源。`tests/test_p2paddressprobe.cpp`（Suite STUN、RL，各 3 例）；STUN3 用 RFC 5769 第 2.2 节示例里的 `XOR-MAPPED-ADDRESS`（192.0.2.1:32853）逐字节核对。

**2e-1、2e-2 测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 占位实现下 UDP1～3、STUN1～3、RL1～3 共 9 例全部失败 |
| 实现之后 | 9 例全部通过，74 个断言 |
| Windows chatserver / imgserver | 隔离编译通过（输出 `imgserver_p2p.exe`），无编译警告 |
| Windows 全量 | 931 例全部通过 |
| WSL 全量 | g++ 构建全部目标成功（含 imgserver），931 例全部通过；UDP1～3 在 Linux 上真实收发 |

**2e-3 改动**：新增 `imgserversrc/P2PAddressProbe.h/.cpp`（登记进 imgserver 的两份清单）。两个 UDP 端口挂在 imgserver 的主循环上，可读时一次最多处理 64 个报文；两个端口共用一个限频表（6.5 万个来源上限）；被限频时每秒至多记一条日志；端口绑定失败只记日志、不影响 imgserver 的其它服务。配置项 `p2pprobelistenip`（默认 0.0.0.0）、`p2pprobeport1` / `p2pprobeport2`（默认 20004 / 20005，配 0 关闭）、`p2pprobeperipps`（默认 200），仓库里的 `etc/imgserver.conf` 已写上带注释的默认值。`imgserversrc/main.cpp` 在初始化完成前启动探测服务，在 `g_mainLoop.loop()` 返回后由主循环线程关闭端口：`prog_exit` 是信号处理函数，在 Windows 上运行在控制台控制线程里，不能在那里操作挂在主循环上的 `Channel`。开发约定文档的 imgserver 一节补充了这两个 UDP 端口与「20 字节请求不应答」的说明。

联调中发现 `net/Sockets.cpp` 的 `sockets::toIp` 长度判断写反了（缓冲足够大时什么都不写），限频日志里的来源 IP 因此为空。它是三个服务共用的代码、早已存在，本步不改它，记为 `bugs.md` BUG-79；地址探测的日志改用工作正常的 `toIpPort`，同时带上来源端口。

**2e-3 测试**（隔离的联调服务端，imgserver 用本期的 `imgserver_p2p.exe`，探测端口 29904 / 29905，联调环境把限频设为每秒 20 个）：独立按 RFC 5389 编写、不与服务端共享代码的 Python 客户端。

| 核对项 | 结果 |
| --- | --- |
| 两个端口用 32 字节请求探测 | 都回了 32 字节的成功应答，事务号对得上，`XOR-MAPPED-ADDRESS` 解出 127.0.0.1 与客户端端口 |
| 20 字节的请求、魔数不对的请求 | 都没有应答 |
| 新的一秒内连发 50 个请求 | 正好应答 20 个 |
| imgserver 日志 | 两个端口的启动记录；限频记录带来源 `from: 127.0.0.1:端口`（第一次联调时这里为空，查出 BUG-79 后改正并重跑） |
| 2c 的 P2PL1～7 与服务端核对（`Sockets.cpp` 改过后重跑） | 全部通过，211 个断言 |
| Windows 全量 | 931 例全部通过 |
| WSL 全量 | g++ 构建全部目标成功，确认 imgserver 已重新链接且包含探测服务；931 例全部通过 |

2e 至此完成。下一步是 2f（`RelaySession` 接入 1861 / 1862）。

### 14.13 第 2 期 2f：imgserver 的 P2P 回落中转（2026-10-04）

用户确认 2f 的用例时说「不用每一步都和我确认，全部做完告诉我」，自本步起不再逐步确认用例、不逐块汇报。

**改动**：

- 新增 `imgserversrc/RelayHelloPolicy.h/.cpp`（纯逻辑，登记进 imgserver 与测试的清单）：报到命令对应的种类、令牌角色能否用于这种报到（1861 只接受 11 / 12；1853 / 1855 不接受 11 / 12，其余角色维持原来的照单全收）、某种报到之后能转发哪些数据命令（P2P 只转发 1862；远程桌面与音视频照旧转发 1854 / 1856，不转发 1862）、当前状态能否接受一对一报到（已配对或已在屏幕共享房间里都不能）。没有放进 `utils/RelayRoleCheck.h`：那个头文件被 chatserver 包含，加进命令号就要包含 `Protocol.h`。
- `RelaySession`：分发 1861 / 1862；报到时角色不符回 3（报文非法）并断开；数据命令与报到种类不符时丢弃该帧；`onHello` 改用 `canAcceptPairHello(m_paired, m_inRoom)`。
- 第 9.2 节记下的疑点（`onHello` 只判断 `m_paired`）先用联调用例在修改前复现，再修正：

| 修改前（imgserver 日志） | 说明 |
| --- | --- |
| `screenshare relay hello ok ... client: 127.0.0.1:33940`，随后同一连接 `relay hello ok, session: p2p-…-5, role: 11` | 房间连接又被接受加入了一对一配对表 |
| `screenshare relay member left ... client: 127.0.0.1:33940` | 断开时只退出了房间，配对表里的表项没有摘除 |
| `relay hello: join failed (dup role or full), session: p2p-…-5, role: 11, client: 127.0.0.1:33943` | 新连接用同一会话、同一角色报到，被残留表项挡住 |

  修改后日志为 `relay hello rejected, already paired: 0, in room: 1`，新连接报到成功。
- `docs/协议设计-v2.md` 18.4 节补充报到与转发的规则；`P2PRelayLiveTests.cpp` 登记进 `FlamingoUnit.vcxproj`（先经注入的 props 在隔离构建里编过）。

**测试**：

| 阶段 | 结果 |
| --- | --- |
| 测试先行（纯逻辑） | 占位实现下 IRR1～3 全部失败（15 个断言不通过）；实现之后 3 例全部通过，35 个断言 |
| 联调，修改 `onHello` 之前 | P2PR1～4 通过；P2PR5 两处失败（房间连接的 1861 报到没被拒、新连接报到失败），与日志一致 |
| 联调，修改之后 | P2PR1～5 全部通过，35 个断言；同一环境里重跑 2c 的 P2PL1～7（211 个断言）与 2e 的地址探测核对，全部通过 |
| Windows 全量 | 934 例全部通过 |
| WSL 全量 | g++ 构建全部目标成功，确认 imgserver 用新的 `RelaySession.cpp.o` 与 `RelayHelloPolicy.cpp.o` 重新链接；934 例全部通过 |

用例 P2PR1～5（`flamingoclient/Source/Tests/P2PRelayLiveTests.cpp`，标签 `[.live][p2prelay]`）：两端用角色 11 / 12 报到；互发 1862，载荷覆盖 0～255 全部字节值，命令号与序号不变；角色不符的报到被拒并断开；一端断开后另一端被关闭；房间连接不能再做一对一报到，也不留残留表项。令牌在测试里自签，会话号用 chatserver 实际使用的 `p2p-…` 格式（令牌对会话号的字符没有限制，只要求不含 `|`，见 `utils/RelayToken.cpp`）。

2f 至此完成。下一步是 2g（flamingoAdmin 的开关界面）。

### 14.14 第 2 期 2g：flamingoAdmin 的开关界面（2026-10-04）

**改动**：

- `FeatureSwitchPanel` 改成由 `FeatureSwitchSpec`（设置项名、名称、紧凑样式的短名称、说明、打开与关闭的确认正文、关闭是否用危险样式）驱动，同一个类同时服务文件共享与客户端点对点直连；`FileShareSwitchSpec()` 保持原有全部文字，`P2PSwitchSpec()` 的名称为「客户端点对点直连」、短名称「点对点直连」。关闭 P2P 只影响新建的会话、已建立的直连不中断（9.4 节），所以关闭的确认框不用危险样式，正文写明这一点。确认框标题、成功提示、「服务端没有返回某项设置」的失败原因都由名称拼出。紧凑样式的名称列由 72 像素加宽到 96 像素，放得下「点对点直连」，两行对齐。
- 企业设置弹窗（平台管理员）与平台设置页（企业管理员）各加一个 P2P 面板，排在文件共享面板之下：弹窗的「功能开关」块由一行变为两行（弹窗高 36 像素），设置页的功能开关卡加高一个面板。`AdminFrame` 接上两个面板的请求出口、`Load`、开关拨动与接口应答的分发。新增设置项名 `kSettingP2PEnabled`、结果标签 80～83、控件 ID 602 / 603。
- 两个面板各发一次 `setting/get.do`（同一份应答里各取各的设置项），没有合并成一次请求：多一次本地 HTTP 往返，换来面板之间互不依赖。

**测试**（flamingoAdmin 自建的断言框架，`Tests/FileShareScreensTests.cpp`；新文件先经隔离的编译配置编出 `flamingoAdminTests_p2p.exe`）：

| 用例 | 内容 |
| --- | --- |
| FSP-1～3（改用新构造函数） | 文件共享面板的原有行为不变 |
| FSP-4（新增） | P2P 面板在同一份应答里只取 `p2p.enabled`；打开与关闭的确认框标题、关闭不用危险样式且正文说明直连不中断；提交的设置项为 `p2p.enabled`；成功提示；应答里没有这一项时按读取失败处理、原因写明是点对点直连；文件共享面板在同一份应答里取 `fileshare.enabled`；两个描述的设置项名与短名称 |
| FSL-1（扩展） | 企业设置弹窗里 P2P 面板紧接在文件共享面板之下，一行高、左边对齐、在底部按钮之上，开关有实际尺寸；两位管理员时整个弹窗仍在最小窗口内 |
| FSL-2（扩展） | 平台设置页的功能开关卡里 P2P 面板在文件共享面板之下、卡片之内、左右对齐，随卡片一起显隐 |

| 阶段 | 结果 |
| --- | --- |
| 测试先行 | 临时让 `P2PSwitchSpec()` 返回文件共享的描述：FSP-4 四处失败，其余 1529 个断言全部通过（flamingoAdmin 测试此前没有失败项）。第一次运行时 FSP-4 在没有弹出确认框时解引用了空指针、整个测试程序崩溃，输出缓冲使最后一行停在 ST-3，一度误判为别处的问题；用 cdb 看栈定位到 FSP-4，给用例补上非空检查后重跑 |
| 实现之后 | 1550 个断言全部通过 |
| flamingoAdmin 主程序 | 隔离编译（`flamingoAdmin_p2p.exe`）通过 |

界面没有在真实窗口里人工核对：在共用桌面上启动 flamingoAdmin 需要先征得用户同意，留到全部做完时一并请示。第 2 期至此全部完成。

### 14.15 第 3 期 3a、3b：报文与加密、候选地址与地址探测客户端（2026-10-04）

**3a 改动**（`flamingoclient/FlamingoP2P/Core/`，只依赖标准库与 libsodium）：

- `P2PPacket`：报文类型 0x81～0x87（探测、探测应答、选定、选定确认、数据、保活、关闭），首字节都不小于 0x80，与 STUN（首字节最高两位为 0）可以按首字节区分；14 字节报文头（类型、保留、会话标签、计数器，多字节字段按网络字节序）；单个报文上限 1200 字节，明文最多 1170 字节。
- `P2PReplayWindow`：1024 个报文的重放窗口（位图），先判断、认证通过后再记下，认证失败的报文不推动窗口。
- `P2PCrypto`：`P2PKeyPair`（临时 X25519 密钥对、公钥 Base64 文本）与 `P2PLinkCipher`（`crypto_kx` 按角色推导收发密钥；ChaCha20-Poly1305 IETF，nonce 为 4 个零字节加 8 字节计数器，报文头作附加数据；会话标签取 `crypto_generichash(上下文, key=发送密钥)` 的前 4 字节；拆封依次核对格式、会话标签、重放窗口，最后解密认证）。
- 生成脚本 `gen_p2p_vcxproj.py` 的模块目录加 `Core`；`Core` 的源文件与 `Tests/P2PCoreTests.cpp` 编进 FlamingoUnit。

**3b 改动**（`FlamingoP2P/Ice/`）：

- `P2PCandidate`：网卡筛选（排除未连接、回环网卡与回环 / 链路本地 / 全零地址，虚拟网卡按描述或名称里的关键词降低优先级而不排除，同一 IP 留优先级高的一份，最多 8 个）；ICE 形式的优先级（本机地址无论如何高于公网映射地址）；不依赖 winsock 的 IP 文本互转；2303 `data.candidates` 的 JSON 编解码（跳过不合法的条目，前缀长度不发给对方）。
- `P2PStunClient`：32 字节的 Binding 请求（20 字节头 + 8 字节值的 SOFTWARE 属性）；应答按 RFC 5389 逐个遍历属性取 IPv4 的 XOR-MAPPED-ADDRESS，能解析带其它属性的标准服务器应答。
- `P2PAddressProber`：每个探测服务器一个随机事务号，无应答时每 250 毫秒重发、最多 3 次，约 1 秒内完成；两个服务器看到的映射端口相同 / 不同分别判为映射与目的地址无关 / 有关；重复、迟到与陌生事务号的应答忽略。发送经接口 `IP2PDatagramSender`、时间由调用方传入。
- DLL 工程加入 jsoncpp（与 FlamingoFileShare 一样屏蔽 `json_reader.cpp` 的 C4244）与 `Ice` 目录。

**测试**（FlamingoUnit，Catch2）：

| 组 | 用例 | 结果 |
| --- | --- | --- |
| `[p2pcore]` P2PK1～5 | 密钥协商与公钥文本（含全零弱公钥 Init 失败，实测 libsodium 拒绝）、加解密往返与计数器、任意一字节被改都失败、重放窗口、报文分类与报文头 | 占位实现下 5 例全部失败；实现后全部通过，206 个断言 |
| `[p2pice]` P2PC1～5 | 网卡筛选与优先级、Binding 请求格式、应答解析（RFC 5769 第 2.2 节示例里的 XOR-MAPPED-ADDRESS 192.0.2.1:32853 等）、探测流程（相同 / 不同端口、重发与放弃、只有一个应答、陌生应答）、候选地址 JSON 与 IP 文本 | 占位实现下 5 例全部失败；实现后全部通过，139 个断言 |
| 回归 `[p2pdll],[p2phost],[p2pcore],[p2pice]` | DLL（Debug\|Win32，隔离中间目录）编译无警告、加载与工程守卫 | 22 例全部通过，553 个断言 |

### 14.16 第 3 期 3c：连通性检查状态机与模拟网络（2026-10-04）

**改动**：

- `Ice/P2PConnectivityChecker`：检查表就是对方的候选地址，按「与本方网卡同网段的对方本机地址 > 对方其它本机地址 > 公网映射地址 > 对端反射地址」排序；每项每 50 毫秒发一次探测，建链超时之前一直重发（第 0a 步的结论：防火墙丢弃对方先到的包，要等本方也发过之后对方重发的才能进来）；收到探测回应答并立即向来源补发探测，来源不在表里时作为对端反射地址加入；发起方第一项连通后再等 100 毫秒，选定已连通项里优先级最高的一项，重发「选定」直到收到「选定确认」；应答方收到「选定」即锁定，之后只认同一地址的「选定」；应答方判超时比发起方多等 500 毫秒。
- `Tests/P2PSimNet`：只给单测用的确定性模拟网络，模拟完全锥形 / 受限锥形 / 端口受限锥形 / 对称型 NAT、同一 NAT 后面的局域网、公网主机、地址探测服务器，以及延迟、抖动（乱序）与丢包（固定种子的线性同余发生器）。

**测试**（`[p2psim]` P2PN0～10，11 例 166 个断言，全部通过）：模拟网络自检；同一局域网选同网段本机地址；两边都是公网主机；完全锥形 × 端口受限锥形；端口受限锥形 × 端口受限锥形（私网网段相同时不能误选对方的私网地址）；对称型 × 完全锥形（经对端反射地址打通）；对称型 × 端口受限锥形、对称型 × 对称型按时判失败；30% 丢包加乱序仍能打通；地址探测服务器不可达；选定确认丢失与选定后不切换。

### 14.17 第 3 期 3d：信令编解码、会话状态机与 IO 线程（2026-10-04）

**改动**：

- `Link/P2PSignalCodec`：2301 / 2303 / 2304 / 2305 / 2306 的请求包体，2301 / 2306 的应答与 2302 / 2303 / 2305 推送的解析，字段名与服务端 `P2PProtocol` 一致；2303 的 `data` 由两端约定为 `{ "pubkey", "candidates" }`，服务端原样转发。
- `Link/P2PLinkSession`：一个会话从 2302 到就绪、断开的状态机（取映射地址 → 发 2303 → 收到对方 2303 后推导密钥并打洞 → 发 2304 → 就绪；就绪后保活与断开判定；业务方关闭、对方关闭报文、服务端推来的 2305 三种结束方式）。信令阶段有总时限（探测预留 1500 毫秒 + 建链超时 + 应答方额外等待）。第 3 期只有数据报通道（一块数据一个链路报文），分片与可靠通道在第 4 期。
- `Io/P2PUdpPort`（绑定 0.0.0.0 随机端口、非阻塞、关闭 `SIO_UDP_CONNRESET`）、`Io/P2PAdapters`（`GetAdaptersAddresses` 枚举网卡、服务器地址按 IP 文本或 DNS 解析）。
- `Io/P2PEngine`：一个 IO 线程驱动全部会话。登录后发 2301；收到 2302 给会话开一个 UDP socket、取本机候选后开始建链；2303 / 2305 推送交给对应会话；会话就绪时按用途找业务方交出 `IP2PLink`（引用计数，引擎持有一个，会话结束时放掉；业务方多持有的引用在引擎停止之后仍可安全调用，只会得到「已关闭」）；退出登录与停止时关闭全部会话（经 chatserver 发 2305，原因为本方退出）。
- `Host/P2PModule`：Init 时创建并启动引擎，Shutdown 时先停止引擎（此时宿主仍然有效，2305 能发出去），再关闭服务、解除宿主；引擎用裸指针，不放进静态对象的析构函数，避免 DLL 卸载时在加载器锁里等待 IO 线程结束。
- 生成脚本的模块目录加 `Link`、`Io`；这两个目录的源文件与 `Tests/P2PLinkTests.cpp`、`Tests/P2PIoTests.cpp` 编进 FlamingoUnit。

**测试**（FlamingoUnit，Catch2；DLL 与测试程序都用隔离的中间目录编译，Debug|Win32 无警告）：

| 组 | 用例 | 结果 |
| --- | --- | --- |
| `[p2plink]` P2PSC1～3、P2PLS1～9 | 信令编解码（含经服务端加上 fromuid 后的往返）；会话状态机接在模拟网络上：局域网、端口受限锥形打洞、对称型失败、对方 2303 先到、对方 2303 一直不来、数据报通道、保活与断开判定、业务方关闭、服务端推来 2305 | 12 例全部通过，214 个断言 |
| `[p2pio]` P2PIO1～6 | 同一进程开两个引擎，经本机真实 UDP 互连（chatserver 的转发由假宿主模拟）：建链就绪且路径为局域网直连、数据报往返、业务方关闭后对方报告断开、对方不应答按时判失败、退出登录、停止引擎时业务方仍持有链路；网卡枚举与地址解析；UDP socket 基本行为 | 6 例全部通过，108 个断言；建链约 0.2 秒，失败用例约 2.5 秒；与 `[p2pdll]` 连跑 15 轮全部通过 |
| `[p2pdll]` P2PD7（新增） | 加载真实 DLL：登录后经宿主发出 2301，收到 2302 后发出 2303，Shutdown 发出 2305（原因为本方退出） | 通过；P2PD1～6 的桩宿主因 IO 线程会并发回调而改为加锁 |
| 回归 `[p2pdll],[p2phost],[p2pcore],[p2pice],[p2psim],[p2plink],[p2pio]` 与 `[filters],[filesharedll]` | — | 前者 52 例 1056 个断言、后者 19 例全部通过 |

**留到第 4 期的事项**：会话的观察者回调与通道回调都在持有会话互斥量时于 IO 线程上发生。业务方若在持有自己的锁时调用 `IP2PLink`，同时又在回调里取同一把锁，就会互相等待。第 3 期只有联调用途，没有真实业务方；第 4 期改通道时把回调移到释放会话互斥量之后，再在接口注释里写明约束。

### 14.18 第 3 期 3e、3f：回落中转与同机联调（2026-10-04）

**3e 改动**：

- `Link/P2PLinkSession`：新增状态「正在走中转」。打洞失败（或联调用的强制中转）且 2302 带了中转参数时，经观察者的 `StartRelay` 请求连中转；连上后发起方每 200 毫秒经中转重发「选定」，应答方回「选定确认」，就绪后路径为中转，2304 的地址对写 `relay <地址>:<端口>`。中转阶段总时限 10 秒；中转连不上、报到被拒、就绪前后中转连接断开都有对应处理（就绪前按建链失败，就绪后按链路超时断开，都发 2305）。就绪之后 UDP 上迟到的链路报文一律忽略。中转路径上的报文与 UDP 路径相同，imgserver 只看到端到端加密之后的密文。
- `Io/P2PRelayConn`：一条到 imgserver 中转端口的 TCP 连接。连接线程负责解析地址、带超时连接、三帧协议加密握手（宿主给了服务端公钥时）与 1861 报到；就绪之后不另起接收线程，由引擎的 IO 线程与 UDP socket 一起 select，拆出 1862 帧逐跳解密后交给会话；发送在锁内完成加密与写 socket。
- `Io/P2PEngine`：会话请求中转时建 `P2PRelayConn`，IO 线程把连接结果、收到的报文与断开交给会话，会话结束时关闭中转连接；宿主接口加 `GetServerPublicKey`；强制中转可由环境变量 `FLAMINGO_P2P_FORCE_RELAY=1`（引擎启动时读取）或 `SetForceRelay` 打开。
- DLL 工程加入 `Source/net` 的帧编解码、协议加密握手、帧体加解密，以及它们依赖的 `ZlibUtil` 与 zlib（与 FlamingoFileShare 相同，gzread / gzwrite 屏蔽 C4267）。Debug|Win32 与 Debug|x64（输出到隔离目录，没有覆盖运行目录里的 `FlamingoP2P.dll`）都编译无警告。

**3e 测试**：

| 组 | 用例 | 结果 |
| --- | --- | --- |
| `[p2plink]` P2PLS3（改）、P2PLS10～14 | 模拟中转（两端都报到后才转发、报到前的报文丢弃）：对称型 × 对称型且中转连不上 → 失败；中转可用 → 经中转就绪、发起方的「选定」有被丢弃的并靠重发完成、数据往返、保活维持；强制中转时 UDP 上一个报文都不发；就绪后中转断开 → 两端报告链路超时；对方一直不来 → 按中转时限失败；2302 没带中转参数 → 不请求中转直接失败 | 全部通过 |
| `[p2pio]` P2PIO7～10 | 两个真实引擎接本机假中转服务（明文、按 1861 配对）：强制中转建链与数据往返、对方报告断开；端口无人监听（约 2 秒失败）；报到被拒；就绪后中转服务关闭连接 → 两端报告链路超时 | 全部通过 |
| 回归 | `[p2pdll],[p2phost],[p2pcore],[p2pice],[p2psim],[p2plink],[p2pio]` 61 例 1249 个断言全部通过；`[p2pio],[p2plink],[p2pdll]` 连跑 10 轮全部通过 | — |

**3f 同机联调**（`Source/Tests/P2PEngineLiveTests.cpp`，`[.live][p2pengine]`，环境为隔离的联调服务端）：两个账号各登录一条连接、各挂一个真实引擎，2300 段命令都经测试服务端的 chatserver 真实收发，会话由 2306 建立。

| 用例 | 结果与服务端日志 |
| --- | --- |
| P2PE1 局域网直连 | 通过。chatserver 收到两端的 2304：`ok` 为真、`path` 为 1、建链 21 毫秒，地址对是本机网卡地址之间的一对；`nat.samemapping` 为真，说明 imgserver 的两个探测端口都应答了；业务方关闭后收到原因 1 的 2305 |
| P2PE2 强制中转 | 通过。imgserver 的两条中转连接都完成协议加密握手，角色 12 与 11 按同一会话号报到成功；chatserver 收到 `path` 为 3、地址对 `relay 127.0.0.1:29903` 的 2304；退出登录后收到原因 5 的 2305，imgserver 随即关闭两条中转连接 |

**尚未验证的部分**：跨机器、两端都在 NAT 后面的打洞与自然回落中转（同机上 UDP 必然能通，自然回落只在模拟网络里验证过），以及真实 exe 加载新 DLL 后的界面联调。两者分别留到第 4 期的吞吐联调（4f）与第 5 期接入文件夹传输时进行；后者要在共用桌面上启动客户端，届时先征得用户同意。

### 14.19 第 4 期 4a～4e：通道、可靠通道、拥塞控制、统计与成批发送（2026-10-04）

**4a 通道复用与数据报通道**（新目录 `FlamingoP2P/Chan/`）：

- `P2PDatagramFrag`：数据报一次 Send 的数据切片，每片带 8 字节分片头（消息号、片号、片数）；接收端按消息号收齐后交出。一条消息最多 1024 片（UDP 路径约 1.1 MB），每条通道同时最多重组 16 条，1 秒内没收齐或表满时丢弃最旧的。
- `P2PChannelMux`：一条链路上的全部通道，数据报文明文为 `[通道号 2B][通道类型 1B][负载]`。中转路径的单个报文明文放宽到 64 KiB（`kP2PRelayMaxPlainBytes`，`P2PLinkCipher::Seal` 增加上限参数），UDP 路径仍是 1170 字节。
- 会话的事件（就绪、失败、断开、通道收到数据、可写）改为持锁时排队、释放互斥量后再分发（3d 留下的事项）：业务方在回调里可以调用链路的任何方法，也可以在持有自己的锁时调用链路。只有 IO 线程上的入口分发事件。

**4b 可靠通道**：KCP 源码（上游提交 `b1a7a21`，带本项目的发送额度判断顺序修正）放在 `FlamingoP2P/third-party/kcp/`，附说明文件 `README_flamingo.md`。`Chan/P2PReliableChannel`：UDP 路径每条通道一个 KCP（流模式、`nodelay=1`、快速重传阈值 2、刷新间隔 2 毫秒、窗口 4096），中转路径不经 KCP、按 64 KiB 切块直接透传。发送队列超过 4 MiB 返回「稍后重试」，降到 2 MiB 以下通知可写。空闲（没有待发、待确认的数据与待回的确认）时不需要定时，避免空闲通道每 2 毫秒唤醒一次 IO 线程；业务方在自己的线程上交了数据后经 `RequestWake` 唤醒 IO 线程。中转连接改为非阻塞发送：帧先进发送缓冲，写不完的部分在 socket 可写时由 IO 线程继续写，积压降下来后通知会话检查可写。

**4c 拥塞控制**：`Chan/P2PBbrCc` 由第 0b 步的原型整理而来，经 `ikcp_setcc` 挂到每个 KCP 上（三个必须给出的回调都给出，超时余量在 `on_rtt` 里放宽）。确定性单测在虚拟时钟下的模拟瓶颈链路上运行（与原型相同的链路模型），两端用真实的 `P2PReliableChannel`：

| 条件 | 带宽利用率（原型） | 平均排队延迟（原型） |
| --- | --- | --- |
| 千兆局域网，往返 1 毫秒 | 96.2%（98%） | 1.3 毫秒（1.3） |
| 200M Wi-Fi，往返 5 毫秒，丢包 0.5% | 88.0%（81%） | 1.7 毫秒（1.3） |
| 100M，往返 30 毫秒，无丢包 | 87.8%（91%） | 30.4 毫秒（27.7） |
| 100M，往返 30 毫秒，丢包 0.1% | 85.6%（89%） | 7.1 毫秒（4.0） |
| 20M，往返 60 毫秒，丢包 1% | 84.3%（84%） | 8.0 毫秒（5.4） |
| 8M，往返 120 毫秒，丢包 5% | 62.3%（68%） | 1.5 毫秒（6.3） |

传输时长比原型（理想 8 秒）短，利用率略低于原型。无丢包长肥链路上排队延迟约等于一个往返时延的问题仍在（BBR 第一版的特点），压低它留作后续改进。（2026-10-05 已改进，原因与改后的数据见第 14.27 节。）

联调中查出并修正了一个问题：BBR 的发送额度用完时，还没发出的分段重传时刻已到，`ikcp_check` 会一直返回「现在」，而 `ikcp_update` 只在到达下一个刷新时刻（`ts_flush`）才真正刷新，按 `ikcp_check` 定时会让 IO 线程在两次刷新之间空转（模拟推进的用例因此卡住）。改为按 `ts_flush` 定时。

**4d 统计**：`GetStats` 报告路径、可靠通道的平滑往返时延、按重传分段数估算的丢包率（千分比）、发送队列积压（含中转连接的发送缓冲）与累计收发字节数。

**4e 成批发送**：会话的 UDP socket 设置 `UDP_SEND_MSG_SIZE`（每段 1200 字节）；定时、收包与业务方发送这三个入口在持锁期间把 UDP 报文攒起来，退出时把连续的满载报文（末尾可带一个短报文）一次交给系统，一批最多 32 段。Win10 之前的系统设置会失败，此时逐个发送。本机（Win10 19045）设置成功。诊断用环境变量 `FLAMINGO_P2P_DISABLE_USO=1` 可关闭它。

**测试**（FlamingoUnit，Debug|Win32）：`[p2pchan]` P2PCH1～7（分片、重组、复用层、可靠通道的有序送达与 10% 丢包、队列上限与可写通知、中转透传）；`[p2pcc]` P2PCC1～3；`[p2plink]` 新增 P2PLS15（回调期间会话互斥量没有被持有）、P2PLS16（统计）、P2PLS17（成批发送）；`[p2pio]` 新增 P2PIO11～13（本机 UDP 与假中转上的可靠通道传输、UDP socket 成批发送）；`[p2pcore]` 新增 P2PK6。全部 P2P 标签 78 例 1661 个断言通过。

### 14.20 第 4 期 4f：吞吐联调（2026-10-04）

**基准程序**：临时写的基准程序（用 cl 直接编成 Release x86、静态 CRT），同一进程里开两个引擎，一条可靠通道，A 向 B 传 N MiB 并逐块核对内容。`lan` 模式走本机网卡地址直连，`relay` 模式强制中转，经隔离联调服务端的 imgserver（Debug x64 版，带协议加密握手与逐跳加密）。

**联调中查出并修正的两个问题**：

1. **UDP socket 的收发缓冲**：会话的 socket 用的是系统默认缓冲（几十 KB）。改为与第 0b 步原型相同的 8 MiB。
2. **系统定时器精度**：Windows 默认约 15.6 毫秒，KCP 每 2 毫秒刷新一次，IO 线程按 2 毫秒 `select` 时实际要睡十几毫秒；而拥塞控制算发送额度时距上次刷新的时长最多计 10 毫秒，于是每轮实际发出的少于测得的带宽，测得的带宽又一轮比一轮低。实测本机直连只有约 45 KB/s，同一程序调用 `timeBeginPeriod(1)` 后为 48.6 MiB/s。修正：引擎在有会话的下一次定时在 50 毫秒以内时调用 `timeBeginPeriod(1)`，空闲 1 秒后恢复（Win10 2004 起只影响本进程，空闲时 IO 线程最长等 100 毫秒，不增加唤醒）；拥塞控制的额度时长上限放宽到 32 毫秒（两个默认定时器周期），系统没有按要求提高精度时也不会越测越慢。

**结果**（Release，本机回环；两个引擎在同一进程里，各自的 IO 线程与发送、接收线程分占 CPU）：

| 情况 | 吞吐 | CPU（以一个核为 100%） |
| --- | --- | --- |
| 直连，成批发送开 | 51.6～53.8 MiB/s | 约 160% |
| 直连，成批发送关（`FLAMINGO_P2P_DISABLE_USO=1`） | 26.6 MiB/s | 165% |
| 强制中转，经 Debug 版 imgserver | 36.0～44.2 MiB/s | 约 100%～120% |

成批发送让直连吞吐翻倍。对照第 0b 步：现有 TCP 加 512 KiB 帧加密在回环上约 280 MB/s，KCP 加逐包加密的原型约 44～52 MB/s，正式实现与原型相当。跑满千兆局域网（约 119 MiB/s）还差一倍左右，接收端合并（`UDP_RECV_MAX_COALESCED_SIZE`，第 0b 步在回环上没有生效）与减少拷贝留作以后的优化；丢包与长肥链路上的表现以 4c 的模拟结果为准，跨机器、跨 NAT 的实测需要两台 Windows 机器，暂时没有条件。

**同期修正（第 5 期准备）**：业务方在别的线程上 `Close` 链路时，要等 IO 线程上正在进行的那一轮回调结束才返回，之后排队的事件不再分发（用例 P2PLS18）。4a 把回调移到会话锁之外以后，原来 `Close` 返回时 IO 线程可能还在回调业务方的接收端，业务方随即销毁接收端就会访问已释放的对象。

### 14.21 第 5 期：单聊文件夹传输接入 P2P（2026-10-04）

**5a 服务端**：`ChatSession::pushFolderRelayInfo`（接受邀请与 1817 重试两处调用）先经 `P2PSignalService::createForFeature` 为文件夹传输建 P2P 会话（发送端为发起方，两端连接按 `P2PSessionManager::latestConnectionOf` 取各自最近一次报到的那条），成功时两帧 1816 带同一个 `p2psessionid`，随后再推两份 2302（客户端先知道这次传输带着哪个 P2P 会话，再收到会话本身）；建不成时照旧只下发中转参数，原因只记日志。`FolderTransferProtocol::buildRelayInfo` 增加可选参数，协议文档 12.2 节补 `p2psessionid` 字段。用例 P2PS7、P2PSV7、FTP29。

**5b 传输抽象**（`Source/Feature/FolderTransfer/FolderLaneTransport.h`，只有头文件）：`IFolderLaneTransport`（连接、发应用帧、关闭、是否连着）与工厂 `IFolderLaneTransportFactory`；中转实现 `FolderRelayLaneTransport` 包装原有的 `FolderRelayClient`。`FolderSendSession` 与 `FolderRecvSession` 的参数增加工厂指针，为空时各条通道照旧走中转，因此 FlamingoFileShare.dll（编译同一批源文件、不设工厂）的行为不变。

**5c P2P 实现**（`Source/Feature/P2P/FolderP2PTransport`）：

- `FolderP2PLinkBroker`：用途 `folder` 的业务方（主程序用单例，单测每端各建一个），把 DLL 交来的链路按功能会话号放进链路槽，等传输认领；1816 与 2302 谁先到都能配上；同一会话号重试时新建槽，上一轮的槽释放时关闭它的链路。
- `FolderP2PTransportFactory`：由 `FolderTransferManager` 的会话记录持有，第一条通道 `Connect` 时等链路结果（最多 20 秒），就绪则整次传输走 P2P，失败则全部退回中转；之后各条通道沿用同一决定。
- P2P 通道：一条链路上开 1 + N 条可靠通道（0 号对应控制通道），应用帧前加 4 字节长度；收到的数据在独立线程上拆帧交给会话；发送队列满时最多等 60 秒可写。
- 主程序在 P2P 模块加载成功后登记业务方、关闭 P2P 模块前注销；用户要求默认打开，诊断开关 `FLAMINGO_FOLDER_P2P=0` 时不登记，文件夹传输全部走中转。

**FP2P4 查出的文件夹传输原有问题**：发送队列把 0 字节文件当成「续传时已完整落盘」跳过（起传偏移 0 等于文件长度 0），接收端却要等每个文件的开始与结束帧才计为完成，所以文件夹里只要有空文件，传输就永远完成不了，走中转同样如此。`FolderSendFileQueue::IsAlreadyComplete` 改为只有长度大于 0 的文件才可能被跳过；续传时重发一个已收完的空文件，接收端按「重传一个已完成的文件」清零后重新收下（`FolderRecvSession.cpp` 的 `ft_file_begin` 处理），没有副作用。FSS7 的预期随之改正。

**5d 真实信令联调**（`Source/Tests/FolderP2PLiveTests.cpp`，`[.live][folderp2plive]`，环境为隔离的联调服务端）：两个账号各登录一条连接、各挂一个真实引擎与一个文件夹业务方，1810 / 1811 / 1812 / 1816 / 1817 / 1814 与 2300 段命令都经测试服务端真实收发；用真实的发送、接收会话传一个含子目录、空文件与大文件的文件夹，落地文件逐个比对。

| 用例 | 内容 | 结果 |
| --- | --- | --- |
| FP2PL1 | 局域网直连：1816 带同一个 `p2psessionid`、角色相反；两端都决定走 P2P，路径为局域网直连 | 通过（0.4 秒） |
| FP2PL2 | 两端强制中转：链路经 imgserver 的 P2P 中转建成，路径为中转 | 通过 |
| FP2PL3 | 64 MiB 文件传到约 4 MiB 时两端停掉、以 616 失败结束；A 发 1817，服务端推新的 1816，带新的 `p2psessionid`；接收端沿用第一次的接收状态，第二次从断点续传（续传起点大于 0），两端都走 P2P | 通过（11.5 秒） |
| FP2PL4 | 对方是旧客户端（不报到 P2P 能力）：1816 不带 `p2psessionid`，两端都走 fileserver 中转 | 通过 |
| FP2PL5 | 对方关掉了文件夹 P2P | 修正前失败，见下文；修正后通过 |

**FP2PL5 查出的设计缺陷与修正**：服务端建会话时只检查两端是否报到过 P2P、企业是否开启，不管对方能不能接住这个用途的链路。对方有 P2P 能力但没有 `folder` 业务方时，链路照样建成，对方的引擎随即关闭它；本端在这之前已经看到链路就绪、决定走 P2P，而对方没有建通道工厂、各条通道直接连了 fileserver，两端对不上，发送端打开 0 号通道失败，以 616 结束（日志里 A 等了 26 毫秒决定走 P2P，同一毫秒 B 以「没有业务方」关闭链路）。眼下只有设置 `FLAMINGO_FOLDER_P2P=0` 才会碰到，但第 6 期接入新用途后，新旧版本客户端混用就会普遍出现。修正：

- 2301 增加 `purposes` 字段，DLL 报上已登记业务方的用途；业务方登记或注销之后（`CP2PService` 通知作为观察者的模块对象），已登录时引擎重新报到，服务端以同一连接最后一次报到为准。
- `P2PSessionManager::tryCreate` 要求两端都报到了该用途，否则返回新错误码 908 `error_code_p2p_purpose_unsupported`（只记日志，不返回客户端），业务照旧走中转；联调用途 `test` 不检查。字段缺省视为不支持任何业务用途。
- 用例：服务端 P2PP7（解析）、P2PS8（建会话的用途检查、重新报到）、P2PSV8（报到包体非法、一端不支持时 908）；客户端 P2PSC1（构造）、P2PD8（登录前登记不报到、登录后带上用途、登记与注销后重新报到、退出登录后不再报到）。

据推测还有一种小概率情形没有覆盖：两端在建链超时的边界上得出相反的结果（一端就绪、另一端失败），两端同样会对不上，本次传输失败，用户点「重试」即可。这一点尚未用用例复现，留作以后视情况处理。

**测试结果**：

- 服务端：Windows（`Tests_p2p.exe`）与 WSL（g++）全量回归各 966 例全部通过，含新增的 P2PP7、P2PS8、P2PSV8；两份 `Protocol.h` 的一致性用例 PP1～PP5 通过。
- 客户端（FlamingoUnit，Debug|Win32）：P2P 相关标签 95 例 1873 个断言全部通过（含 P2PD8 与 `[folderp2p]` FP2P1～4）；文件夹传输 FIN / FLR / FMAN / FPC / FRS / FRC / FTF / FSS / FTM 共 157 例全部通过；`[filesharedll]` 15 例通过。
- 真实服务端联调（隔离的联调服务端，换成带用途检查的版本）：`[folderp2plive]` 5 例 282 个断言全部通过；`[p2pengine]` 2 例、`[p2plive]` 7 例照常通过（联调用途不受用途检查影响）。chatserver 日志里报到包体为 `{"purposes":["folder"],"version":1}`；FP2PL4 建会话返回 903（对方没有报到 P2P），FP2PL5 对方报到 `"purposes":[]`、建会话返回 908；FP2PL3 重试时换了新的 P2P 会话号。

**界面联调**（2026-10-04，经用户同意在共用桌面上进行；两个自动登录的 x64 Debug 测试客户端连隔离的联调服务端，全程只用 PostMessage 按窗口句柄投递、不动真实鼠标键盘）：A 在单聊窗点「发送文件夹」，系统「浏览文件夹」框由脚本写入路径选中 `sendsrc\p2pfolder`（24 MiB 大文件、空文件、子目录里的小文件）；B 在气泡上点「接收」。两端日志：1816 带 P2P 会话号，链路 54 毫秒以局域网直连就绪，两端各等 53 毫秒决定走 P2P，控制通道与 4 条数据通道都是 P2P 通道；24 MiB 用时约 3.5 秒；接收端三个文件逐个校验通过、SHA256 与源文件一致；两端气泡分别显示「全部发送完成」「全部接收完成」（截图 `snapshots/p2p_ui/`）。

**尚未验证的部分**：跨机器、跨 NAT 的文件夹传输（需要两台 Windows 机器）。

### 14.22 第 6 期第一项：文件共享的传输走 P2P（2026-10-04）

用户定第 6 期先接文件共享的传输。文件共享的下载、上传本来就复用文件夹传输的发送 / 接收会话，所以 P2P 一侧几乎不用新代码，主要是服务端下发会话号、DLL 拿到 P2P 服务。浏览用的访问会话（只有控制通道）不变，仍走 fileserver 中转。

**6a-1 服务端**：

- `P2PSignalService::createForConnections`：按调用方给定的两条连接建会话。文件共享的分享者一端是提供该共享的那条连接（`access.m_ownerConn`），不一定是该用户最近一次报到的连接，不能用 `createForFeature` 按用户找连接；两种连接键都是 `ChatSession` 对象地址，可以直接互换。`createForFeature` 改为找到连接后调用它。
- `ChatSession::onShareRequestTransfer`：签好两帧 2222 之后，以访问者（本连接）为发起方、分享者的提供连接为应答方，为 `transferid` 建用途为 `fileshare` 的会话；建成时两帧 2222 带同一个 `p2psessionid`，2302 排在 2222 之后；建不成（老客户端、企业关闭、用途没报到）时只记日志。
- `FileShareProtocol::buildRelayInfo`：`m_p2pSessionId` 只在传输子会话且非空时写出；共用样本 `fileshare_wire_samples.h` 新增 `kWireNotifyRelayVisitorP2P`。
- 用例 FSP7、P2PSV9。Windows 990 例（临时排除了另一个会话当时尚未配齐的用例文件）、WSL 993 例全部通过。

**6a-2 业务方按用途登记**：`FolderP2PLinkBroker` 增加按用途构造的构造函数（默认仍是 `folder`），用途常量 `kFolderP2PPurpose` / `kFileShareP2PPurpose` 放进头文件。用例 FP2P5。

**6a-3 宿主接口**：`IFileShareHost` 末尾新增 `GetP2PService()`，exe 的 `CFileShareHost` 转交 `CP2PHost::GetService()`。按用户「产品未发布、插件接口不升版本」的规定，`kApiVersion` 不变，exe 与 DLL 须一起编译；已通知全部并行会话。四个测试假宿主各补一个实现。

**6a-4 DLL**：

- `FlamingoFileShare.vcxproj` 编入 `Source/Feature/P2P/FolderP2PTransport.cpp`（DLL 里不调用文件夹专用的登记函数）。
- `CShareService` 持有一个用途为 `fileshare` 的业务方：`Attach` 时宿主给了 P2P 服务才登记，`Detach` 在全部传输停止之后注销。DLL 在首次登录之后才加载，此时 P2P 模块已经报到过一次，登记表变化会触发重新报到（第 5 期 5d 做的机制），服务端此后才为文件共享建会话。
- `ShareRelayParams` 解析 `p2psessionid`；`MakeTransportFactory` 在带了会话号且业务方已登记时按 `transferid` 建通道工厂，交给 `CShareTransferSession` 持有（成员声明在发送 / 接收会话之前，保证后析构），填参时设进 `m_transportFactory`。
- 用例 SPC4（解析）、FSD13（宿主给 P2P 服务时 Init 登记 `fileshare`、Shutdown 注销；不给时不登记）。

**6a-5 联调**：`FolderP2PLiveTests.cpp` 新增 FP2PL6：A 在与 B 的单聊里发起文件夹共享（2201）、报告可提供（2210），B 打开（2220）、申请下载（2224）；两帧 2222 带同一个 `p2psessionid`，两端从各自的 `fileshare` 业务方按 `transferid` 建工厂，A 发、B 收，局域网直连约 0.7 秒传完，落地文件一致；最后结束传输、取消共享。服务端日志据实证：两端报到 `{"purposes":["fileshare","folder"],"version":1}`，`share transfer p2p session: transferid: share-1-…-t1, code: 0`。FP2PL1～5 照常通过。

**界面联调**（2026-10-04，经用户同意在共用桌面上进行，只用 PostMessage 投递、按句柄截图）：用当前源码隔离编出 x64 主程序（`Flamingo_p2p.exe`）、文件共享 DLL 与 P2P DLL（均在隔离目录编译），由脚本放进两个测试客户端（不带 FlamingoMedia.dll：它的接口当天被别的会话改过，与这个主程序不一定一致）；再由脚本在隔离库与 A 的本机库里写共享 9101（A 的文件夹共享给 B，只有下载权限，共享根为测试目录下的 `share_root`，data 目录下 24 MiB 大文件、空文件、子目录里的小文件）。B 在「文件共享」窗口的「共享给我的」里打开 9101，浏览窗口里选中 `data` 点「下载」，选目录框由脚本填路径。两端日志据实证：2222 带 P2P 会话号（`share transfer …: p2p session … offered`），链路约 50 毫秒以局域网直连就绪，两端各等 49 毫秒决定走 P2P，控制通道与 2 条数据通道都是 P2P 通道；3 个文件、25166824 字节约 3.4 秒传完，传输列表显示「已完成」，落地文件 SHA256 与源文件逐个一致（截图 `snapshots/p2p_ui/fs_b_browser5.png`）。联调中发现测试环境的 fileserver 是 10 月 2 日的旧版，不认识文件共享的中转角色 9 / 10，浏览窗口连不上（与 P2P 无关），换成文件共享联调环境里 10 月 3 日的版本后正常。

**联调环境的两处调整**：

- 并行会话在 `t_user` 加了 `f_post`、在两张群表各加了一列，隔离测试库是旧副本，chatserver 只建缺失的表、不给已有的表补列，启动时报「no such column」或在加载用户时崩溃。当时新增了一个临时脚本，对照 `table_sqlite.sql` 给测试库补缺失的列。
- 宿主接口加了虚函数而不升版本，新 DLL 不能放进别的会话也在用的运行目录与测试目录。本期的 DLL 与测试程序都编到隔离的输出目录。测试工程复制插件 DLL 的目标在 Target 内部定义目录，命令行 `/p:` 指定不生效，编完要手工把自己的 DLL 放进测试目录。

**同期修正的测试问题**：P2PIO8（中转端口无人监听）断言「A 端一定发出 2305」、P2PIO9（中转报到被拒）断言「中转收到两份报到」都有竞争：两端几乎同时连中转失败时，先失败的一端发 2305，另一端可能先收到经服务端转来的这条，按「对端已关闭」收尾而不再自己发（`P2PLinkSession::OnServerClose`）。机器负载高时出现（P2PIO9 实测中转只收到 1 份报到）。P2PIO8 改为要求至少一端发出原因为「建链失败」的 2305，P2PIO9 改为要求 1～2 份报到；改后负载下连跑 18 次全部通过。

**测试进程里偶发崩溃的原因**：本期连跑用例时，FSD7、FSD11（文件共享模块）在机器负载高时约 5% 的概率以 SIGSEGV 失败，挂调试器时不出现。测试程序新增环境变量 `FLAMINGO_TEST_MINIDUMP_DIR`（`Source/Tests/TestCrashDialogSetup.cpp`，第一次访问冲突时写一份带已卸载模块清单的 minidump），三份转储的崩溃地址都是 `<Unloaded_HuayuPYTSF.dll>+0x6425c`：桌面上的第三方输入法（华宇拼音的 TSF 模块）在测试窗口获得焦点时载入测试进程、窗口销毁后被卸载，它留在消息队列里的消息随后被用例抽取分发，跳进已卸载的代码。与本期改动无关。测试程序启动时改为禁用输入法（`ImmDisableIME` 与 `ImmDisableTextFrameService`，作用于全进程；用例都用 `WM_CHAR` 投递文字，不需要输入法）。改后在同样的负载下连跑 40 次没有再崩溃，也没有写出转储（改前同条件下 40 次崩溃 3 次）。期间 FSD10 有 1 次断言失败（不是崩溃），原因尚未查清，据推测与本期无关，记为 `bugs.md` BUG-82。

### 14.23 第 6 期第二项：远程桌面走 P2P（2026-10-04）

用户说「继续」之后，第 6 期第二项选了远程桌面（风险最小：画面与控制命令原样搬到可靠通道上，不涉及音视频的丢包处理）。

**6b-0 复用层：对方先发、本方后打开的通道**。写远程桌面接入时查出一个第 4 期遗留的问题，据实证（代码）：`P2PChannelMux::OnPacket` 对本方还没打开的通道直接丢弃报文，而中转路径上的可靠通道不经 KCP、没有重传（`P2PReliableChannel::Send` 的 `!m_overKcp` 分支），对方先发出的数据就永久丢了，字节流的长度字段还会因此错位。两端各自打开通道、先后没有保证：文件夹传输的发送端连上通道就发清单，远程桌面的被控端连上就推关键帧。FP2PL2（强制中转传文件夹）能通过，据推测是因为用例里接收端先启动。修法：

- 没打开的通道上的报文先暂存（总量上限 `kP2PEarlyDataLimitBytes` 16 MiB，超出的丢弃），本方打开该通道时登记为待补交，`NextTimerMs` 返回 0；IO 线程下一次 `OnTimer` 按到达顺序补交（事件只在 IO 线程上分发，`Open` 在业务线程上调用，不能直接交出）；
- 等着补交的通道上新到的报文排在暂存的后面，保持顺序；
- `P2PLinkSession::OpenChannel` 打开后有待补交的数据时 `RequestWake`，让 IO 线程立即重新计算定时。

用例 P2PCH8：中转路径与 UDP 路径上，打开前收到的数据暂存、打开时不交出、下一次 `OnTimer` 按原顺序补交，之后的数据接在后面；类型不一致的暂存报文补交时丢弃；暂存总量不超过上限。

**6b-1 服务端**：`ChatSession::onRemoteDesktopResponse` 在被控端接受、下发 1716 之前，以控制端为发起方、被控端为应答方，为远程桌面会话号建用途为 `rd` 的 P2P 会话（`createForFeature`）；建成时两帧 1716 带同一个 `p2psessionid`（`RemoteDesktopProtocol::buildRelayInfo` 新增可选参数，为空时与原来逐字相同），2302 排在 1716 之后；建不成只记日志。用例 RDP8。WSL 全量 1025 例通过。

**6b-2 各业务共用的链路层**：远程桌面的关键帧约 4 MB（1080p、16 位色），超过文件夹中转的应用帧上限；它的退路是 imgserver 中转而不是 fileserver。于是把 `FolderP2PTransport` 里与业务无关的部分抽成 `Source/Feature/P2P/P2PLinkBroker.h/.cpp`：`P2PLinkSlot`（链路槽）、`P2PStreamChannel`（帧通道，单帧上限与等可写的时限由业务指定）、`P2PLinkBroker`（一个用途的业务方，`ClaimSlot` 按功能会话号认领槽）。文件夹传输与文件共享改为建在它上面：`FolderP2PLinkSlot` 是 `P2PLinkSlot` 的别名，`FolderP2PLinkBroker` 继承 `P2PLinkBroker` 只加 `CreateFactory`，`FolderP2PLane` 只做接口转换。抽取时顺带改了两处：

- 过期或被替换的未认领槽改在业务方的互斥量之外释放：槽析构时关闭链路，`IP2PLink::Close` 要等 IO 线程上正在进行的回调结束，而那个回调可能正等着这把互斥量；
- 帧通道只有打开成功才在槽上登记，关闭时只注销自己登记过的通道号：原来打开失败（通道号重复）的那个对象关闭时会把别人的登记一并删掉。

链路槽新增 `CloseLink`：业务结束时立即关闭链路（服务端随之结束 P2P 会话），引用保留到槽析构，别的线程手里已经取到的链路指针仍然有效。用例 FP2P6；FP2P1～5 不改。新文件编进主程序、单测、文件共享 DLL 与媒体 DLL（`gen_media_vcxproj.py`）。

**6b-3 宿主接口**：`IMediaHost` 末尾新增 `GetP2PService()`（与语音消息的会话协调过插入位置），exe 的 `CMediaHostAdapter` 转交 `CP2PHost::GetService()`；按用户的规定不升 `kApiVersion`，exe 与 FlamingoMedia.dll 须一起编译，已通知并行会话。

**6b-4 媒体 DLL**：

- `RemoteDesktop/RdTransport.h/.cpp`：远程桌面一端的传输。有链路槽时分段等链路结果（最多 20 秒，每 100 毫秒检查取消标志），就绪就在通道 1 上打开帧通道（单帧上限 64 MiB）；链路失败、超时或通道打不开时连 imgserver 中转；没有链路槽时直接连中转，与原来相同。两条路径对上层都是 `RdRelayClientHandler`。关闭时调用 `CloseLink`。
- 连接从 UI 线程挪到工作线程：被控端在推流线程上连接，连上再抓屏；失败时向控制器投 `kMsgRdStreamerConnectFailed`（`WM_APP + 0xB0`），控制器结束会话、停推流、关提示条并提示用户。控制端的观看窗先显示，在自己的连接线程上连接，结果经 `WM_RD_VIEWER_TRANSPORT`（`WM_APP + 0xB1`）交回 UI 线程，连上后请求关键帧；失败时提示、发 1714 并关窗。两个通知都带序号，上一次会话迟到的通知丢弃。两个偏移已登记进开发约定文档的占用清单（0x96 已被语音消息占用，改用 0xB0 段）。
- `CRdController`：`Init` 时经宿主取 P2P 服务登记用途 `rd`（环境变量 `FLAMINGO_RD_P2P` 为 0 时不登记，诊断用），`Shutdown` 注销；1716 带 `p2psessionid` 时按远程桌面会话号认领链路槽，连同中转参数交给推流器或观看窗。
- 用例 RD10（1716 解析 `p2psessionid`）、RDT1～5（`[rdtransport]`：已就绪走 P2P 且整帧收发、本端关闭立即关链路、等待中就绪、失败或无槽改连中转、等待中取消不再连中转）。

**6b-5 联调**：`FolderP2PLiveTests.cpp` 的单端封装加了用途 `rd` 的业务方，新增 RDL1（局域网直连，两端同时连接，被控端发 4 MiB 关键帧与增量帧、控制端发请求关键帧与输入命令，各自按序收到）与 RDL2（两端强制中转，被控端先连上并立即推帧，控制端 500 毫秒后才打开通道，靠 6b-0 的暂存补交完整收到）。

**验证**（2026-10-04，单测程序、文件共享 DLL 与媒体 DLL 都在隔离目录编译；隔离的联调服务端换上新编的 chatserver）：

- 单测：`[p2pchan]`、`[folderp2p]`、`[rdtransport]` 19 例；P2P DLL 各层、`[p2phost]`、`[mediadll]`（加载新编的媒体 DLL，宿主给的 P2P 服务为空）、`[filesharedll]`、`[msgid]`、`[filters]` 124 例；名称以 RD 开头的 77 例（含 RD10），全部通过。
- 联调：RDL1、RDL2 通过并连跑 3 次稳定；FP2PL1～6 回归通过。服务端日志据实证：两端 2301 报到 `{"purposes":["fileshare","rd","folder"],"version":1}`，`remote desktop p2p session: … code: 0`；客户端日志里两端都认领了 `rd` 链路槽、在通道 1 上打开帧通道并选择走 P2P；RDL2 中被控端在 15:21:35.293 打开通道，控制端在 15:21:36.213 才打开，仍完整收到 4 MiB 关键帧；收尾时两端都以「业务结束」关闭链路。
- 服务端：WSL 全量 1025 例通过，`RDP*` 8 例通过；Windows 隔离编译 chatserver 通过。

**编译中遇到的两处问题**：观看窗在全局命名空间，`Start` 的参数类型漏写 `remotedesktop::`；并行会话中途重编了 balloonui（`DuiScrollView` 新增 `HitTest`），隔离目录里复制的旧静态库要换成新的才能链接。另外测试文件里的变量名 `small` 被 Windows 头文件的 `#define small char` 替换，编译出错（a3 发现），改了名。

**还没做的**：远程桌面的界面联调（被控端会采集真实屏幕、把控制端的鼠标键盘注入真实桌面，在共用桌面上做之前须征得用户同意）。

### 14.24 第 6 期第三项：一对一通话走 P2P（2026-10-04）

用户定通话按方案原文走数据报通道（没选「可靠通道原样迁移」）：音频丢了就丢、视频整帧丢失时请对方补关键帧，响铃阶段就开始建链。

**6c-1 服务端**：`ChatSession::onCallInvite` 建好通话会话、被叫在线时，以主叫为发起方、被叫为应答方为通话会话号建用途为 `call` 的 P2P 会话；建成时邀请应答（给主叫）与来电推送（给被叫）带同一个 `p2psessionid`（`CallProtocol::buildInviteResponse` / `buildInviteNotify` 新增可选参数，为空时与原来逐字相同），2302 排在两者之后。与方案原文的差异：P2P 会话号不放在接听后下发的中转参数里，而是在响铃阶段就交给两端，两端据此先认领链路槽；通话没接通就结束（拒绝、取消、超时、关窗）时由客户端放弃这个会话号、关闭链路，服务端收到 2305 即结束 P2P 会话，不必在五六条结束路径上各补清理代码；客户端掉线时按原有的断开处理。用例 CP8、CP9，WSL 下 CP1～9 通过。

**6c-2 通用层的数据报通道**：链路槽上登记的通道抽象为 `IP2PSlotChannel`，帧通道与新的 `P2PDatagramChannel` 都实现它（`AddChannel` 增加通道类型参数）。数据报通道一条消息原样整条交给链路（DLL 按路径分片，丢一片整条丢弃），收到的消息由自己的转交线程交给业务，排队超过上限丢最旧的并计数。业务方新增 `Abandon`：放弃某个会话号后，已就绪的链路立即关闭，之后 DLL 才交来的链路直接关闭、不进链路槽（响铃阶段对方很快拒绝时链路往往还没建好）。用例 FP2P7、FP2P8。

**6c-3 通话的 P2P 消息格式与丢包处理**（`FlamingoMedia/Call/CallP2PFraming`，纯逻辑）：消息头 14 字节（类型、标志、序号、会话相对时间戳，网络字节序），类型有音频、视频（标志里带关键帧）、关键帧请求。`CallAudioSeqTracker` 按序号算出丢了几个包（最多补 3 帧，即 60 毫秒），迟到、重复的丢弃；`CallVideoLossGuard` 在序号断档或会话开始时丢弃非关键帧直到关键帧到达，并按 300 毫秒间隔请求关键帧；`CallKeyframeForceLimiter` 让发送端两次强制关键帧至少隔 500 毫秒。用例 CPF1～5（`[callp2p]`）。

**6c-4 媒体 DLL**：

- `CallVideoEngine::RequestKeyFrame`：任意线程上只置标志，由采集线程在编下一帧之前调用编码器的 `ForceKeyFrame`（编码器只能在采集线程上使用）。
- `CallMediaSession`：`Start` 多一个链路槽参数。连接线程有链路槽时先分段等链路（最多 20 秒，会话停止时立即放弃），就绪就在通道 1 上开数据报通道，否则连 imgserver 中转。P2P 路径上音频一个包一条消息、视频一整帧一条消息，各带序号；收到的音频先为丢掉的包调用解码器的丢包补偿再解码，视频按丢帧规则交给解码线程或丢弃、需要时发关键帧请求，收到关键帧请求时让编码器出关键帧。`Stop` 先关数据报通道再以「业务结束」关闭链路。`IsRunning` 的含义不变（语音消息的「通话中不能录音」依赖它）。
- `CCallController`：`Init` 时登记用途 `call`（环境变量 `FLAMINGO_CALL_P2P` 为 0 时不登记）；邀请应答、来电推送带 `p2psessionid` 时按通话会话号认领链路槽，接通时交给媒体会话；拒绝、取消、关窗、状态推送结束、登出时放弃会话号。
- `CallProtocolClient` 解析两处 `p2psessionid`，用例 CC10。

**6c-5 联调**：`FolderP2PLiveTests.cpp` 的单端封装加了用途 `call` 的业务方，新增 CL1（局域网直连）、CL2（强制中转）：真实的呼叫信令，两端认领槽、接听，在通道 1 上开数据报通道，A 发 50 个音频消息与一帧 300 KB 的视频关键帧、B 全部按序收到，B 发关键帧请求、A 收到；CL3：被叫收到来电后放弃会话号并拒绝，主叫一端的链路槽随后进入断开或失败，不会一直挂着。媒体会话本身（设备、编解码）不在单测里跑，靠编译、纯逻辑用例与界面联调覆盖。

**验证**（2026-10-04，单测程序、文件共享 DLL、媒体 DLL、chatserver 都在隔离目录编译，隔离的联调服务端换上新编的 chatserver）：

- 单测：`[callp2p]`、`[folderp2p]`（FP2P1～8）、`[rdtransport]`、`[p2pchan]`、`[mediadll]`、`[msgid]`、`[filters]` 53 例，名称以 CC 开头的通话协议 40 例（含 CC10），全部通过。媒体 DLL 编译通过（含媒体会话与控制器的 P2P 代码）。
- 联调：CL1～CL3 通过并连跑 3 次稳定；RDL1/RDL2、FP2PL1～6 回归通过。服务端日志据实证：`call p2p session: … code: 0`，两端在通道 1 上打开数据报通道；CL3 中被叫在链路建好之前就放弃了会话号，链路建好时被立即关闭（`P2P link ready after the business abandoned it, closing`）。
- 联调用例第一版的问题：CL1 挂断后等被叫的状态推送，取到的是接听时就排在队列里的「已接通」，随即断开连接，服务端还没处理挂断，通话按掉线宽限期保留，后面两条用例呼叫时得到「对方忙」。改为一直等到「已结束」。

**还没做的**：通话的界面联调（真实的麦克风、摄像头与编解码；在共用桌面上做之前须征得用户同意）。媒体会话里数据报路径的收发衔接目前只经过编译与纯逻辑用例验证。

### 14.25 第 6 期第四项：单聊屏幕共享走 P2P（2026-10-04）

用户选了「可靠通道整体搬迁」：发起人与那一名观看者之间开一条可靠帧通道，原样承载房间包体（画面、声音、标注、控制，与 1860 的包体相同），收发两端的解帧、标注、声音代码不动；房间中转里由服务端做的三件事（改写座位号、新成员报到时请求关键帧、给慢观看者丢非关键帧）改由发起人一侧做。群聊共享不变。

**6d-1 服务端**：`ScreenShareService::handleJoin` 在单聊共享里有观看者**新**加入时，经新接口 `IScreenShareP2PLinker`（`ChatSession` 的 `ChatSessionShareP2PLinker` 实现）以发起人为发起方、为功能会话号「房间号#座位号」建用途为 `screenshare` 的 P2P 会话；建成时会话号记进成员（`ScreenShareMember::m_p2pSessionId`），发给这名观看者的 1726 与房间快照 1725 里他那一项都带 `p2psessionid`；2302 由 `onScreenShare` 在发完应答与 1726 之后发出。重复加入（P2P 中途断开后观看者回到房间中转换令牌）不再建。用例 SSP2P1（协议）、SSP2P2（管理器），WSL 下屏幕共享相关 54 例通过。

**6d-2 客户端解析**：`ShareRelayInfo` 与 `ShareMember` 各加 `m_p2pSessionId`。用例 SSP2PC1。

**6d-3 观看端**（`ShareViewerReceiver`）：`Start` 带链路槽时改在连接线程上进行（等链路最多 20 秒，不占 UI 线程），就绪就在通道 1 上开帧通道，帧通道的转交线程代替中转的接收线程（通用层的帧通道回调新增 `OnStreamThreadStart` / `OnStreamThreadExit`，观看端在这两处初始化与释放 COM、Media Foundation，解码器只在一条线程上用）；不成就连房间中转。P2P 中途断开时由回退线程先等转交线程结束、再改连房间中转（1726 的令牌 5 分钟内有效，过期报 `share_viewer_relay_lost`，与原来中转断开的提示相同）。观看者发出的声音、标注、摄像头经当前路径发出。

**6d-4 发起人一侧**：

- `ShareP2PRoute`（纯逻辑）：`ShareRewriteSenderSeat` 把观看者经 P2P 发来的包体的发送者座位改写为他的座位；`ShareP2PVideoGate` 在链路积压超过 1 MiB 时让视频进入「等关键帧」、非关键帧不发，每 2 秒至多请求一次关键帧（与 imgserver 的规则与参数相同）。用例 SHP2P1、SHP2P2（`[sharep2p]`）。
- `ShareHostStreamer`：`SetP2PViewer` / `ClearP2PViewer` 登记或撤下一名 P2P 观看者；工作线程每一轮检查链路，就绪就开帧通道，并按「服务端发来关键帧请求」的同一条路径出关键帧、通知控制器补发标注、让摄像头出关键帧；通道开着时全部包体经它发给观看者（视频经丢帧规则），否则经房间中转；房间中转连接始终保持，链路断开即回到中转。
- `CShareController`：`Init` 登记用途 `screenshare`（环境变量 `FLAMINGO_SHARE_P2P` 为 0 时不登记）；观看者收到带 `p2psessionid` 的 1726 时认领链路槽交给观看窗；发起人在每次 1725 里按快照同步 P2P 观看者（新出现的认领交给推流管线，离开的撤下并放弃会话号）；结束时放弃全部会话号。

**6d-5 联调**：`FolderP2PLiveTests.cpp` 新增 SSPL1（局域网直连）、SSPL2（强制中转）、SSPL3（P2P 中途断开）：A 用合成画面推流，B 加入，1726 与 A 收到的 1725 里 B 一项带同一个 `p2psessionid`；B 经 P2P 收到尺寸与颜色正确的画面；B 发的标注（帧头座位号故意写错）到 A 时已改写为 B 的座位；暂停与继续的状态送达；SSPL3 中 A 撤下通道后 B 改连房间中转并继续收到画面。

**验证**（2026-10-04）：单测 120 例（`[sharep2p]`、非联调的 `[screenshare]` / `[shareroom]`、以及前三项的相关标签）通过；SSPL1～3 通过并连跑 3 次稳定，CL1～3、RDL1/2、FP2PL1～6 回归通过。日志据实证：`screenshare p2p session: feature: ss-…#1 … code: 0`，观看端 31 毫秒决定走 P2P，发起人「p2p channel to seat 1 open」；SSPL3 中观看端「p2p link lost … falling back to the relay」后「joined room … as seat 1」。联调用例第一版发起共享被拒（错误码 3）：第 15 期起 1720 的标题必填，用例补上标题。

**还没做的**：屏幕共享、通话、远程桌面的界面联调（在共用桌面上采集真实屏幕、使用麦克风与摄像头、注入鼠标键盘，做之前须征得用户同意）。

### 14.26 第 6 期界面联调（2026-10-04 傍晚）

用户同意后在共用桌面上做，环境是两个 x64 测试客户端（A = MY0127、B = MY0308）连隔离的联调服务端，程序在隔离目录编译后由脚本放进测试目录。为不碰用户的真实内容与设备：屏幕共享只共享品红色的 `SHARE-TEST` 测试窗口；观看窗、通话画面、远程桌面画面只在内存里统计像素，不保存截图；通话设 `FLAMINGO_CALL_NO_AUDIO=1`、账号配置 `Mute=1`（不开麦克风与扬声器、来电与回铃不出声）；远程桌面的观看窗一出现就挪到屏幕外，控制端不发鼠标键盘命令。两端启动后都报到了五个用途（服务端日志 `p2p register … ["call","fileshare","folder","rd","screenshare"]`）。

**屏幕共享（通过）**：A 在单聊窗点「共享屏幕」、选测试窗口，B 从右下角提示加入。A 日志「p2p channel to seat 1 open, media now goes over p2p」，B 日志「path=p2p, waited=0 ms」，链路为局域网直连（`path 1`）；观看窗 14～15 fps，画面区品红占比 0.818（四周留边）。B 关窗离开后两端链路正常关闭并放弃会话号；B 从聊天卡片重新加入，服务端建了新的 P2P 会话，仍走 P2P。A 结束共享后 B 收到房间结束（原因 1）并提示「屏幕共享已结束」，没有误走回退中转。

**通话（走 P2P 通过，另查出一个原有缺陷）**：语音与视频通话都在振铃期间建好链路（「link already ready=1」），接通后两端「path=p2p, waited=0 ms」，数据报通道打开；视频通话中 A 日志「MfH264Decoder: NV12 output 1280x720」，即解出了 B 经 P2P 发来的视频（两个客户端在同一台机器上抢同一个摄像头，B 先打开，A 读取失败（0xC00D3704）并显示「摄像头不可用」，所以只有 B→A 一个方向有画面）。查出的缺陷与 P2P 无关、迁移进 DLL 之前就有：通话中本端点「挂断」按钮只发挂断信令就销毁窗口，不请求通话控制器停媒体会话；服务端只把结束推送发给对方（`ChatSession::onCallHangup`），于是挂断的一方采集线程一直运行（视频通话时摄像头一直开着），P2P 链路槽与「通话中可共享屏幕」的状态也没有清掉，直到下一次通话开始才被停掉（实测 A 在下一通电话开始时才记「stopped relay session, session=…-4」）。已修：通话窗新增 `CloseByUser()`，「取消」「拒绝」「挂断」三个按钮与标题栏关闭按钮都经它按形态发信令（纯函数 `call::PickUserCloseSignal`，用例在 `CallDlgLayoutTests.cpp`）、投递 `kMsgCallStopMedia`、销毁窗口。19:46 在真实客户端上复验（语音通话接通 5 秒后 A 点「挂断」），据实证 A 端依次记「CallDlg: closed by user, mode=2, signal=3」「call window closed, stop media session」，4 毫秒后「stopped relay session」「screenshare: call with 5308 ended」。

**远程桌面（查出并修复观看端画面反复消失的缺陷，复验通过）**：A 点「远程桌面」、B 接受后两端都走 P2P（A「path=p2p, waited=0 ms」，B「waited=26 ms」），B 用 DXGI 采集 1920×1080、每约 3.3 秒发一次「BITMAPINFO + 首屏全量」（16 位色约 4 MB），一直没有发送失败；但 A 的观看窗每次去看都停在「正在连接…」。新增联调用例 RDL3（`[.live][rdp2pstream]`）按推流器的真实节奏持续推流 12 秒（每 200 毫秒一帧，每 10 帧一次 BITMAPINFO 与 4 MiB 关键帧，其余是 200 KB 的增量帧），Win32 与 x64 的单测下都是 66 帧全部按序收齐，复现不了；于是给观看窗加了诊断日志（首帧就绪时记耗时，连上 5 秒后仍无画面时每 5 秒记收到的帧数、字节数与链路统计），推流端每 10 秒记一次发送统计，经用户同意在真实客户端上再做一次。据实证（19:45 那次的日志）：帧一直在到达，A 连上 1.8 秒后就拼出了第一帧，30 秒收到 98 帧约 40 MB；但「first picture ready」每约 3.3 秒出现一次、中间穿插「no picture yet」——解码器每收到一个 BITMAPINFO 就把「已有首屏」清掉（`RdScreenDecoder::applyBitmapInfo` 置 `m_hasFirst = false`），要等后面 4 MB 的首屏到齐才恢复。每个 4 MB 首屏从 B 发出到在 A 端拼齐约 1.2～1.5 秒（如 B 10.587 发出、A 11.782 拼齐），链路吞吐约 3 MB/s、与联调用例相当（B 端每秒约 1.5 MB 的统计是推流器产生的数据量，积压大多为 0，链路并没有跑满），在约 3.3 秒的重发周期里观看端约四成时间没有画面、画面闪烁，标题栏每秒刷新一次时常落在没有画面的时段；本机中转走 TCP，两帧几乎同时到，空档只有几毫秒，从来没有显出来。修法两处：解码器收到与当前逐字节相同的 BITMAPINFO 且已有画面时保留画面与就绪状态（RDDEC5；尺寸变化仍按新尺寸重建，RDDEC6）；推流器走 P2P 时不再定时重发关键帧，只在首轮与控制端请求时发（`RdTransport::NeedsPeriodicKeyframes`，RDT6），周期重发原本是给中转路径上晚连上的控制端重新对齐用的，P2P 帧通道可靠有序、未打开前的数据由 DLL 暂存补交，控制端连上后也会请求一次关键帧。诊断日志保留。用例 RDDEC5、RDDEC6、RDT6 先确认红灯再改，改后与 `[calldlg]`、RDL1～3 一并通过。20:11 经用户同意在真实客户端上复验，据实证：B「push loop started, path=1, periodic keyframes=0」，33 秒只发 1 次关键帧、共约 8.7 MB（修复前同样时长约 40 MB），链路积压一直为 0；A 连上 1.75 秒后「first picture ready」只出现一次，再没有「no picture yet」，观看窗标题持续显示「1920×1080 4 fps 帧 6 KB」。

**联调中的其它情况**：用户 18:02 前后操作过测试客户端 B（从托盘与主面板系统菜单打开了「文件共享」），18:11 从托盘退出了两个测试客户端；之后的排查先用无界面的联调用例，19:44～19:47 经用户同意、等语音会话用完桌面后再做了一次约 3 分钟的实测（远程桌面诊断与挂断复验），20:10～20:12 又做了一次远程桌面修复后的复验，每次做完即让两个客户端正常退出。

### 14.27 拥塞控制：无丢包长肥链路的排队延迟（2026-10-05）

**原因**（据实证，诊断用例 P2PCCD1、P2PCCD2 的逐时段输出）：启动阶段按 2.89 倍增益发送，结束时瓶颈队列里积压了约一个带宽时延积的数据；排空阶段本该把它放掉，但 `P2PBbrCc` 给排空阶段设了最多停留 2 轮的上限（`kDrainMaxRounds`，用来防止成片丢包时一直退不出来），往返 30 毫秒的链路上两轮放不完。进入带宽探测阶段后在途上限是 2 倍带宽时延积，正好容得下这部分积压，又没有别的机制把它排掉，于是稳态里一直留着约一个往返时延（约 30 毫秒）的排队，只有每 10 秒一次的最小往返时延探测时才短暂清空。

**修改**：`kDrainMaxRounds` 从 2 改为 6。在模拟链路上试过：取 3 没有效果，取 4 只排掉一半，取 5 起能排干净；取 6 多留一轮余量。成片丢包的情况仍受这个上限保护。另试过「只在因丢包退出启动阶段时才限制轮数」，没有效果：无丢包链路上启动阶段末尾也会因为误判的重传被记为丢包，同样走了限制轮数的路径，已撤回。

**结果**（`[p2pcc]` P2PCC1，六种条件，与第 14.19 节同一模拟链路）：

| 条件 | 带宽利用率（改前） | 平均排队延迟（改前） |
| --- | --- | --- |
| 千兆局域网，往返 1 毫秒 | 93.7%（96.2%） | 1.3 毫秒（1.3） |
| 200M Wi-Fi，往返 5 毫秒，丢包 0.5% | 88.0%（88.0%） | 1.7 毫秒（1.7） |
| 100M，往返 30 毫秒，无丢包 | 87.1%（87.8%） | 6.5 毫秒（30.4） |
| 100M，往返 30 毫秒，丢包 0.1% | 85.9%（85.6%） | 6.1 毫秒（7.1） |
| 20M，往返 60 毫秒，丢包 1% | 84.3%（84.3%） | 8.0 毫秒（8.0） |
| 8M，往返 120 毫秒，丢包 5% | 62.3%（62.3%） | 1.5 毫秒（1.5） |

千兆局域网的利用率下降约 2.5 个百分点（排空阶段多停了几轮），仍高于验收下限 90%。无丢包 100M 链路的排队上限随之从 40 毫秒收紧到 15 毫秒。

**剩余的特点**：P2PCCD1（按 15 秒的数据量传输，实际用了约 16 秒）的逐时段输出显示，排空结束后（第 1 秒）排队只有 2～3 毫秒，之后每 0.5 秒上涨约 0.7 毫秒，到第 11 秒左右被最小往返时延探测清空，再从头上涨，全程平均 8.8 毫秒。据推测是测得的瓶颈带宽比实际略高（按上涨速度折算约 0.15%），按它发送时队列缓慢积累；这比改前稳定在一个往返时延好得多，暂不处理，以后要进一步压低时从带宽估计的计算查起。

**测试**：新增 P2PCC4（无丢包 100M、往返 30 毫秒的链路传 8 秒，第 1 秒之后各 0.5 秒时段的平均排队延迟不超过往返时延的三分之一，即 10 毫秒；改前 29.3 毫秒、红灯，改后 8.1 毫秒。模拟链路的随机数种子固定，结果每次相同）；诊断用例 P2PCCD1（传 15 秒，每 0.5 秒打印一行排队延迟、阶段、增益与在途数据）与 P2PCCD2（传 2 秒，每 20 毫秒打印一行，看启动到带宽探测的过程）标签为 `[.][p2pccdiag]`，默认不运行、只打印不断言。全部 P2P 标签（85 个用例、1687 个断言）通过。

**同期修正的测试代码**：组合运行全部 P2P 标签时 P2PH1 偶发失败一次（`bugs.md` BUG-97）。用例里记录 DLL 日志的宿主追加与读取都不加锁，而 DLL 初始化时启动的引擎 IO 线程一启动就写日志，存在数据竞争（据实证）；已给 `RecordingHost` 与 `FakeNetwork` 加锁。它是不是那次失败的原因仍只是推测。
