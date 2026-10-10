# FlamingoIM 控制台 Admin 接口文档

> 配套设计稿：`design/FlamingoIM Enterprise Admin.html`
> 接口逐模块确认于 2026.05.21，共 9 个模块、52 个接口。

## 一、通用约定

- **接入点**：admin 接口挂在 chatserver 的 `HttpServer` 上（`httplistenport`，默认 12345）。
- **请求**：一律 `POST /admin/<模块>/<动作>.do`，请求体为 JSON。
- **应答**：HTTP 200，body 为 JSON：`{"code":0,"msg":"ok", ...数据}`。
  - `code` 取 `chatserversrc/AdminApiCodes.h` 的 `enum error_code`（`0`成功、`1`未知失败、`2`未登录/token 失效、`103`密码错误、`110`~`116`部门相关等），admin 专属错误码也在该文件里扩充。
  - `export` 类接口应答为 CSV 文本（`Content-Type: text/csv`），非 JSON。
- **鉴权**：除 `login.do` 外，所有请求体须含 `token`；服务端按 token 查 `t_admin_session` 校验登录态，并按管理员的管辖范围（平台超管 / 限定单企业）做越权拦截。
- **审计**：所有"写"接口成功后自动写一条 `t_admin_audit_log`，无独立的审计写接口。
- **不做双因子（2FA）**：`login.do` 仅邮箱 + 密码。

---

## 二、模块 1：认证 Auth（5）

### 1.1 `POST /admin/login.do` —— 管理员登录
- 请求：`{"email":"li@flamingo.com", "password":"明文密码"}`
- 应答：`{"code":0,"msg":"ok","token":"<会话令牌>","admin":{"adminid":N,"name":"李正阳","email":"...","enterpriseid":0},"linkport":20006,"linkpubkey":"<base64>"}`
- 说明：`enterpriseid=0` 为平台超级管理员，非 0 表示仅管辖该企业。密码在库中以 MD5 加盐存储，服务端比对哈希。
- `linkport` / `linkpubkey`（2026-10-04 起）：管理控制台长连接（FlamingoLink.dll，企业详情页「在线用户」）用的管理端口与服务端身份公钥。服务端没有配置 `adminlinklistenport` 时 `linkport` 为 0；未开启协议加密时 `linkpubkey` 为空串，控制台据此跳过握手。长连接的协议见 `docs/协议设计-v2.md` 第 22 节，方案见 `docs/FlamingoLink方案.md`。

### 1.2 `POST /admin/logout.do` —— 登出
- 请求：`{"token":"..."}`
- 应答：`{"code":0,"msg":"ok"}`（删除 `t_admin_session` 对应记录）

### 1.3 `POST /admin/profile.do` —— 取当前管理员信息与管辖范围
- 请求：`{"token":"..."}`
- 应答：`{"code":0,"msg":"ok","admin":{...},"scope":"platform"|"enterprise"}`

### 1.4 `POST /admin/changepassword.do` —— 修改自己的登录密码
- 请求：`{"token":"...","oldpassword":"...","newpassword":"..."}`
- 应答：`{"code":0,"msg":"ok"}`

### 1.5 `POST /admin/updateprofile.do` —— 修改自己的个人资料
- 请求：`{"token":"...","name":"..."}`
- 应答：`{"code":0,"msg":"ok"}`

---

## 三、模块 2：仪表盘 Dashboard（2）

### 2.1 `POST /admin/dashboard/stats.do` —— 概览统计
- 请求：`{"token":"..."}`
- 应答：`{"code":0,"msg":"ok","stats":{"enterprises_total":N,"enterprises_active":N,"enterprises_pending":N,"users_total":N,"audit_today":N,"audit_today_warn":N}}`
- 说明：平台超管返回全平台口径，企业级管理员返回其所辖企业口径：企业数按本企业自身的状态计（`enterprises_total` 为 1，`enterprises_active` / `enterprises_pending` 按状态取 0 或 1），`users_total`、`audit_today`、`audit_today_warn` 只算本企业的记录（与 `audit/list.do` 一致）。2026-10-04 之前企业数与两项审计计数对企业管理员也是全平台口径。控制台在企业管理员登录时不显示「企业总数」卡。

### 2.2 `POST /admin/dashboard/messagetrend.do` —— 近 14 天消息量
- 请求：`{"token":"..."}`
- 应答：`{"code":0,"msg":"ok","trend":[{"date":"MM-DD","count":N}]}`（`trend` 恰为 14 个元素，按日期从旧到新、最后一个为今天；缺消息的天 `count` 为 0）
- 说明：平台超管统计全平台消息，企业级管理员只统计本企业员工发出的消息。供仪表盘「近 14 天消息量」柱状图使用。

---

## 四、模块 3：企业管理 Enterprise（10）

### 3.1 `POST /admin/enterprise/list.do` —— 企业列表
- 请求：`{"token":"...","keyword":"","status":-1,"page":1,"pagesize":20}`（`status=-1` 不筛选）
- 可选筛选（2026-10-04 起，控制台「更多筛选」使用，不带即不限，与 `keyword`、`status` 同时生效）：`"industry":"金融服务"`（行业，精确匹配）、`"plan":N`（套餐编号，`-1` 不限）、`"openfrom":"YYYY-MM-DD"` 与 `"opento":"YYYY-MM-DD"`（按开通日期 `f_open_date` 筛选，起止当天都包含；格式不对的日期被忽略，不报错）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"enterpriseid":N,"name":"...","shortname":"...","industry":"...","region":"...","members":N,"depts":N,"plan":N,"status":N,"adminname":"...","opendate":"...","rootdeptid":N}]}`
- `rootdeptid`：**公司根部门 ID**，组织架构树上代表"公司本身"的那个真实部门，员工可以直接挂在它下面。`0` 表示该企业尚未建立根部门（存量数据迁移之前），此时管理端会退回旧的合成根节点显示。
- `accountprefix`（2026-10-06 起，3.2 同此）：新建账号的前缀，大写；空串表示没设，新账号按默认的 `EMP` 生成。设置见 3.4，规则见 `docs/登录账号与手机号登录方案.md`。

### 3.2 `POST /admin/enterprise/detail.do` —— 企业详情
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","enterprise":{企业全字段}}`（含 `rootdeptid`，含义同 3.1）
- 平台超管按 `enterpriseid` 读任意企业；企业管理员只能读本企业，请求里的 `enterpriseid` 一律按其所属企业处理（2026-10-04 起；此前只允许平台超管调用）。控制台的企业管理员据此直接打开本企业详情。

