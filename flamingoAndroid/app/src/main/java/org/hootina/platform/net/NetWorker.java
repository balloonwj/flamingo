package org.hootina.platform.net;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Message;
import android.support.v4.app.NotificationCompat;
import android.util.JsonReader;
import android.util.JsonToken;
import android.util.Log;

import com.google.gson.Gson;

import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.MainActivity;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.activities.member.NewFriendActivity;
import org.hootina.platform.db.ContactsDao;
import org.hootina.platform.db.MyDbUtil;
import org.hootina.platform.enums.ClientType;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.enums.OperateFriendEnum;
import org.hootina.platform.enums.TabbarEnum;
import org.hootina.platform.model.ChatContent;
import org.hootina.platform.model.Contacts;
import org.hootina.platform.model.CreateGroup;
import org.hootina.platform.model.LoginResult;
import org.hootina.platform.model.SearchUser;
import org.hootina.platform.result.MessageTextEntity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.LoggerFile;
import org.hootina.platform.utils.NetUtils;
import org.hootina.platform.utils.NotificationUtil;
import org.hootina.platform.utils.ZlibUtil;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.StringReader;
import java.io.UnsupportedEncodingException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;

//TODO: 最好做成单例
public class NetWorker {
    //网络类型
    public static final int NETWORKER_TYPE_WIFI = 0;    //wifi在线
    public static final int NETWORKER_TYPE_CELLULAR = 1;    //3G/4G/5G在线

    private static final String NETWORKER_TAG = "NetworkerTag";
    private final static int MAX_CONNECT_TIMEOUT = 3000;
    //最大的包不能超过5M
    private final static int MAX_PACKAGE_SIZE = 5 * 1024 * 1024;

    private static String mChatServerIp = "120.55.94.78";
    private static short mChatServerPort = 20000;
    private static String mFileServerIp = "120.55.94.78";
    private static short mFileServerPort = 20001;
    private static String mImgServerIp = "120.55.94.78";
    private static short mImgServerPort = 20002;

    private static Socket mSocket;
    private static DataInputStream mDataInputStream;
    private static DataOutputStream mDataOutputStream;

    //数据包协议号
    private static int mSeq = 0;
    private static List<NetPackage> mNetPackages = new LinkedList<>();
    public static boolean mNetChatThreadRunning;

    private static List<String> mNotificationSenders = new ArrayList<>();
    private static int mNoficationCount = 0;

    public static long sLastReceiveTime = 0;

    public NetWorker() {

    }

    public NetWorker(Context context) {
        //Context = context;
        //db = DbUtils.create(context);
    }

    public static void setChatServerIp(String ip) {
        mChatServerIp = ip;
    }

    public static void setChatPort(short port) {
        mChatServerPort = port;
    }

    public static void setImgServerIp(String ip) {
        mImgServerIp = ip;
    }

    public static void setImgPort(short port) {
        mImgServerPort = port;
    }

    public static void setFileServerIp(String ip) {
        mFileServerIp = ip;
    }

    public static void setFilePort(short port) {
        mFileServerPort = port;
    }

    public static String getChatServerIp() {
        return mChatServerIp;
    }

    public static int getChatPort() {
        return mChatServerPort;
    }

    public static String getImgServerIp() {

        return mImgServerIp;
    }

    public static int getImgPort() {
        return mImgServerPort;
    }

    public static String getFileServerIp() {
        return mFileServerIp;
    }

    public static int getFilePort() {
        return mFileServerPort;
    }

    public static int getNetworkerType() {
        return 1;
    }

    public synchronized static void addSender(String sender) {
        for (String iter : mNotificationSenders) {
            if (sender != null) {
                if (sender.equals(iter))
                    return;
            } else {
                return;
            }

        }

        mNotificationSenders.add(sender);
    }

    public synchronized static int getSenderCount() {
        return mNotificationSenders.size();
    }

    public synchronized static void clearSenderCount() {
        mNotificationSenders.clear();
    }

    public synchronized static void addNotificationCount() {
        mNoficationCount++;
    }

    public synchronized static int getNotificationCount() {
        return mNoficationCount;
    }

    public synchronized static void clearNotificationCount() {
        mNoficationCount = 0;
    }

    public synchronized static void addPackage(NetPackage p) {
        mNetPackages.add(p);
    }

    public static synchronized NetPackage retrievePackage() {
        if (mNetPackages.isEmpty())
            return null;

        NetPackage p = mNetPackages.get(0);
        mNetPackages.remove(0);
        //Log.i("Networker", "mNetPackages size: " + mNetPackages.size());
        return p;
    }

