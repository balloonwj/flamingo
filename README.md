### FlamingoIM

[Read in English?](README_en.md)

**我为 Flamingo 专门录制了两部高清技术讲解视频以方便读者学习，视频中介绍了Flamingo的编译和部署方法、整体架构、各个模块的技术实现细节以及如何学习Flamingo的方法，需要视频教程的读者可以扫描[高性能服务器开发]公众号二维码，关注后并在后台回复关键字“flamingo”即可得到下载链接。**

![高性能服务器开发](snapshots/easyserverdev_logo.jpg)
<!-- 占位：公众号 logo -->

#### 介绍

Flamingo IM 是一款高性能、轻量级的开源即时通讯软件，包含服务器端、PC 端、安卓端与企业管理控制台。服务器代码同时也是一款通用的 C++11 服务器框架。

#### 功能列表

目前已经实现了如下功能（这里只列举网络相关的功能，其他客户端已经实现的功能不统计在列，请自行发现）：

- 注册、登录、修改密码、修改个人资料（昵称、签名、个性头像等）
- 查找联系人、查找群、加入群组
- 通讯录（企业组织架构）、群列表、最近会话
- 单人聊天（文字、表情、图片、窗口抖动、离线文件）
- 群组聊天（文字、表情、群公告、群文件、@提醒）
- 消息收藏、消息 Pin、历史消息记录、消息盒子与平台公告
- 音视频通话、远程桌面
- 群发消息、自动升级、断线重连
- 企业管理控制台：企业 / 部门 / 员工管理、公告投放、数据看板

> **企业版说明：** 本分支为企业版，已于 2026-08-05 整体删除好友关系子系统。通讯录直接来自企业组织架构，
> 同一企业的同事天然互相可见、可直接发消息，不需要“先加好友”这一步，因此没有加好友、删好友、
> 好友分组、好友标签这些功能。联系人备注名与黑名单保留。协议层面 1203~1208、1210、1214
> 已作废且编号不复用，详见 [`docs/协议设计-v2.md`](docs/协议设计-v2.md)。

#### 仓库结构

| 顶层目录 | 说明 |
| :--- | :--- |
| `flamingoserver/` | 服务器端（C++11），产出 chatserver / fileserver / imgserver 三个可执行文件 |
| `flamingoclient/` | Windows PC 客户端（MFC + WTL + balloonui 自绘控件库） |
| `flamingoAdmin/` | 企业管理控制台（Windows 桌面程序，走 HTTP 接口管理企业与员工） |
| `flamingoAndroid/` | 安卓客户端（Gradle 工程） |
| `docs/` | 协议设计、各功能接入方案、数据库方案等设计文档 |
| `snapshots/` | README 使用的界面截图 |
| `tools/` | 与具体工程无关的辅助脚本 |
| `third-party/` | 第三方依赖源码副本（opus、speexdsp 等），体积较大，不入版本库 |

服务端与两个 Windows 工程的内部结构：

```
flamingoserver/
├── net/            Reactor 网络框架：EventLoop / TcpServer / Poller / 协议流
├── base/           基础设施：单例、异步日志、配置文件读取、时间戳
├── utils/          通用工具：MD5、SHA-256、Base64、口令哈希、令牌、守护进程
├── dbapi/          数据库抽象层，业务代码只依赖这一层
├── mysqlapi/       MySQL 后端实现        sqliteapi/  SQLite 后端实现
├── mysqlmgr/       数据库工作线程池
├── chatserversrc/  聊天服务：会话、协议分发、各业务 Manager
├── fileserversrc/  文件服务（imgserver 复用其中四个文件）
├── imgserversrc/   图片服务与远程桌面中转
├── tests/          doctest 单元测试
├── etc/            三个服务的配置文件      table.sql  数据库建表脚本
├── windowsproj/    Visual Studio 工程文件  tools/     数据库与联调脚本
└── migrations/     数据库变更脚本

flamingoclient/
├── Source/         客户端主程序。Balloon* 子目录按界面模块划分（主面板、聊天窗、
│                   通讯录、群管理、历史记录、截图、远程桌面等），net/ 为协议层
├── balloonui/      自绘控件库（独立的 git 仓库，见下文说明）
├── iAutoUpdate/    自动升级用的 zip 解压工具
└── Bin/            运行目录，含 Skins / Image / Face / Sound / config 等资源

flamingoAdmin/
├── Screens/        各功能页面        Widgets/  自绘控件
├── Shell/          窗口外壳与导航     Theme/    配色与样式
├── Util/           HTTP 客户端、接口封装、纯函数工具
└── Tests/          单元测试
```