### 3.3 `POST /admin/enterprise/create.do` —— 新增企业（连带建初始管理员与公司根部门）
- 请求：`{"token":"...","name":"...","shortname":"...","industry":"...","region":"...","plan":N,"adminname":"...","adminemail":"..."}`
- `plan`：套餐编号，`0` 基础版 / `1` 专业版 / `2` 旗舰版，与 `t_enterprise.f_plan` 一致（3.1 的应答、3.4 的修改同此）。2026-10-04 起统一为这三档：产品本身就是企业版 IM，`2` 不再叫「企业版」；控制台此前另行约定过的编号 `3` 不再使用。
- 应答：`{"code":0,"msg":"ok","enterpriseid":N,"adminid":N,"initialpassword":"..."}`
- 说明：同时在 `t_admin` 写一条企业级管理员（`enterpriseid` 为新企业、邮箱为 `adminemail`），并返回生成的初始密码。
- 说明：同时在 `t_department` 建一行**公司根部门**（`f_parent_dept_id=0`、层级 1、名字取企业全称），并把其 ID 回写到 `t_enterprise.f_root_dept_id`。企业改名（3.4）时该部门名同步更新；该部门不允许删除或移动。

### 3.4 `POST /admin/enterprise/update.do` —— 修改企业信息
- 请求：`{"token":"...","enterpriseid":N,"name":"...","shortname":"...","industry":"...","region":"...","plan":N,"accountprefix":"..."}`
- 应答：`{"code":0,"msg":"ok"}`
- `accountprefix` 可选（2026-10-06 起）：**带了才改**，老版本控制台不带这个字段时已设的前缀保持不变；空串表示清除、改回默认的 `EMP`。只能由英文字母与点号组成、去掉首尾空白后 1～10 个字符、至少一个字母，服务端转成大写保存；不合格返回 `code=152`（`error_code_account_prefix_invalid`），整个请求不生效。前缀只影响之后新建的账号（客户端自助注册与 5.3 新建员工），已有账号不变。
- 企业全称与别的企业重复返回 `code=119`（`error_code_enterprise_nameexist`）。

### 3.5 `POST /admin/enterprise/setstatus.do` —— 审核/活跃/停用企业
- 请求：`{"token":"...","enterpriseid":N,"status":N}`（status: 0待审核 1活跃 2停用）
- 应答：`{"code":0,"msg":"ok"}`

### 3.6 `POST /admin/enterprise/export.do` —— 导出企业列表
- 请求：`{"token":"...","keyword":"","status":-1}`，另接受与 3.1 相同的可选筛选（`industry`、`plan`、`openfrom`、`opento`）；控制台按企业列表当前的筛选条件导出
- 应答：CSV 文本

### 3.7 `POST /admin/enterprise/delete.do` —— 硬删除企业
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok"}`
- 说明：物理删除企业及其组织/成员等数据，区别于 `setstatus` 的停用。

### 3.8 `POST /admin/enterprise/admins.do` —— 企业的管理员列表
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","list":[{"adminid":N,"name":"...","email":"...","status":N,"lastlogintime":"..."}]}`

### 3.9 `POST /admin/enterprise/addadmin.do` —— 为企业新增管理员
- 请求：`{"token":"...","enterpriseid":N,"name":"...","email":"..."}`
- 应答：`{"code":0,"msg":"ok","adminid":N,"initialpassword":"..."}`

### 3.10 `POST /admin/enterprise/removeadmin.do` —— 移除企业管理员
- 请求：`{"token":"...","adminid":N}`
- 应答：`{"code":0,"msg":"ok"}`

---

## 五、模块 4：组织架构 Department（7）

> 底层复用 `DepartmentManager`，与 IM 客户端的 cmd 1201/1202 同源、两个入口。

### 4.1 `POST /admin/dept/tree.do` —— 取某企业组织架构树
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","departments":[{"deptid":N,"deptname":"...","parentdeptid":N,"deptlevel":N,"deptorder":N,"leaderuserid":N,"deptdesc":"...","members":[...]}]}`（结构同 cmd 1200）
- `members` 的每个元素：`{"userid":N,"username":"...","employeeno":"...","nickname":"...","facetype":N,"customface":"...","status":N,"clienttype":N,"position":"...","isprimary":N,"memberorder":N,"accountstatus":N}`。
  其中 `status` 是**在线状态**（0 离线、1 在线、2 离开、3 勿扰，与 `Protocol.h` 的 `presence_type` 一致），`clienttype` 是在线时的客户端类型；
  `accountstatus` 是**账号状态**，取 `t_user.f_status`（0 启用、1 停用），控制台据此显示「已停用」与启用 / 停用按钮。
  2026-10-10 起新增 `accountstatus`（此前没有账号状态，控制台把在线状态 1 当成停用，在线员工都显示「已停用」）；
  员工经 5.5 `user/setstatus.do` 启用 / 停用后，本接口立即返回新的 `accountstatus`，不需要等组织架构重新加载。
- 说明：本接口不单独返回公司根部门 ID，它随企业信息（3.1 / 3.2 的 `rootdeptid`）下发。调用方据此在返回的部门数组中认出代表公司本身的那个部门，并把它作为整棵树的根。

### 4.2 `POST /admin/dept/detail.do` —— 部门详情
- 请求：`{"token":"...","deptid":N}`
- 应答：`{"code":0,"msg":"ok","department":{"deptid":N,"deptname":"...","leadername":"...","directmembers":N,"subdepts":N,"totalmembers":N}}`

### 4.3 `POST /admin/dept/create.do` —— 新增部门
- 请求：`{"token":"...","enterpriseid":N,"parentdeptid":N,"deptname":"...","deptdesc":"...","leaderuserid":N,"deptorder":N}`
- 应答：`{"code":0,"msg":"ok","deptid":N}`

### 4.4 `POST /admin/dept/update.do` —— 修改部门
- 请求：`{"token":"...","deptid":N,"deptname":"...","deptdesc":"...","leaderuserid":N,"deptorder":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 4.5 `POST /admin/dept/move.do` —— 移动部门
- 请求：`{"token":"...","deptid":N,"newparentdeptid":N}`（`newparentdeptid=0` 表示移到本企业的公司根部门之下；企业尚未建立根部门时才退回旧含义"移到顶层"）
- 应答：`{"code":0,"msg":"ok"}`（被移动的是公司根部门时 `code=118`，公司根部门不允许移动）

### 4.6 `POST /admin/dept/delete.do` —— 删除（停用）部门
- 请求：`{"token":"...","deptid":N}`
- 应答：`{"code":0,"msg":"ok"}`（其下若有在用子部门或在职成员则 `code=112`；目标是公司根部门则 `code=118`，它代表企业本身，不允许删除）

### 4.7 `POST /admin/dept/reorder.do` —— 批量重排序同级部门
- 请求：`{"token":"...","parentdeptid":N,"orderedids":[deptid1,deptid2,...]}`
- 应答：`{"code":0,"msg":"ok"}`（按数组顺序刷新 `f_dept_order`）

---

## 六、模块 5：员工/用户 User（12）

### 5.1 `POST /admin/user/list.do` —— 员工列表
- 请求：`{"token":"...","enterpriseid":N,"deptid":N,"keyword":"","page":1,"pagesize":20}`
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"userid":N,"name":"...","title":"...","deptname":"...","role":N,"status":N}]}`

### 5.2 `POST /admin/user/detail.do` —— 员工详情
- 请求：`{"token":"...","userid":N}`
- 应答：`{"code":0,"msg":"ok","user":{基本信息 + 个人扩展}}`