    public static boolean writePackage(NetPackage p, DataOutputStream stream) {
        if (p == null || stream == null)
            return false;

        byte[] b = p.getBytes();
        int packageSize = p.getBytesSize();
        try {
            //写入包头
            //不压缩
            stream.writeByte(NetPackage.PACKAGE_UNCOMPRESSED_FLAG);
            //原始包大小
            stream.writeInt(BinaryWriteStream.intToLittleEndian(packageSize));
            //压缩后大小，不压缩这个大小为0
            stream.writeInt(0);
            byte[] reserved = new byte[16];
            stream.write(reserved);
            //写入包体
            stream.write(b, 0, b.length);
            stream.flush();
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

    public static void uninit() {
        close(mSocket, mDataOutputStream, mDataInputStream);
    }

    public static void close(Socket s, DataOutputStream outputStream, DataInputStream inputStream) {
        if (s != null) {
            try {
                s.close();
            } catch (IOException e) {

            }

            s = null;
        }

        if (outputStream != null) {
            try {
                outputStream.close();
            } catch (IOException e) {

            }
            outputStream = null;
        }

        if (inputStream != null) {
            try {
                inputStream.close();
            } catch (IOException e) {

            }
            inputStream = null;
        }
    }

    public static void disconnectChatServer() {
        close(mSocket, mDataOutputStream, mDataInputStream);
    }

    /**
     * 注册协议
     **/
/*
    cmd = 1001, seq = 0,  {"username": "13917043329", "nickname": "balloon", "password": "123"}
    cmd = 1001, seq = 0,  {"code": 0, "msg": "ok"}
 **/

    //太啰嗦的代码，需要优化；
    //需要增加一些日志
    public static void registerUser(final String username, final String password, final String nickname) {
        new Thread() {
            @Override
            public void run() {
                Socket _socket = null;
                DataOutputStream _dataOutputStream = null;
                DataInputStream _dataInputStream = null;
                int cmd;
                int seq;
                String retJson;
                try {
                    _socket = new Socket();
                    //设置连接请求超时时间5s
                    _socket.connect(new InetSocketAddress(mChatServerIp, mChatServerPort), MAX_CONNECT_TIMEOUT);
                    if (_socket == null) {
                        LoggerFile.LogError("Unable to connect to " + mChatServerIp + ":" + mChatServerPort);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    _socket.setSoTimeout(500);

                    BinaryWriteStream writeStream = new BinaryWriteStream();
                    writeStream.writeInt32(MsgType.msg_type_register);
                    writeStream.writeInt32(0);
                    String strJson = "{\"username\": \"";
                    strJson += username;
                    strJson += "\", \"nickname\": \"";
                    strJson += nickname;
                    strJson += "\", \"password\": \"";
                    strJson += password;
                    strJson += "\"}";
                    writeStream.writeString(strJson);
                    writeStream.flush();

                    _dataOutputStream = new DataOutputStream(new BufferedOutputStream(_socket.getOutputStream()));
                    if (_dataOutputStream == null) {
                        close(_socket, null, null);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    byte[] b = writeStream.getBytesArray();
                    if (b == null) {
                        close(_socket, _dataOutputStream, null);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    int packageSize = writeStream.getSize();
                    try {
                        //写入包头
                        _dataOutputStream.writeByte(0);
                        //原始包大小
                        _dataOutputStream.writeInt(BinaryWriteStream.intToLittleEndian(packageSize));
                        _dataOutputStream.writeInt(0);
                        byte[] reserved = new byte[16];
                        _dataOutputStream.write(reserved);
                        //写入包体
                        _dataOutputStream.write(b, 0, b.length);
                        _dataOutputStream.flush();
                    } catch (IOException e) {
                        //Log.i(TAG, "写入流异常");
                        //e.printStackTrace();
                        close(_socket, _dataOutputStream, null);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    _dataInputStream = new DataInputStream(new BufferedInputStream(_socket.getInputStream()));
                    if (_dataInputStream == null) {
                        close(_socket, _dataOutputStream, _dataInputStream);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    //读取包头
                    byte compressFlag = _dataInputStream.readByte();
                    int rawpackagelength = _dataInputStream.readInt();
                    int packagelength = BinaryReadStream.intToBigEndian(rawpackagelength);
                    //TODO: 很奇怪的包头，直接断开连接
                    if (packagelength <= 0 || packagelength > 65535) {
                        close(_socket, _dataOutputStream, _dataInputStream);
                        LoggerFile.LogError("recv a strange packagelength: " + packagelength);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    int compresslengthRaw = _dataInputStream.readInt();
                    int compresslength = BinaryReadStream.intToBigEndian(compresslengthRaw);

                    _dataInputStream.skipBytes(16);

                    byte[] bodybuf = new byte[compresslength];
                    _dataInputStream.read(bodybuf);

                    byte[] registerResult = null;
                    if (compressFlag == 1)
                        registerResult = ZlibUtil.decompressBytes(bodybuf);
                    else
                        registerResult = bodybuf;
                    if (registerResult == null || registerResult.length != packagelength) {
                        close(_socket, _dataOutputStream, _dataInputStream);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    BinaryReadStream binaryReadStream = new BinaryReadStream(registerResult);
                    cmd = binaryReadStream.readInt32();
                    seq = binaryReadStream.readInt32();
                    retJson = binaryReadStream.readString();

                } catch (Exception e) {
                    close(_socket, _dataOutputStream, _dataInputStream);
                    notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                    return;
                }

                if (cmd != MsgType.msg_type_register || retJson == "") {
                    close(_socket, _dataOutputStream, _dataInputStream);
                    notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                    return;
                }

                //{"code": 0, "msg": "ok"}
                int retcode = MsgType.ERROR_CODE_UNKNOWNFAILED;
                try {
                    JsonReader reader = new JsonReader(new StringReader(retJson));

                    reader.beginObject();
                    while (reader.hasNext()) {
                        String name = reader.nextName();
                        if (name.equals("code")) {
                            retcode = reader.nextInt();
                            //注册只需要一个结果码就可以了
                            //TODO: 不能break吗？
                            // break;
                        } else {
                            reader.skipValue();
                        }

                    }// end while-loop
                    reader.endObject();
                    reader.close();

                } catch (NumberFormatException e) {
                    e.printStackTrace();
                } catch (IllegalStateException e) {
                    e.printStackTrace();
                } catch (IOException e) {
                    e.printStackTrace();
                }

                try {
                    _socket.close();
                    _dataOutputStream.close();
                    _dataInputStream.close();
                } catch (IOException e) {

                }

                notifyUI(MsgType.msg_type_register, retcode, 0, null);
            }

        }.start();
    }

    //太啰嗦的代码，需要优化；
    //需要增加一些日志
    public static void login(final String username, final String password, final int clientType, final int status) {
        new Thread() {
            @Override
            public void run() {
                int cmd;
                int seq;
                String retJson;
                try {
                    mSocket = new Socket();
                    //设置连接请求超时时间5s
                    mSocket.connect(new InetSocketAddress(mChatServerIp, mChatServerPort), MAX_CONNECT_TIMEOUT);

                    mSocket.setSoTimeout(500);
                    BinaryWriteStream writeStream = new BinaryWriteStream();
                    writeStream.writeInt32(MsgType.msg_type_login);
                    writeStream.writeInt32(0);
                    String strJson = String.format("{\"username\": \"%s\", \"password\": \"%s\", \"clienttype\": %d, \"status\": %d}",
                            username, password, clientType, status);
                    writeStream.writeString(strJson);
                    writeStream.flush();

                    mDataOutputStream = new DataOutputStream(new BufferedOutputStream(mSocket.getOutputStream()));
                    if (mDataOutputStream == null) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    byte[] b = writeStream.getBytesArray();
                    if (b == null) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    int packageSize = writeStream.getSize();
                    try {
                        //写入包头
                        mDataOutputStream.writeByte(0);
                        //原始包大小
                        mDataOutputStream.writeInt(BinaryWriteStream.intToLittleEndian(packageSize));
                        mDataOutputStream.writeInt(0);
                        byte[] reserved = new byte[16];
                        mDataOutputStream.write(reserved);
                        //写入包体
                        mDataOutputStream.write(b, 0, b.length);
                        mDataOutputStream.flush();
                    } catch (IOException e) {
                        //Log.i(TAG, "写入流异常");
                        //e.printStackTrace();
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    mDataInputStream = new DataInputStream(new BufferedInputStream(mSocket.getInputStream()));
                    if (mDataInputStream == null) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    //读取包头
                    byte compressFlag = mDataInputStream.readByte();
                    int rawpackagelength = mDataInputStream.readInt();
                    int packagelength = BinaryReadStream.intToBigEndian(rawpackagelength);
                    //TODO: 很奇怪的包头，直接断开连接
                    if (packagelength <= 0 || packagelength > 65535) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        LoggerFile.LogError("recv a strange packagelength: " + packagelength);
                        notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    int compresslengthRaw = mDataInputStream.readInt();
                    int compresslength = BinaryReadStream.intToBigEndian(compresslengthRaw);

                    mDataInputStream.skipBytes(16);

                    byte[] bodybuf = new byte[compresslength];
                    mDataInputStream.read(bodybuf);

                    byte[] loginResult = null;
                    if (compressFlag == 1)
                        loginResult = ZlibUtil.decompressBytes(bodybuf);
                    else
                        loginResult = bodybuf;
                    if (loginResult == null || loginResult.length != packagelength) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    BinaryReadStream binaryReadStream = new BinaryReadStream(loginResult);
                    cmd = binaryReadStream.readInt32();
                    seq = binaryReadStream.readInt32();
                    retJson = binaryReadStream.readString();
                    try {
                        LoginResult loginResult1 = new Gson().fromJson(retJson, LoginResult.class);
                        LoginResult.setLoginResult(loginResult1);
                    } catch (Exception e) {
                        e.printStackTrace();
                    }

                } catch (Exception e) {
                    close(mSocket, mDataOutputStream, mDataInputStream);
                    notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                    return;
                }

                if (cmd != MsgType.msg_type_login || retJson == "") {
                    close(mSocket, mDataOutputStream, mDataInputStream);
                    notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                    return;
                }

				/*
				 * cmd = 1002, seq = 0, {"code": 0, "msg": "ok", "userid": 8, "username": "13917043320",
				  * "nickname": "zhangyl",
					"facetype": 0, "customface":"文件md5", "gender":0, "birthday":19891208, "signature":"哈哈，终于成功了",
					"address":"上海市东方路3261号", "phonenumber":"021-389456", "mail":"balloonwj@qq.com"}
				 **/
                int retcode = MsgType.ERROR_CODE_UNKNOWNFAILED;
                UserSession userSession = UserSession.getInstance();
                UserInfo loginUserInfo = userSession.loginUser;
                try {
                    JsonReader reader = new JsonReader(new StringReader(retJson));

                    reader.beginObject();
                    while (reader.hasNext()) {
                        String name = reader.nextName();
                        if (name.equals("code")) {
                            retcode = reader.nextInt();
                        } else if (name.equals("userid")) {
                            loginUserInfo.set_userid(reader.nextInt());
                        } else if (name.equals("username")) {
                            loginUserInfo.set_username(reader.nextString());
                        } else if (name.equals("nickname")) {
                            loginUserInfo.set_nickname(reader.nextString());
                        } else if (name.equals("facetype")) {
                            loginUserInfo.set_faceType(reader.nextInt());
                        } else if (name.equals("customface")) {
                            loginUserInfo.set_customFacePath(reader.nextString());
                        } else if (name.equals("gender")) {
                            loginUserInfo.set_gender(reader.nextInt());
                        } else if (name.equals("birthday")) {
                            loginUserInfo.set_birthday(reader.nextInt());
                        } else if (name.equals("signature")) {
                            loginUserInfo.set_signature(reader.nextString());
                        } else if (name.equals("address")) {
                            loginUserInfo.set_address(reader.nextString());
                        } else if (name.equals("phonenumber")) {
                            loginUserInfo.set_phoneNumber(reader.nextString());
                        } else if (name.equals("mail")) {
                            loginUserInfo.set_mail(reader.nextString());
                        } else {
                            reader.skipValue();
                        }

                    }// end while-loop
                    reader.endObject();
                    reader.close();

                } catch (NumberFormatException e) {
                    e.printStackTrace();
                } catch (IllegalStateException e) {
                    e.printStackTrace();
                } catch (IOException e) {
                    e.printStackTrace();
                }

                //登陆成功以后，创建以userid命名的用户目录
                if (retcode == MsgType.ERROR_CODE_SUCCESS) {
                    //创建Logs目录
                    String userDir = String.format("/sdcard/flamingo/Users/%d", loginUserInfo.get_userid());
                    File logsDir = new File(userDir);
                    if (!logsDir.exists()) {
                        if (!logsDir.mkdir()) {
                            retcode = MsgType.ERROR_CODE_UNKNOWNFAILED;
                        }
                    }

                    String chatImagePath = userDir + "/chatImages";
                    File chatImageDir = new File(chatImagePath);
                    if (!chatImageDir.exists()) {
                        if (!chatImageDir.mkdir()) {
                            retcode = MsgType.ERROR_CODE_UNKNOWNFAILED;
                        }
                    }
                }

                notifyUI(MsgType.msg_type_login, retcode, 0, null);
            }

        }.start();
    }

    //获取好友列表
    public static void getFriendList() {
        NetPackage netPackage = new NetPackage(MsgType.msg_type_getfriendlist, mSeq, "");
        mSeq++;
        addPackage(netPackage);
    }

    //获取群成员
    public static void getGroupMember(int groupid) {
        //{"groupid": 群id}
        String strJson = String.format("{\"groupid\": %d}", groupid);
        NetPackage netPackage = new NetPackage(MsgType.msg_type_getgroupmembers, mSeq, strJson);
        mSeq++;
        addPackage(netPackage);
    }

    // 发送消息
    public static void sendChatMsg(int targetID, String chatMsg) {
        NetPackage netPackage = new NetPackage(MsgType.msg_type_chat, mSeq, chatMsg, targetID);
        mSeq++;
        addPackage(netPackage);
    }

    //cmd = 1005, seq = 0, {"userid": 9, "type": 5}
    public static void deleteFriend(int userid) {
        Contacts contacts = new Contacts(userid, 4);
        String json = new Gson().toJson(contacts, Contacts.class);
        NetPackage netPackage = new NetPackage(MsgType.msg_type_operatefriend, mSeq, json);
        mSeq++;
        addPackage(netPackage);
    }

    //搜索好友、群
    //请求，使用用户名搜索
    //cmd = 1004, seq = 0, {"type": 1, "username": "zhangyl"}

    //应答
    //cmd = 1004, seq = 0, { "code": 0, "msg": "ok", "userinfo": [{"userid": 2, "username": "qqq", "nickname": "qqq123", "facetype":0}] }
    public static void searchPersonOrGroup(int type, String userName) {
        try {
            SearchUser searchUser = new SearchUser(type, userName);
            NetPackage netPackage = new NetPackage(MsgType.msg_type_finduser, mSeq, new Gson().toJson(searchUser, SearchUser.class));
            mSeq++;
            addPackage(netPackage);
        } catch (Exception e) {
            e.printStackTrace();
        }

    }


    //A向B发出加好友申请
    //cmd = 1005, seq = 0, {"userid": 9, "type": 1}
    public static void addFriend(int userid) {
        Contacts addUser = new Contacts(userid, 1);
        NetPackage netPackage = new NetPackage(MsgType.msg_type_operatefriend, mSeq, new Gson().toJson(addUser, Contacts.class));
        mSeq++;
        addPackage(netPackage);
    }

    //B在添加好友页面点击同意，向服务器发数据
    //cmd = 1005, seq = 0, {"userid": 9, "type": 3, "username": "xxx", "accept": 1}
    public static void acceptNewFriend(int userid, String userName) {
        Contacts addUser = new Contacts(userid, 3, userName, 1);
        NetPackage netPackage = new NetPackage(MsgType.msg_type_operatefriend, mSeq, new Gson().toJson(addUser, Contacts.class));
        mSeq++;
        addPackage(netPackage);
    }

    public static void sendHeartBeat() {
        NetPackage netPackage = new NetPackage(1000, 0, "");
        mSeq++;
        addPackage(netPackage);
    }

//    cmd = 1009, seq = 0, {"groupname": "我的群名称", "type": 0}
//    cmd = 1009, seq = 0, {"code":0, "msg": "ok", "groupid": 12345678, "groupname": "我的群名称"}, 用户id和群id都是4字节整型，32位，群id的高位第七位为1

    public static void createGroup(String name) {
        CreateGroup createGroup = new CreateGroup(name);
        NetPackage netPackage = new NetPackage(1009, 0, new Gson().toJson(createGroup, CreateGroup.class));
        mSeq++;
        addPackage(netPackage);
    }


    public static boolean startNetChatThread() {
        if (mSocket == null || mDataOutputStream == null || mDataInputStream == null)
            return false;

        new Thread() {
            @Override
            public void run() {
                mNetChatThreadRunning = true;
                int packageNum = 0;
                while (mNetChatThreadRunning) {
                    while (true) {
                        NetPackage netPackage = NetWorker.retrievePackage();
                        if (netPackage == null)
                            break;

                        packageNum++;
                        Log.i("Networker", "packageNum: " + packageNum);
                        if (!writePackage(netPackage, mDataOutputStream)) {
                            close(mSocket, mDataOutputStream, mDataInputStream);
                            mNetChatThreadRunning = false;
                            notifyUI(MsgType.msg_networker_disconnect, 0, 0, null);
                            return;
                        }
                    }

                    NetDataParser parser = new NetDataParser();
                    if (!recvPackage(mDataInputStream, parser)) {
                        close(mSocket, mDataOutputStream, mDataInputStream);
                        mNetChatThreadRunning = false;
                        notifyUI(MsgType.msg_networker_disconnect, 0, 0, null);
                        return;
                    }

                    if (parser.mCmd != MsgType.msg_type_unknown)
                        handleServerResponseMsg(parser);
                }//end while-loop
            }// end run
        }.start();

        return true;
    }

    public static void stopNetChatThread() {
        mNetChatThreadRunning = false;
    }

    public static boolean isNetChatThreadRunning() {
        return mNetChatThreadRunning;
    }

    public static void notifyUI(int what, int arg1, int arg2, Object obj) {
        Message msg = new Message();
        msg.what = what;
        msg.arg1 = arg1;
        msg.arg2 = arg2;
        msg.obj = obj;
        BaseActivity.sendMessage(msg);
    }

    private static boolean recvPackage(DataInputStream inputStream, NetDataParser parser) {
        if (inputStream == null || parser == null)
            return false;

        try {
            //读取包头
            //TODO: 这样写是有问题的，可能在中间的read过程中阻塞住
            //改成先判断字节数是否够包头大小，再够包体大小
            byte compressFlag = inputStream.readByte();
            int rawpackagelength = inputStream.readInt();
            int packagelength = BinaryReadStream.intToBigEndian(rawpackagelength);
            if (packagelength > 0 && packagelength < 65535) {
                int compresslengthRaw = inputStream.readInt();
                int compresslength = BinaryReadStream.intToBigEndian(compresslengthRaw);
                if (compresslength < 0 || compresslength >= MAX_PACKAGE_SIZE)
                    return false;

                inputStream.skipBytes(16);

                byte[] bodybuf = new byte[compresslength];
                inputStream.read(bodybuf);

                byte[] result = null;
                if (compressFlag == 1)
                    result = ZlibUtil.decompressBytes(bodybuf);
                else
                    result = bodybuf;

                if (result != null && result.length == packagelength) {
                    BinaryReadStream binaryReadStream = new BinaryReadStream(result);
                    int cmd = binaryReadStream.readInt32();
                    int seq = binaryReadStream.readInt32();
                    String retJson = binaryReadStream.readString();
                    parser.mCmd = cmd;
                    parser.mSeq = seq;
                    parser.mJson = retJson;

                    if (cmd == MsgType.msg_type_chat) {
                        parser.mArg1 = binaryReadStream.readInt32();
                        parser.mArg2 = binaryReadStream.readInt32();
                    }

                    return true;
                }
            }
        } catch (SocketTimeoutException ex) {
            return true;
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }

        return false;
    }

    private static void handleServerResponseMsg(NetDataParser parser) {
        if (parser == null)
            return;

        long currentTime = System.currentTimeMillis();

        if (currentTime - sLastReceiveTime > 5000) {
            sendHeartBeat();
        }

        sLastReceiveTime = currentTime;

        //_seq = parser.mSeq;
        Log.d(NETWORKER_TAG, parser.mJson);

        switch (parser.mCmd) {
            //好友列表
            case MsgType.msg_type_getfriendlist:
                handleFriendList(parser.mJson);
                break;

            //群成员信息
            case MsgType.msg_type_getgroupmembers:
                handleGroupMemberInfo(parser.mJson);
                break;

            //收到聊天消息
            case MsgType.msg_type_chat:
                Log.i(NETWORKER_TAG, "recv chat data: " + parser.mJson);
                handleChatMsg(parser.mJson, parser.mArg1, parser.mArg2);
                break;

            //搜索好友，群
            case MsgType.msg_type_finduser:
                handleFindUserResult(parser.mJson);
                break;

            //添加好友
            case MsgType.msg_type_operatefriend:
                handleOperateFriend(parser.mJson);
                break;

                //建群结果返回
            case MsgType.msg_type_creategroup:
                handleCreateGroupResult(parser.mJson);
                break;

                //被服务器踢下线
            case MsgType.msg_type_kickuser:
                Message kickMsg = new Message();
                kickMsg.what = MsgType.msg_type_kickuser;
                BaseActivity.sendMessage(kickMsg);
                break;

                //修改密码结果
            case MsgType.msg_type_modifypassword:
                handleModifyPasswordResult(parser.mJson);
                break;

                //用户信息发生改变
            case MsgType.msg_type_userstatuschange:
                //TODO：简化一下，直接重新拉一下好友列表
                getFriendList();
                break;

            default:
                Log.w(NETWORKER_TAG, "Unhandled cmd: " + parser.mCmd);
                break;
        }
    }

    // 好友列表
    private static void handleFriendList(String data) {
        if (data.isEmpty()) {
            LoggerFile.LogInfo("response friend list is empty.");
            return;
        }

		/*
        {
			"code": 0,
			"msg": "ok",
			"userinfo": [
				{
					"teamindex": 0,
					"teamname": "My Friends",
					"members": [
						{
							"userid": 2,
							"username": "qqq",
							"nickname": "qqq123",
							"facetype": 0,
							"customface": "a31992feafd1989925f1995328b4269b",
							"gender": 1,
							"birthday": 19900101,
							"signature": "hello qqq~kppp",
							"address": "",
							"phonenumber": "",
							"mail": "",
							"clienttype": 1,
							"status": 1
						}
					]
				}
			]
		}
       */

        LoggerFile.LogInfo("response friend list:", data);
        int retCode = 0;

        UserSession.getInstance().clearFriendInfo();
        UserSession.getInstance().clearGroupInfo();;
        List<UserInfo> friends = UserSession.getInstance().friends;
        List<UserInfo> groups = UserSession.getInstance().groups;

        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("code")) {
                    retCode = reader.nextInt();
                } else if (name.equals("userinfo") && reader.peek() != JsonToken.NULL) {
                    reader.beginArray();
                    while (reader.hasNext()) {
                        reader.beginObject();
                        while (reader.hasNext()) {
                            String nodename = reader.nextName();
                            if (nodename.equals("members")) {
                                reader.beginArray();
                                while (reader.hasNext()) {
                                    reader.beginObject();
                                    UserInfo u = new UserInfo();
                                    while (reader.hasNext()) {
                                        String nodename2 = reader.nextName();
                                        if (nodename2.equals("userid")) {
                                            u.set_userid(reader.nextInt());
                                        } else if (nodename2.equals("username")) {
                                            u.set_username(reader.nextString());
                                        } else if (nodename2.equals("nickname")) {
                                            u.set_nickname(reader.nextString());
                                        } else if (nodename2.equals("signature")) {
                                            u.set_signature(reader.nextString());
                                        } else if (nodename2.equals("facetype")) {
                                            u.set_faceType(reader.nextInt());
                                        } else if (nodename2.equals("customface")) {
                                            u.set_customFacePath(reader.nextString());
                                        } else if (nodename2.equals("status")) {
                                            u.set_onlinetype(reader.nextInt());
                                        } else if (nodename2.equals("clienttype")) {
                                            u.set_clientType(reader.nextInt());
                                        } else {
                                            reader.skipValue();
                                        }
                                    }
                                    reader.endObject();
                                    if (u.isGroup()) {
                                        groups.add(u);
                                    } else {
                                        friends.add(u);
                                    }
                                }
                                reader.endArray();
                            } else {
                                reader.skipValue();
                            }
                        }
                        reader.endObject();

                    }
                    reader.endArray();
                } else {
                    reader.skipValue();
                }

            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();

            LoggerFile.LogError("parse friend list error, data=", data);
            return;
        } catch (IllegalStateException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse friend list error, data=", data);
            return;
        } catch (IOException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse friend list error, data=", data);
            return;
        }


//        int num = 0;
//        for (UserInfo u : friends) {
//			if (u.isGroup()) {
//				UserSession.getInstance().makeExistGroupFlag(true);
//                //向服务器请求群组信息
//                num ++;
//                Log.i("NetWorker", "request group info: " + num);
//                getGroupMember(u.get_userid());
//			}
//		}

        Message msg = new Message();
        msg.what = MsgType.msg_type_getfriendlist;
        msg.arg1 = retCode;
        BaseActivity.sendMessage(msg);
    }

    private static void handleGroupMemberInfo(String data) {
        if (data.isEmpty())
            return;

		/*
		{"code":0, "msg": "ok", "groupid": 12345678,
		"members": [{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1},
		{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1},
		{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1}]}
		 */

        int retCode = 0;
        int groupID = 0;
        UserInfo currentGroup = null;

        List<UserInfo> groups = UserSession.getInstance().groups;

        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("code")) {
                    retCode = reader.nextInt();
                } else if (name.equals("groupid")) {
                    groupID = reader.nextInt();

                    currentGroup = UserSession.getInstance().getGroupById(groupID);
                    if (currentGroup != null)
                        currentGroup.groupMembers.clear();

                } else if (name.equals("members") && reader.peek() != JsonToken.NULL) {
                    reader.beginArray();
                    while (reader.hasNext()) {
                        UserInfo u = new UserInfo();
                        reader.beginObject();
                        while (reader.hasNext()) {
                            String nodename = reader.nextName();
                            if (nodename.equals("userid")) {
                                u.set_userid(reader.nextInt());
                            } else if (nodename.equals("username")) {
                                u.set_username(reader.nextString());
                            } else if (nodename.equals("nickname")) {
                                u.set_nickname(reader.nextString());
                            } else if (nodename.equals("signature")) {
                                u.set_signature(reader.nextString());
                            } else if (nodename.equals("facetype")) {
                                u.set_faceType(reader.nextInt());
                            } else if (nodename.equals("customface")) {
                                u.set_customFacePath(reader.nextString());
                            } else if (nodename.equals("status")) {
                                u.set_onlinetype(reader.nextInt());
                            } else if (nodename.equals("clienttype")) {
                                u.set_clientType(reader.nextInt());
                            }
                            else {
                                reader.skipValue();
                            }
                        }
                        reader.endObject();
                        if (currentGroup != null)
                            currentGroup.groupMembers.add(u);
                    }
                    reader.endArray();
                } else {
                    reader.skipValue();
                }

            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group member list error, data=", data);
            return;
        } catch (IllegalStateException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group member list error, data=", data);
            return;
        } catch (IOException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group member list error, data=", data);
            return;
        }

        Message msg = new Message();
        msg.what = MsgType.msg_type_getgroupmembers;
        msg.arg1 = retCode;
        BaseActivity.sendMessage(msg);
    }

    private static void handleCreateGroupResult(String data) {
        //{"code":0, "msg": "ok", "groupid": 12345678, "groupname": "我的群名称"}
        int retCode = 0;
        int groupID = 0;
        String groupName = "";

        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("code")) {
                    retCode = reader.nextInt();
                } else if (name.equals("groupid")) {
                    groupID = reader.nextInt();
                } else if (name.equals("groupname") ) {
                    groupName = reader.nextString();
                } else {
                    reader.skipValue();
                }

            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group info error, data=", data);
            return;
        } catch (IllegalStateException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group info list error, data=", data);
            return;
        } catch (IOException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse group info list error, data=", data);
            return;
        }

        Message msg = new Message();
        msg.what = MsgType.msg_type_creategroup;
        msg.arg1 = retCode;
        BaseActivity.sendMessage(msg);
    }

    // 修改密码
    private static void handleModifyPasswordResult(String data) {
        Message msg = new Message();
        //msg.obj = changepwa;
        msg.what = MsgType.msg_type_modifypassword;
        BaseActivity.sendMessage(msg);
    }

    // 接受好友消息
    private static void handleChatMsg(String data, int senderID, int targetID) {
        Message msg = new Message();
        msg.what = MsgType.msg_type_chat;
        //msg.obj = data;
        msg.arg1 = senderID;
        msg.arg2 = targetID;

        Context context = FlamingoApplication.getContext();
        NotificationManager manager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

        int msgType = MessageTextEntity.CONTENT_TYPE_UNKNOWN;
        int clientType = ClientType.CLIENT_TYPE_UNKNOWN;
        //TODO: 时间改成用服务器时间
        long msgTime = 0;
        //用于存库的json，格式：[{"faceID":1},{"faceID":2},{"faceID":-1}]
        String json = "[";

        String msgText = "";
        int faceId;
        //用于做通知栏和会话列表的消息概要字符串, 格式: [表情]文本[表情]
        String notifySnapshot = "";

        //data的格式：
        //{"msgType":1,"time":1522722880,"clientType":1,"font":["微软雅黑",12,0,0,0,0],"content":[{"msgText":"hello"},{"faceID":13},{"msgText":"world"},{"faceID":14}]}
        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("msgType")) {
                    msgType = reader.nextInt();
                } else if (name.equals("time")) {
                    msgTime = reader.nextLong();
                } else if (name.equals("clientType")) {
                    clientType = reader.nextInt();
                } else if (name.equals("content")) {
                    //json = reader.nextString();
                    //json = reader.toString();
                    reader.beginArray();
                    while (reader.hasNext()) {
                        reader.beginObject();
                        while (reader.hasNext()) {
                            String innerName = reader.nextName();
                            //注意：这里不能使用==，必须使用equals
                            if (innerName.equals("faceID")) {
                                faceId = reader.nextInt();
                                json += "{\"faceID\":";
                                json += faceId;
                                json += "},";
                                notifySnapshot += "[表情]";
                            } else if (innerName.equals("msgText")) {
                                msgText = reader.nextString();
                                json += "{\"msgText\":\"";
                                json += msgText;
                                json += "\"},";
                                notifySnapshot += msgText;
                            } else {
                                reader.skipValue();
                            }
                        }

                        reader.endObject();
                    }
                    reader.endArray();
                } else {
                    reader.skipValue();
                }
            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse chat data error, data=", data);
            return;
        } catch (IllegalStateException e) {
            LoggerFile.LogError("parse chat data error, data=", data);
            return;
        } catch (IOException e) {
            LoggerFile.LogError("parse chat data error, data=", data);
            return;
        }

        //去掉最后一个多余的逗号
        json = json.substring(0, json.length() - 1);
        json += "]";

        //msg.obj = json;
        ChatContent chatContent = null;
        try {
            chatContent = new Gson().fromJson(data, ChatContent.class);
        } catch (Exception e) {
            e.printStackTrace();
            return;
        }

        //if (chatContent.getTargetId() < 0xf)
        msg.obj = chatContent;

        boolean updateNewMsgCountAndMakeNotifications = true;
        Activity currentActivity = ((FlamingoApplication)FlamingoApplication.getContext()).getAppManager().currentActivity();
        //如果当前的聊天界面正好是新聊天消息的目标窗口，则不更新新消息条数和不更新通知栏
        if (currentActivity instanceof ChattingActivity) {
            int friendID = ((ChattingActivity)currentActivity).getFriendID();
            if (UserInfo.isGroup(targetID)) {
                if (friendID == targetID)
                    updateNewMsgCountAndMakeNotifications = false;
            } else if (senderID == friendID)
                updateNewMsgCountAndMakeNotifications = false;
        }

        //Bitmap senderHeadBmp = PictureUtil.getFriendHeadPic(context.getAssets(), userinfo);
        //BitmapDrawable bd = new BitmapDrawable(context.getResource(), senderHeadBmp);

//        String senderName = "";
//        //消息目标是群，且消息发送者不是自己（不是设备同步信息）
//        if (GroupConstant.isGroup(targetID) && senderID != UserSession.getInstance().loginUser.get_userid())
//            senderName = FriendList.getInstance().getUserName(targetID);
//        //消息目标是自己
//        else if (targetID == UserSession.getInstance().loginUser.get_userid())
//            senderName = FriendList.getInstance().getUserName(senderID);

        String senderName = UserSession.getInstance().getNicknameById(senderID);

        NetWorker.addSender(senderName);
        NetWorker.addNotificationCount();

        String notificationTitle = UserSession.getInstance().getNicknameById(senderID);
//        if (GroupConstant.isGroup(targetID)) {
//            notificationTitle = FriendList.getInstance().getNickName(targetID);
//        } else {
//            notificationTitle = FriendList.getInstance().getNickName(senderID);
//        }

        String contentText = notifySnapshot;
        int senderCount = NetWorker.getSenderCount();
        int notificationCount = NetWorker.getNotificationCount();
        if (senderCount == 1 && notificationCount > 1)
            notificationTitle = String.format("%s (%d条新消息)", senderName, notificationCount);
        else if (senderCount > 1) {
            notificationTitle = "Flamingo 多人消息";
            contentText = String.format("有 %d 个联系人给您发过来%d条新消息", senderCount, notificationCount);
        }


//            mSenderID = getIntent().getIntExtra("selfid", 0);
//            mFriendID = getIntent().getIntExtra("friendid", 0);

        //新增一条聊天消息
        BaseActivity.getChatMsgDb().insertChatMsg(  MessageTextEntity.generateMsgID(),
                                                    msgTime,
                                                    senderID, targetID,
                                                    MessageTextEntity.CONTENT_TYPE_TEXT, json,
                                                    ClientType.CLIENT_TYPE_ANDROID, 0,
                                                    "", "");

        //ChatMsgMgr.getInstance().increaseUnreadChatMsgCount(senderID);
        if (UserInfo.isGroup(targetID)) {
            ChatSessionMgr.getInstance().updateSession(targetID, senderName, json, "", new Date());
            if (updateNewMsgCountAndMakeNotifications)
                ChatMsgMgr.getInstance().increaseUnreadChatMsgCount(targetID);
        } else {
            ChatSessionMgr.getInstance().updateSession(senderID, senderName, json, "", new Date());
            if (updateNewMsgCountAndMakeNotifications)
                ChatMsgMgr.getInstance().increaseUnreadChatMsgCount(senderID);
        }

        Intent intent = null;
        if (senderCount == 1) {
            intent = new Intent(context, ChattingActivity.class);
            //intent.putExtra("nickname", senderName);
            if (UserInfo.isGroup(targetID)) {
                intent.putExtra("selfid", senderID);
                intent.putExtra("friendid", targetID);
            } else {
                intent.putExtra("selfid", targetID);
                intent.putExtra("friendid", senderID);
            }

            //intent.putExtra("msgtexts", "");
            //intent.putExtra("type", "");
        } else if (senderCount > 1) {
            intent = new Intent(context, MainActivity.class);
            ((FlamingoApplication) context).setTabIndex(TabbarEnum.MESSAGE);
        }

        if (updateNewMsgCountAndMakeNotifications) {
            PendingIntent pendingIntent = PendingIntent.getActivity(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT);
            Notification notification = new NotificationCompat.Builder(context)
                    .setContentTitle(notificationTitle)
                    .setContentText(contentText)
                    .setWhen(System.currentTimeMillis())
                    .setSmallIcon(R.raw.head)
                    .setAutoCancel(true)
                    .setVibrate(new long[]{0, 1000, 1000, 1000})
                    .setLights(Color.GREEN, 1000, 1000)
                    .setDefaults(NotificationCompat.DEFAULT_ALL)
                    .setContentIntent(pendingIntent)
                    .build();
            manager.notify(FlamingoApplication.CHATMSG_NOTIFICATION_ID, notification);
        }

        BaseActivity.sendMessage(msg);
    }

    private static void handleFindUserResult(String data) {
        /*
            { "code": 0, "msg": "ok", "userinfo": [{"userid": 2,
                "username": "qqq", "nickname": "qqq123", "facetype":0}] }
         */
        int retCode = 0;
        List<UserInfo> matchedUsers = new ArrayList<>();
        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("code")) {
                    retCode = reader.nextInt();
                } else if (name.equals("userinfo") && reader.peek() != JsonToken.NULL) {
                    reader.beginArray();
                    while (reader.hasNext()) {
                        UserInfo u = new UserInfo();

                        reader.beginObject();

                        while(reader.hasNext()) {
                            String name2 = reader.nextName();
                            if (name2.equals("userid")) {
                                u.set_userid(reader.nextInt());
                            } else if (name2.equals("username")) {
                                u.set_username(reader.nextString());
                            } else if (name2.equals("nickname")) {
                                u.set_nickname(reader.nextString());
                            } else if (name2.equals("facetype")) {
                                u.set_faceType(reader.nextInt());
                            } else
                                reader.skipValue();
                        }

                        reader.endObject();

                        matchedUsers.add(u);
                    }
                    reader.endArray();
                } else {
                    reader.skipValue();
                }

            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse finduser result error1, data=", data);
            return;
        } catch (IllegalStateException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse finduser result error2, data=", data);
            return;
        } catch (IOException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse finduser result error3, data=", data);
            return;
        }

        Message msg = new Message();
        msg.what = MsgType.msg_type_finduser;
        msg.obj = matchedUsers;
        BaseActivity.sendMessage(msg);
    }

