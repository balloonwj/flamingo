# FlamingoIM 控制台 Admin 接口文档

> 配套设计稿：`design/FlamingoIM Enterprise Admin.html`
> 接口逐模块确认于 2026.05.21，共 9 个模块、52 个接口。

## 一、通用约定

- **接入点**：admin 接口挂在 chatserver 的 `HttpServer` 上（`httplistenport`，默认 12345）。
- **请求**：一律 `POST /admin/<模块>/<动作>.do`，请求体为 JSON。
  - `HttpSession` 当前只解析 HTTP 请求行，需扩展为支持解析 POST 请求体。
- **应答**：HTTP 200，body 为 JSON：`{"code":0,"msg":"ok", ...数据}`。
  - `code` 复用 `chatserversrc/Msg.h` 的 `enum error_code`（`0`成功、`1`未知失败、`2`未登录/token 失效、`103`密码错误、`110`~`116`部门相关等）；admin 专属错误码后续在 `Msg.h` 扩充。
  - `export` 类接口应答为 CSV 文本（`Content-Type: text/csv`），非 JSON。
- **鉴权**：除 `login.do` 外，所有请求体须含 `token`；服务端按 token 查 `t_admin_session` 校验登录态，并按管理员的管辖范围（平台超管 / 限定单企业）做越权拦截。
- **审计**：所有"写"接口成功后自动写一条 `t_admin_audit_log`，无独立的审计写接口。
- **不做双因子（2FA）**：`login.do` 仅邮箱 + 密码。

---

## 二、模块 1：认证 Auth（5）

### 1.1 `POST /admin/login.do` —— 管理员登录
- 请求：`{"email":"li@flamingo.com", "password":"明文密码"}`
- 应答：`{"code":0,"msg":"ok","token":"<会话令牌>","admin":{"adminid":N,"name":"李正阳","email":"...","enterpriseid":0}}`
- 说明：`enterpriseid=0` 为平台超级管理员，非 0 表示仅管辖该企业。密码在库中以 MD5 加盐存储，服务端比对哈希。

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
- 说明：平台超管返回全平台口径，企业级管理员返回其所辖企业口径。

### 2.2 `POST /admin/dashboard/messagetrend.do` —— 近 14 天消息量
- 请求：`{"token":"..."}`
- 应答：`{"code":0,"msg":"ok","trend":[{"date":"MM-DD","count":N}]}`（`trend` 恰为 14 个元素，按日期从旧到新、最后一个为今天；缺消息的天 `count` 为 0）
- 说明：平台超管统计全平台消息，企业级管理员只统计本企业员工发出的消息。供仪表盘「近 14 天消息量」柱状图使用。

---

## 四、模块 3：企业管理 Enterprise（10）

### 3.1 `POST /admin/enterprise/list.do` —— 企业列表
- 请求：`{"token":"...","keyword":"","status":-1,"page":1,"pagesize":20}`（`status=-1` 不筛选）
- 应答：`{"code":0,"msg":"ok","total":N,"list":[{"enterpriseid":N,"name":"...","shortname":"...","industry":"...","region":"...","members":N,"depts":N,"plan":N,"status":N,"adminname":"...","opendate":"...","rootdeptid":N}]}`
- `rootdeptid`：**公司根部门 ID**，组织架构树上代表"公司本身"的那个真实部门，员工可以直接挂在它下面。`0` 表示该企业尚未建立根部门（存量数据迁移之前），此时管理端会退回旧的合成根节点显示。

### 3.2 `POST /admin/enterprise/detail.do` —— 企业详情
- 请求：`{"token":"...","enterpriseid":N}`
- 应答：`{"code":0,"msg":"ok","enterprise":{企业全字段}}`（含 `rootdeptid`，含义同 3.1）

### 3.3 `POST /admin/enterprise/create.do` —— 新增企业（连带建初始管理员与公司根部门）
- 请求：`{"token":"...","name":"...","shortname":"...","industry":"...","region":"...","plan":N,"adminname":"...","adminemail":"..."}`
- 应答：`{"code":0,"msg":"ok","enterpriseid":N,"adminid":N,"initialpassword":"..."}`
- 说明：同时在 `t_admin` 写一条企业级管理员（`enterpriseid` 为新企业、邮箱为 `adminemail`），并返回生成的初始密码。
- 说明：同时在 `t_department` 建一行**公司根部门**（`f_parent_dept_id=0`、层级 1、名字取企业全称），并把其 ID 回写到 `t_enterprise.f_root_dept_id`。企业改名（3.4）时该部门名同步更新；该部门不允许删除或移动。

### 3.4 `POST /admin/enterprise/update.do` —— 修改企业信息
- 请求：`{"token":"...","enterpriseid":N,"name":"...","shortname":"...","industry":"...","region":"...","plan":N}`
- 应答：`{"code":0,"msg":"ok"}`

### 3.5 `POST /admin/enterprise/setstatus.do` —— 审核/活跃/停用企业
- 请求：`{"token":"...","enterpriseid":N,"status":N}`（status: 0待审核 1活跃 2停用）
- 应答：`{"code":0,"msg":"ok"}`

### 3.6 `POST /admin/enterprise/export.do` —— 导出企业列表
- 请求：`{"token":"...","keyword":"","status":-1}`
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
- 请求：`{"token":"...","enterpriseid":N,"deptid":N,"name":"...","title":"...","employeeno":"...","email":"...","phone":"...","joindate":"..."}`
- 应答：`{"code":0,"msg":"ok","userid":N,"initialpassword":"..."}`
- `deptid` 传 `0` 表示"直接挂在公司下"，服务端会把员工加入该企业的公司根部门。企业尚未建立公司根部门时返回 `code=110`（部门不存在）并拒绝创建 —— 员工必须有确定的部门归属，否则会成为组织架构里看不见的孤儿账号。

### 5.4 `POST /admin/user/update.do` —— 修改员工基本信息
- 请求：`{"token":"...","userid":N,"name":"...","title":"...","employeeno":"...","joindate":"...","landline":"...","workstation":"...","phone":"...","email":"...","signature":"..."}`
- 应答：`{"code":0,"msg":"ok"}`

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
- 应答：`{"code":0,"msg":"ok","newpassword":"..."}`（生成新密码并返回）

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

## 八、模块 7：平台公告 Announcement（7）

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
- 请求：`{"token":"...","serviceid":0,"enterpriseid":N,"name":"...","kind":N,"description":"...","avatarcolor":"...","allowunsubscribe":bool}`（`serviceid=0` 表示新建；kind: 0通知型 1助手型）
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
- **`HttpSession`**：需扩展为支持解析 POST 请求体。
