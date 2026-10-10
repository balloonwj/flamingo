# 聊天消息 Pin 接入方案与开发计划

> 作者：balloonwj@qq.com　起草日期：2026-08-04
> 用途：记录给 PC 客户端聊天消息增加「Pin」功能的整体方案、分期路线与开发步骤。后续每步开工前据此细化、收尾后回填实际结果。

---

## 1. 背景与目标

会话里总有那么几条消息事后要反复回头找——一个会议链接、一份定稿文件、一句拍板的结论。现在只能靠翻历史或关键字搜索，翻到哪算哪。本方案给消息加一个「Pin」标记：钉住之后，这条消息就固定出现在「消息记录」窗口的 Pin 分类里，一眼可见。

最终形态：在聊天气泡上右键即可 Pin 一条消息或取消 Pin；在「消息记录」窗口里同样能 Pin / 取消；被 Pin 的消息集中在该窗口的「Pin」分类下。

以下口径均已与用户逐条确认，实现时不再另行请示：

| 决策点 | 取值 | 详见 |
| --- | --- | --- |
| 可见范围 | **会话内共享** —— 单聊里对方也看得到，群里全体成员都看得到 | §3.1 |
| 存储位置 | **服务端** —— 换台机器登录、重装客户端后 Pin 仍在 | §3.1 |
| 群里谁能 Pin | 全体成员 | §4.4 |
| 群里谁能取消 Pin | Pin 的操作者本人 + 群主 / 管理员 | §4.4 |
| 每会话 Pin 数量上限 | **不限制** | §4.4 |
| 一次拉取的返回条数 | 限制为最近 200 条（Pin 总数不限，只限单次应答体量） | §4.3 |
| Pin 分类下的排序 | 消息时间倒序，与其它六个分类一致 | §6.6 |
| 消息记录窗的分类形态 | 第七枚单选胶囊，不与类型胶囊叠加 | §7.3 |
| 推送落空（本地没有该消息）的处理 | 命中 0 行时自动补拉一次消息体 | §4.3 |
| 聊天窗顶部「已 Pin N 条」提示带 | 首轮不做，已在 P5 单独一轮完成 | §8 P5 |
| 测试覆盖范围 | 界面布局契约测试省掉，其余照做 | §9 |

---

## 2. 现状盘点（据实证，均已核对代码位置）

### 2.1 撤回是最贴近的参照物，整条链路可以照抄

Pin 与撤回的形状几乎一样：右键触发 → 发请求 → 服务端落库 → 向会话相关方推送 → 各端收到推送才改界面。撤回这条链路已经完整跑通，每一环都有现成写法可照：

| 环节 | 撤回的实现位置 |
| --- | --- |
| 气泡右键菜单 | `flamingoclient/Source/UI/BalloonChatWnd/ChatBubble.cpp`（菜单模型在 `ChatMessageMenu.cpp` 的 `BuildChatMessageMenuModel`） |
| 气泡 → 消息列表 | `ChatMessageList::RequestRecallBubble`——只把 msgid 投给宿主窗，自己不改界面 |
| 发请求 | `net/IUSocket.cpp` `CIUSocket::RecallMessage` |
| 请求 / 应答 / 推送编解码 | `net/MessageProtocolClient.cpp` 的 `buildRecallMessageRequest` / `parseRecallResponse` / `parseMessageRecalledNotify` |
| 收包分发 | `net/IUSocket.cpp` 的 `case cmd_recall_message`（应答）、`case cmd_message_recalled_notify`（推送） |
| 投递给 UI 的窗口消息 | `Session/UserSessionData.h` `FMG_MSG_V2_MESSAGE_RECALLED` |
| UI 入口 | `UI/BalloonMain/BalloonMainPanelNetHandlers.cpp` `CBalloonMainPanel::OnV2MessageRecalled` |
| 扇出三件事 | `UI/BalloonMain/BalloonMainPanel.cpp` `OnMessageRecalled`：本地库、已打开的聊天窗、会话列表预览 |
| 聊天窗改气泡 | `CBalloonChatWindow` / `CBalloonGroupChatWindow` 的 `OnMessageRecalled` → `ChatMessageList::RecallMessageByMsgid` |
| 本地库落标记 | `V2MsgStore::MarkRecalled`（`UI/BalloonHistory/V2MsgStore.h`） |
| 服务端处理 | `chatserversrc/ChatSession.cpp` `onRecallMessage`；落库 `MessageManager::recallMessage` |

其中两条口径值得原样继承：

- **界面改动一律等推送**，请求应答只用于失败时提示原因（见 `IUSocket.cpp` 中 `case cmd_recall_message` 的注释）。这样本端与对端看到的结果必然一致，Pin 沿用同一套，不做乐观更新。
- **单聊推送的 `cid` 按收件人视角分别构造**（`ChatSession.cpp` 的 `onRecallMessage`：给对端推 `cid=自己`，给自己推 `cid=对端`）。Pin 的推送必须照做，否则收到推送的一方对不上会话。

### 2.2 消息记录窗的类型分类本来就是可扩展的枚举

- `UI/BalloonHistory/V2MsgQuery.h` —— `HistoryTypeFilter` 目前 0~5（全部 / 文字 / 图片 / 文件 / 语音 / 链接）
- `V2MsgQuery.h` —— `TypeFilterToSqlFragment` 把枚举翻成 SQL 的 `WHERE` 片段
- `UI/BalloonHistory/HistoryWindowLayout.h` —— 六枚胶囊的控件 id 是连续的 510~515
- `BalloonHistoryWindow.cpp` —— 注释明写"类型胶囊：id 连续，510..515 → 类型枚举 0..5"

**加一个「Pin」分类就是加一个枚举值 + 一枚胶囊 + 一段 SQL 片段**，没有结构性改动。

### 2.3 客户端本地库没有补列机制——这是本方案唯一要新打的地基

`V2MsgStore::Open`（`UI/BalloonHistory/V2MsgStore.cpp`）只做三件事：`CREATE TABLE IF NOT EXISTS t_msg`、建两条索引。**没有任何 `ALTER TABLE`、也没有读写 `PRAGMA user_version`**。

这意味着：直接往 `kCreateTableSql` 里加一列，对**新装的客户端**有效，对**已经有 `ChatLog\msgv2.db` 的存量客户端无效**——`IF NOT EXISTS` 见表已存在就跳过，新列永远不会出现，随后所有带新列的 SQL 全部报 `no such column`，消息记录窗直接空白。

所以本方案必须先给 `CV2MsgStore` 补上"存量库缺列自动补齐"，再谈加列。

还有一个连带陷阱：`Upsert` 用的是 `INSERT OR REPLACE`，语义是**整行替换**。加了 pin 列之后，实时消息重复落库（同步回填与实时推送撞上同一条）会把已有行的 pin 标记冲成 0。这条必须一并处理，见 §3.3。

### 2.6 有个同名陷阱：1901~1903 是「收藏消息」

`Protocol.h` 已占用 `cmd_add_favorite`(1901) / `cmd_get_favorites`(1902) / `cmd_delete_favorite`(1903)，语义是把某条聊天记录存进"我的收藏"里跨会话查看。本方案的 Pin 是**把消息钉在它所属的那个会话里给全体参与者看**，两件事，<u>不得复用</u>，数据表也不要混。

