### Flamingo

[阅读中文版？](README.md)

#### Introduction

Flamingo is a high-performance & light-weight open-source IM software. It consists of a server side, a Windows PC client, an Android client and an enterprise admin console. The server code doubles as a general-purpose C++11 server framework.

#### Feature List

Flamingo has implemented these features below（Remind that we just list the features here related to network communication, as to some other features, you can explore them in Flamingo by yourself.）

- user registering, logining, modifying password and profile (nickname, signature, customed avatar, etc)
- searching users and groups, joining in groups
- displaying enterprise contact directory, group list and recent session list
- chatting with a single contact (texts, emotions, images, window shake and offline files)
- chatting in groups (texts, emotions, group announcements, group files, @mentions)
- message favorites, message pinning, chat history, message box and platform announcements
- audio/video calls and remote desktop
- sending messages to multiple contacts or groups (This is different from chatting in groups)
- dectecting new version and upgrading automatically, reconnecting automatically when disconnecting
- enterprise admin console: enterprise / department / employee management, announcement publishing, dashboard

> **Note on the enterprise edition:** this branch is the enterprise edition. The whole friend-relationship
> subsystem was removed on 2026-08-05. The contact directory is derived directly from the enterprise
> organization structure, so colleagues of the same enterprise can see and message each other without
> "adding a friend" first. There is no add-friend / delete-friend / friend group / friend tag any more.
> Contact remark names and the blacklist are kept. On the wire, 1203~1208, 1210 and 1214 are retired
> and their numbers must not be reused — see [`docs/协议设计-v2.md`](docs/协议设计-v2.md).

#### Repository Structure

| Top-level directory | Description |
| :--- | :--- |
| `flamingoserver/` | Server side (C++11), builds chatserver / fileserver / imgserver |
| `flamingoclient/` | Windows PC client (MFC + WTL + the balloonui owner-drawn control library) |
| `flamingoAdmin/` | Enterprise admin console (Windows desktop app, manages enterprises and employees over HTTP) |
| `flamingoAndroid/` | Android client (Gradle project) |
| `docs/` | Protocol design, per-feature integration plans, database design |
| `snapshots/` | Screenshots used by this README |
| `tools/` | Helper scripts not tied to a specific sub-project |
| `third-party/` | Vendored third-party sources (opus, speexdsp, etc), large and not tracked in git |

Internal layout of the server and the two Windows projects:

```
flamingoserver/
├── net/            Reactor networking: EventLoop / TcpServer / Poller / protocol stream
├── base/           Infrastructure: singleton, async logging, config reader, timestamp
├── utils/          Utilities: MD5, SHA-256, Base64, password hashing, tokens, daemonize
├── dbapi/          Database abstraction; business code depends only on this layer
├── mysqlapi/       MySQL backend           sqliteapi/  SQLite backend
├── mysqlmgr/       Database worker thread pool
├── chatserversrc/  Chat service: sessions, command dispatch, business managers
├── fileserversrc/  File service (imgserver reuses four of its files)
├── imgserversrc/   Image service and remote-desktop relay
├── tests/          doctest unit tests
├── etc/            Config files for the three services   table.sql  Schema script
├── windowsproj/    Visual Studio project files           tools/     Database & e2e scripts
└── migrations/     Database migration scripts

flamingoclient/
├── Source/         Client main program. The Balloon* sub-directories are split by UI module
│                   (main panel, chat window, contacts, group management, history, screenshot,
│                   remote desktop, ...); net/ holds the protocol layer
├── balloonui/      Owner-drawn control library (a separate git repository, see below)
├── FlamingoUpdater/    Zip extractor used by the auto-updater
└── Bin/            Run directory with Skins / Image / Face / Sound / config resources

flamingoAdmin/
├── Screens/        Feature screens      Widgets/  Owner-drawn controls
├── Shell/          Window shell and navigation    Theme/  Colors and styling
├── Util/           HTTP client, API wrappers, pure-function helpers
└── Tests/          Unit tests
```

#### Quick Start

The quickest way to get a working setup is to run the server on SQLite, which needs no database service at all:

