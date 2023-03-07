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

   All you need to do is just that open file **Flamingo.sln** in **flamingoclient/** directory with Visual Studio 2019 and compile it . The solution includes three projects illstrated in the table below:：

   | Project Name |                       Usage                        |
   | :----------: | :------------------------------------------------: |
   |   Flamingo   | The main executable program of Flamingo pc client. |
   | CatchScreen  |     A tool to catch screen in chatting window.     |
   | iUpdateAuto  |     Unzipping downloading files when upgrades.     |

​      When compiling finished successfully, the executable programs will be located in directory **flamingoclient\Bin**. And the configuration file which contains configured lines is located in file **flamingoclient\Bin\config\flamingo.ini**. Run **Flamingo.exe** and have fun!     



#### Android Version Compiling and Installing

Use Android Studio to open the project in directory **flamingoAndroid** and build to generate **flamingo.apk** file. Install **flamingo.apk** to your mobiles. Server settings of Android version is on the logining Activity.



#### Change Log

I am afraid that I can not maintain Flamingo in regular intervals because I am very busy with my own work. But I promise that I am going to maintain Flamingo continually.

Change Log see here: 

[Flamingo Change Log](https://github.com/baloonwj/flamingo/issues/1)



#### BUG Report

If you have any questions, you can contact with me by sending email to balloonwj@qq.com. Also, you can report any bugs in the BUG Report Page, its link is https://github.com/baloonwj/flamingo/issues/5.

I promise that fatal problem of Flamingo, such as crashing or halting will be solved in three weekdays and other questions that are not severe will be responsed in two weeks.



#### Contact

At last not the least, if you have any suggestions or questions, welcome to contact with me. China Mainland users can contact with me by joining in my QQ group(**729995516**) or linking my public webchat account(『**easy_coder**』).  Also you can send me emails,  my email address is **balloonwj@qq.com**. I am waiting for You sincerely.


#### Features Snapshot Below：

![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/1.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/2.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/3.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/4.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/5.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/6.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/7.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/8.png)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/9.png)

##### Android Version Snapshot
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/m1.jpg)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/m2.jpg)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/m3.jpg)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/m4.jpg)
![flamingo Snapshot](https://github.com/balloonwj/flamingo/blob/master/snapshots/m5.jpg)