（顺带：`cmd_set_favorite`(1215) 是收藏联系人 / 群组，又是第三件事，见 `docs/收藏接入方案.md`。）

### 2.7 客户端 SQLite 版本足够新

`Flamingo.vcxproj` 编入的是 `third-party/sqlite-src-3530100`，即 SQLite **3.53.1**。远高于 UPSERT（`ON CONFLICT ... DO UPDATE`）所需的 3.24，§3.3 可以放心用。

---

## 3. 数据库设计

### 3.1 服务端新建 `t_message_pin`

```sql
//消息Pin表（会话内共享）：一行表示某条消息被钉在它所属的会话里。
//主键取 f_msgid —— msgid 全局唯一，且经 t_chatmsg 的 f_ctype/f_from/f_to 就能定位它属于
//哪个会话，本表不必再存一份会话键。单聊的 cid 是"各人视角各不同"的（我看到的 cid 是对方、
//对方看到的 cid 是我），存进这张共享表反而两边对不齐。
//独立成表而不往 t_chatmsg 加列：这是只有极少数消息才有的附加状态，与 t_message_deleted、
//文件收取状态表 t_file_ack 同思路。
CREATE TABLE IF NOT EXISTS  t_message_pin  (
         f_msgid     bigint(20) NOT NULL COMMENT '被Pin的消息msgid',
         f_operator  bigint(20) NOT NULL DEFAULT 0 COMMENT '执行Pin的用户userid',
         f_pin_time  bigint(20) NOT NULL DEFAULT 0 COMMENT 'Pin时间,Unix毫秒',
         PRIMARY KEY ( f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8
```

取消 Pin 走 `DELETE`，不留软删标记——共享状态没有"谁取消过"的历史价值，留着反而要在每处查询里多带一个条件。

`f_operator` 不只是留作日后显示"某某钉了这条消息"，**取消 Pin 的权限判断要用它**（见 §4.4）。

**列举某会话的 Pin 全集**靠与 `t_chatmsg` 联表，复用其现有索引（`table_mysql.sql` 里 `t_chatmsg` 的 `idx_from_to` / `idx_to_from`）：

```sql
-- 群聊
SELECT m.* FROM t_chatmsg m JOIN t_message_pin p ON m.f_msgid = p.f_msgid
 WHERE m.f_ctype = 2 AND m.f_to = ? AND m.f_recalled = 0
 ORDER BY m.f_create_time DESC LIMIT ?

-- 单聊（双向都要取）
SELECT m.* FROM t_chatmsg m JOIN t_message_pin p ON m.f_msgid = p.f_msgid
 WHERE m.f_ctype = 1 AND m.f_recalled = 0
   AND ((m.f_from = ? AND m.f_to = ?) OR (m.f_from = ? AND m.f_to = ?))
 ORDER BY m.f_create_time DESC LIMIT ?
```

`LIMIT` 的取值与"为什么要有它"见 §4.3；不带 `LIMIT` 的同一套 `WHERE` 另外做一次 `COUNT(*)` 拿总数。

### 3.2 三处建表定义都要改，漏一处就有一套库跑不起来

| 文件 | 改什么 |
| --- | --- |
| `flamingoserver/table_mysql.sql` | 新增上面的 `t_message_pin` 建表语句 |
| `flamingoserver/table_sqlite.sql` | 由脚本生成，不手改 |
| `flamingoserver/sqliteapi/SqliteSchema.cpp` | 由脚本生成，不手改 |

改完 `table_mysql.sql` 后重跑 `tools/gen_table_sqlite.py`，后两个文件一起更新。生成结果要检查两处：建表语句、`SCHEMA_TABLES` 表名清单。

### 3.3 客户端本地库 `t_msg` 补两列，并新建补列机制

新增两列：

```
pinned    INTEGER NOT NULL DEFAULT 0   -- 是否被Pin:0否 1是
pin_time  INTEGER NOT NULL DEFAULT 0   -- Pin时间,Unix毫秒(未Pin时为0)
```

配套三处改动：

**(1) 新增补列机制。** 在 `CV2MsgStore::Open` 的"建表之后、建索引之前"插一步 `EnsureColumnsLocked()`：用 `PRAGMA table_info(t_msg)` 读回现有列名，缺哪列就 `ALTER TABLE t_msg ADD COLUMN <列名> <定义>`。客户端只有一张表，内嵌一张 `{列名, 定义}` 小表（`V2MsgStore.cpp` 的 `kSchemaColumns`）即可，不必引入生成脚本。

顺序不能反：结构落后的存量库里，新索引要用的列可能还没补上，先建索引会直接报 `no such column`。

**(2) `Upsert` 从 `INSERT OR REPLACE` 改成显式 UPSERT**，只更新内容类字段，不碰 pin 列：

```sql
INSERT INTO t_msg (msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled)
VALUES (?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(ctype,cid,msgid) DO UPDATE SET
    clientmsgid=excluded.clientmsgid, fromid=excluded.fromid, msgtype=excluded.msgtype,
    content=excluded.content, search_text=excluded.search_text, is_link=excluded.is_link,
    timestamp=excluded.timestamp, recalled=excluded.recalled
```

不这么改的话，同步回填与实时推送撞上同一条消息时，后落库的那次会把 pin 标记冲掉（见 §2.3）。这是一处只在特定时序下才暴露的问题，必须有单测钉住。

**(3) 新增两个写接口：**

- `SetPinned(ctype, cid, msgid, bool pinned, int64_t pinTimeMs, bool& outHit)` —— 收到 Pin 推送后落标记，形态照 `MarkRecalled`：命中 0 行也算成功（本机没同步过这条历史属正常），但要经 `outHit` 把"到底有没有命中"如实告诉调用方。主面板据它决定要不要补拉消息体，见 §4.3。
- `ReplacePinnedSet(ctype, cid, const std::vector<int64_t>& msgids, int64_t minTimestampMs)` —— 在一个事务里，把该会话中 `timestamp >= minTimestampMs` 的行 `pinned` 清零，再把 `msgids` 里的各条置 1。用于与服务端对账。`minTimestampMs` 传 0 表示全量清零；为什么需要这个下界，见 §4.3。

`HistoryRecord`（`V2MsgQuery.h`）相应增加 `m_pinned` / `m_pinTime` 两个字段，`Upsert` 与 `QueryPage` 的列清单同步。

---

## 4. 协议设计

### 4.1 新增 `cmd_pin_message = 1518`

```
请求  {"ctype":1, "cid":"5081", "msgid":"91002", "pin":1}
应答  {"code":0, "msg":"ok", "ctype":1, "cid":"5081", "msgid":"91002", "pin":1}
```

`pin` 取 1 钉住 / 0 取消，一条命令兼管两个方向——与 `cmd_set_blacklist`(1213)、`cmd_set_conversation`(1513)、`cmd_set_favorite`(1215) 的既有写法一致。

应答回带 `ctype` / `cid` / `msgid` / `pin`，便于客户端在多条请求在途时对上号并给出准确的失败提示。

64 位 ID 一律以字符串编码进 JSON，沿用本协议既有约定。