#### 快速开始

最省事的联调方式是让服务端使用 SQLite，不需要安装任何数据库服务：

```shell
# 1. 编译服务端（Linux 或 WSL）
cd flamingoserver
cmake -S . -B build-linux -DWITH_MYSQL=OFF
make -C build-linux -j$(nproc)

# 2. 启动三个服务（工作目录必须是 flamingoserver/）
./build-linux/fileserver -d
./build-linux/imgserver  -d
./build-linux/chatserver -d
```

首次启动时 chatserver 会自动建库建表，并创建一个平台超级管理员账号，用于登录管理控制台：

| 项目 | 默认值 |
| :--- | :--- |
| 邮箱 | `admin@flamingo.com` |
| 密码 | `admin123` |

> **部署前务必修改。** 该账号由 `etc/chatserver.conf` 的 `defaultadminenable` / `defaultadminemail` /
> `defaultadminpassword` 控制，默认口令在源码仓库里是公开的。也可以把 `defaultadminenable` 置 0 完全关闭这一行为。

接着用 Visual Studio 2022 编译 PC 客户端（`flamingoclient/FlamingoClient.sln`，选 `Debug|x64`），直接运行 `flamingoclient/Bin/Flamingo.exe` 即可连上本机服务端。员工账号请在管理控制台里创建。

#### 服务器端

##### Linux / WSL 下编译

服务器代码使用纯 C++11 开发，gcc/g++ 版本需在 4.7 以上。推荐使用 out-of-source 方式构建，避免 CMake 的生成物与 `windowsproj/` 下的 Visual Studio 工程混在一起：

```shell
cd flamingoserver
cmake -S . -B build-linux -DWITH_MYSQL=OFF
make -C build-linux -j$(nproc)
```

`WITH_MYSQL` 是 MySQL 后端的编译开关，默认为 `ON`。置为 `OFF` 时编出的二进制不依赖 `libmysqlclient`，只支持 `dbtype=sqlite`，适合本地联调与不想安装数据库服务的部署；需要连接 MySQL 时去掉该选项即可，此时需安装 **mysql-devel**（或 **mariadb-devel**）。

产物为 `build-linux/` 下的 `chatserver`、`fileserver`、`imgserver` 与测试程序 `flamingo_tests`。

##### Windows 下编译

用 Visual Studio 2022 打开 `flamingoserver/FlamingoServer.sln`，解决方案含 `chatserver`、`fileserver`、`imgserver`、`Tests` 四个工程。**请使用 `Debug|Win32` 配置编译**，其他配置未做维护。

##### 三个服务的分工

| 服务名称 | 用途说明 |
| :---: | :--- |
| chatserver | 聊天服务，负责注册、登录、消息收发、组织架构等核心业务，并对管理控制台提供 HTTP 接口 |
| fileserver | 文件服务，用于离线文件、群文件的收发以及客户端升级包下载 |
| imgserver | 图片服务，用于聊天图片与自定义头像的上传下载，并承担远程桌面的数据中转 |

##### 数据库

支持 MySQL 与 SQLite 两种后端，由 `etc/chatserver.conf` 的 `dbtype` 决定，一份二进制同时支持两种模式（`WITH_MYSQL=OFF` 编译时仅支持 SQLite）。

- **SQLite**：单文件本地库，无需额外服务，路径由 `dbpath` 指定。**相对路径以 chatserver 可执行文件所在目录为基准，而不是进程工作目录** —— 这样无论从哪个目录启动，打开的都是同一个库。文件与父目录会在首次运行时自动创建。
- **MySQL**：由 `dbserver` / `dbuser` / `dbpassword` / `dbname` 配置。首次启动会自动建库建表。

两种后端的建表语句都由 `table.sql` 生成，企业版共三十余张表，完整清单以该文件为准。核心的几张：

| 表名 | 用途说明 |
| :---: | :--- |
| `t_user` | 用户信息 |
| `t_enterprise` | 企业 |
| `t_department` / `t_department_member` | 企业部门树与部门成员，通讯录即由此生成 |
| `t_chatmsg` / `t_conversation` | 聊天记录与会话 |
| `t_group` / `t_group_member` | 群与群成员 |
| `t_contact_remark` | 联系人备注名（单向） |
| `t_blacklist` | 黑名单 |
| `t_admin` | 管理控制台账号 |