### 5.3 `POST /admin/user/create.do` —— 新增员工
- 请求：`{"token":"...","enterpriseid":N,"deptid":N,"name":"...","title":"...","employeeno":"...","email":"...","phone":"...","joindate":"...","gender":N}`
- 应答：`{"code":0,"msg":"ok","userid":N,"initialpassword":"..."}`
- `gender` 可选：0 保密 / 1 男 / 2 女（与 `chatserversrc/Protocol.h` 的 `enum profile_gender` 一致），不带时按 0；其它取值返回 `code=1`。2026-10-04 起支持。
- `deptid` 传 `0` 表示"直接挂在公司下"，服务端会把员工加入该企业的公司根部门。企业尚未建立公司根部门时返回 `code=110`（部门不存在）并拒绝创建 —— 员工必须有确定的部门归属，否则会成为组织架构里看不见的孤儿账号。
- 应答另带 `username`：服务端生成的登录账号。2026-10-06 起为「企业账号前缀 + 8 位随机数字」（企业没设前缀时为 `EMP`，如 `EMP48203917`），此前为 `EMP` + 用户 ID。
- `phone` 必填（2026-10-06 起）：须为 11 位以 1 开头的数字，否则返回 `code=150`（`error_code_phone_invalid`）；已被其他账号使用返回 `code=151`（`error_code_phone_taken`）。手机号可用于登录，必须全局唯一。
- `employeeno` 与本企业已有员工重复返回 `code=101`。

### 5.4 `POST /admin/user/update.do` —— 修改员工基本信息
- 请求：`{"token":"...","userid":N,"name":"...","title":"...","employeeno":"...","joindate":"...","landline":"...","workstation":"...","phone":"...","email":"...","signature":"...","gender":N}`
- 应答：`{"code":0,"msg":"ok"}`
- `gender` 可选，取值同 5.3；带了就改，没带则保持原值，其它取值返回 `code=1`。2026-10-04 起支持。
- `phone` 必填，规则与错误码同 5.3；保留员工自己原来的号码不算重复（2026-10-06 起）。
- `employeeno` 为空时库里写 NULL；与本企业其他员工重复返回 `code=101`（2026-10-06 起；此前空工号写成空串，同一企业第二名没填工号的员工保存时撞唯一索引、返回 `code=1`）。员工不存在返回 `code=102`。

### 5.5 `POST /admin/user/setstatus.do` —— 启用/停用员工
- 请求：`{"token":"...","userid":N,"status":N}`（0启用 1停用；停用后无法登录任何客户端）
- 应答：`{"code":0,"msg":"ok"}`

### 5.6 `POST /admin/user/delete.do` —— 删除员工
- 请求：`{"token":"...","userid":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 5.7 `POST /admin/user/move.do` —— 调整员工所属部门
- 请求：`{"token":"...","userid":N,"deptid":N,"newdeptid":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 5.8 `POST /admin/user/loginrecords.do` —— 登录历史与已登录设备
- 请求：`{"token":"...","userid":N,"page":1,"pagesize":20}`
- 应答：`{"code":0,"msg":"ok","devices":[{设备},...],"total":N,"records":[{"ts":"...","device":"...","ip":"...","location":"...","result":N,"failreason":"..."}]}`

### 5.9 `POST /admin/user/activity.do` —— 员工操作历史时间线
- 请求：`{"token":"...","userid":N,"type":"all","range":"7d","page":1}`
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"ts":"...","action":"...","detail":"...","severity":N}]}`
- 说明：数据源为 `t_user_activity_log`。

### 5.10 `POST /admin/user/kickdevice.do` —— 强制注销设备
- 请求：`{"token":"...","userid":N,"deviceid":""}`（`deviceid` 为空表示注销全部设备）
- 应答：`{"code":0,"msg":"ok"}`

### 5.11 `POST /admin/user/resetpassword.do` —— 重置员工登录密码
- 请求：`{"token":"...","userid":N}`
- 应答：`{"code":0,"msg":"ok","newpassword":"...","kicked":N}`（生成新密码并返回）
- 2026-10-07 起，重置成功后服务端立即踢掉该员工全部在线设备（`cmd_kickout`，`reason = 10`，见协议设计-v2.md §5.6），旧密码登录的会话不再可用；`kicked` 为实际下线的会话数。

### 5.12 `POST /admin/user/messagestats.do` —— 员工消息与会话统计
- 请求：`{"token":"...","userid":N}`
- 应答：`{"code":0,"msg":"ok","stats":{"sent":N,"received":N,"conversations":N},"trend":[{"date":"MM-DD","count":N}]}`
- 说明：`sent` 发出的消息总数、`received` 收到的单聊消息总数、`conversations` 单聊会话数（去重对端数）；`trend` 为该员工近 14 天每日发出的消息数（恰 14 个，旧→新，缺天补 0）。

---

## 七、模块 6：角色与权限 Role & Permission（3）

### 6.1 `POST /admin/user/setrole.do` —— 设置员工角色
- 请求：`{"token":"...","userid":N,"role":N}`（role: 0普通成员 1部门负责人 2企业管理员）
- 应答：`{"code":0,"msg":"ok"}`
- 说明：改角色时**清空**该用户在 `t_user_permission` 的所有逐人覆盖，权限回到新角色默认。

### 6.2 `POST /admin/user/getpermissions.do` —— 取员工权限矩阵
- 请求：`{"token":"...","userid":N}`
- 应答：`{"code":0,"msg":"ok","role":N,"permissions":[{"group":"...","key":"...","label":"...","roledefault":bool,"overridden":bool,"effective":bool}]}`

### 6.3 `POST /admin/user/setpermissions.do` —— 设置员工权限覆盖
- 请求：`{"token":"...","userid":N,"perms":[{"key":"msg.external","granted":true}]}`
- 应答：`{"code":0,"msg":"ok"}`（只提交与角色默认不同的覆盖项，写 `t_user_permission`）

---

## 八、模块 7：平台公告 Announcement（8）

### 7.1 `POST /admin/announcement/list.do` —— 公告列表
- 请求：`{"token":"...","status":-1,"page":1,"pagesize":20}`（status: 0草稿 1已发布）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{公告摘要}]}`

### 7.2 `POST /admin/announcement/detail.do` —— 公告详情
- 请求：`{"token":"...","announcementid":N}`
- 应答：`{"code":0,"msg":"ok","announcement":{...,"readcount":N,"totalcount":N}}`

### 7.3 `POST /admin/announcement/save.do` —— 新建/存草稿/编辑
- 请求：`{"token":"...","announcementid":0,"title":"...","body":"...","severity":N,"audiencetype":N,"targets":[{"type":N,"id":N}],"pinned":bool}`（`announcementid=0` 表示新建）
- 应答：`{"code":0,"msg":"ok","announcementid":N}`
- 说明：audiencetype: 0全部企业 1指定企业 2指定部门 3指定员工 4平台内部；`targets` 仅在 1/2/3 时有值。