### 4.2 新增 `cmd_message_pinned_notify = 1519`

```
推送  {"ctype":1, "cid":"5081", "msgid":"91002", "pin":1,
       "operator":"3001", "pintime":"1754300000000"}
```

- 单聊：向双方各推一条，**`cid` 按收件人视角分别构造**（照 §2.1 末尾那条口径）
- 群聊：向全体在线成员推，`cid` 取群 id
- 操作者本人也会收到，界面据推送才改——与撤回同口径
- `operator` 用于后续显示"某某钉了这条消息"；本轮界面上不显示，先落进协议免得以后再改报文

### 4.3 新增 `cmd_get_pinned_messages = 1520`

```
请求  {"ctype":2, "cid":"268435456"}
应答  {"code":0, "msg":"ok", "ctype":2, "cid":"268435456", "total":37,
       "msgs":[ {<与 cmd_sync_messages 应答同结构的完整消息体>,
                 "pin":{"operator":"3001","pintime":"1754300000000"}}, ... ]}
```

**这条命令不是可选项。** 消息记录窗的 Pin 分类查的是本地 SQLite，而本地库里只有"本机同步过的那些消息"。同事钉了一条三个月前的老消息，本机很可能压根没有那一行，Pin 分类就会漏掉它——用户看到的是"明明钉了却找不到"。

#### 单次返回条数上限

Pin 的**总数不限**（§4.4），但一次应答里不能什么都往回塞——每条都带完整消息体，一个被钉了上千条的群会把包体撑爆（`BINARY_PACKLEN_LEN_2` 虽有 256 MiB 上限，离谱的包体本身就是问题）。故服务端按消息时间倒序**最多返回最近 200 条**，同时用 `total` 字段带回真实总数。

200 这个数在服务端定义为具名 `constexpr`（如 `PIN_FETCH_LIMIT`），不得写成裸数字。超出的那些并没有丢——它们仍在服务端的表里，本地库里若有对应行也照常带着 pin 标记，只是不由这次拉取来补齐。

#### 拉取时机与对账

客户端在两个时机拉一次：**打开会话窗时**、**打开消息记录窗时**（`BalloonMainPanel.cpp` `OpenHistory`）。拿到后做两件事：

1. 把返回的完整消息体 `Upsert` 进本地库——补齐本地缺的那些行；
2. 调 `ReplacePinnedSet` 对账——把本地多出来的 pin 标记清掉（在别的设备上取消的、或推送因离线丢掉的）。

**对账的下界必须跟着截断走，否则会误清。** 如果应答被截断了（`total > msgs.size()`），本地那些"更老的 Pin"并不在返回集合里，无条件全量清零会把它们错杀。规则是：

- 未截断（`total == msgs.size()`）：`ReplacePinnedSet(..., minTimestampMs = 0)`，全量清零后置位；
- 已截断：取返回集合里**最老一条的 timestamp** 作为 `minTimestampMs`，只清这个时间点之后的 pin 标记，更早的一律不动。

这样既不会留下脏标记，也不会误清服务端确实还钉着、只是这次没返回的那些。

#### 推送落空时补拉

窗口开着的时候，别人钉了一条本机本地库里没有的老消息，`SetPinned` 会命中 0 行、推送等于落空。处理办法：`SetPinned` 的 `outHit` 为 false 时，主面板对该会话自动发一次 `cmd_get_pinned_messages` 把消息体补回来。

这不会形成循环：补拉是由**一次推送**触发的，补齐之后本地已有该行，不会再次落空；即便服务端也返回不了那条（消息已被删等异常），也只是这一次推送多花一个来回，不会重复触发。

### 4.4 权限与新增错误码

| 场景 | 谁能 Pin | 谁能取消 Pin |
| --- | --- | --- |
| 单聊 | 双方任一 | 双方任一 |
| 群聊 | 全体成员 | Pin 的操作者本人（`t_message_pin.f_operator`）+ 群主 / 管理员 |

群里"谁都能钉、但别人钉的只有他自己和管理员能取消"——放开钉是因为 Pin 本就是轻量标记，卡死会没人用；收紧取消是因为共享状态被旁人随手取消，原本钉它的人会莫名其妙。群主 / 管理员保留兜底权限。

**每会话的 Pin 数量不设上限**，故服务端不做计数校验，客户端也没有"超上限"这条提示路径。单次拉取的条数限制是另一回事（§4.3），那约束的是应答体量而不是用户能钉多少。

| 校验 | 不通过时 |
| --- | --- |
| `ctype` 为 1 或 2、`cid` 与 `msgid` 为正、`pin` 为 0/1 | `error_code_bad_request`(3) |
| 消息存在且未撤回 | `error_code_message_not_exist`(503) |
| 请求者是该会话参与方（单聊双方之一 / 群成员） | `error_code_not_group_member`(302) 或 `error_code_permission_denied`(6) |
| 取消 Pin 的权限（见上表） | 新增 `error_code_pin_not_allowed`(506) |

重复 Pin 一条已 Pin 的消息、取消一条没 Pin 的消息，都**幂等返回成功**，不重复落库也不重复推送——照 `onRecallMessage` 对已撤回消息的处理（`ChatSession.cpp`）。幂等的重复 Pin 不刷新 `f_pin_time`，也不改 `f_operator`：否则甲钉的消息被乙点一下"Pin"，取消权限就悄悄转到乙手里了。

### 4.5 与撤回的联动

消息被撤回时，服务端在 `recallMessage` 成功后顺手删掉该 msgid 的 Pin 行。

**不额外推 `pin=0`**：客户端本地库的所有查询恒含 `recalled=0`（`V2MsgQuery.h` 已写明），撤回推送到达时 `MarkRecalled` 一置位，这条自然就从 Pin 分类里消失了。少一条推送就少一处时序竞态。

反向不需要处理：Pin 一条消息不影响它能否被撤回。

### 4.6 本端删除消息与 Pin 的关系

本端删除（`cmd_delete_message`(1512) → `t_message_deleted`）是"从我自己视角隐藏"，Pin 是会话共享状态，两者不互相干扰：我删掉一条别人钉着的消息，只是我自己看不到，别人的 Pin 不受影响。服务端在 `cmd_get_pinned_messages` 里按现有惯例过滤掉请求者已删除的那些即可。

---

## 5. 服务端行为

### 5.1 新增 `chatserversrc/MessagePinManager.{h,cpp}`

单例，照 `GroupManager` 的形态，只碰数据库不碰网络：

- `pinMessage(msgid, operatorId, nowMs)` —— 已存在则幂等返回成功，且不刷新 `f_pin_time` / `f_operator`
- `unpinMessage(msgid)` —— 不存在则幂等返回成功；**权限判断不在这里**，由 `ChatSession` 先查 `getPinRecord` 再决定
- `getPinRecord(msgid, PinRecord& out)` —— 取回 operator 与 pintime，供取消权限判断
- `getPinnedMessages(ctype, cid, selfUserId, int limit, std::vector<MessageRecord>& out, int64_t& outTotal)` —— 走 §3.1 的联表查询，`out` 最多 `limit` 条，`outTotal` 为不带 `LIMIT` 的真实总数
- `removePinForMessage(msgid)` —— 撤回时调
- `removePinsForGroup(groupid)` —— 群解散时调

