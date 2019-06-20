### Flamingo

#### Introduction

Flamingo is a high-performance & light-weight open-source IM software.  It has server side, Windows pc version and android version so far, and Wechat version and IOS version is under developing.

#### Feature List

Flamingo has implemented these features below（Remind that we just list the features here related to network communication, as to some other features,  you can explore them in Flamingo by yourself.）

- user registering

- user logining

- searching users and groups

- adding user to friend list & joining in groups

- displaying friends list, group list and recent session list

- chatting with a single friend (including sending texts, emotions, images, window shake and offline files, etc).

- chatting in groups(include sending texts, emotions, etc)

- sending messages to multiple friends or groups (This is different from chatting in groups) 

- modifying password

- modifying one's profile(including nickname, signature, customed avatar, etc)

- dectecting new version and upgrading automatically in Windows pc version

- reconnecting automatically when disconnecting

- some other handsome features, such as three mode of friend list, playing animations when a friend going online, chat message history, auto-replying chat message when chatting, etc. You can explore more amazing features by your own in Flamingo.

  

#### Compile and Install

##### Compiling and Installing of Server

###### Dev tools Dependencies

1. Install **cmake**, **makefile** and **gcc** tools.

​     Flamingo server is based on linux OS as platform(CentOS 7.0 is highly recommended), and compilers that support C++ 11 is required because the server code is fully developping with C++ 11. So on linux OS, the version of gcc/g++ must be high than 4.7(4.8.5 is recommended). In addition, cmake and makefile tool is required, so you need to install them.

2. install **mysql**

​    Flaming server uses mysql database to store user data. If your OS is CentOS 7.0, you need to install **mariadb-server**, **mariadb-client** and **mariadb-devel**. On other linux OS, you need to install **mysql-server**, **mysql-client** and **mysql-devel**.

​    The Flamingo **chatserver** will use mysql database. The default database is named "**flamingo**". The database name,  login user account(including username, password, etc) is configured in the file **flamingoserver/etc/chatserver.conf**. You can change this configuration as you will.

​     When **chatserver** startups at first time, it will dectect whether the database you configured exist in mysql. If it does not exist, the **chatserver** will create it including creating its related tables. So you do not need to create the database and tables manually. However the SQL syntax of creating table differs a litle in different mysql version, some SQL of creating tables might failed in some mysql version, therefore under this condition, you have to create tables manually. We prepare these SQLs for you in the file **flamingoserver/table.sql**.

Flamingo server uses four database tables illustrated below:

|     Table Name      |                     Usage                      |
| :-----------------: | :--------------------------------------------: |
|       t_user        |              store user accounts               |
| t_user_relationship | store user relationship and group members info |
|      t_chatmsg      |             store chatting message             |

###### Compiling