> `table.sql` 是唯一事实来源，`table_sqlite.sql` 与 `sqliteapi/SqliteSchema.cpp` 由 `tools/gen_table_sqlite.py` 从它生成，请勿手工编辑这两个文件。
>
> 好友关系表 `t_user_relationship` 与加好友申请表 `t_friend_request` 已随好友子系统于 2026-08-05 删除。

##### 配置与启动

三个服务的配置分别位于 `etc/chatserver.conf`、`etc/fileserver.conf`、`etc/imgserver.conf`，逐项说明见 [`docs/服务端配置项说明.md`](docs/服务端配置项说明.md)。

三个服务相互独立，可直接前台启动，也可加 `-d` 以守护进程方式启动。**工作目录必须是 `flamingoserver/`**，否则 `etc/`、`logs/`、`filecache/` 等相对路径无法解析（`-d` 不会改变工作目录）：

```shell
./build-linux/chatserver -d
./build-linux/fileserver -d
./build-linux/imgserver  -d
```

日志写入 `logs/` 目录，按服务名与启动时间命名。

##### 端口说明

| 端口号 | 所属服务 | 用途说明 |
| :---: | :---: | :--- |
| 20000 | chatserver | 客户端聊天端口 |
| 18889 | chatserver | 监控端口，可用 telnet / nc 连接查看服务器实时状态 |
| 12345 | chatserver | HTTP 端口，管理控制台的接口即走这里 |
| 20001 | fileserver | 文件服务端口 |
| 20002 | imgserver | 图片服务端口 |
| 20003 | imgserver | 远程桌面中转端口 |

监控端口用 `nc -v 127.0.0.1 18889` 连接，支持 `help`、`ul`（在线用户列表）、`su [userid]`（查看指定用户）三个命令：

```
[zhangyl@localhost flamingoserver]$ nc -v 127.0.0.1 18889
1. help-show help info
2. ul-show online user list
3. su-show userinfo specified by userid: su [userid]
ul
No user online.
```

##### 单元测试

服务端使用 doctest，测试代码在 `tests/` 目录：

```shell
cd flamingoserver
./build-linux/flamingo_tests                      # 全部用例
./build-linux/flamingo_tests --test-case="SHA*"   # 按用例名过滤
```

Windows 下编译 `Tests` 工程后运行 `windowsproj/Tests.exe` 即可。

#### PC 客户端

用 Visual Studio 2022 打开 `flamingoclient/FlamingoClient.sln`，解决方案包含六个工程：

| 工程名称 | 说明 |
| :---: | :--- |
| `Flamingo` | PC 客户端主程序 |
| `balloonui_static` | 自绘控件库，客户端界面由它绘制 |
| `FlamingoUnit` | 客户端单元测试（Catch2） |
| `iUpdateAuto` | 自动升级用的 zip 解压工具 |
| `opus_static` / `speexdsp_static` | 音视频通话用的第三方音频库 |

##### 输出目录

**`Debug|x64` 是首选配置**，只有它编到资源齐全的 `Bin/` 下，可以直接运行：

| 配置 | 输出目录 | 能否直接运行 |
| :--- | :--- | :--- |
| `Debug\|x64` | `Bin/` | 可以，资源齐全 |
| `Release\|x64` | `Bin/x64/Release/` | 否，目录内只有 exe |
| `Debug\|Win32` | `Bin/Win32/Debug/` | 否 |
| `Release\|Win32` | `Bin/Win32/Release/` | 否 |

后三个配置的输出目录里没有 `Skins`、`Image`、`Face`、`config` 等资源，直接启动是一个空壳。若需运行这些配置的产物，请复制到 `Bin/` 下改名后再启动。

##### 运行与配置

服务器地址、端口、心跳等运行期配置位于 `Bin/config/flamingo.ini`。**该文件必须保持 ASCII 编码**，写入非 ASCII 字符会导致界面文字乱码。默认指向 `127.0.0.1`，与本机服务端配套。

客户端运行期数据按登录账号隔离存放在 `Bin/Users/<账号>/` 下：`ChatImage/` 聊天图片、`Avatar/` 头像、`ChatLog/` 本地消息与公告库、`ChatFile/` 聊天文件。

##### 免登录调试入口

主程序支持一组命令行参数，用 mock 数据直接打开某个窗口，便于调试界面：

```shell
Flamingo.exe --mainpanel       # 主面板
Flamingo.exe --chatwindow      # 聊天窗口
Flamingo.exe --groupmanage     # 群管理
Flamingo.exe --profile         # 个人资料
```

完整列表见 `Source/MainDlg.cpp` 中的命令行判断。

##### 单元测试