### 5.2 `MessageProtocol` 扩展（纯逻辑，进 doctest）

新增 `PinMessageRequest` / `GetPinnedMessagesRequest` 两个结构与四个编解码函数：`parsePinMessageRequest` / `buildPinMessageResponse` / `buildMessagePinnedNotify` / `buildGetPinnedMessagesResponse`。形态照同文件里撤回那三个（`MessageProtocol.h` 的 `parseRecallMessageRequest` / `buildMessageRecalledNotify` 等）。

### 5.3 `ChatSession` 增加两个 handler

在 `ChatSession` 的 cmd 分发里加 `cmd_pin_message` / `cmd_get_pinned_messages` 两个 `case`，实现 `onPinMessage()` / `onGetPinnedMessages()`。`onPinMessage` 的骨架照 `onRecallMessage`：取消息 → 幂等判断 → 分单聊 / 群聊两支做权限校验 → 落库 → 回应答 → 推送。

推送用现成的两个出口：群聊 `notifyGroupMembers`、单聊 `sendToUser` 各推一次（与 `onRecallMessage` 相同）。

### 5.4 清理挂载点

| 时机 | 动作 |
| --- | --- |
| 消息被撤回（`onRecallMessage` 成功后） | `removePinForMessage(msgid)` |
| 群被解散（`onDismissGroup`） | `removePinsForGroup(groupid)` |

退群不清理——人退了群，群里的 Pin 与他无关，但对留下的人仍然有效。

### 5.5 关键路径日志

按仓库规范，`onPinMessage` 的成功与各失败分支、`onGetPinnedMessages` 的返回条数与是否发生截断、`removePinForMessage` 的实际影响行数都要打 `LOGI` / `LOGE`，格式照 `onRecallMessage` 那几条撤回日志（带 userid / cid / msgid / 结果）。截断那条尤其要打——线上出现"用户说钉了却看不到"时，第一眼就能判断是不是撞上了 200 条上限。

---

## 6. 客户端数据流

### 6.1 协议层 `net/MessageProtocolClient`

新增三个结构与五个函数，与服务端一一镜像：

- `PinMessageResponse`（`m_code` / `m_msg` / `m_ctype` / `m_cid` / `m_msgid` / `m_pin`）
- `MessagePinnedNotify`（`m_ctype` / `m_cid` / `m_msgid` / `m_pin` / `m_operator` / `m_pinTime`）
- `PinnedMessagesResult`（`m_code` / `m_ctype` / `m_cid` / `m_total` / `m_msgs`）
- `buildPinMessageRequest` / `parsePinResponse` / `parseMessagePinnedNotify` / `buildGetPinnedMessagesRequest` / `parseGetPinnedMessagesResponse`

### 6.2 `net/IUSocket`

新增 `PinMessage(int ctype, int64_t cid, int64_t msgid, bool pin)` 与 `GetPinnedMessages(int ctype, int64_t cid)`，形态照 `RecallMessage`（`IUSocket.cpp`）：参数非法直接不发，并打日志说明原因。

收包分发（`IUSocket.cpp` 里 `case cmd_recall_message` 一带）加三个 `case`，各自解析后投给 UI 线程：

| 命令 | 新增窗口消息 |
| --- | --- |
| `cmd_pin_message` 应答 | `FMG_MSG_V2_PIN_RESULT` = `WM_USER + 545` |
| `cmd_message_pinned_notify` | `FMG_MSG_V2_MESSAGE_PINNED` = `WM_USER + 546` |
| `cmd_get_pinned_messages` 应答 | `FMG_MSG_V2_PINNED_MESSAGES` = `WM_USER + 547` |

三者的 `lParam` 都是接收线程堆分配的结构指针，UI 处理后 `delete`——与现有各条推送同约定。

### 6.4 主面板 `CBalloonMainPanel`

**`OnMessagePinned(notify)`** —— 照 `OnMessageRecalled`（`BalloonMainPanel.cpp`）做三件事，外加一件：

1. `m_msgStore.SetPinned(...)` 落本地标记，接住 `outHit`；
2. 该会话的聊天窗若开着，调其 `OnMessagePinned(msgid, pinned)` 刷新气泡；
3. `m_historyWindow.RefreshIfConversation(ctype, cid)` 让消息记录窗即时反映；
4. `outHit` 为 false（本地没有这条消息）时，对该会话发一次 `GetPinnedMessages` 补拉（§4.3）。

会话列表预览**不动**——Pin 不改变"最后一条消息是什么"。

**`OnPinResult(result)`** —— 只在失败时弹提示：`error_code_pin_not_allowed` 说明"只有钉它的人和群主 / 管理员能取消"，`error_code_message_not_exist` 说明消息已撤回或不存在，其余按通用失败。成功什么都不做（界面等推送）。

**`OnPinnedMessagesArrived(result)`** —— 逐条 `Upsert` 消息体，按 §4.3 的规则算出 `minTimestampMs` 后 `ReplacePinnedSet` 对账，最后 `RefreshIfConversation`。这段与 `BalloonMainPanel.cpp` 的同步回填落库路径可以共用同一个"网络消息 → HistoryRecord"转换（`RecordFromNotify`）。

**发起时机** —— `OpenHistory(ctype, cid)` 与打开聊天窗时各发一次 `GetPinnedMessages`，另加上面第 4 条的补拉。

### 6.5 聊天窗与消息列表

- `CBalloonChatWindow` / `CBalloonGroupChatWindow` 各加 `OnMessagePinned(long long msgid, bool pinned)`，转调 `ChatMessageList::SetPinnedByMsgid`。
- `ChatMessageList` 新增：
  - `RequestPinBubble(ChatBubble* bubble, bool pin)` —— 只把 msgid 与目标状态经新的 `WM_CHAT_PIN_REQUEST` 投给宿主窗，自己不改界面（照 `RequestRecallBubble`）。
  - `SetPinnedByMsgid(long long msgid, bool pinned)` —— 按 msgid 找到行并置标记；找不到安静忽略。Pin 标记画在气泡外侧、不改行高，故只 `Invalidate` 不重排。
- `ChatMessage`（`ChatMockData.h`）加 `bool pinned = false`；`ChatBubble` 加 `SetPinned(bool)`，内部置渲染缓存脏位（`ChatBubble.h` 的 `Invalidate` 已代办）。

### 6.6 本地查询层 `V2MsgQuery`

- `HistoryTypeFilter` 增加 `kHistoryTypePinned = 6`（含中文注释说明取该值的场景）
- `TypeFilterToSqlFragment` 对该值返回 `"pinned=1"`
- `BuildPageSql` 的 `SELECT` 列清单加 `pinned`（`BuildCountSql` 不需要）
- 排序**不变**，仍是 `timestamp DESC, msgid DESC`——与其它六个分类一致，切换胶囊时排序规则不会突然变一个样，`BuildPageSql` 也不用多一条分支

---

## 7. 界面与交互

### 7.1 气泡右键菜单

在现有菜单（`ChatMessageMenu.cpp` 的 `BuildChatMessageMenuModel`）的「转发」之后、第一条分隔线之前插一项 `kMenuPin`：