1. Enter **flamingoserver/** directory, then type commands below:

   ```
   cmake .
   ```

   If no error occurs, a **Makefile** file will generated. We use this **Makefile** to generate server executable programs in next step.

   

2. Execute commands below to generate executable programs: **chatserver**、**fileserver**、**imgserver**.

   ```shell
   make
   ```

   

   All servers usage illustrated below: 

   | Server Name |                            Usage                             |
   | :---------: | :----------------------------------------------------------: |
   | chatserver  | for user registeration, logining and chatting, and other IM essential functionalities |
   | fileserver  | for offline files transferring when chatting and pc upgrading |
   |  imgserver  | for user avatar downloading & uploading and sending chatting image |

   

   ###### How to Startup Servers

   **chatserver**、**fileserver**、**imgserver** are independent with each other. You can startup one of them in Linux shell or run in daemon mode. Use following commands to run them in daemon mode:

   ```shell
   ./chatserver -d
   ./fileserver -d
   ./imgserver -d
   ```

   

   ##### Port Usage

   You can use lsof command to confirm that the specified ports are on listening state normally:

   ```shell
   [zhangyl@localhost flamingoserver]$ lsof -i -Pn
   COMMAND     PID    USER   FD   TYPE  DEVICE SIZE/OFF NODE NAME
   chatserve 18540 zhangyl   19u  IPv4 1129031      0t0  TCP *:20000 (LISTEN)
   chatserve 18540 zhangyl   27u  IPv4 1129032      0t0  TCP *:8888 (LISTEN)
   chatserve 18540 zhangyl   35u  IPv4 1129033      0t0  TCP *:12345 (LISTEN)
   fileserve 18552 zhangyl   25u  IPv4 1130523      0t0  TCP *:20001 (LISTEN)
   imgserver 18562 zhangyl   25u  IPv4 1131971      0t0  TCP *:20002 (LISTEN)
   ```

   

   | Port Number |   Owner    |                            Usage                             |
   | :---------: | :--------: | :----------------------------------------------------------: |
   |    20000    | chatserver |                        Chatting Port                         |
   |    8888     | chatserver | Port for monitoring chatserver's inner state, you can use telnet or nc command to connect to chatserver to retrieve some realtime status and information of chatserver |
   |    12345    | chatserver | Http service port for chatserver, chatserver both support long TCP and short http connections |
   |    20001    | fileserver |                    File transferring port                    |
   |    20002    | imgserver  |   Tranferring port for sent chat images or user avatars    |

   We can use Linux nc command to connect to the Port 8888 of chatserver in table above, in the form of **nc -v ip port**. The port argument here is 8888 and -v option makes nc command to display more detailed information. Here is the usage:

   ```
   [zhangyl@localhost flamingoserver]$ nc -v 127.0.0.1 8888
   Ncat: Version 6.40 ( http://nmap.org/ncat )
   Ncat: Connected to 127.0.0.1:8888.
   1. help-show help info
   2. ul-show online user list
   3. su-show userinfo specified by userid: su [userid]
   ul
   No user online.
   su
   please specify userid.
   help
   1. help-show help info
   2. ul-show online user list
   3. su-show userinfo specified by userid: su [userid]
   ```

   

   When connected to chatserver with **nc** successfully, you can type any one of the commands in the given list. I hava just implemented three commands as examples, namely: help, ul, su. You can add more functional commands if you need. 

   

   **Hint:**  The server code is not only an IM Server source, but also a powerfully network framework writting in C++ 11. You will be able to of benefit  more from it if you explore it more.

   

   #### PC Client Compiling and Installing

   All you need to do is just that open file **Flamingo.sln** in **flamingoclient/** directory with Visual Studio 2013 and compile it . The solution includes three projects illstrated in the table below:：

   | Project Name |                       Usage                        |
   | :----------: | :------------------------------------------------: |
   |   Flamingo   | The main executable program of Flamingo pc client. |
   | CatchScreen  |     A tool to catch screen in chatting window.     |
   | iUpdateAuto  |     Unzipping downloading files when upgrades.     |

​      When compiling finished successfully, the executable programs will be located in directory **flamingoclient\Bin**. And the configuration file which contains configured lines is located in file **flamingoclient\Bin\config\flamingo.ini**. Run **Flamingo.exe** and have fun!     



#### Android Version Compiling and Installing

Use Android Studio to open the project in directory **flamingoAndroid/** and build to generate **flamingo.apk** file. Install **flamingo.apk** to your mobiles. Server settings of Android version is on the logining Activity.



#### Change Log

I am afraid that I can not maintain Flamingo in regular intervals because I am very busy with my own work. But I promise that I am going to maintain Flamingo continually.

Change Log see here: 

https://github.com/baloonwj/flamingo/issues/1



#### BUG Report

If you have any questions, you can contact with me by sending email to balloonwj@qq.com. Also, you can report any bugs in the BUG Report Page, its link is https://github.com/baloonwj/flamingo/issues/5.

I promise that fatal problem of Flamingo, such as crashing or halting will be solved in three weekdays and other questions that are not severe will be responsed in two weeks.



#### Contact

At last not the least, if you have any suggestions or good advice, welcome to contact with me. China Mainland users can contact with me by joining in my QQ group(**578019391**) or linking my public webchat account(『**easyserverdev**』).  Also you can send me emails,  my email address is **balloonwj@qq.com**. I am waiting for You sincerely.


### flamingo

#### 介绍

flamingo IM是一款高性能、轻量级的开源即时通讯软件，目前包括服务器端、pc端、安卓端，微信版本和IOS版本目前正在开发中。

#### 功能列表

目前已经实现了如下功能（这里只列举网络相关的功能，其他客户端已经实现的功能不统计在列，请自行发现）：

- 注册
- 登录
- 查找好友、查找群
- 添加好友、添加群
- 好友列表、群列表、最近会话
- 单人聊天功能（包括发文字、表情、窗口抖动、离线文件）
- 群组功能（包括发文字、表情）
- 群发消息
- 修改密码
- 修改个人信息（自定义昵称、签名、个性头像等个人信息）
- 自动升级功能
- 断线重连
- 客户端还有很多细节功能，比如头像有三种显示模式、好友上线动画、聊天记录、聊天自动回复功能等，有兴趣的同学可以自己探索尝试一下吧，这里就不截图了。

#### 编译和安装

##### 服务器端的编译与安装

###### 依赖的开发工具

1. 安装cmake、makefile和gcc。

​       作系统是linux，推荐的版本是CentOS 7.0以上。服务器代码使用纯C++11开发，所以您的gcc/g++版本必须至少在4.7以上，推荐的版本是4.8.5。另外，使用cmake和makefile工具进行项目管理和编译，因此您需要安装cmake和makefile工具。

2. 安装mysql。

   使用的数据库是mysql，如果您使用的是CentOS 7.0及以上系统，需要安装**mariadb-server**、**mariadb-client**和**mariadb-devel**。如果您使用的是其他版本的linux系统，请安装**mysql-server**、**mysql-client**和**mysql-devel**。

   聊天服务chatserver会使用到mysql，mysql的库名（默认库名叫flamingo）、登陆用户名和密码配置在**flamingoserver/etc/chatserver.conf**文件中。

   首次启动聊天服务chatserver时，程序会自动检测是否存在flamingo这样的库，如果不存在则创建之，并检测相应的数据表是否存在，如果不存在则创建它们。所以，无需手动创建对应的库和表。当然，不排除由于不同的mysql版本对应的SQL语法有细微差别，可能建表会失败，这个时候你可能需要手动建表，建表语句在**flamingoserver/table.sql**中。flamingo目前使用的四个表分别是：

   |        表名         |        用途说明        |
   | :-----------------: | :--------------------: |
   |       t_user        |       用户信息表       |
   | t_user_relationship | 好友关系及群成员信息表 |
   |      t_chatmsg      |       聊天记录表       |

   

###### 编译方法

1. 进入flamingoserver目录，执行：

   ```
   cmake .
   ```

   如果没有错误，会产生一个Makefile文件。

2. 执行如下命令生成可执行文件：**chatserver**、**fileserver**、**imgserver**。

   ```shell
   make
   ```

   

   各个服务的作用如下：

   |  服务名称  |                           用途说明                           |
   | :--------: | :----------------------------------------------------------: |
   | chatserver |        聊天服务，用于基本的用户注册、登陆、聊天功能。        |
   | fileserver | 文件服务，用于聊天窗口中的离线文件的发送和客户端自动升级包的下载。 |
   | imgserver  | 图片服务，用于聊天中图片文件的发送与用户自定义头像的上传与下载。 |

   

   ###### 启动方式

   **chatserver**、**fileserver**、**imgserver**这三个服务是相互独立的，可以直接在linux shell中单独启动，也可以以守护进程的形式启动，守护进程形式启动命令：

   ```shell
   ./chatserver -d
   ./fileserver -d
   ./imgserver -d
   ```

   

   ##### 使用的端口说明

   可以使用一下lsof命令查看端口是否处于正常侦听状态：

   ```shell
   [zhangyl@localhost flamingoserver]$ lsof -i -Pn
   COMMAND     PID    USER   FD   TYPE  DEVICE SIZE/OFF NODE NAME
   chatserve 18540 zhangyl   19u  IPv4 1129031      0t0  TCP *:20000 (LISTEN)
   chatserve 18540 zhangyl   27u  IPv4 1129032      0t0  TCP *:8888 (LISTEN)
   chatserve 18540 zhangyl   35u  IPv4 1129033      0t0  TCP *:12345 (LISTEN)
   fileserve 18552 zhangyl   25u  IPv4 1130523      0t0  TCP *:20001 (LISTEN)
   imgserver 18562 zhangyl   25u  IPv4 1131971      0t0  TCP *:20002 (LISTEN)
   ```

   

   | 端口号 |  所属服务  |                           用途说明                           |
   | :----: | :--------: | :----------------------------------------------------------: |
   | 20000  | chatserver |                           聊天端口                           |
   |  8888  | chatserver | 聊天服务的监控端口，可以使用telnet或nc命令远程连接至聊天服务去查看服务器的一些实时状态信息 |
   | 12345  | chatserver | 聊天服务的http端口，聊天服务不仅支持长连接，也支持http短连接。 |
   | 20001  | fileserver |                         文件服务端口                         |
   | 20002  | imgserver  |                         图片服务端口                         |

   上表中的8888端口可以使用nc命令，使用方式： **nc -v ip port**去连接，其中port这里就是8888，-v选项是nc命令支持的选项，表示要求显示的信息丰富一点。示例：

   ```
   [zhangyl@localhost flamingoserver]$ nc -v 127.0.0.1 8888
   Ncat: Version 6.40 ( http://nmap.org/ncat )
   Ncat: Connected to 127.0.0.1:8888.
   1. help-show help info
   2. ul-show online user list
   3. su-show userinfo specified by userid: su [userid]
   ul
   No user online.
   su
   please specify userid.
   help
   1. help-show help info
   2. ul-show online user list
   3. su-show userinfo specified by userid: su [userid]
   ```

   连接成功以后，你可以根据命令说明输入相应的命令来查看服务器的状态信息，目前只实现三个命令，即上面显示的help、ul、su，您可以继续增加更多的命令。

   

   **提醒一下：**服务器代码不仅是一款即时通讯软件的服务器代码，同时也是一款通用的C++11服务器框架。

   

   #### pc客户端编译和安装

   使用Visual Studio 2013打开**flamingoclient/**目录下的**Flamingo.sln**即可编译。这个解决方案包括三个工程项目，说明如下：

   |   项目名称   |                 项目说明                 |
   | :----------: | :--------------------------------------: |
   | Flamingo.sln |           flamingo pc版主程序            |
   | CatchScreen  | flamingo聊天中用的抓屏程序，类似QQ截图。 |
   | iUpdateAuto  |     用户pc端自动升级的zip解压工具。      |

​      生成后的程序位于目录**flamingoclient\Bin**目录下，其中客户端与服务器的连接配置信息位于**flamingoclient\Bin\config\flamingo.ini**文件中，直接打开**Flamingo.exe**即可启动flamingo pc端。



####     安卓客户端编译和安装

用Android Studio打开**flamingoAndroid/**目录下的安卓工程，编译产生相应的flamingo.apk文件，安装至安卓手机即可使用。与服务器的连接设置，在安卓版本的登陆界面的服务器设置中。



#### 更新日志

由于平常工作比较忙，不能固定周期地更新flamingo，但我保证我会一直维护下去。更新日志请参见：

https://github.com/baloonwj/flamingo/issues/1



#### BUG反馈

如果您在使用的过程中发现任何bug，可以给我发邮件：balloonwj@qq.com，也可以去bug反馈页面进行反馈：

https://github.com/baloonwj/flamingo/issues/5。

凡是涉及到崩溃、死机等影响使用的重大bug，我将在三个工作日内予以解决；其他不影响使用的功能性bug，两个星期内解决。



#### 联系方式

如果您在使用这个项目的过程中遇到任何问题，或者您对我的项目有任何意见或者建议，再或者有不错的想法欢迎与我交流，可以通过我的微信公众号『**easyserverdev**』与我取得联系，或者可以给我发邮件：**balloonwj@qq.com**，您也可以加入QQ技术群进行交流讨论：**578019391**。

![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/articlelog.jpg)



#### Features Snapshot Below：

#### 以下是部分功能截图：

##### pc版截图
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/1.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/2.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/3.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/4.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/5.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/6.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/7.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/8.png)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/9.png)

##### Android Version Snapshot
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/m1.jpg)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/m2.jpg)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/m3.jpg)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/m4.jpg)
![flamingo Snapshot](http://www.hootina.org/github_flamingo_imgs/m5.jpg)