    private static void handleOperateFriend(String data) {
        try {
            Contacts dealUser = new Gson().fromJson(data, Contacts.class);

            int friendID = dealUser.getUserid();
            String friendUsername = dealUser.getUsername();
            int operateFriendType = dealUser.getType();

            //B收到A的好友申请
            //存数据库
            //弹出通知
            //点击通知，进入新的朋友页面，数据库读数据并显示
            //cmd = 1005, seq = 0, {"userid": 9, "type": 2, "username": "xxx"}  //收到的好友申请
            if (operateFriendType == OperateFriendEnum.OPERATE_FRIEND_RECV_APPLY) {
                dealUser.setAccept(OperateFriendEnum.RESPONSE_APPLY_REFUSE);
                ContactsDao contactsDao = MyDbUtil.getContactsDao();
                if (contactsDao != null) {
                    contactsDao.insertOrReplace(dealUser);
                }
                NotificationUtil.showNotification(FlamingoApplication.getInstance(),
                        friendID,
                        friendUsername,
                        friendUsername + "申请添加您为好友",
                        R.drawable.ic_launcher,
                        NewFriendActivity.class);
            }

            // A收到B的同意信息，存入数据库。
            // 存数据库
            // toast 提示
            //cmd = 1005, seq = 0, {"userid": 9, "type": 3, "username": "xxx"} //已同意的好友申请
            else if (operateFriendType == OperateFriendEnum.OPERATE_FRIEND_RESPONSE_APPLY) {
                dealUser.setAccept(OperateFriendEnum.RESPONSE_APPLY_AGREE);
                ContactsDao contactsDao = MyDbUtil.getContactsDao();
                if (contactsDao != null) {
                    contactsDao.insertOrReplace(dealUser);
                }
                String notificationText = "您和" + friendUsername + "已经成为好友";
                if (UserInfo.isGroup(friendID))
                    notificationText = "您已经成功加入群[" + friendUsername + "]";
                NotificationUtil.showNotification(FlamingoApplication.getInstance(),
                        friendID,
                        friendUsername,
                        notificationText,
                        R.drawable.ic_launcher,
                        NewFriendActivity.class);
            }

            //发送到 新的朋友页面
            Message friendMsg = new Message();
            friendMsg.what = MsgType.msg_type_operatefriend;
            friendMsg.arg1 = dealUser.getType();
            friendMsg.obj = dealUser;
            BaseActivity.sendMessage(friendMsg);
        } catch (Exception e) {
            //e.printStackTrace();
        }
    }