```
复制
引用回复
转发
Pin / 取消 Pin      ← 新增，文案随 m_msg.pinned 取反
─────────
撤回（条件出现）
删除
─────────
多选
```

- 文案取反的写法照主面板那几处菜单（置顶 / 取消置顶）
- `msgid == 0`（本端刚发出、服务器回执还没回填）时**不显示**该项——照撤回的做法，免得用户点了才被服务端拒绝
- 时间分隔行与撤回提示行本来就不弹菜单（`IsRealMessage()` 已挡）
- 取消 Pin 的权限**不在客户端预判**：本端不一定知道是谁钉的（`operator` 只在推送里带、历史行未必有），预判会出现"该显示的没显示"。菜单项一律给出，无权时由服务端以 `error_code_pin_not_allowed` 拒绝并提示

### 7.2 气泡上的 Pin 标记

`ChatIcons.h` 的 `ChatIconKind` 增加 `kChatIconPin = 16`（图钉），被 Pin 的气泡在**气泡右上角外侧**画一枚小图钉。

这是 `ChatBubble` 自绘范围内的事，不动 balloonui。标记画在气泡矩形之外的空白里、不占内容宽度、不改行高，因此 `SetPinnedByMsgid` 只需重绘不必重排。

### 7.3 消息记录窗：「Pin」分类胶囊

`HistoryWindowLayout.h` 的 `HistoryCtrlId` 增加 `kIdHistoryChipPin = 516`，胶囊排在「链接」之后：

```
[全部] [文字] [图片] [文件] [语音] [链接] [Pin]
```

`BalloonHistoryWindow.cpp` 那句"510..515 → 类型枚举 0..5"的映射相应扩到 510..516 → 0..6，注释一并更新。

胶囊是单选的，所以「Pin」与「图片」不能同时选——选了 Pin 就是"本会话全部被钉的消息，不分类型"。这是已确认的取舍：做成可与类型叠加的独立开关交互上更强，但要改胶囊行布局与 `HistoryFilter` 结构，本轮不做。

### 7.4 消息记录窗：行内 Pin 按钮

`HistoryRowView`（`HistoryWindowLayout.h`）增加 `bool m_pinned`；每行在「定位」按钮**左侧**加一枚同风格的次操作小按钮（`DuiButton` + `StylePushButton` + `Variant::Default`，照「定位」按钮），文案随 `m_pinned` 取反：「Pin」/「取消」。

按钮 id 用新基址 `kIdHistoryPinBase = 3200`（第 i 行 = 基址 + i），与页码基址 1000、定位基址 3000 各占一段，各段容量见 `HistoryWindowLayout.h` 的 `kIdHistoryXxxCapacity` 常量。

点击后走与胶囊 / 翻页同一套局部刷新：发请求 → 等推送 → 主面板 `RefreshIfConversation` → 重建列表。**不做乐观更新**，与聊天窗那侧口径一致。

### 7.5 其它约定

- **哪些消息不能 Pin**：时间分隔行、撤回提示行（本就无 msgid）；已撤回的消息服务端会以 `error_code_message_not_exist` 拒绝
- 图片 / 文件 / 语音消息**可以** Pin——恰恰是"那份定稿文件"最需要被钉住
- Pin 数量不设上限，界面上没有"已达上限"这类提示
- **本轮不做**：Pin 列表的手动排序、"某某钉了这条消息"的系统提示行（「已 Pin N 条」提示带已在 P5 完成）

---

## 8. 分期路线

| 阶段 | 内容 | 完成判据 |
| --- | --- | --- |
| P1 ✅ | 服务端：建表（改 `table_mysql.sql` + 重跑生成脚本）、`MessagePinManager`、`MessageProtocol` 扩展、三条命令、撤回与解散的清理挂载 | doctest 全过；用日志确认 Pin 写库、回读、推送、截断 |
| P2 ✅ | 客户端本地库：补列机制、`Upsert` 改 UPSERT、`SetPinned` / `ReplacePinnedSet`、`kHistoryTypePinned` | FlamingoUnit 全过，**含存量库补列、Upsert 不冲 pin、带下界对账三条回归用例** |
| P3 ✅ | 客户端协议层与数据流：`MessageProtocolClient` 扩展、`IUSocket` 三个入口、三条窗口消息、主面板三个 handler、对账与补拉 | FlamingoUnit 全过；两台客户端对钉互看 |
| P4 ✅ | 界面：气泡右键菜单 + 气泡 Pin 标记 + 消息记录窗 Pin 胶囊 + 行内 Pin 按钮 | 截图目视检查 |
| P5 ✅ | 聊天窗顶部「已 Pin N 条」提示带（点开跳消息记录窗的 Pin 分类） | FlamingoUnit 全过；截图目视检查 |
| P6（后续，暂无排期） | Pin 列表手工排序 | —— |

P2 必须先于 P3 落地：补列机制没上就改列，存量客户端的消息记录窗会直接空白（见 §2.3）。

### P1 实施记录（2026-08-04）

落地的文件：

| 文件 | 改动 |
| --- | --- |
| `flamingoserver/table_mysql.sql` | 新增 `t_message_pin` 建表语句 |
| `flamingoserver/table_sqlite.sql`、`sqliteapi/SqliteSchema.cpp` | 重跑 `tools/gen_table_sqlite.py` 生成；建表语句与 `SCHEMA_TABLES` 均已带上新表 |
| `chatserversrc/Protocol.h`、`flamingoclient/Source/net/Protocol.h` | 同步新增 1518 / 1519 / 1520 三条命令与 `error_code_pin_not_allowed`(506) |
| `chatserversrc/MessagePinManager.{h,cpp}` | 新增，含 `PIN_FETCH_LIMIT = 200` |
| `chatserversrc/MessageProtocol.{h,cpp}` | 新增 `PinRecord` / `PinMessageRequest` / `GetPinnedMessagesRequest` 与四个编解码函数 |
| `chatserversrc/ChatSession.{h,cpp}` | 新增 `onPinMessage` / `onGetPinnedMessages`，撤回与群解散两处清理挂载 |
| `chatserversrc/main.cpp` | 启动时 `MessagePinManager::init` |
| `tests/test_messagepinmanager.cpp` | 新增 9 个用例（MPM1~MPM9） |
| `tests/test_messageprotocol.cpp` | 追加 6 个用例（S23~S28） |
| `CMakeLists.txt`、`windowsproj/chatserver.vcxproj{,.filters}`、`windowsproj/Tests.vcxproj` | 加入新源文件 |

与方案原文的两处差异，都是实现时发现的问题：

1. **`PinRecord` 定义在 `MessageProtocol.h` 而不是 `MessagePinManager.h`。** 它既是数据库行、又是 `cmd_get_pinned_messages` 应答里 pin 子对象的来源，放在 Manager 那边会让 `MessageProtocol.h` 反过来依赖它，两个头文件互相包含。放到 `MessageProtocol.h` 与 `MessageRecord` 做邻居，与既有设计一致。
2. **`getPinnedMessages` 多了一个 `outPins` 出参**，Pin 元信息跟着同一条联表查询一起取回。原方案没写这一步，按字面实现就得逐条再查一次 `getPinRecord` —— 每次都新建一条数据库连接，200 条就是 200 次建连接。