### 7.4 `POST /admin/announcement/send.do` —— 发布/定时发布
- 请求：`{"token":"...","announcementid":N,"scheduled":bool,"scheduledtime":"..."}`
- 应答：`{"code":0,"msg":"ok"}`

### 7.5 `POST /admin/announcement/delete.do` —— 删除公告
- 请求：`{"token":"...","announcementid":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 7.6 `POST /admin/announcement/recall.do` —— 撤回已发公告
- 请求：`{"token":"...","announcementid":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 7.7 `POST /admin/announcement/setpinned.do` —— 置顶切换
- 请求：`{"token":"...","announcementid":N,"pinned":bool}`
- 应答：`{"code":0,"msg":"ok"}`

### 7.8 `POST /admin/announcement/repush.do` —— 重新推送（2026-10-04 新增）
- 请求：`{"token":"...","announcementid":N}`
- 应答：`{"code":0,"msg":"ok","pushed":N}`，`pushed` 为实际推送的人数
- 说明：把一条**已发布**的公告再推一次，只推给受众里当前在线、且还没有已读回执（`t_announcement_read`）的成员；推送包与发布时相同，客户端按公告 ID 更新本地记录并再弹一次新公告提示。没有在线且未读的成员时 `pushed=0`，仍算成功。仅平台超管可调用，企业管理员返回 `code=116`。
- 本接口的专属错误码（`chatserversrc/AdminApiCodes.h`）：

  | code | 含义 |
  | --- | --- |
  | 140 | 公告不是「已发布」状态（草稿或已撤回），不能重新推送 |

> 已读统计：用 `t_announcement_read` 读回执表（每用户读过一条），需 IM 客户端上报已读。

---

## 九、模块 8：审计日志 Audit（2）

### 8.1 `POST /admin/audit/list.do` —— 审计日志查询
- 请求：`{"token":"...","actortype":-1,"action":"","enterpriseid":0,"severity":-1,"starttime":"...","endtime":"...","page":1,"pagesize":50}`
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"id":N,"ts":"...","actortype":N,"actorname":"...","ip":"...","enterpriseid":N,"action":"...","target":"...","severity":N,"detail":"..."}]}`

### 8.2 `POST /admin/audit/export.do` —— 导出审计日志
- 请求：同 `list.do` 的筛选字段
- 应答：CSV 文本

---

## 十、模块 9：服务号 Service Account（6）

### 9.1 `POST /admin/service/list.do` —— 服务号列表
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","list":[{服务号摘要}]}`

### 9.2 `POST /admin/service/detail.do` —— 服务号详情（含绑定）
- 请求：`{"token":"...","serviceid":N}`
- 应答：`{"code":0,"msg":"ok","service":{...,"bindings":[{"bindtype":N,"targetid":N}]}}`

### 9.3 `POST /admin/service/save.do` —— 新建/编辑服务号
- 请求：`{"token":"...","serviceid":0,"enterpriseid":N,"name":"...","kind":N,"description":"...","avatarcolor":"...","allowunsubscribe":bool}`（`serviceid=0` 表示新建；kind: 0通知型 1助手型 2集成型，其它取值返回 `code=1`。类型 2 于 2026-10-04 加入，目前与通知型的服务端行为相同，只作分类）
- 应答：`{"code":0,"msg":"ok","serviceid":N}`