    /**
     *  文件上传
     */
    /**
     * 客户端：cmd = msg_type_upload_req, seq, filemd5, offset ,filesize, filedata
     * 服务器应答： cmd = msg_type_upload_resp, seq, errorcode, filemd5, offset, filesize
     **/

    public static final int EACH_BYTES_TO_SEND = 64 * 1024;
    public static final int msg_type_upload_req = 1;
    public static final int msg_type_upload_resp = 2;
    public static final int msg_type_download_req = 3;
    public static final int msg_type_download_resp = 4;

    public static final int file_msg_error_unknown = 0;
    public static final int file_msg_error_progress = 1;
    public static final int file_msg_error_complete = 2;

    // 相册或相机发送图片
    public static boolean uploadFile(final String fileName, final String fileMD5, final int targetid) {

        final byte[] fileBytes = getBytesFromFile(new File(fileName));
        if (fileBytes == null || fileBytes.length == 0) {
            return false;
        }

        new Thread() {
            @Override
            public void run() {

                FileServerConnectionHelper fileServerConnectionHelper = new FileServerConnectionHelper();

                if (!connectToFileServer(mImgServerIp, mImgServerPort, fileServerConnectionHelper)) {
                    closeFileServerConnection(fileServerConnectionHelper);
                    Log.e(NETWORKER_TAG, "Failed to connect img server, ip: " + mImgServerIp + ", port: " + mImgServerPort);
                    //TODO: 连接服务器失败，上传失败通知相应的聊天activity
                    return;
                }

                boolean bError = false;

                long currentSendSize = EACH_BYTES_TO_SEND;
                long offset = 0;

                while (true) {
                    if (fileBytes.length - offset < currentSendSize)
                        currentSendSize = fileBytes.length - offset;

                    if (!sendUploadPackageToFileServer(fileMD5,
                            offset,
                            fileBytes.length,
                            fileBytes,
                            offset,
                            offset + currentSendSize,
                            fileServerConnectionHelper)) {

                        bError = true;
                        break;
                    }

                    int errorCode = recvUploadPackageFromFileServer(fileMD5,
                            offset,
                            fileBytes.length,
                            fileServerConnectionHelper);
                    if (errorCode == file_msg_error_progress) {
                        offset += currentSendSize;
                        continue;
                    } else if (errorCode == file_msg_error_complete) {
                        break;
                    } else {
                        bError = true;
                        break;
                    }

                }// end while

                //不管是成功还是失败，都要关闭socket，清理流对象
                closeFileServerConnection(fileServerConnectionHelper);

                if (bError) {
                    Log.e(NETWORKER_TAG, "Failed to upload imgfile, fileName: fileMd5: "
                            + fileMD5 + ", fileSize: " + fileBytes.length +
                            ", ip: " + mImgServerIp + ", port: " + mImgServerPort);

                    //TODO: 上传失败通知相应的聊天activity
                } else {
                    Log.i(NETWORKER_TAG, "Upload imgfile successfully, fileMd5: "
                            + fileMD5 + ", fileSize: " + fileBytes.length +
                            ", ip: " + mImgServerIp + ", port: " + mImgServerPort);


                    //TODO: 上传成功通知拼一个json：
                    /**
                     {
                     "msgType": 5,
                     "time": 2434167,
                     "clientType": 3,		//0未知 1pc端 2苹果端 3安卓端
                     "content":
                     [
                     {"pic": ["xx.jpg", "cb88aeb13017d24f2ea87f570105baf3", fileSize, imageWidth, imageHeight]}
                     ]
                     }

                     */

                    //TODO: 利用BinaryWriteStream组成一个聊天消息发出去。

                    StringBuilder stringBuilder = new StringBuilder();
                    stringBuilder.append("{");
                    stringBuilder.append("\"msgType\":5,");
                    stringBuilder.append("\"time\":" + System.currentTimeMillis() / 1000 + ",");
                    stringBuilder.append("\"clientType\": 3,");
                    stringBuilder.append("\"content\":");
                    stringBuilder.append("[");
                    stringBuilder.append("{");
                    stringBuilder.append("\"pic\":");
                    stringBuilder.append("[");
                    stringBuilder.append("\"" + fileName + "\"" + ",");
                    stringBuilder.append("\"" + fileMD5 + "\"" + ",");
                    stringBuilder.append("" + fileBytes.length + ",");
                    stringBuilder.append("0,");
                    stringBuilder.append("0");
                    stringBuilder.append("]");
                    stringBuilder.append("}");
                    stringBuilder.append("]");
                    stringBuilder.append("}");

                    sendChatMsg(targetid, stringBuilder.toString());


                }

            }// end run

        }.start();

        return true;
    }