```shell
# 1. Build the server (Linux or WSL)
cd flamingoserver
cmake -S . -B build-linux -DWITH_MYSQL=OFF
make -C build-linux -j$(nproc)

# 2. Start the three services (working directory MUST be flamingoserver/)
./build-linux/fileserver -d
./build-linux/imgserver  -d
./build-linux/chatserver -d
```

On first start chatserver creates the database and tables automatically, plus a platform super
administrator account used to sign in to the admin console:

| Item | Default |
| :--- | :--- |
| Email | `admin@flamingo.com` |
| Password | `admin123` |

> **Change this before any real deployment.** The account is controlled by `defaultadminenable` /
> `defaultadminemail` / `defaultadminpassword` in `etc/chatserver.conf`, and the default password is
> public in this repository. Setting `defaultadminenable` to 0 disables the behaviour entirely.

Then build the PC client with Visual Studio 2022 (`flamingoclient/FlamingoClient.sln`, configuration
`Debug|x64`) and run `flamingoclient/Bin/Flamingo.exe`; it connects to the local server out of the box.
Employee accounts are created from the admin console.

#### Server

##### Building on Linux / WSL

The server is written in pure C++11, so gcc/g++ 4.7 or newer is required. An out-of-source build is
recommended so that CMake output does not mix with the Visual Studio projects under `windowsproj/`:

```shell
cd flamingoserver
cmake -S . -B build-linux -DWITH_MYSQL=OFF
make -C build-linux -j$(nproc)
```

`WITH_MYSQL` is the compile switch for the MySQL backend and defaults to `ON`. With `OFF` the binaries
do not link against `libmysqlclient` and support `dbtype=sqlite` only, which suits local development and
deployments that should not require a database service. Drop the option to keep MySQL support, in which
case **mysql-devel** (or **mariadb-devel**) must be installed.

The build produces `chatserver`, `fileserver`, `imgserver` and the test runner `flamingo_tests` under `build-linux/`.

##### Building on Windows

Open `flamingoserver/FlamingoServer.sln` with Visual Studio 2022. The solution contains four projects:
`chatserver`, `fileserver`, `imgserver` and `Tests`. **Use the `Debug|Win32` configuration** — the other
configurations are not maintained.

##### What each service does

| Service | Description |
| :---: | :--- |
| chatserver | Chat service: registration, login, messaging, organization structure; also serves the admin console over HTTP |
| fileserver | File service: offline files, group files and client upgrade packages |
| imgserver | Image service: chat images and custom avatars; also relays remote-desktop traffic |

##### Database

Both MySQL and SQLite are supported, selected by `dbtype` in `etc/chatserver.conf`. A single binary
supports both modes (a binary built with `WITH_MYSQL=OFF` supports SQLite only).

- **SQLite**: a single local file, no extra service, located by `dbpath`. **A relative path is resolved
  against the directory holding the chatserver executable, not the process working directory** — so the
  same database is opened no matter where the server is started from. The file and its parent
  directories are created automatically on first run.
- **MySQL**: configured by `dbserver` / `dbuser` / `dbpassword` / `dbname`. The database and tables are
  created automatically on first start.

The schema for both backends comes from `table.sql`. The enterprise edition has thirty-odd tables; that
file is the authoritative list. The core ones:

| Table | Description |
| :---: | :--- |
| `t_user` | User accounts |
| `t_enterprise` | Enterprises |
| `t_department` / `t_department_member` | Department tree and members; the contact directory is derived from these |
| `t_chatmsg` / `t_conversation` | Chat messages and conversations |
| `t_group` / `t_group_member` | Groups and group members |
| `t_contact_remark` | Contact remark names (one-way) |
| `t_blacklist` | Blacklist |
| `t_admin` | Admin console accounts |

> `table.sql` is the single source of truth. `table_sqlite.sql` and `sqliteapi/SqliteSchema.cpp` are
> generated from it by `tools/gen_table_sqlite.py` — never edit those two by hand.
>
> The friend tables `t_user_relationship` and `t_friend_request` were dropped together with the
> friend subsystem on 2026-08-05.