### 9.4 `POST /admin/service/delete.do` —— 删除服务号
- 请求：`{"token":"...","serviceid":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 9.5 `POST /admin/service/setbindings.do` —— 设置可见范围绑定
- 请求：`{"token":"...","serviceid":N,"bindings":[{"bindtype":N,"targetid":N}]}`（bindtype: 0本企业全员 1部门 2员工）
- 应答：`{"code":0,"msg":"ok"}`

### 9.6 `POST /admin/service/setstatus.do` —— 启用/停用服务号
- 请求：`{"token":"...","serviceid":N,"status":N}`（0正常 1停用）
- 应答：`{"code":0,"msg":"ok"}`

---

## 十一、模块 11：敏感词 Sensitive Word（8）

> 编号说明：模块 10 是客户端版本管理（`/admin/clientversion/*.do`），接口说明在 `docs/客户端自动更新方案.md`，没有收进本文档，所以本节跳过 10 编为模块 11。

> 2026-10-02 加入，方案见 `docs/敏感词方案.md`。词库按企业划分：企业管理员只能操作本企业，请求里的 `enterpriseid` 一律按本企业处理；平台超级管理员**必须**在请求里用 `enterpriseid` 指定企业，不指定时回 `code=1`、`msg="enterpriseid required"`。服务端在单聊、群聊、改资料等场合按提交者所属企业的词库拦截，命中的词不告诉提交者。
>
> 本模块的专属错误码（`chatserversrc/AdminApiCodes.h`）：
>
> | code | 含义 |
> |----:|------|
> | 120 | 词条为空，或去掉空白、标点符号与表情之后为空 |
> | 121 | 词条去掉首尾空白后超过 32 个字符 |
> | 122 | 归一化后（统一大小写与全半角，去掉空白、符号与表情）与本企业已有词条重复 |
> | 123 | 本企业词条数已达 10000 条上限 |
> | 124 | 要修改的词条不存在，或不属于该企业 |
>
> 分类 `category` 取值：1 违法违规、2 色情低俗、3 攻击辱骂、4 商业机密、5 广告引流、6 其他（不填或不在范围内时按 6）。拦截来源 `source` 取值见 `chatserversrc/SensitiveWordDefs.h` 的 `sensitive_hit_source`（1 文字消息、2 转发、3 文件名、4 卡片消息、5 个人资料、6 群资料、7 群名片、8 加群申请、9 禁言原因、10 文件夹名、11 注册昵称、12 群公告、13 屏幕共享的标题与描述）。时间一律为 Unix 毫秒。

### 10.1 `POST /admin/sensitive/list.do` —— 词库列表
- 请求：`{"token":"...","enterpriseid":N,"page":1,"pagesize":50,"keyword":"","category":0}`（`keyword` 按词条原文模糊匹配；`category=0` 表示全部；`pagesize` 最大 200）
- 应答：`{"code":0,"msg":"ok","total":N,"enabled":1,"wordcount":N,"list":[{"id":N,"word":"...","category":N,"remark":"...","creatorname":"...","createtime":N,"updatetime":N}]}`
- 说明：按添加先后倒序（最新在前）。`enabled` 是企业的过滤总开关，`wordcount` 是该企业的词条总数（界面显示「已用 N / 10000」）。企业不存在时回 `code=1`、`msg="enterprise not found"`。

### 10.2 `POST /admin/sensitive/add.do` —— 添加词条
- 请求：`{"token":"...","enterpriseid":N,"word":"...","category":N,"remark":"..."}`（备注超过 64 个字符的部分截掉）
- 应答：`{"code":0,"msg":"ok","id":N}`；失败时 `code` 为 120～123
- 审计：`sensitive.add`

### 10.3 `POST /admin/sensitive/update.do` —— 修改词条
- 请求：`{"token":"...","enterpriseid":N,"id":N,"word":"...","category":N,"remark":"..."}`
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 120～122、124
- 审计：`sensitive.update`

### 10.4 `POST /admin/sensitive/delete.do` —— 删除词条
- 请求：`{"token":"...","enterpriseid":N,"ids":[N,...]}`（1～500 个；不属于该企业的 ID 直接忽略）
- 应答：`{"code":0,"msg":"ok","deleted":N}`
- 审计：`sensitive.delete`（严重程度 2）

### 10.5 `POST /admin/sensitive/import.do` —— 批量导入
- 请求：`{"token":"...","enterpriseid":N,"category":N,"lines":["...", ...]}`（1～1000 行，客户端按此分批提交；每行格式 `词条[<Tab>分类名[<Tab>备注]]`，只写词条或分类名写错时用 `category`；空行忽略）
- 应答：`{"code":0,"msg":"ok","added":N,"skipped":[{"line":N,"word":"...","reason":N}]}`（`line` 从 1 起，按请求里的行号计；`reason` 为 120～123；同一批里与靠前的行重复也按 122）
- 说明：整批在一个事务里写入，数据库出错时整批回滚、回 `code=1`。
- 审计：`sensitive.import`（详情写成功与跳过的条数）

### 10.6 `POST /admin/sensitive/export.do` —— 导出词库
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：纯文本（`Content-Type: text/plain;charset=utf-8`），非 JSON。每行一条 `词条<Tab>分类名<Tab>备注`，按添加先后排列；词条与备注里的制表符、换行替换为空格，可原样提交给 `import.do`。

### 10.7 `POST /admin/sensitive/setenabled.do` —— 过滤总开关
- 请求：`{"token":"...","enterpriseid":N,"enabled":true}`（`enabled` 也接受整数，非 0 为启用；缺字段时回 `code=1`、`msg="enabled required"`）
- 应答：`{"code":0,"msg":"ok"}`；企业不存在时 `code=1`、`msg="enterprise not found"`
- 说明：关闭后该企业不做任何敏感词拦截，词库保留。
- 审计：`sensitive.setenabled`

### 10.8 `POST /admin/sensitive/hits.do` —— 拦截记录
- 请求：`{"token":"...","enterpriseid":N,"page":1,"pagesize":50,"begintime":0,"endtime":0,"keyword":"","source":0,"category":0}`（`begintime` / `endtime` 为 0 表示不限，两端都包含；`keyword` 按提交者名称模糊匹配；`source`、`category` 为 0 表示全部）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"id":N,"time":N,"userid":N,"username":"...","source":N,"ctype":N,"cid":N,"targetname":"...","word":"...","category":N,"snippet":"..."}]}`
- 说明：按拦截时间倒序。`ctype` 为 0 与会话无关、1 单聊、2 群聊；`cid` 单聊为对方 userid、群聊为群 ID；`snippet` 是命中位置前后各 20 个字符的内容摘要。记录保留 180 天，只读，不提供删除与导出。

---

## 十二、模块 12：文件共享 File Share（6）

> 2026-10-03 加入（文件共享第 3 期第 3 步），方案见 `docs/文件共享方案.md` 5.8、6.5、8.8。文件共享的开关按企业控制：企业没有设置记录时按内置默认值（开启）处理，新开通的企业不必做任何设置即可使用；平台管理员可以代任意企业设置，没有平台总开关。
>
> 企业范围：**读写设置**时企业管理员一律按本企业处理（请求里的 `enterpriseid` 不理会），平台管理员**必须**用 `enterpriseid` 指定企业，不指定时回 `code=1`、`msg="enterpriseid required"`，企业不存在时回 `code=1`、`msg="enterprise not found"`。**列表与审计**里企业管理员只看本企业，平台管理员可以用 `enterpriseid` 过滤、不填为全部企业。**单个共享的操作**（成员、强制取消）企业管理员只能操作本企业的共享。
>
> 本模块的专属错误码（`chatserversrc/AdminApiCodes.h`）：
>
> | code | 含义 |
> |----:|------|
> | 130 | 共享不存在（`shareid` 无效） |
> | 131 | 共享已经取消过，不能再强制取消 |
> | 132 | 共享不属于企业管理员的本企业 |
> | 133 | 功能设置项或取值不认识（开关项有 `fileshare.enabled`、`p2p.enabled`、`group.inviteconfirm`、`register.enabled`、`groupcall.enabled`、`caption.enabled`、`cloudrecord.enabled`、`secretchat.enabled`、`threadgroup.enabled`、`bot.enabled`、`bot.groupbot`、`bot.personal` 与 `bot.groupexternal`，取值 `"0"` / `"1"`；数值项有 `cloudrecord.quotagb` 与 `cloudrecord.retaindays`，取值为范围内不带符号的十进制整数） |
>
> 取值：`state` 1 有效、2 已取消；`cancelreason` 见 `share_invalid_reason`（1 分享者取消、2 群主或群管理员取消、3 管理员在后台强制取消、4 群已解散、5 分享者已退群）；`ctype` 1 单聊、2 群聊（单聊的 `cid` 为对方 userid）；`kind` 1 整个磁盘、2 文件夹、3 单个文件；`perms` 为写权限位（1 上传、2 新建文件夹、4 重命名、8 删除、16 移动）。审计的 `action` 见 `share_audit_action`（1 打开、2 申请下载、3 申请上传、4 下载文件、5 上传文件、6 新建文件夹、7 重命名、8 删除、9 移动、10 发起共享、11 取消共享、12 调整成员），`source` 1 为服务端亲眼所见、2 为分享者客户端上报（被篡改的客户端可以不报）。时间一律为 Unix 毫秒。

### 12.1 `POST /admin/setting/get.do` —— 读企业的功能设置
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","enterpriseid":N,"settings":[{"key":"fileshare.enabled","value":"1","isdefault":1},{"key":"p2p.enabled","value":"1","isdefault":1},{"key":"group.inviteconfirm","value":"0","isdefault":1},{"key":"register.enabled","value":"1","isdefault":1},{"key":"groupcall.enabled","value":"1","isdefault":1},{"key":"caption.enabled","value":"1","isdefault":1}]}`
- 说明：`isdefault=1` 表示该企业没有设置记录、取的是内置默认值。目前有十五项（与 `chatserversrc/FeatureSwitchManager.cpp` 的 `kKnownFeatures`、`kKnownNumbers` 一致），其中十三个开关项：`fileshare.enabled`（文件共享）与 `p2p.enabled`（客户端点对点直连）默认开启，`group.inviteconfirm`（邀请入群需被邀请人同意，2026-10-04 起）默认关闭，`register.enabled`（允许用户自助注册，2026-10-04 起）、`groupcall.enabled`（群语音 / 群视频通话，2026-10-04 起）与 `caption.enabled`（语音字幕，2026-10-06 起）默认开启，`cloudrecord.enabled`（云端录制，2026-10-06 起）默认关闭，`secretchat.enabled`（单聊密聊，2026-10-07 起）默认开启，`threadgroup.enabled`（帖子群）默认开启，`bot.enabled`（机器人）、`bot.groupbot`（允许群主添加群机器人）与 `bot.personal`（允许员工配置个人机器人）默认开启，`bot.groupexternal`（允许群主给群机器人配置外部服务）默认关闭；另有两个数值项（2026-10-06 起，`value` 同样是字符串）：`cloudrecord.quotagb`（云端录像的企业总配额，单位 GB，默认 `"50"`）与 `cloudrecord.retaindays`（云端录像的保留天数，默认 `"30"`，`"0"` 表示永久保留）。客户端按 `key` 查找自己要的那一项，不要依赖数组顺序。
- 应答另带 `cloudrecordusedbytes`（2026-10-06 起）：该企业云端录像的已用字节数，原始帧文件与合成后的录像都计入，口径与配额检查相同；云端录制没有初始化或查库失败时没有这一项。

### 12.2 `POST /admin/setting/set.do` —— 改企业的功能设置
- 请求：`{"token":"...","enterpriseid":N,"key":"fileshare.enabled","value":"0"}`（`value` 也接受整数；`key` 也可以是 `p2p.enabled`、`group.inviteconfirm`、`register.enabled`、`groupcall.enabled`、`caption.enabled`、`cloudrecord.enabled`、`secretchat.enabled`、`threadgroup.enabled`、`bot.enabled`、`bot.groupbot`、`bot.personal`、`bot.groupexternal`，或数值项 `cloudrecord.quotagb`（0～1000000）、`cloudrecord.retaindays`（0～3650））
- 应答：`{"code":0,"msg":"ok","enterpriseid":N,"enabled":0}`；设置项或取值不认识时 `code=133`
- 说明（`fileshare.enabled`）：写库后向**该企业**的在线用户推 `cmd_features_notify`（1013，包体如 `{"fileshare":0,"p2p":1}`）；客户端据此隐藏或恢复三处入口。开关由开变关时服务端结束该企业全部的访问会话与传输子会话，并向各有效共享的成员推「暂不可用，功能已被关闭」；此后拒绝该企业用户的发起、调整成员、打开与传输（800）。由关变开时按分享者的在线提供状态恢复，已有共享随分享者客户端重新上报逐步恢复「可用」。别的企业不受影响。
- 说明（`p2p.enabled`）：写库后同样推 1013，此外不做别的处理。已经建立的 P2P 会话不强制断开；此后新的一对一会话按新开关决定是否尝试点对点直连，关闭时直接走原来的服务端中转（见 `docs/P2P通信方案.md` 9.4 节）。
- 说明（`group.inviteconfirm`）：写库后同样推 1013（包体多一项 `inviteconfirm`），此外不做别的处理。开启后，被邀请人属于该企业的邀请入群改为在邀请人与被邀请人的单聊里发入群邀请卡片，被邀请人点「加入群聊」后才入群；关闭后已经发出的卡片照常可用（见 `docs/邀请入群方案.md` §3.5）。
- 说明（`register.enabled`）：只在服务端的注册流程里检查，**不推 1013**，登录应答的 `features` 里也没有这一项（注册发生在登录之前，客户端用不上）。关闭后，PC 客户端向该企业自助注册时返回 `error_code_register_closed`（410），不建账号；新员工只能由管理员用 `user/create.do` 新建。已有账号的登录与使用不受影响，别的企业也不受影响。企业待审核或已停用时注册仍返回 408，不看这个开关。
- 说明（`groupcall.enabled`）：写库后推 1013（包体多一项 `groupcall`），客户端据此隐藏或恢复群聊窗的「语音通话」「视频通话」按钮。关闭后服务端拒绝该企业成员发起、加入与追加邀请群通话（`error_code_groupcall_disabled`，727）；**正在进行的群通话不强制结束**，持续到所有人离开（见 `docs/群音视频通话方案.md` 第 2 节）。别的企业不受影响。
- 说明（`caption.enabled`，2026-10-06 起）：写库后推 1013（包体多一项 `caption`），客户端据此加载或卸载语音字幕模块：关闭后录屏与屏幕共享都不再提供字幕，已经生成的字幕文件不受影响。识别全在成员本机完成，服务端不做检查（见 `docs/语音字幕方案.md` 第 10 节）。
- 说明（`cloudrecord.enabled`，2026-10-06 起）：写库后推 1013（包体多一项 `cloudrecord`），客户端据此显示或隐藏屏幕共享控制条的「云端录制」与录制选择窗的「保存到云端」。关闭后服务端拒绝开始云端录制与新建上传；正在进行的云端录制会录完，已有的云端录像照常可看、可删（见 `docs/屏幕共享与录制方案.md` 12.6.5）。
- 说明（`secretchat.enabled`，2026-10-07 起）：写库后，开关由开变关时先结束该企业成员参与的全部待应答与进行中的密聊（双方收到 2605「已结束」，原因「管理员已关闭密聊」，客户端转为「只能查看」），再推 1013（包体多一项 `secretchat`），客户端据此隐藏或恢复单聊窗的「密聊」按钮。关闭后服务端拒绝该企业成员发起与应答密聊（`error_code_secret_disabled`，1200），对方企业关闭时同样拒绝（见 `docs/密聊方案.md` 4.9）。
- 说明（`threadgroup.enabled`）：写库后推 1013（包体多一项 `threadgroup`），此外不做别的处理，已有的帖子群与帖子保留。关闭后服务端拒绝该企业成员新建帖子群（建群请求带 `grouptype = 1` 时回 `error_code_thread_disabled`，1300）与 2700 段请求（同样回 1300），判定按请求者所属企业（`ChatSession::onCreateGroup`、`checkThreadAccess`、`checkThreadSpeak`）。
- 说明（`bot.enabled`、`bot.groupbot`、`bot.personal`、`bot.groupexternal`）：写库后推 1013（包体带 `bot`、`botgroup`、`botpersonal`、`botgroupexternal` 四项），此外不做别的处理，已有的机器人与规则都保留、不推 2820。服务端在用到时按开关判定（`BotEngine::botAllowed` 与 `ChatSessionBot.cpp`）：
  - `bot.enabled` 关闭：本企业的企业机器人、群机器人、个人机器人一律不再响应（含定时、入群与 Webhook）；2801 不再返回机器人；新建或修改群机器人、个人机器人回 `error_code_bot_disabled`（1405）。
  - `bot.groupbot` 关闭：群机器人不再响应，群主新建或修改群机器人回 1405；企业机器人不受影响。
  - `bot.personal` 关闭：个人机器人（含「离开 / 忙碌自动回复」的预置规则）不再代答，新建或修改个人机器人回 1405；2806、2810 应答里的 `allowed` 为 0。
  - `bot.groupexternal` 关闭：群机器人的 Webhook 推送被拒（Webhook 应答 `code=3`），「呼叫外部服务」的规则跳过不执行；群主保存带这两类规则的群机器人回 1405。企业机器人不受这一项影响。
- 说明（`cloudrecord.quotagb` / `cloudrecord.retaindays`，2026-10-06 起）：数值项，不推 1013。应答里的 `enabled` 对数值项没有意义，固定为 0。配额在开始云端录制、申请上传时检查，超出时回 604；保留天数在录像结束或上传完成时用来算出到期时间，改天数只影响之后结束的录像。
- 审计：`setting.set`（严重程度 1，开关项的详情为「打开 / 关闭 + 功能名」，如「打开文件共享」「关闭云端录制」；数值项的详情为「设置名 + 新值 + 单位」，如「云端录像配额设为 80 GB」）

### 12.3 `POST /admin/fileshare/list.do` —— 共享列表
- 请求：`{"token":"...","enterpriseid":0,"page":1,"pagesize":50,"keyword":"","state":0}`（`keyword` 按分享者的账号或昵称模糊匹配；`state=0` 表示全部；`pagesize` 最大 200）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"shareid":N,"enterpriseid":N,"ownerid":N,"ownername":"...","owneraccount":"...","ctype":N,"cid":N,"conversationname":"...","kind":N,"name":"...","displaypath":"...","membercount":N,"state":N,"cancelreason":N,"createtime":N,"canceltime":N}]}`
- 说明：按 `shareid` 倒序（新发起的在前）。`conversationname` 群聊为群名、单聊为对方昵称。

### 12.4 `POST /admin/fileshare/members.do` —— 共享的成员与权限
- 请求：`{"token":"...","shareid":N}`
- 应答：`{"code":0,"msg":"ok","members":[{"userid":N,"name":"...","account":"...","perms":N}]}`；共享不存在 `code=130`，不属于本企业 `code=132`

### 12.5 `POST /admin/fileshare/cancel.do` —— 强制取消共享
- 请求：`{"token":"...","shareid":N}`
- 应答：`{"code":0,"msg":"ok"}`；不存在 `code=130`，已取消过 `code=131`，不属于本企业 `code=132`
- 说明：原因为「管理员在后台强制取消」（`cancelreason=3`）。全部卡片对全部人显示「已失效」，正在进行的访问与传输被结束，分享者客户端删除本机记录。文件共享的审计里记一条「取消共享」，操作人为管理员。
- 审计：`fileshare.cancel`（严重程度 2）

### 12.6 `POST /admin/fileshare/audit.do` —— 文件共享的审计记录
- 请求：`{"token":"...","enterpriseid":0,"page":1,"pagesize":50,"actorkeyword":"","action":0,"starttime":0,"endtime":0,"shareid":0}`（`actorkeyword` 按操作人的账号或昵称模糊匹配，后台强制取消那一条按管理员姓名匹配；`starttime` / `endtime` 为 0 表示不限，两端都包含）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"id":N,"time":N,"shareid":N,"sharename":"...","ownerid":N,"ownername":"...","actorid":N,"actorname":"...","actorisadmin":0,"action":N,"source":N,"path":"...","topath":"...","size":N,"result":N,"accessid":"..."}]}`
- 说明：按时间倒序。`actorisadmin=1` 时 `actorid` 是管理员 id、`actorname` 是管理员姓名。`path` 为相对共享根的路径（申请传输时为条目清单的摘要），`topath` 是重命名后的名字或移动的目标目录；`result` 为 0 表示成功，否则为客户端协议的错误码（800 段）。只读，不提供删除与导出。

---

## 十三、模块 13：企业问答 Enterprise QA（5）

> 2026-10-07 加入，方案见 `docs/群问答方案.md`。企业问答显示在本企业群聊窗的「问答」页签里，群成员只读、不能隐藏；某个群自己维护了同一问题（去掉首尾空白、英文不区分大小写后比较）时，该群只显示群里那一条。企业的确定方式与敏感词模块相同：企业管理员按本企业处理，平台超级管理员必须用 `enterpriseid` 指定企业。「本企业的群」按群主所属企业认定。时间一律为 Unix 毫秒，ID 一律为字符串。
>
> 本模块的专属错误码（`chatserversrc/AdminApiCodes.h`）：
>
> | code | 含义 |
> |----:|------|
> | 160 | 问题或答案为空、问题超过 128 个字符、答案超过 2048 个字符，或 `scope` 不是 0 / 1 |
> | 161 | 要修改或删除的问答不存在，或不属于该企业 |
> | 162 | 本企业问答数已达 1000 条上限 |
> | 163 | `scope` 为 1 时没有选群，或所选的群不属于本企业 |
>
> 应用范围 `scope`：0 应用到本企业全部群（`groupids` 忽略），1 只应用到 `groupids` 列出的群。

### 13.1 `POST /admin/qa/list.do` —— 企业问答列表
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","list":[{"id":"...","question":"...","answer":"...","scope":0,"groupids":["..."],"creatorname":"...","updatetime":N}]}`
- 说明：按修改时间倒序。`creatorname` 是最后一次保存它的管理员。

### 13.2 `POST /admin/qa/add.do` —— 添加企业问答
- 请求：`{"token":"...","enterpriseid":N,"question":"...","answer":"...","scope":1,"groupids":["268435457"]}`（`groupids` 里重复的群只算一次）
- 应答：`{"code":0,"msg":"ok","id":"..."}`；失败时 `code` 为 160、162、163
- 审计：`qa.add`

### 13.3 `POST /admin/qa/update.do` —— 修改企业问答
- 请求：`{"token":"...","enterpriseid":N,"id":"...","question":"...","answer":"...","scope":0,"groupids":[]}`
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 160、161、163
- 说明：应用到的群整份覆盖。
- 审计：`qa.update`

### 13.4 `POST /admin/qa/delete.do` —— 删除企业问答
- 请求：`{"token":"...","enterpriseid":N,"id":"..."}`
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 161
- 审计：`qa.delete`（严重程度 2）

### 13.5 `POST /admin/qa/groups.do` —— 本企业的群（供选择应用范围）
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","list":[{"groupid":"...","name":"..."}]}`，按群 ID 升序

---

## 十四、模块 14：机器人 Bot（10）

> 2026-10-08 加入，方案见 `docs/机器人方案.md`，机器人对象与规则对象的字段见 `docs/协议设计-v2.md` 25.1。本模块只管理**企业机器人**
> （`ownertype` 为 1）；群机器人由群主在客户端配置，个人机器人由员工本人配置。企业的确定方式与企业问答相同：企业管理员按本企业处理，
> 平台超级管理员必须用 `enterpriseid` 指定企业。「本企业的群」按群主所属企业认定。时间一律为 Unix 毫秒，ID 一律为字符串。
>
> 本模块的专属错误码（`chatserversrc/AdminApiCodes.h`，名称带 `botadmin` 以免与客户端协议的 `error_code_bot_*` 撞名）：
>
> | code | 含义 |
> |----:|------|
> | 170 | 机器人或规则的字段不合法：名称为空或超过 20 字、简介超过 100 字、头像底色不是 `#RRGGBB`、关键词为空、回复为空、时间格式错误等 |
> | 171 | 机器人不存在、已删除，或不是本企业的企业机器人 |
> | 172 | 本企业的企业机器人已达 50 个，或规则超过 50 条、关键词超过 20 个 |
> | 173 | `scope` 为 1 时没有选群，或所选的群、定时消息的目标群不属于本企业 |
> | 174 | 规则用了不允许的触发方式或动作，或外部服务地址不在域名白名单内 |
> | 175 | 域名白名单里有不合法的域名，或超过 50 条 |
> | 176 | 仓库通知的机器人（`source` 为 1 / 2）只能在「仓库通知」页修改 |
>
> 写操作成功后服务端向该企业的在线客户端推 2820（`groupid` 为 "0"），打开着的群聊窗重拉本群机器人列表。
> 企业开关见模块 12 的 `setting/get.do` / `set.do`：`bot.enabled`、`bot.groupbot`、`bot.personal`、`bot.groupexternal`。

### 14.1 `POST /admin/bot/list.do` —— 企业机器人列表
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","list":[机器人对象（不带规则与密钥）+ "lastfire","webhookpath"，配置了对外地址时再加 "webhookurl"]}`，按机器人 ID 升序
- 说明：`rulecount` 为规则条数，`lastfire` 为最近一次发出消息的时间（0 表示还没发过）。

### 14.2 `POST /admin/bot/detail.do` —— 企业机器人详情
- 请求：`{"token":"...","enterpriseid":N,"botid":"..."}`
- 应答：`{"code":0,"msg":"ok","bot":{机器人对象，含 rules、webhooktoken、secret、webhookpath，配置了对外地址时再加 webhookurl}}`；失败时 `code` 为 171
- 说明：`webhookurl` 是服务端按配置项 `botwebhookbaseurl` 拼出的对外完整地址（2026-10-08 起），控制台有它就直接显示；
  没有时（服务端没有配置）控制台按自己连接服务器所用的协议、主机与端口拼上 `webhookpath`，服务器在 NAT 或反向代理之后时这个地址
  未必是外部系统能访问的地址。推送格式见协议文档 25.4。

### 14.3 `POST /admin/bot/save.do` —— 新建或修改企业机器人
- 请求：`{"token":"...","enterpriseid":N,"bot":{"botid":"0","name":"...","desc":"...","glyph":"IT","color":"#1E88E5","status":0,"scope":1,"groupids":["..."],"allowgroupoff":1,"rules":[规则对象...]}}`
  - `botid` 为 "0" 表示新建，否则修改（规则整份覆盖，按 `id` 对上的保留创建时间）。
- 应答：`{"code":0,"msg":"ok","botid":"..."}`；失败时 `code` 为 170～174、176，`msg` 写明是哪条规则的什么问题（英文）
- 审计：`bot.create` / `bot.update`

### 14.4 `POST /admin/bot/delete.do` —— 删除企业机器人
- 请求：`{"token":"...","enterpriseid":N,"botid":"..."}`
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 171、176
- 说明：软删除，机器人账号保留并停用，历史消息仍显示它的名字。
- 审计：`bot.delete`

### 14.5 `POST /admin/bot/setstatus.do` —— 停用 / 启用
- 请求：`{"token":"...","enterpriseid":N,"botid":"...","status":1}`（0 启用、1 停用）
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 170、171
- 审计：`bot.status`

### 14.6 `POST /admin/bot/groups.do` —— 本企业的群
- 与 13.5 `qa/groups.do` 相同：`{"code":0,"msg":"ok","list":[{"groupid":"...","name":"..."}]}`

### 14.7 `POST /admin/bot/try.do` —— 试一试
- 请求：`{"token":"...","enterpriseid":N,"botid":"..."}` 或 `{"token":"...","enterpriseid":N,"bot":{尚未保存的机器人}}`，
  再加 `"input":{"text":"...","at":1,"groupid":"...","time":N}`（`groupid` 用来取该群的问答，可省略；`time` 省略表示现在）
- 应答：`{"code":0,"msg":"ok","matched":1,"kind":1,"ruleid":"...","rulename":"...","text":"...","markdown":0,"url":"","reason":0}`
- 说明：只判定被 @ 与关键词两种触发方式，不发任何消息；外部服务只返回地址，不实际调用。`kind`、`reason` 的取值见协议文档 25.2。

### 14.8 `POST /admin/bot/resettoken.do` —— 重新生成 Webhook 令牌与签名密钥
- 请求：`{"token":"...","enterpriseid":N,"botid":"..."}`
- 应答：`{"code":0,"msg":"ok","webhooktoken":"...","secret":"..."}`；旧令牌立即失效
- 审计：`bot.resettoken`

### 14.9 `POST /admin/bot/domains.do` —— 外部服务的域名白名单
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","domains":["example.com"]}`

### 14.10 `POST /admin/bot/setdomains.do` —— 修改外部服务的域名白名单
- 请求：`{"token":"...","enterpriseid":N,"domains":["example.com","api.example.org"]}`（整份覆盖；大写转小写，重复的只算一次）
- 应答：`{"code":0,"msg":"ok"}`；失败时 `code` 为 175
- 说明：外部服务地址的主机等于白名单里某一项或是其子域名才允许；白名单为空时不能配置外部服务。
- 审计：`bot.domains`

---

## 附录 A：权限项清单（13 项，固定为代码常量）

| 分组 | key | 说明 | 全员默认 |
|---|---|---|---|
| 通讯 | `msg.send` | 发送消息 | 是 |
| 通讯 | `msg.recall` | 撤回消息（5 分钟内） | 是 |
| 通讯 | `msg.audio_video` | 发起音视频通话 | 是 |
| 通讯 | `msg.external` | 添加外部联系人 | 否 |
| 文件与会议 | `file.upload` | 上传文件（≤500MB） | 是 |
| 文件与会议 | `file.large` | 上传大文件（>500MB） | 否 |
| 文件与会议 | `meet.create` | 创建会议室 | 是 |
| 文件与会议 | `meet.record` | 会议录制 | 否 |
| 群组 | `group.create` | 创建群组 | 是 |
| 群组 | `group.invite_ext` | 邀请外部成员入群 | 否 |
| 群组 | `group.broadcast` | 发送群公告 | 否 |
| 数据 | `data.export_chat` | 导出个人聊天记录 | 是 |
| 数据 | `data.export_org` | 导出企业通讯录 | 否 |

> 企业管理员（role=2）默认拥有全部权限；其余角色按上表"全员默认"取值，再叠加 `t_user_permission` 的逐人覆盖。

## 附录 B：本接口清单引出的库表改动

- **新增表**：在原 9 张表基础上再加 `t_admin_session`（token）、`t_user_activity_log`（操作历史）、`t_announcement_read`（公告已读回执），共 **12 张新表**。
- **`t_admin`**：去掉 `f_2fa_enabled`；`f_password` 以 MD5 加盐存储。
- **现有表多租户改造**：`t_user` / `t_department` / `t_department_member` 增加 `f_enterprise_id`。