测试基础设施上多做的一件事：`MessagePinManager` 经 `DbFactory` 取连接，而测试程序原先没有编进 `dbapi/DbFactory.cpp`。已把它加进两套测试构建，并单独给测试目标定义 `FLAMINGO_NO_MYSQL`（CMake 用 `target_compile_definitions`，vcxproj 加进预处理器定义），使 `DbFactory` 编出只带 SQLite 后端的版本。这是本仓库第一个走真实数据库的单元测试套。

### P2 实施记录（2026-08-04）

落地的文件：

| 文件 | 改动 |
| --- | --- |
| `UI/BalloonHistory/V2MsgStore.h` | 新增 `PinnedItem` 结构、`SetPinned` / `ReplacePinnedSet` 两个接口、私有的 `EnsureColumnsLocked` |
| `UI/BalloonHistory/V2MsgStore.cpp` | 建表语句加两列；新增补列机制与 `kSchemaColumns` 清单；`Upsert` 改显式 UPSERT；两个写接口的实现；`QueryPage` 多读两列 |
| `UI/BalloonHistory/V2MsgQuery.h` | `HistoryTypeFilter` 增加 `kHistoryTypePinned=6`；`HistoryRecord` 增加 `m_pinned` / `m_pinTime` |
| `UI/BalloonHistory/V2MsgQuery.cpp` | `kColumns` 加两列；`TypeFilterToSqlFragment` 对 Pin 返回 `pinned=1` |
| `Tests/V2MsgStoreTests.cpp` | 新增 11 个用例（V2S12~V2S22） |
| `Tests/V2MsgQueryTests.cpp` | 新增 3 个用例（V2Q9~V2Q11）；V2Q5 / V2Q6 的既有断言随列清单更新 |

与方案原文的一处差异，是被测试逼出来的：

**`ReplacePinnedSet` 的入参从 `vector<int64_t> msgids` 改成了 `vector<PinnedItem> items`（msgid + Pin 时间）。** 方案原文写的是"对账只对齐钉没钉住这一件事，不写 `pin_time`"，照此实现之后 V2S22 直接失败：清零那一步为了不留"没钉住却有 Pin 时间"的半截状态，把 `pin_time` 也归了零，而置位那一步不写回去，于是每对账一次就把所有 Pin 时间抹平成 0。两个值本来就一起来自服务端应答里每条消息挂的 `pin` 子对象，分开处理反而要多绕一圈，故改成一并对齐。

一个踩到的坑记在这里：中间目录里残留的 `.obj` 时间戳比源文件还新（快了约 3 小时），MSBuild 据此判定"无需重编"，改完代码重新构建后跑的仍是旧二进制 —— 表现为测试报的失败位置与当前源码对不上。清掉中间目录重新构建即可。改完代码测试结果没有随之变化时，先核对一下 exe 的时间戳。

### P3 实施记录（2026-08-04）

落地的文件：

| 文件 | 改动 |
| --- | --- |
| `net/MessageProtocolClient.{h,cpp}` | 新增 `PinMessageResponse` / `MessagePinnedNotify` / `PinnedMessageItem` / `PinnedMessagesResult` 四个结构与五个编解码函数 |
| `net/IUSocket.{h,cpp}` | 新增 `PinMessage` / `GetPinnedMessages` 两个发送入口；收包分发加 1518 / 1519 / 1520 三个 `case` |
| `Session/UserSessionData.h` | 三条窗口消息（`WM_USER + 545`~`547`） |
| `App/FlamingoClient.cpp` | 消息代理窗口的转发 switch 加这三条（**补于 P4 联调，见下**） |
| `UI/BalloonMain/BalloonMainPanel.h`、`BalloonMainPanelNetHandlers.cpp` | 三个 handler：`OnV2PinResult` / `OnV2MessagePinned` / `OnV2PinnedMessages` |
| `UI/BalloonMain/BalloonMainPanel.{h,cpp}` | `OnMessagePinned` / `OnPinnedMessagesArrived`；`OpenHistory` 里多发一次 `GetPinnedMessages` |
| `Tests/MessageProtocolTests.cpp` | 新增 8 个用例（MP-PIN1~MP-PIN8） |

两处值得记下来的实现细节：

1. **`parseMessagePinnedNotify` 对 `pin` 字段缺失或越界一律丢弃整条推送**，不做"缺省当取消"的容错。缺省成取消会把别人刚钉上的消息在本端悄悄抹掉，比丢弃这条推送糟得多。
2. **`parseGetPinnedMessagesResponse` 在应答缺 `total` 时按"返回多少就是多少"兜底。** 照原样留 0 的话，调用方会算出 `total < 返回条数`、被当成没截断……实际上更糟：`total` 为 0 而 `items` 非空时，截断判断 `total > items.size()` 为假，走全量对账，行为恰好正确；但若日后有人把判断改成 `total != items.size()` 就会出错。兜底成相等最稳。

**联调时发现的一处漏接（2026-08-04 修）**：新增窗口消息一共要接<u>四层</u>，我漏了第三层。`CIUSocket` 收到帧后 `PostMessage` 的目标是 `CFlamingoClient` 的<u>消息代理窗口</u>，那里有一个 switch 把消息转发给主窗口（`m_UserMgr.m_hCallBackWnd`）；不在 switch 里的消息落进 `default` 被丢掉，堆负载也跟着泄漏。三条 Pin 消息当时只做了「定义 → IUSocket 投递 → 主窗口消息映射」三层，代理窗那层没加，于是网络全通、日志里应答与推送都收到了，界面却纹丝不动。**日后新增 `FMG_MSG_*` 一律照着 `FMG_MSG_V2_MESSAGE_RECALLED` 把四层全找一遍。**

### P4 实施记录（2026-08-04）

落地的文件：

| 文件 | 改动 |
| --- | --- |
| `UI/BalloonChatWnd/ChatMessageMenu.{h,cpp}` | 菜单模型加 `kChatMsgMenuPin`；`BuildChatMessageMenuModel` 增加 `canPin` / `pinned` 两个入参 |
| `UI/BalloonChatWnd/ChatMockData.h` | `ChatMessage` 加 `pinned` 字段 |
| `UI/BalloonChatWnd/ChatIcons.{h,cpp}` | 新增 `kChatIconPin`（水滴形图钉，头部挖孔） |
| `UI/BalloonChatWnd/ChatBubble.{h,cpp}` | `SetPinned` / `IsPinned`；菜单接上 Pin 项与分发；气泡外侧上角画图钉 |
| `UI/BalloonChatWnd/ChatMessageList.{h,cpp}` | `RequestPinBubble` / `SetPinnedByMsgid` |
| `App/CustomMsgDef.h` | 新增 `WM_CHAT_PIN_REQUEST` |
| `UI/BalloonChatWnd/BalloonChatWindow.{h,cpp}`、`BalloonGroupChatWindow.{h,cpp}` | `OnChatPinRequest` / `OnMessagePinned`；开窗时多拉一次被 Pin 消息全集 |
| `UI/BalloonMain/BalloonMainPanel.{h,cpp}` | `OnMessagePinned` 补上向已打开聊天窗的扇出；新增 `ChatWindowApplyPinnedSet` |
| `UI/BalloonHistory/HistoryWindowLayout.{h,cpp}` | `kIdHistoryChipPin` / `kIdHistoryPinBase`；第七枚胶囊；行内 Pin 按钮 |
| `UI/BalloonHistory/BalloonHistoryWindow.cpp` | 胶囊 id 映射扩到 0..6；行数据填 `m_pinned`；行内 Pin 按钮发请求 |
| `Tests/ChatImageMenuTests.cpp` | 既有用例适配新签名；新增 5 个用例（CIM-PIN1~CIM-PIN5） |