    private static boolean connectToFileServer(String ip,
                                               short port,
                                               FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null)
            return false;

        try {
            fileServerConnectionHelper.mSocket = new Socket();
            //设置连接请求超时时间5s
            fileServerConnectionHelper.mSocket.connect(new InetSocketAddress(ip, port), MAX_CONNECT_TIMEOUT);
            fileServerConnectionHelper.mSocket.setSoTimeout(500);

            fileServerConnectionHelper.mDataInputStream = new DataInputStream(new BufferedInputStream(fileServerConnectionHelper.mSocket.getInputStream()));
            fileServerConnectionHelper.mDataOutputStream = new DataOutputStream(new BufferedOutputStream(fileServerConnectionHelper.mSocket.getOutputStream()));

        } catch (Exception e) {
            closeFileServerConnection(fileServerConnectionHelper);
            return false;
        }

        return true;
    }

    private static void closeFileServerConnection(FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null)
            return;

        if (fileServerConnectionHelper.mSocket != null) {
            try {
                fileServerConnectionHelper.mSocket.close();
            } catch (IOException ex) {

            }

            fileServerConnectionHelper.mSocket = null;
        }

        if (fileServerConnectionHelper.mDataInputStream != null) {
            try {
                fileServerConnectionHelper.mDataInputStream.close();
            } catch (IOException ex) {

            }

            fileServerConnectionHelper.mDataInputStream = null;
        }

        if (fileServerConnectionHelper.mDataOutputStream != null) {
            try {
                fileServerConnectionHelper.mDataOutputStream.close();
            } catch (IOException ex) {

            }

            fileServerConnectionHelper.mDataOutputStream = null;
        }
    }

    private static boolean sendUploadPackageToFileServer(String fileMd5,
                                                         long offset,
                                                         long fileSize,
                                                         byte[] fileBytes,
                                                         long startByte,
                                                         long endByte,
                                                         FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null || fileServerConnectionHelper.mDataOutputStream == null) {
            return false;
        }

        BinaryWriteStream writeStream = new BinaryWriteStream();
        writeStream.writeInt32(msg_type_upload_req); //msg_type_upload_req
        writeStream.writeInt32(mSeq);
        writeStream.writeString(fileMd5);
        writeStream.writeInt64(offset);
        writeStream.writeInt64(fileSize);
        writeStream.writeBytes(Arrays.copyOfRange(fileBytes, (int) startByte, (int) endByte));
        mSeq++;
        writeStream.flush();
        byte[] packageBytes = writeStream.getBytesArray();
        if (packageBytes == null || packageBytes.length == 0)
            return false;

        //long packageSize = packageBytes.length;
        try {
            fileServerConnectionHelper.mDataOutputStream.writeLong(BinaryWriteStream.longToLittleEndian(packageBytes.length));
            fileServerConnectionHelper.mDataOutputStream.write(packageBytes, 0, packageBytes.length);
            fileServerConnectionHelper.mDataOutputStream.flush();
        } catch (IOException e) {
            return false;
        }

        return true;
    }

    private static int recvUploadPackageFromFileServer(String fileMd5,
                                                       long fileSize,
                                                       long offset,
                                                       FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null || fileServerConnectionHelper.mDataInputStream == null)
            return file_msg_error_unknown;

        try {
            //读取包头
            long receivedPackageLengthLittleEndian = fileServerConnectionHelper.mDataInputStream.readLong();
            Log.d("NetworkerTag", "" + receivedPackageLengthLittleEndian);
            long receivedPackageLength = BinaryReadStream.longToBigEndian(receivedPackageLengthLittleEndian);
            Log.d("NetworkerTag", "" + receivedPackageLength);
            if (receivedPackageLength <= 0L || receivedPackageLength >= 4 * 1024 * 1024 * 1024L) {
                return file_msg_error_unknown;
            }

            byte[] bodyBytes = new byte[(int) receivedPackageLength];
            int actualBytesRead = fileServerConnectionHelper.mDataInputStream.read(bodyBytes);
            if (actualBytesRead != bodyBytes.length) {
                return file_msg_error_unknown;
            }

            BinaryReadStream binaryReadStream = new BinaryReadStream(bodyBytes);
            int responseCmd = binaryReadStream.readInt32();
            if (responseCmd != msg_type_upload_resp) {
                return file_msg_error_unknown;
            }

            int responseSeq = binaryReadStream.readInt32();
            int responseErrorCode = binaryReadStream.readInt32();
            String responseMd5 = binaryReadStream.readString();
            if (!responseMd5.equals(fileMd5)) {
                return file_msg_error_unknown;
            }

            long responseOffsetLittleEndian = binaryReadStream.readInt64();
            long responseOffset = BinaryReadStream.longToBigEndian(responseOffsetLittleEndian);
//            if (responseOffset != offset) {
//                return file_msg_error_unknown;
//            }

            long responseFileSizeLittleEndian = binaryReadStream.readInt64();
            long responseFileSize = BinaryReadStream.longToBigEndian(responseFileSizeLittleEndian);
//            if (responseFileSize != fileSize) {
//                return file_msg_error_unknown;
//            }

            String responseFileData = binaryReadStream.readString();
            if (responseFileData.length() != 0) {
                return file_msg_error_unknown;
            }

            if (responseErrorCode == file_msg_error_complete) {
                //上传成功
                return file_msg_error_complete;
            } else if (responseErrorCode == file_msg_error_progress) {
                //继续上传
                return file_msg_error_progress;
            }

            return file_msg_error_unknown;

        } catch (IOException e) {

        }

        return file_msg_error_unknown;
    }

    private static boolean sendDownloadPackageToFileServer(String fileMd5,
                                                           long offset,
                                                           long fileSize,
                                                           FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null || fileServerConnectionHelper.mDataOutputStream == null) {
            return false;
        }

        BinaryWriteStream writeStream = new BinaryWriteStream();
        writeStream.writeInt32(msg_type_download_req);
        writeStream.writeInt32(mSeq);
        writeStream.writeString(fileMd5);
        writeStream.writeInt64(offset);
        writeStream.writeInt64(fileSize);
        writeStream.writeString("");

        int clientNetType = NETWORKER_TYPE_CELLULAR;
        if (NetUtils.isWifi(FlamingoApplication.getContext()))
            clientNetType = NETWORKER_TYPE_WIFI;

        writeStream.writeString("");
        writeStream.writeInt32(clientNetType);
        mSeq++;
        writeStream.flush();
        byte[] packageBytes = writeStream.getBytesArray();
        if (packageBytes == null || packageBytes.length == 0)
            return false;

        //long packageSize = packageBytes.length;
        try {
            fileServerConnectionHelper.mDataOutputStream.writeLong(BinaryWriteStream.longToLittleEndian(packageBytes.length));
            fileServerConnectionHelper.mDataOutputStream.write(packageBytes, 0, packageBytes.length);
            fileServerConnectionHelper.mDataOutputStream.flush();
        } catch (IOException e) {
            return false;
        }

        return true;
    }

    private static int recvDownloadPackageFromFileServer(String fileMd5,
                                                         long fileSize,
                                                         long offset,
                                                         ByteArrayOutputStream byteArrayOutputStream,
                                                         FileServerConnectionHelper fileServerConnectionHelper) {
        if (fileServerConnectionHelper == null || fileServerConnectionHelper.mDataInputStream == null)
            return file_msg_error_unknown;

        byte[] fileBytes = null;

        try {
            //读取包头
            long receivedPackageLengthLittleEndian = fileServerConnectionHelper.mDataInputStream.readLong();
            Log.d("NetworkerTag", "" + receivedPackageLengthLittleEndian);
            long receivedPackageLength = BinaryReadStream.longToBigEndian(receivedPackageLengthLittleEndian);
            Log.d("NetworkerTag", "" + receivedPackageLength);
            if (receivedPackageLength <= 0L || receivedPackageLength >= 4 * 1024 * 1024 * 1024L) {
                return file_msg_error_unknown;
            }

            byte[] bodyBytes = new byte[(int) receivedPackageLength];
            int actualBytesRead = fileServerConnectionHelper.mDataInputStream.read(bodyBytes);
//            if (actualBytesRead != bodyBytes.length) {
//                return file_msg_error_unknown;
//            }

            BinaryReadStream binaryReadStream = new BinaryReadStream(bodyBytes);
            int responseCmd = binaryReadStream.readInt32();
            if (responseCmd != msg_type_download_resp) {
                return file_msg_error_unknown;
            }

            int responseSeq = binaryReadStream.readInt32();
            int responseErrorCode = binaryReadStream.readInt32();
            String responseMd5 = binaryReadStream.readString();
            if (!responseMd5.equals(fileMd5)) {
                return file_msg_error_unknown;
            }

            long responseOffset = binaryReadStream.readInt64();

            long responseFileSize = binaryReadStream.readInt64();


            String responseFileData = binaryReadStream.readString();
//            if (responseFileData.length() != 0) {
//                return file_msg_error_unknown;
//            }

            if (responseErrorCode == file_msg_error_complete) {
                //上传成功
                if (!responseFileData.isEmpty()) {
                    try {
                        fileBytes = responseFileData.getBytes("UTF-8");
                    } catch (UnsupportedEncodingException e) {
                        return file_msg_error_unknown;
                    }

                    if (fileBytes == null || fileBytes.length == 0)
                        return file_msg_error_unknown;

                    byteArrayOutputStream.write(fileBytes);
                }

                return file_msg_error_complete;
            } else if (responseErrorCode == file_msg_error_progress) {
                //继续上传
                if (responseFileData.isEmpty())
                    return file_msg_error_unknown;

                try {
                    fileBytes = responseFileData.getBytes("UTF-8");
                } catch (UnsupportedEncodingException e) {
                    return file_msg_error_unknown;
                }

                if (fileBytes == null || fileBytes.length == 0)
                    return file_msg_error_unknown;

                byteArrayOutputStream.write(fileBytes);

                return file_msg_error_progress;
            }

            return file_msg_error_unknown;

        } catch (IOException e) {

        }

        return file_msg_error_unknown;
    }

    /**
     *  文件下载
     */
    /**
     * 客户端：cmd = msg_type_download_req, seq, filemd5, offset, filesize, filedata
     * 服务器: cmd = msg_type_download_resp, seq, errorcode, filemd5, offset, filesize, filedata
     **/
    public static boolean downloadFile(final String localFileName, final String fileMd5OnServer) {
        if (localFileName.isEmpty() || fileMd5OnServer.isEmpty())
            return false;

        new Thread() {
            @Override
            public void run() {
                FileServerConnectionHelper fileServerConnectionHelper = new FileServerConnectionHelper();

                if (!connectToFileServer(mImgServerIp, mImgServerPort, fileServerConnectionHelper)) {
                    closeFileServerConnection(fileServerConnectionHelper);
                    Log.e(NETWORKER_TAG, "Failed to connect img server, ip: " + mImgServerIp + ", port: " + mImgServerPort);
                    //TODO: 连接服务器失败，下载失败通知相应的聊天activity
                    return;
                }

                boolean bError = false;

                long currentSendSize = EACH_BYTES_TO_SEND;
                long offset = 0;
                ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();

                while (true) {

                    if (!sendDownloadPackageToFileServer(fileMd5OnServer,
                            offset,
                            0,
                            fileServerConnectionHelper)) {

                        bError = true;
                        break;
                    }

                    int errorCode = recvDownloadPackageFromFileServer(fileMd5OnServer,
                            offset,
                            0,
                            byteArrayOutputStream,
                            fileServerConnectionHelper);
                    if (errorCode == file_msg_error_progress) {
                        continue;
                    } else if (errorCode == file_msg_error_complete) {
                        break;
                    } else {
                        bError = true;
                        break;
                    }

                }// end while

                //不管是成功还是失败，都要关闭socket，清理流对象
                closeFileServerConnection(fileServerConnectionHelper);

                if (bError) {
                    Log.e(NETWORKER_TAG, "Failed to upload imgfile, fileName: fileMd5: "
                            + fileMd5OnServer +
                            ", ip: " + mImgServerIp + ", port: " + mImgServerPort);

                    //TODO: 上传失败通知UI， 下载失败通知相应的聊天activity
                } else {
                    Log.i(NETWORKER_TAG, "Upload imgfile successfully, fileMd5: "
                            + fileMd5OnServer +
                            ", ip: " + mImgServerIp + ", port: " + mImgServerPort);

                    if (!writeToLocalFile(localFileName, byteArrayOutputStream)) {
                        //TODO：写文件失败，下载失败通知相应的聊天activity
                    }

                    try {
                        byteArrayOutputStream.close();
                    } catch (IOException e) {

                    }


                    //TODO: 上传成功通知UI，通知相应的activity显示聊天图片。
                }

            }

        }.start();

        return true;
    }

    private static boolean writeToLocalFile(String localFileName, ByteArrayOutputStream byteArrayOutputStream) {
        if (byteArrayOutputStream == null)
            return false;

        //文件不存在，先创建文件
        File localFile = new File(localFileName);
        try {
            //如果文件不存在，则创建新的文件
            if (!localFile.exists()) {
                if (!localFile.createNewFile())
                    return false;
            }
        } catch (Exception e) {
            //e.printStackTrace();
            return false;
        }

        try {
            FileOutputStream fileOutputStream = new FileOutputStream(localFile);
            byteArrayOutputStream.writeTo(fileOutputStream);
            fileOutputStream.flush();
            fileOutputStream.close();
        } catch (IOException e) {
            return false;
        }

        return true;
    }


