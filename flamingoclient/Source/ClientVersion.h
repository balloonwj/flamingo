/**
 *  PC 客户端版本号的<u>唯一出处</u>，ClientVersion.h
 *
 *  版本号此前散在三处且互相不一致：登录时报给服务端的是 "3.2.1"（net/IUSocket.cpp）、
 *  关于页显示的是 "2.0.0"（BalloonMain/AboutDlgModel.h）、exe 文件属性里是 2,0,0,0
 *  （flamingo.rc）。自动更新上线后这种不一致会直接表现为"升级完还提示有新版本"——
 *  服务端拿登录上报的版本号比对，而用户看到的是关于页那个，两者对不上就永远升不完。
 *  故收口到本文件，其余各处一律引用它。
 *
 *  <u>发版时要改的地方</u>：只改本文件下面那三个版本号宏，且版本号必须与控制台
 *  「客户端版本」页填的版本号一致。flamingo.rc 已 #include 本文件，exe 文件属性
 *  里的版本会跟着一起变，不必再手工同步。
 *
 *  <u>本文件必须保持 CRLF 换行，不要改成 LF</u>：资源编译器 rc.exe 只把 CRLF 当换行，
 *  遇到纯 LF 的文件会把整个文件看成一行 —— 于是第一个 // 注释就把它后面所有 #define
 *  一路吞到文件末尾，宏一个都定义不了。表现是 rc.exe 报
 *  "error RC2104: undefined keyword or key name: FLAMINGO_CLIENT_VERSION_RC"，
 *  而同一个头文件被 cl.exe 编译却毫无问题（cl.exe 认 LF），很容易看岔。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

//客户端版本号，点分十进制。登录（cmd_login 的 clientversion）与检查更新
//（cmd_check_update 的 version）都上报它，服务端据此判断要不要升级、是否强制。
#define FLAMINGO_CLIENT_VERSION      "2.0.0"

//上面同一个版本号写给资源脚本的两种形式。资源脚本的 FILEVERSION / PRODUCTVERSION
//只认逗号分隔的四段数字，没法直接吃 "3.2.1" 这种字符串，故单列一份；第四段是
//构建序号，本项目不用，固定为 0。改版本号时这三个宏要一起改。
#define FLAMINGO_CLIENT_VERSION_RC       2,0,0,0
#define FLAMINGO_CLIENT_VERSION_RC_STR   "2.0.0.0"

//关于页显示的 build 号<u>不在这里</u>：它已改为编译期自动取当次的构建时间，
//见 UI/BalloonMain/AboutDlgModel.cpp 的 AboutBuildStamp。发版时不必再手工改它。

//宽字符版本的同一个值。界面层（关于页、升级窗）用它，
//不在那边另写一份字面量 —— 否则又回到“版本号散在多处”的老问题。
//两层宏是必需的：一层展开参数，一层才能把 L 拼到展开后的字面量上。
#define FLAMINGO_WIDEN2(x)           L ## x
#define FLAMINGO_WIDEN(x)            FLAMINGO_WIDEN2(x)
#define FLAMINGO_CLIENT_VERSION_W    FLAMINGO_WIDEN(FLAMINGO_CLIENT_VERSION)