客户端使用 Catch2，测试代码在 `Source/Tests/`。编译 `FlamingoUnit` 工程后运行生成的 `FlamingoUnit.exe`，支持按用例名或标签过滤。

##### 关于 balloonui

`flamingoclient/balloonui/` 是客户端使用的自绘控件库，**它是一个独立的 git 仓库**，有自己的提交历史与远端，不随主仓库一起提交。修改该目录下的代码需要单独提交到它自己的仓库。

#### 企业管理控制台

`flamingoAdmin/` 是企业管理控制台，用于管理企业、部门、员工、公告，以及查看数据看板。它通过 chatserver 的 HTTP 端口（默认 12345）与服务端通信，接口文档见 [`docs/Admin控制台接口文档.md`](docs/Admin控制台接口文档.md)。

用 Visual Studio 2022 打开 `flamingoAdmin/flamingoAdmin.sln`，选 `Debug|x64` 编译，解决方案含 `balloonui_static`、`flamingoAdmin`、`flamingoAdminTests` 三个工程，一次编译全部产出：

| 产物 | 说明 |
| :--- | :--- |
| `Bin/x64/flamingoAdmin.exe` | 管理控制台主程序 |
| `Bin/x64/flamingoAdminTests.exe` | 单元测试，直接运行即可，末尾打印通过与失败数 |

控制台没有 mock 数据，需要先启动 chatserver 才能登录使用，登录账号见上文“快速开始”。

#### 安卓客户端

用 Android Studio 打开 `flamingoAndroid` 目录下的安卓工程，编译产生相应的 flamingo.apk 文件，安装至安卓手机即可使用。与服务器的连接设置，在安卓版本的登录界面的服务器设置中。

#### 文档索引

`docs/` 目录下的主要文档：

| 文档 | 内容 |
| :--- | :--- |
| [协议设计-v2.md](docs/协议设计-v2.md) | v2 协议的命令字、请求与应答 JSON 示例（权威文档） |
| [Admin控制台接口文档.md](docs/Admin控制台接口文档.md) | 管理控制台使用的 HTTP 接口 |
| [服务端配置项说明.md](docs/服务端配置项说明.md) | 三个服务配置文件的逐项说明 |
| [SQLite本地数据库模式方案.md](docs/SQLite本地数据库模式方案.md) | SQLite 后端的设计与实现 |
| [git代码提交规范及简明教程.md](docs/git代码提交规范及简明教程.md) | 提交规范 |

其余为各功能模块的接入方案（群管理、群公告、消息盒子、消息 Pin、收藏、音视频通话、远程桌面等）。

#### 更新日志

由于平常工作比较忙，不能固定周期地更新flamingo，但我保证我会一直维护下去。更新日志请参见：

[Flamingo更新日志](https://github.com/baloonwj/flamingo/issues/1)

#### BUG反馈

如果您在使用的过程中发现任何bug，可以给我发邮件：balloonwj@qq.com，也可以去 bug 反馈页面进行反馈：

https://github.com/baloonwj/flamingo/issues/5。

凡是涉及到崩溃、死机等影响使用的重大 bug，我将在三个工作日内予以解决；其他不影响使用的功能性 bug，两个星期内解决。

#### 联系方式

如果您在使用这个项目的过程中遇到任何问题，或者您对我的项目有任何意见或者建议，再或者有不错的想法欢迎与我交流，可以加 **Flamingo微信技术交流群**，先加微信 **easy_coder** 备注“flamingo”，我会邀请你进群。备用 QQ 技术群：**729995516**。

#### 功能截图

##### PC 客户端

![主面板](snapshots/pc-main-panel.png)
<!-- 占位：主面板，展示通讯录（企业组织架构）与最近会话列表 -->

![聊天窗口](snapshots/pc-chat.png)
<!-- 占位：单人聊天窗口，含文字、表情、图片消息 -->

![群聊](snapshots/pc-group-chat.png)
<!-- 占位：群聊窗口，含群成员列表与群公告 -->

![历史消息](snapshots/pc-history.png)
<!-- 占位：历史消息记录窗口 -->

##### 企业管理控制台

![数据看板](snapshots/admin-dashboard.png)
<!-- 占位：管理控制台首页数据看板 -->

![组织架构](snapshots/admin-org.png)
<!-- 占位：企业组织架构与员工管理界面 -->

##### 安卓客户端

![安卓会话列表](snapshots/android-session.jpg)
<!-- 占位：安卓端会话列表 -->

![安卓聊天](snapshots/android-chat.jpg)
<!-- 占位：安卓端聊天界面 -->