//    public static boolean downloadFile(String localFileName, final String fileMd5OnServer) {
//
//        final byte[] bytes = getBytesFromFile(new File(localFileName));
//        //Log.d("zzh", "zzh");
//
//        new Thread() {
//            @Override
//            public void run() {
//                Socket _socket = null;
//                DataOutputStream _dataOutputStream = null;
//                DataInputStream _dataInputStream = null;
//                int cmd;
//                int seq;
//                try {
//                    _socket = new Socket();
//                    //设置连接请求超时时间5s
//                    _socket.connect(new InetSocketAddress(mFileServerIp, mFileServerPort), MAX_CONNECT_TIMEOUT);
//                    if (_socket == null) {
//                        LoggerFile.LogError("Unable to connect to " + mChatServerIp + ":" + mChatServerPort);
//                        notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                        return;
//                    }
//
//                    _socket.setSoTimeout(500);
//
//                    int i;
//
//                    int result = bytes.length / EACH_BYTES_TO_SEND;
//
//                    int times = bytes.length % EACH_BYTES_TO_SEND == 0 ? result : result + 1;
//
//                    for (i = 0; i < times; i++) {
//
//                        BinaryWriteStream writeStream = new BinaryWriteStream();
//                        writeStream.writeInt32(1); //msg_type_upload_req
//                        writeStream.writeInt32(mSeq);
//                        writeStream.writeString(fileMd5OnServer);
//                        writeStream.writeInt32(EACH_BYTES_TO_SEND * (times + 1));
//                        writeStream.writeInt64(bytes.length);
//                        int start, end;
//                        start = i * EACH_BYTES_TO_SEND;
//                        end = (i + 1) * EACH_BYTES_TO_SEND;
//                        if (times == 1 && bytes.length < EACH_BYTES_TO_SEND) {
//                            end = bytes.length;
//                        }
//                        writeStream.writeBytes(Arrays.copyOfRange(bytes, start, end));
//                        mSeq++;
//                        writeStream.flush();
//
//                        _dataOutputStream = new DataOutputStream(new BufferedOutputStream(_socket.getOutputStream()));
//                        if (_dataOutputStream == null) {
//                            close(_socket, null, null);
//                            //notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                            return;
//                        }
//
//                        byte[] b = writeStream.getBytesArray();
//                        if (b == null) {
//                            close(_socket, _dataOutputStream, null);
//                            notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                            return;
//                        }
//
//                        int packageSize = writeStream.getSize();
//                        try {
//                            //写入包头
//                            //_dataOutputStream.writeByte(0);
//                            //原始包大小
//                            _dataOutputStream.writeLong(BinaryWriteStream.longToLittleEndian(packageSize));
//                            //_dataOutputStream.writeInt(0);
//                            //byte[] reserved = new byte[16];
//                            // _dataOutputStream.write(reserved);
//                            //写入包体
//                            _dataOutputStream.write(b, 0, b.length);
//                            _dataOutputStream.flush();
//                        } catch (IOException e) {
//                            //Log.i(TAG, "写入流异常");
//                            //e.printStackTrace();
//                            close(_socket, _dataOutputStream, null);
//                            notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                            return;
//                        }
//
//
//                        //读数据
//
//                        _dataInputStream = new DataInputStream(new BufferedInputStream(_socket.getInputStream()));
//                        if (_dataInputStream == null) {
//                            close(_socket, _dataOutputStream, _dataInputStream);
//                            //notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                            return;
//                        }
//
//                        //读取包头
//                        long resultCmd = _dataInputStream.readLong();
////                        int rawpackagelength = _dataInputStream.readInt();
////                        int packagelength = BinaryReadStream.intToBigEndian(rawpackagelength);
////                        //TODO: 很奇怪的包头，直接断开连接
////                        if (packagelength <= 0 || packagelength > 65535) {
////                            close(_socket, _dataOutputStream, _dataInputStream);
////                            LoggerFile.LogError("recv a strange packagelength: " + packagelength);
////                            //notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
////                            return;
////                        }
////
////                        int compresslengthRaw = _dataInputStream.readInt();
////                        int compresslength = BinaryReadStream.intToBigEndian(compresslengthRaw);
////
////                        _dataInputStream.skipBytes(16);
////
////                        byte[] bodybuf = new byte[compresslength];
////                        _dataInputStream.read(bodybuf);
////
////                        byte[] registerResult = null;
////                        if (compressFlag == 1)
////                            registerResult = ZlibUtil.decompressBytes(bodybuf);
////                        else
////                            registerResult = bodybuf;
////                        if (registerResult == null || registerResult.length != packagelength) {
////                            close(_socket, _dataOutputStream, _dataInputStream);
////                            notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
////                            return;
////                        }
////
////                        BinaryReadStream binaryReadStream = new BinaryReadStream(registerResult);
////                        cmd = binaryReadStream.readInt32();
////                        seq = binaryReadStream.readInt32();
////
////                        if (cmd != 2) {
////                            //未收到数据
////                            break;
////                        }
//
//
//                    }
//
//                } catch (Exception e) {
//                    close(_socket, _dataOutputStream, _dataInputStream);
//                    notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
//                    return;
//                }
//
//                        BinaryReadStream binaryReadStream = new BinaryReadStream(registerResult);
//                        cmd = binaryReadStream.readInt32();
//                        seq = binaryReadStream.readInt32();
//
//                } finally {
//                    if (_socket != null) {
//                        try {
//                            _socket.close();
//                        } catch (IOException e) {
//                            e.printStackTrace();
//                        }
//                    }
//                }
//            }
//
//        }.start();
//
//        return true;
//    }


    public static byte[] getBytesFromFile(File f) {
        if (f == null) {
            return null;
        }

        FileInputStream stream = null;
        ByteArrayOutputStream out = new ByteArrayOutputStream(1000);
        byte[] b = new byte[1000];
        int n;

        try {
            stream = new FileInputStream(f);
            while ((n = stream.read(b)) != -1) {
                out.write(b, 0, n);
            }
        } catch (IOException e) {
            Log.d(NETWORKER_TAG, "getBytesFromFile error: " + e.getMessage());
        } finally {
            if (stream != null) {
                try {
                    stream.close();
                } catch (IOException e) {

                }
            }

            if (out != null) {
                try {
                    out.close();
                } catch (Exception e) {

                }
            }

        }

        return out.toByteArray();
    }

    //收发线程会同时调用这个函数，所以用锁保护起来
    public synchronized void closeConnection() {
        //if (mSocket == null)
        //	return;

//		try {
//			mDataInputStream.close();
//			mDataInputStream = null;
//
//			mDataOutputStream.close();
//			mDataOutputStream = null;
//
//			mSocket.close();
//			mSocket = null;
//
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
    }


    //选择服务器
    public void changeServer(String ip, int port) {
        //mIp = ip;
        //mPort = port;

        closeConnection();
    }


    public boolean writeBuf(byte[] data) {
        if (data.length <= 0)
            return false;

        try {
            mDataOutputStream.write(data);
        } catch (IOException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

    public void setMessageFailed() {
//		if (uAccountID != 0 && BaseActivity.getDb() != null) {
//			try {
//
//				List<ChatMessage> tis = BaseActivity.getDb().findAll(
//						Selector.from(ChatMessage.class));
//
//				if (tis != null && tis.size() > 0) {
//					for (int i = 0; i < tis.size(); ++i) {
//						ChatMessage msg = tis.get(i);
//						if (msg.getmMsgState() == 0) {
//							msg.setmMsgState(2);
//							BaseActivity.getDb().update(msg);
//						}
//					}
//				}
//			} catch (DbException e) {
//				e.printStackTrace();
//			}
//		}
    }

}


//
//import android.content.Context;
//        import android.net.ConnectivityManager;
//        import android.net.NetworkInfo;
//
///**@author
// *
// *这是一个判断当前网络状态的工具类
// *
// */
//public class netState {
//    /**
//     *
//     * @return 是否有活动的网络连接
//     */
//    public final boolean hasNetWorkConnection(Context context){
//        //获取连接活动管理器
//        final ConnectivityManager connectivityManager= (ConnectivityManager) context.
//                getSystemService(Context.CONNECTIVITY_SERVICE);
//        //获取链接网络信息
//        final NetworkInfo networkInfo=connectivityManager.getActiveNetworkInfo();
//
//        return (networkInfo!= null && networkInfo.isAvailable());
//
//    }
//    /**
//     * @return 返回boolean ,是否为wifi网络
//     *
//     */
//    public final boolean hasWifiConnection(Context context)
//    {
//        final ConnectivityManager connectivityManager= (ConnectivityManager) context.
//                getSystemService(Context.CONNECTIVITY_SERVICE);
//        final NetworkInfo networkInfo=connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
//        //是否有网络并且已经连接
//        return (networkInfo!=null&& networkInfo.isConnectedOrConnecting());
//
//
//    }
//
//    /**
//     * @return 返回boolean,判断网络是否可用,是否为移动网络
//     *
//     */
//
//    public final boolean hasGPRSConnection(Context context){
//        //获取活动连接管理器
//        final ConnectivityManager connectivityManager= (ConnectivityManager) context.
//                getSystemService(Context.CONNECTIVITY_SERVICE);
//        final NetworkInfo networkInfo=connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
//        return (networkInfo!=null && networkInfo.isAvailable());
//
//    }
//    /**
//     * @return  判断网络是否可用，并返回网络类型，ConnectivityManager.TYPE_WIFI，ConnectivityManager.TYPE_MOBILE，不可用返回-1
//     */
//    public static final int getNetWorkConnectionType(Context context){
//        final ConnectivityManager connectivityManager=(ConnectivityManager) context.
//                getSystemService(Context.CONNECTIVITY_SERVICE);
//        final NetworkInfo wifiNetworkInfo=connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
//        final NetworkInfo mobileNetworkInfo=connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
//
//
//        if(wifiNetworkInfo!=null &&wifiNetworkInfo.isAvailable())
//        {
//            return ConnectivityManager.TYPE_WIFI;
//        }
//        else if(mobileNetworkInfo!=null &&mobileNetworkInfo.isAvailable())
//        {
//            return ConnectivityManager.TYPE_MOBILE;
//        }
//        else {
//            return -1;
//        }
//
//
//    }
//
//}