两处实现时才发现、方案里没写到的问题：

1. **气泡外侧画图钉时左右要按发送方向取反。** 方案原文只说"气泡右上角外侧"，但自己发的消息气泡靠右，右边紧挨着头像，图钉画上去会压到头像。实际做法是：自己的气泡把图钉放左边、对方的放右边，两种情形下它都落在中间那片空白里。
2. **`P3` 遗留的一个真空。** 聊天窗的历史是从网络渲染的，而 `net::MessageNotify` 里没有 Pin 信息 —— 只补主面板的推送扇出还不够，刚开窗时那些被钉住的老消息仍然不带图钉。故开窗时另发一次 `cmd_get_pinned_messages`，应答到达后经 `ChatWindowApplyPinnedSet` 把标记打到已渲染的气泡上。

3. **假数据必须带上 `msgid`，否则调试入口里这一项根本不出现。** 菜单项的出现条件是 `msgid > 0`，而 `ChatMockData` 造的假消息从来不设 `msgid`（真实会话由 `MakeRealTextMessage` 填）。于是 `--chatwindow` / `--groupchatwindow` 两个调试入口下右键菜单里看不到「Pin」—— 真实客户端是好的，但目检时看到的是"功能没做"。修法是给两批假消息按顺序补上 `msgid`（单聊 900001 起、群聊 910001 起，两段不重叠），并把第一条标成已 Pin，使图钉标记与「取消 Pin」文案在调试入口下也能看到。**凡是"拿 msgid 说事"的界面，加完都要回头看一眼假数据。**

**构建环境的一个坑（比 P2 记的那个更要命）**：不要把多个工程的 `IntDir` 指到<u>同一个</u>目录。客户端的 `Buffer.cpp` 与 speexdsp 的 `buffer.c` 都产出 `Buffer.obj`，共享中间目录时后者会把前者覆盖掉，链接期报 `CBuffer::*` 全部无法解析 —— 看起来像代码问题，其实是构建参数问题。`/p:IntDir=...$(MSBuildProjectName)...` 也不行：该变量在编译阶段被展开、链接阶段不展开。结论是**别覆盖 `IntDir`**，用各工程自己的默认中间目录。

### P5 实施记录（2026-08-05）

聊天窗顶部的「已 Pin N 条」提示带。开工前与用户确认了三件事：

- **位置**：放在头部信息行<u>下方</u>、消息区上方，而不是窗口最顶端。最顶端那条留给离线 /
  正在连接提示带 —— 它讲的是"连接态"，这条讲的是"本会话内容"，分两层两者同时出现时层次才清楚。
- **点击行为**：打开消息记录窗并切到 Pin 分类，不另做一个"Pin 列表浮层"。消息记录窗里那份
  列表本来就可搜索、可翻页、可定位、可取消 Pin，复用它比再画一份省得多。
- **文案**：只报条数（`已 Pin N 条 ›`），不显示最新一条被 Pin 消息的摘要。

落地的文件：

| 文件 | 改动 |
| --- | --- |
| `UI/BalloonChatWnd/ChatWindowLayout.{h,cpp}` | `kIdPinBanner = 415` 与 XML 里那条 28px 的 `chat-banner`；新增单群共用的三个函数 `MakePinBannerText` / `ApplyPinBannerStyle` / `UpdatePinBanner`，以及四个配色常量 |
| `UI/BalloonChatWnd/GroupChatWindowLayout.{h,cpp}` | `kGIdPinBanner = 515` 与同形的 XML 一行；建树后调同一个 `ApplyPinBannerStyle` |
| `UI/BalloonChatWnd/ChatBannerLabel.h` | 类注释更新：这个控件本身不接管交互，可点与否由调用方 `SetMode` 决定 |
| `UI/BalloonChatWnd/BalloonChatWindow.{h,cpp}`、`BalloonGroupChatWindow.{h,cpp}` | `m_pinnedCount` 成员；`SetPinnedCount` / `AdjustPinnedCount` / `RefreshPinBanner`；把「消息记录」按钮那段抽成 `RequestOpenHistory(initialTypeFilter)`，提示带点击复用它 |
| `Session/UserSessionData.h` | `FMG_MSG_OPEN_HISTORY` 的 `wParam` 约定改为"低字节=ctype、次低字节=预选的类型胶囊" |
| `UI/BalloonMain/BalloonMainPanel.{h,cpp}` | `OpenHistory` 与 `OnOpenHistoryMsg` 接受并解出类型胶囊；`ChatWindowApplyPinnedSet` 顺带 `SetPinnedCount(total)`；`OnMessagePinned` 就地 ±1 |
| `UI/BalloonHistory/BalloonHistoryWindow.{h,cpp}` | `OpenForConversation` 多一个 `initialTypeFilter` 参数（默认 0=全部，越界回退全部） |
| `Tests/ChatWindowLayoutTests.cpp` | 新增 6 个用例（PB1~PB6） |

实现时的两个决定，记下来免得日后被当成疏漏：

1. **没有新增窗口消息，而是复用 `FMG_MSG_OPEN_HISTORY` 的 `wParam` 高位。** 这条消息的
   `wParam` 原先只放 `ctype`（取值 1 / 2），高位全空着。把"预选哪个类型胶囊"塞进次低字节，
   比再开一条 `WM_USER + N`（还得在四层转发里各接一遍）代价小得多。约定写在 `UserSessionData.h`
   那一行的注释里。
2. **条数有两个来源，权威的那个是服务端的 `total`。** `cmd_get_pinned_messages` 应答里的
   `total` 不受单次拉取上限（`PIN_FETCH_LIMIT = 200`）截断影响，故用它 `SetPinnedCount`；
   而单条 Pin / 取消 Pin 推送只做 `AdjustPinnedCount(±1)` —— 为了刷新一个数字再拉一趟全集
   不值当。这份就地加减是"乐观"的，极端情况下（推送在离线期间丢失）可能偏差，但每次重开
   聊天窗都会重新拉一次全集校准回来。

提示带的显隐沿用离线提示带那条既有契约：`SetVisible` 只重绘、不重排，显隐真的变化时必须
`ForceLayout` 补一次整树布局，再投递本窗的 `kRelayoutMsg` 把输入区块高度钉回用户选定值。
PB5 / PB6 两个用例把"占位 28px、且夹在头部行与消息区之间"钉死，防止日后有人把它挪回窗口顶端。

---

## 9. 测试用例清单