##### Configuration and startup

The three services read `etc/chatserver.conf`, `etc/fileserver.conf` and `etc/imgserver.conf`. Every key
is documented in [`docs/服务端配置项说明.md`](docs/服务端配置项说明.md).

The services are independent and can be started in the foreground, or as daemons with `-d`. **The working
directory must be `flamingoserver/`**, otherwise the relative paths `etc/`, `logs/` and `filecache/` do not
resolve (`-d` does not change the working directory):

```shell
./build-linux/chatserver -d
./build-linux/fileserver -d
./build-linux/imgserver  -d
```

Logs are written to `logs/`, named after the service and its start time.

##### Ports

| Port | Service | Description |
| :---: | :---: | :--- |
| 20000 | chatserver | Client chat port |
| 18889 | chatserver | Monitor port; connect with telnet / nc to inspect live server state |
| 12345 | chatserver | HTTP port; the admin console API lives here |
| 20001 | fileserver | File service port |
| 20002 | imgserver | Image service port |
| 20003 | imgserver | Remote-desktop relay port |

Connect to the monitor port with `nc -v 127.0.0.1 18889`. It supports three commands: `help`, `ul`
(online user list) and `su [userid]` (inspect one user):

```
[zhangyl@localhost flamingoserver]$ nc -v 127.0.0.1 18889
1. help-show help info
2. ul-show online user list
3. su-show userinfo specified by userid: su [userid]
ul
No user online.
```

##### Unit tests

The server uses doctest; the tests live in `tests/`:

```shell
cd flamingoserver
./build-linux/flamingo_tests                      # all test cases
./build-linux/flamingo_tests --test-case="SHA*"   # filter by name
```

On Windows, build the `Tests` project and run `windowsproj/Tests.exe`.

#### PC Client

Open `flamingoclient/FlamingoClient.sln` with Visual Studio 2022. The solution holds six projects:

| Project | Description |
| :---: | :--- |
| `Flamingo` | The PC client itself |
| `balloonui_static` | Owner-drawn control library that renders the client UI |
| `FlamingoUnit` | Client unit tests (Catch2) |
| `FlamingoUpdater` | Zip extractor used by the auto-updater |
| `opus_static` / `speexdsp_static` | Third-party audio libraries used by audio/video calls |

##### Output directories

**`Debug|x64` is the preferred configuration** — it is the only one that builds into the resource-complete
`Bin/` directory and can therefore be run in place:

| Configuration | Output directory | Runnable in place |
| :--- | :--- | :--- |
| `Debug\|x64` | `Bin/` | Yes, all resources present |
| `Release\|x64` | `Bin/x64/Release/` | No, the directory holds only the exe |
| `Debug\|Win32` | `Bin/Win32/Debug/` | No |
| `Release\|Win32` | `Bin/Win32/Release/` | No |

The other three output directories contain no `Skins`, `Image`, `Face` or `config` resources, so an
executable started from there is an empty shell. To run one of them, copy it into `Bin/` under a
different name first.

##### Running and configuration

Server addresses, ports and heartbeat settings live in `Bin/config/flamingo.ini`. **That file must stay
ASCII** — non-ASCII characters cause garbled UI text. It points at `127.0.0.1` by default, matching a
local server.

Runtime data is kept per signed-in account under `Bin/Users/<account>/`: `ChatImage/` for chat images,
`Avatar/` for avatars, `ChatLog/` for the local message and announcement databases, `ChatFile/` for files.

##### Login-free debug entries

The client accepts a set of command-line switches that open a single window filled with mock data,
which is handy for UI work:

```shell
Flamingo.exe --mainpanel       # main panel
Flamingo.exe --chatwindow      # chat window
Flamingo.exe --groupmanage     # group management
Flamingo.exe --profile         # user profile
```

See the command-line handling in `Source/MainDlg.cpp` for the full list.

##### Unit tests

The client uses Catch2, with tests under `Source/Tests/`. Build the `FlamingoUnit` project and run the
resulting `FlamingoUnit.exe`; it can filter by test name or tag.

##### About balloonui

`third-party/balloonui/` is the owner-drawn control library, shared by the PC client and the flamingoAdmin
console. **It is a separate git repository** with its own history and remote, and is not committed together
with the main repository. Changes inside that directory must be committed to its own repository. (Before
2026-08-13 it lived at `flamingoclient/balloonui/`.)

#### Enterprise Admin Console

`flamingoAdmin/` is the enterprise admin console: it manages enterprises, departments, employees and
announcements, and shows a dashboard. It talks to the server over chatserver's HTTP port (12345 by
default); the API is documented in [`docs/Admin控制台接口文档.md`](docs/Admin控制台接口文档.md).

Open `flamingoAdmin/flamingoAdmin.sln` with Visual Studio 2022 and build configuration `Debug|x64`. The
solution holds three projects — `balloonui_static`, `flamingoAdmin` and `flamingoAdminTests` — all built
in one go:

| Output | Description |
| :--- | :--- |
| `Bin/x64/flamingoAdmin.exe` | The admin console |
| `Bin/x64/flamingoAdminTests.exe` | Unit tests; run it directly, it prints the pass/fail counts at the end |

The console has no mock mode, so chatserver must be running before you can sign in. Credentials are in
the Quick Start section above.

#### Android Client

Open the project under `flamingoAndroid` with Android Studio and build the apk, then install it on your
phone. The server address is configured in the "server settings" of the login screen.

#### Documentation Index

Main documents under `docs/`:

| Document | Contents |
| :--- | :--- |
| [协议设计-v2.md](docs/协议设计-v2.md) | v2 protocol: command ids, request/response JSON examples (authoritative) |
| [Admin控制台接口文档.md](docs/Admin控制台接口文档.md) | HTTP API used by the admin console |
| [服务端配置项说明.md](docs/服务端配置项说明.md) | Key-by-key reference for the three server config files |
| [SQLite本地数据库模式方案.md](docs/SQLite本地数据库模式方案.md) | Design and implementation of the SQLite backend |
| [git代码提交规范及简明教程.md](docs/git代码提交规范及简明教程.md) | Commit conventions |

The remaining files are per-feature integration plans (group management, group announcements, message
box, message pinning, favorites, audio/video calls, remote desktop, and so on).

#### Change Log

I am afraid that I can not maintain Flamingo in regular intervals because I am very busy with my own work. But I promise that I am going to maintain Flamingo continually.

Change Log see here: 

[Flamingo Change Log](https://github.com/baloonwj/flamingo/issues/1)

#### BUG Report

If you have any questions, you can contact with me by sending email to balloonwj@qq.com. Also, you can report any bugs in the BUG Report Page, its link is https://github.com/baloonwj/flamingo/issues/5.

I promise that fatal problem of Flamingo, such as crashing or halting will be solved in three weekdays and other questions that are not severe will be responsed in two weeks.

#### Contact

At last not the least, if you have any suggestions or questions, welcome to contact with me. China Mainland users can contact with me by joining in my QQ group(**729995516**) or linking my public webchat account(『**easy_coder**』).  Also you can send me emails,  my email address is **balloonwj@qq.com**. I am waiting for You sincerely.

#### Screenshots

##### PC Client

![Main panel](snapshots/pc-main-panel.png)
<!-- placeholder: main panel showing the contact directory and recent sessions -->

![Chat window](snapshots/pc-chat.png)
<!-- placeholder: one-to-one chat with text, emotions and images -->

![Group chat](snapshots/pc-group-chat.png)
<!-- placeholder: group chat with member list and group announcement -->

![Chat history](snapshots/pc-history.png)
<!-- placeholder: chat history window -->

##### Enterprise Admin Console

![Dashboard](snapshots/admin-dashboard.png)
<!-- placeholder: admin console dashboard -->

![Organization](snapshots/admin-org.png)
<!-- placeholder: organization structure and employee management -->

##### Android Client

![Session list](snapshots/android-session.jpg)
<!-- placeholder: session list on Android -->

![Chat](snapshots/android-chat.jpg)
<!-- placeholder: chat screen on Android -->