> 共 39 条。按用户决定，**界面布局契约测试（胶囊 id 连续性、行内按钮 id 基址、按钮文案取反）不写单测**，改由 P4 的截图目视检查覆盖，见文末。
> 按仓库测试规范，本清单须先与用户确认覆盖范围与预期行为，确认后再写产品代码。

### 服务端 `tests/test_messageprotocol.cpp`（doctest，追加）

1. `parsePinMessageRequest` 合法请求取出 ctype / cid / msgid / pin
2. 请求解析：缺字段、ctype 越界（0 / 3）、cid 或 msgid 非正、pin 非 0/1 → 一律判非法
3. `buildPinMessageResponse` code=0 时回带 ctype / cid / msgid / pin
4. `buildPinMessageResponse` 各错误码分支的 code 与 msg 非空
5. `buildMessagePinnedNotify` 六个字段齐全，且 64 位 ID 编码为字符串
6. `buildGetPinnedMessagesResponse` 空列表产出 `"msgs":[]` 且 `total` 为 0
7. `buildGetPinnedMessagesResponse` 每条消息体带 `pin` 子对象，含 operator 与 pintime
8. `buildGetPinnedMessagesResponse` 截断场景：`total` 大于 `msgs` 长度

### 服务端 `tests/test_messagepinmanager.cpp`（doctest，新增，走 SQLite 内存库）

9. Pin 后回读得到该行；重复 Pin 幂等，不产生第二行、`f_pin_time` 与 `f_operator` 都不被刷新
10. 取消 Pin 后回读为空；取消一条没 Pin 的消息不报错
11. `getPinnedMessages` 群聊：只返回本群的、且已排除已撤回的
12. `getPinnedMessages` 单聊：双向消息都能取到（我发的与对方发的）
13. `getPinnedMessages` 单聊：不串到第三个人的会话里
14. `getPinnedMessages` 条数超过 `limit` 时只返回最近 `limit` 条，且 `outTotal` 为真实总数
15. `getPinnedMessages` 条数未超 `limit` 时 `outTotal` 恰等于返回条数（客户端据此判定未截断）
16. `removePinForMessage` 只清该条，不误伤同会话其它 Pin
17. `removePinsForGroup` 清掉本群全部 Pin，不误伤其它群

### 服务端 `tests/test_grouppermission.cpp` 或新建取消权限用例（doctest）

18. 取消权限判定：操作者本人、群主、管理员均可取消；其他普通成员被拒（`error_code_pin_not_allowed`）

### 服务端 `tests/test_sqlite_schema.cpp`（doctest，追加）

19. `SCHEMA_TABLES` 含 `t_message_pin`，且其建表语句可在空库上执行成功

### 客户端 `Tests/V2MsgStoreTests.cpp`（Catch2，追加）

20. **存量库补列**：先用"旧结构"（不含 pinned / pin_time）建库并插入若干行，再 `Open` 同一文件 → 两列被补上、旧数据完好、带 pin 的查询不再报错
21. **补列幂等**：连续 `Open` / `Close` 多次不重复 ALTER、不报错
22. **`Upsert` 不冲掉 pin 标记**：Upsert 一条 → `SetPinned(true)` → 再 Upsert 同一条（内容变化）→ `pinned` 仍为 1，且内容已更新
23. `SetPinned(true)` 后该条在 `kHistoryTypePinned` 过滤下出现，`SetPinned(false)` 后消失
24. `SetPinned` 命中本地已有的行时 `outHit` 为 true
25. `SetPinned` 对本地不存在的 msgid：返回成功但 `outHit` 为 false；msgid <= 0 直接返回 false
26. `SetPinned` 只影响指定的 `(ctype,cid,msgid)`，同 msgid 落在另一会话时互不影响
27. 已撤回的消息即便 `pinned=1` 也不出现在 Pin 分类里（查询恒含 `recalled=0`）
28. `ReplacePinnedSet(minTimestampMs = 0)`：本地多出来的 pin 被清零、缺的被置位、其它会话不受影响
29. **`ReplacePinnedSet` 带下界**：早于 `minTimestampMs` 的本地 pin 标记保持不动（截断场景不误清）
30. `ReplacePinnedSet` 传空列表且下界为 0 → 该会话 pin 全清

### 客户端 `Tests/V2MsgQueryTests.cpp`（Catch2，追加）

31. `TypeFilterToSqlFragment(kHistoryTypePinned)` 产出 `pinned=1`
32. `BuildPageSql` 在 Pin 分类下同时含 `pinned=1` 与 `recalled=0`
33. `BuildPageSql` 的 Pin 分类可与关键字、时间区间叠加
34. `BuildCountSql` 在 Pin 分类下的 WHERE 与 `BuildPageSql` 一致

### 客户端 `Tests/MessageProtocolTests.cpp`（Catch2，追加）

35. `buildPinMessageRequest` 产出的 JSON 字段齐全、64 位 ID 为字符串
36. `parsePinResponse` 取出 code / ctype / cid / msgid / pin
37. `parseMessagePinnedNotify` 取出六个字段；缺 ctype 按单聊、缺 operator 取 0
38. `parseMessagePinnedNotify` 对畸形报文与非正 id 安全失败（空串 / 非 JSON / 数组 / msgid<=0）
39. `parseGetPinnedMessagesResponse` 解析多条消息体与其 `pin` 子对象、取出 `total`；不含 `msgs` 的应答返回空列表且不报错

### 不由单测覆盖、靠目视检查的部分

- 气泡右上角图钉标记的位置、大小与气泡的观感是否协调
- 右键菜单新增项的位置与文案取反
- 「Pin」胶囊与前六枚的间距、选中态，点它之后列表确实只剩被钉的消息
- 行内 Pin 按钮与「定位」并排后的行宽是否挤到预览文字，以及按钮文案随状态取反
- 在另一台客户端上钉 / 取消，本机气泡与消息记录窗是否即时跟着变

---

## 10. 待确认与风险

- **单次返回 200 条是拍的**，没有实测依据。它约束的只是一次应答的体量，不是用户能钉多少（数量不限）。真要调，改服务端一个具名常量即可，协议与表结构都不受影响。
- **截断场景下的对账下界**（§4.3）是本方案里最容易写错的一处：漏掉下界会把本地那些"更老的 Pin"误清，而且只有在同一会话 Pin 超过 200 条时才暴露，日常测不出来。第 29 条用例专门钉这个，不能省。
- **取消权限不在客户端预判**（§7.1），代价是无权的用户点了才会被拒。要想菜单项直接置灰，得让历史消息也带上 `operator`——那意味着 `cmd_sync_messages` 的消息体要加字段，本轮不划算。
- **`Upsert` 改成 UPSERT 语法**依赖 SQLite ≥ 3.24，当前编入的是 3.53.1（§2.7），满足。但这是本方案里唯一一处对 SQLite 版本有硬要求的改动，日后若有人把 amalgamation 换回旧版本会静默失败（语法错误 → `prepare` 失败 → `Upsert` 恒返回 false），这一点已写在 `V2MsgStore.cpp` 的 UPSERT 语句注释里。
- **Android 端不受影响**：新增的是三条全新命令与一张新表，没有改动任何既有报文的形状。Android 端不实现即可，行为与现在完全一致。
