package org.hootina.platform.net;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.os.Message;
import android.support.v4.app.NotificationCompat;
import android.util.JsonReader;
import android.util.JsonToken;
import android.util.Log;

import com.google.protobuf.InvalidProtocolBufferException;

import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.MainActivity;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.enums.ClientType;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.enums.TabbarEnum;
import org.hootina.platform.request.TagServerMsg;
import org.hootina.platform.result.MessageTextEntity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.util.MegAsnType;
import org.hootina.platform.utils.LoggerFile;
import org.hootina.platform.utils.PictureUtil;
import org.hootina.platform.utils.ZlibUtil;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.StringReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

//TODO: 最好做成单例
public class NetWorker {
    //网络类型
    public static final int         NETWORKER_TYPE_OFFLINE  = 0;    //离线
    public static final int         NETWORKER_TYPE_CELLULAR = 1;    //3G/4G/5G在线
    public static final int         NETWORKER_TYPE_WIFI     = 2;    //wifi在线


    private final static int        MAX_CONNECT_TIMEOUT = 3000;
    //最大的包不能超过5M
    private final static int        MAX_PACKAGE_SIZE = 5 * 1024 * 1024;
    private final String 			TAG = "NetWorker";

	private static String 			mChatServerIp = "120.55.94.78";
	private static short			mChatServerPort = 20000;
	private static String			mImgServerIp = "120.55.94.78";
	private static short			mImgServerPort = 20001;
	private static String			mFileServerIp = "120.55.94.78";
	private static short			mFileServerPort = 20002;

	private static Socket 			mSocket;
    private static DataInputStream 	mDataInputStream;
    private static DataOutputStream mDataOutputStream;

	//数据包协议号
	private static int				mSeq = 0;
    private static List<NetPackage> mNetPackages = new LinkedList<>();
    private static boolean          mNetChatThreadRunning;

    private static List<String>     mNotificationSenders = new ArrayList<>();
    private static int              mNoficationCount     = 0;

	public NetWorker() {

	}

	public NetWorker(Context context) {
		//Context = context;
		//db = DbUtils.create(context);
	}

	public static void setChatServerIp(String ip){
		mChatServerIp = ip;
	}

	public static void setChatPort(short port){
		mChatServerPort = port;
	}

	public static void setImgServerIp(String ip){
		mImgServerIp = ip;
	}

	public static void setImgPort(short port){
		mImgServerPort = port;
	}

	public static void setFileServerIp(String ip){
		mFileServerIp = ip;
	}

	public static void setFilePort(short port){
		mFileServerPort = port;
	}

    public static String getChatServerIp(){
        return mChatServerIp;
    }

    public static int getChatPort(){
        return mChatServerPort;
    }

    public static String getImgServerIp(){

        return mImgServerIp;
    }

    public static int getImgPort(){
        return mImgServerPort;
    }

    public static String getFileServerIp(){
        return mFileServerIp;
    }

    public static int getFilePort(){
        return mFileServerPort;
    }

    public static int getNetworkerType(){
        return 1;
    }

    public synchronized static void addSender(String sender){
        for(String iter : mNotificationSenders){
            if (sender.equals(iter))
                return;
        }

        mNotificationSenders.add(sender);
    }

    public synchronized static int getSenderCount(){
        return mNotificationSenders.size();
    }

    public synchronized static void clearSenderCount(){
        mNotificationSenders.clear();
    }

    public synchronized static void addNotificationCount(){
        mNoficationCount++;
    }

    public synchronized static int getNotificationCount(){
        return mNoficationCount;
    }

    public synchronized static void clearNotificationCount(){
        mNoficationCount = 0;
    }

	public synchronized static void addPackage(NetPackage p){
        mNetPackages.add(p);
    }

    public static synchronized NetPackage retrievePackage(){
        if (mNetPackages.isEmpty())
            return null;

		NetPackage p = mNetPackages.get(0);
		mNetPackages.remove(0);
        //Log.i("Networker", "mNetPackages size: " + mNetPackages.size());
        return p;
    }

    public static boolean writePackage(NetPackage p, DataOutputStream stream){
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
			return false;
		}

		return true;
	}

	public static void close(Socket s, DataOutputStream outputStream, DataInputStream inputStream){
        if (s != null){
            try{
                s.close();
            }catch (IOException e){

            }

            s = null;
        }

        if (outputStream != null){
            try{
                outputStream.close();
            }catch (IOException e){

            }
            outputStream = null;
        }

        if (inputStream != null){
            try{
                inputStream.close();
            }catch (IOException e){

            }
            inputStream = null;
        }
    }

    public static void disconnectChatServer(){
        close(mSocket, mDataOutputStream, mDataInputStream);
    }

	//太啰嗦的代码，需要优化；
    //需要增加一些日志
	public static void registerUser(final String username, final String password, final String nickname)
	{
        new Thread()
        {
            @Override
            public void run()
            {
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
                    if (_socket == null){
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
                    if (_dataOutputStream == null){
						close(_socket, null, null);
						notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
                        return;
                    }

                    byte[] b = writeStream.getBytesArray();
                    if (b == null){
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

                if (cmd != MsgType.msg_type_register || retJson == "")
                {
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
                        }else{
                            reader.skipValue();
                        }

                    }// end while-loop
                    reader.endObject();
                    reader.close();

                }catch (NumberFormatException e) {
                    e.printStackTrace();
                }catch (IllegalStateException e){
                    e.printStackTrace();
                } catch (IOException e) {
                    e.printStackTrace();
                }

				try {
					_socket.close();
					_dataOutputStream.close();
					_dataInputStream.close();
				}catch (IOException e){

				}

                notifyUI(MsgType.msg_type_register, retcode, 0, null);
            }

        }.start();
	}

	//太啰嗦的代码，需要优化；
	//需要增加一些日志
	public static void login(final String username, final String password, final int clientType, final int status)
	{
		new Thread()
		{
			@Override
			public void run()
			{
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
					if (mDataOutputStream == null){
						close(mSocket, mDataOutputStream, mDataInputStream);
						notifyUI(MsgType.msg_type_register, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
						return;
					}

					byte[] b = writeStream.getBytesArray();
					if (b == null){
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

				} catch (Exception e) {
					close(mSocket, mDataOutputStream, mDataInputStream);
					notifyUI(MsgType.msg_type_login, MsgType.ERROR_CODE_UNKNOWNFAILED, 0, null);
					return;
				}

				if (cmd != MsgType.msg_type_login || retJson == "")
				{
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
						}else if (name.equals("userid")){
							loginUserInfo.set_userid(reader.nextInt());
						}else if (name.equals("username")){
							loginUserInfo.set_username(reader.nextString());
						}else if (name.equals("nickname")){
							loginUserInfo.set_nickname(reader.nextString());
						}else if (name.equals("facetype")){
							loginUserInfo.set_faceType(reader.nextInt());
						}else if (name.equals("customface")){
							loginUserInfo.set_customFacePath(reader.nextString());
						}else if (name.equals("gender")){
							loginUserInfo.set_gender(reader.nextInt());
						}else if (name.equals("birthday")){
							loginUserInfo.set_birthday(reader.nextInt());
						}else if (name.equals("signature")){
							loginUserInfo.set_signature(reader.nextString());
						}else if (name.equals("address")){
							loginUserInfo.set_address(reader.nextString());
						}else if (name.equals("phonenumber")){
							loginUserInfo.set_phoneNumber(reader.nextString());
						}else if (name.equals("mail")){
							loginUserInfo.set_mail(reader.nextString());
						}else{
							reader.skipValue();
						}

					}// end while-loop
					reader.endObject();
					reader.close();

				}catch (NumberFormatException e) {
					e.printStackTrace();
				}catch (IllegalStateException e){
					e.printStackTrace();
				} catch (IOException e) {
					e.printStackTrace();
				}

				notifyUI(MsgType.msg_type_login, retcode, 0, null);
			}

		}.start();
	}

    //获取好友列表
    public static void getFriendList() {
        NetPackage netPackage = new NetPackage(MsgType.msg_type_getfriendlist, mSeq, "");
        mSeq ++;
        addPackage(netPackage);
    }

    //获取群成员
    public static void getGroupMember(int groupid) {
        //{"groupid": 群id}
        String strJson = String.format("{\"groupid\": %d}", groupid);
        NetPackage netPackage = new NetPackage(MsgType.msg_type_getgroupmembers, mSeq, strJson);
        mSeq ++;
        addPackage(netPackage);
    }

    // 发送消息
    public static void sendChatMsg(int targetID, String chatMsg) {
        NetPackage netPackage = new NetPackage(MsgType.msg_type_chat, mSeq, chatMsg, targetID);
        mSeq ++;
        addPackage(netPackage);
    }

	public static boolean startNetChatThread(){
        if (mSocket == null || mDataOutputStream == null || mDataInputStream == null)
            return false;

        new Thread() {
			@Override
			public void run() {
                mNetChatThreadRunning = true;
                int packageNum = 0;
                while(mNetChatThreadRunning) {
                    while(true){
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

	public static void stopNetChatThread(){
        mNetChatThreadRunning = false;
    }

    public static boolean isNetChatThreadRunning(){
        return mNetChatThreadRunning;
    }

	public static void notifyUI(int what, int arg1, int arg2, Object obj){
        Message msg = new Message();
        msg.what = what;
        msg.arg1 = arg1;
        msg.arg2 = arg2;
        msg.obj = obj;
        BaseActivity.sendMessage(msg);
    }

	private static boolean recvPackage(DataInputStream inputStream, NetDataParser parser){
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
                if (compresslength<0 || compresslength >= MAX_PACKAGE_SIZE)
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

                    if (cmd == MsgType.msg_type_chat)
                    {
                        parser.mArg1 = binaryReadStream.readInt32();
                        parser.mArg2 = binaryReadStream.readInt32();
                    }

                    return true;
                }
            }
        }catch (SocketTimeoutException ex) {
            return true;
        }
        catch (IOException e){
            return false;
        }

        return false;
	}

	// 接收消息
	public synchronized boolean receiveMsg() {
//		try {
//			do {
//				if (mDataInputStream == null) {
//					LoggerFile.LogError("mDataInputStream is null");
//					return false;
//				}
//
//				//接受缓冲区里面的字节数目小于一个包头大小
//				if (mDataInputStream.available() < PACKAGE_SIZE)
//				{
//					try {
//                        Thread.sleep(50);
//                        continue;
//                    }catch(InterruptedException e)
//                    {
//                        e.printStackTrace();
//                        return false;
//                    }
//
//				}
//				//包头都是4个字节
//				int rawpackagelength = mDataInputStream.readInt();
//				int packagelength = BinaryReadStream.intToBigEndian(rawpackagelength);
//				//TODO: 很奇怪的包头，直接断开连接
//				if (packagelength <= 0 || packagelength > 65535)
//				{
//					closeConnection();
//					Log.e(TAG, "收取包头出错：" + packagelength);
//                    LoggerFile.LogError("recv a strange packagelength: " + packagelength);
//					return false;
//				}
//
//				byte[] bodybuf = new byte[packagelength];
//
//				int ret = readmsg(bodybuf);
//				if (ret <= 0 || ret > packagelength) {
//					closeConnection();
//					Log.e("net error", "read failed");
//                    LoggerFile.LogError("read msg error, packagelength=" + packagelength);
//					return false;
//				}
//
//				BinaryReadStream binaryReadStream = new BinaryReadStream(bodybuf);
//				int cmd = binaryReadStream.readInt32();
//				int seq = binaryReadStream.readInt32();
//				//TODO: R改为小写和java风格保持一致
//				String data = binaryReadStream.readString();
//
//				if (cmd != MsgType.msg_type_chat)
//					handleServerResponseMsg(cmd, seq, data);
//				else
//				{
//					//聊天消息
//					int senderId = binaryReadStream.readInt32();
//					int targetId = binaryReadStream.readInt32();
//
//                    //Log.i("recv chat msg, senderID="+ senderId, ", targetId=" + targetId, "msg: " + data);
//                    LoggerFile.LogInfo("recv a chat msg, senderID="+ senderId, ", targetId=" + targetId, "msg: " + data);
//
//					Message msg = new Message();
//					msg.obj = data;
//					msg.arg1 = senderId;
//					msg.arg2 = targetId;
//					msg.what = MsgType.msg_type_chat;
//					BaseActivity.sendMessage(msg);
//				}
//
//			} while (true);
//		} catch (Exception e) {
//			closeConnection();
//            LoggerFile.LogError("recv msg error");
//            return false;
//		}

		return true;
	}

	private static void handleServerResponseMsg(NetDataParser parser) {
	    if (parser == null)
	        return;

		//_seq = parser.mSeq;

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
				Log.i("NetWorker", "recv chat data: " + parser.mJson);
			    handleChatMsg(parser.mJson, parser.mArg1, parser.mArg2);
				break;

			case tms.Base.cmd.n_AnsInfo_VALUE: {

				// 登录注册修改个人信息等
				//ansLonginRegiest();

			}
				break;
			case tms.Base.cmd.n_TargetsNotify_VALUE: {
				// 查询添加好友时返回的好友
				//ansNotify();

			}
				break;
			case tms.Base.cmd.n_UTargetInfos_VALUE: {
				// 好友列表
	//			ansTargeinfo();
			}
				break;

			case tms.Base.cmd.n_TargetsAddNotify_VALUE: {
				// 贴加好友
				// ansAddFriend();

			}
				break;
			case tms.Base.cmd.n_TargetsAdd_VALUE: {
				// 贴加好友
				//ansAddFriendnew();

			}
				break;
			case tms.Base.cmd.n_TalkMsgAns_VALUE: {
				// 发送消息
				//ansSendmsg();

			}
				break;
			case tms.Base.cmd.n_TalkMsg_VALUE: {
				// 接受消息
				//handleChatMsg();

			}
				break;

			case tms.Base.cmd.n_TargetsQuery_VALUE: {
				// 查询好友
				//ansTargetsQuery();

			}
				break;
			case tms.Base.cmd.n_SysUserPrivateInfo_VALUE: {
				// 同步用户私有信息
				//ansSysUserPrivateInfo();

			}
				break;
			case tms.Base.cmd.n_SysUserPrivateInfoList_VALUE: {
				// 同步 好友信息
				//ansSysUserPrivateInfoList();

			}
				break;
			case tms.Base.cmd.n_SysUserInfo_VALUE: {
				// 同步 好友信息
				//ansSysUserInfo();

			}
				break;
			case tms.Base.cmd.n_SysTargetInfoList_VALUE: {
				// 同步 好友信息
				//ansSysTargetInfoList();

			}
				break;
			case tms.Base.cmd.n_UpdateUserInfo_VALUE: {
				// genxingxinxi
				//ansUpdateUserInfo();

			}
				break;
			case tms.Base.cmd.n_SysMemberInfoList_VALUE: {
				// genxingxinxi
				//ansSysMemberInfoList(tagServerMsg);

			}
				break;
			case tms.Base.cmd.n_ChangePasswd_VALUE: {
				//ansChangePasswd();
			}
				break;

			default:
				Log.w("unhandled cmd: ", "" + parser.mCmd);
				break;
		}
	}

	// 添加好友
	public void addFriend(int uTargetId, tms.User.TargetsAdd.cmd ncmd) {

//		try {
//			byte[] data = new byte[1024];
//			msg = new Msg(0, 0);
//			tms.User.TargetsAdd.Builder tagestAddBuilder = tms.User.TargetsAdd
//					.newBuilder();
//			// tms.User.TargetsAdd.cmd
//			tagestAddBuilder.setUTargetID(uTargetId);
//			tagestAddBuilder.setNcmd(ncmd);
//
//			tagServerMsg = new TagServerMsg(tms.Base.cmd.n_TargetsAdd_VALUE,
//					(byte) 0, tagestAddBuilder.build().toByteArray());
//
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//			int index = 0;
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}

	}

	// ///同步用户私有信息
	public void userprivateinfo(int Version, int uTargetID) {

//		try {
//			byte[] data = new byte[1024];
//			msg = new Msg(0, 0);
//			tms.User.SysUserPrivateInfo.Builder sysUserPrivateInfo = tms.User.SysUserPrivateInfo
//					.newBuilder();
//			sysUserPrivateInfo.setUVersion(Version);
//			sysUserPrivateInfo.setUTargetID(uTargetID);
//			tagServerMsg = new TagServerMsg(
//					tms.Base.cmd.n_SysUserPrivateInfo_VALUE, (byte) 0,
//					sysUserPrivateInfo.build().toByteArray());
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//			int index = 0;
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}

	}

	public void SysUserInfo(int Version, int uTargetID) {

//		try {
//
//			msg = new Msg(0, 0);
//			tms.User.SysUserInfo.Builder sSysUserInfo = tms.User.SysUserInfo
//					.newBuilder();
//			sSysUserInfo.setUVersion(Version);
//			sSysUserInfo.setUTargetID(uTargetID);
//			tagServerMsg = new TagServerMsg(tms.Base.cmd.n_SysUserInfo_VALUE,
//					(byte) 0, sSysUserInfo.build().toByteArray());
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//			int index = 0;
//
//			byte[] data = new byte[tagServerMsg.getByteArrayData().length + 1024];
//
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}

	}

	public void send(int ntype, com.google.protobuf.GeneratedMessage bufmsg) {
//		try {
//			msg = new Msg(0, 0);
//			tagServerMsg = new TagServerMsg(ntype, (byte) 0,
//					bufmsg.toByteArray());
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//
//			int nTotalLen = 0;
//			nTotalLen = protocolHead.getByteArrayData().length
//					+ msg.getByteArrayData().length
//					+ tagServerMsg.getByteArrayData().length;
//
//			byte[] data = new byte[nTotalLen + 100];
//
//			int index = 0;
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
	}

	public void sends(int ntype, com.google.protobuf.GeneratedMessage bufmsg) {
//		try {
//
//			msg = new Msg(0, 0);
//			tagServerMsg = new TagServerMsg(ntype, (byte) 0,
//					bufmsg.toByteArray());
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//
//			int nTotalLen = 0;
//			nTotalLen = protocolHead.getByteArrayData().length
//					+ msg.getByteArrayData().length
//					+ tagServerMsg.getByteArrayData().length;
//
//			byte[] data = new byte[nTotalLen + 100];
//
//			int index = 0;
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
	}

	public void updateUserInfo(String name, int naface,
							   com.google.protobuf.GeneratedMessage bufmsg) {
//		try {
//			msg = new Msg(0, 0);
//
//			tagServerMsg = new TagServerMsg(
//					tms.Base.cmd.n_UpdateUserInfo_VALUE, (byte) 0,
//					bufmsg.toByteArray());
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
//
//			int nTotalLen = 0;
//			nTotalLen = protocolHead.getByteArrayData().length
//					+ msg.getByteArrayData().length
//					+ tagServerMsg.getByteArrayData().length;
//
//			byte[] data = new byte[nTotalLen + 100];
//
//			int index = 0;
//			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
//					protocolHead.getByteArrayData().length);
//			index += protocolHead.getByteArrayData().length;
//			System.arraycopy(msg.getByteArrayData(), 0, data, index,
//					msg.getByteArrayData().length);
//			index += msg.getByteArrayData().length;
//
//			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
//					tagServerMsg.getByteArrayData().length);
//			index += tagServerMsg.getByteArrayData().length;
//			if (mDataOutputStream != null) {
//				mDataOutputStream.write(data, 0, index);
//				mDataOutputStream.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
	}

    // 好友列表
    private static void handleFriendList(String data) {
        if (data.isEmpty()){
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
        int retcode = 0;

        UserSession.getInstance().clearFriendInfo();
		List<UserInfo> friends = UserSession.getInstance().friends;

        try {
            JsonReader reader = new JsonReader(new StringReader(data));

            reader.beginObject();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (name.equals("code")) {
                    retcode = reader.nextInt();
                } else if (name.equals("userinfo") && reader.peek() != JsonToken.NULL) {
                    reader.beginArray();
                    while (reader.hasNext()) {
                        reader.beginObject();
                        while (reader.hasNext()) {
                            String nodename = reader.nextName();
                            if (nodename.equals("members")) {
                                reader.beginArray();
                                while (reader.hasNext())
                                {
                                    reader.beginObject();
                                    UserInfo u = new UserInfo();
                                    while(reader.hasNext())
                                    {
                                        String nodename2 = reader.nextName();
                                        if (nodename2.equals("userid")) {
                                            u.set_userid(reader.nextInt());
                                        } else if (nodename2.equals("username")) {
                                            u.set_username(reader.nextString());
                                        } else if (nodename2.equals("nickname")) {
                                            u.set_nickname(reader.nextString());
                                        }else if (nodename2.equals("signature")) {
                                            u.set_signature(reader.nextString());
                                        }else if (nodename2.equals("facetype")) {
                                            u.set_faceType(reader.nextInt());
                                        } else if (nodename2.equals("customface")) {
                                            u.set_customFacePath(reader.nextString());
                                        } else if (nodename2.equals("status")) {
                                            u.set_onlinetype(reader.nextInt());
                                        } else{
                                            reader.skipValue();
                                        }
                                    }
                                    reader.endObject();
                                    friends.add(u);
                                }
                                reader.endArray();
                        } else{
                                reader.skipValue();
                            }
                        }
                        reader.endObject();

                    }
                    reader.endArray();
                } else{
                    reader.skipValue();
                }

            }// end while-loop
            reader.endObject();
            reader.close();

        }catch (NumberFormatException e) {
            e.printStackTrace();

            LoggerFile.LogError("parse friend list error, data=", data);
            return;
        }catch (IllegalStateException e){
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
        msg.arg1 = retcode;
        BaseActivity.sendMessage(msg);
    }

	private static void handleGroupMemberInfo(String data)
	{
		if (data.isEmpty())
			return;

		/*
		{"code":0, "msg": "ok", "groupid": 12345678,
		"members": [{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1},
		{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1},
		{"userid": 1, "username": xxxx, "nickname": yyyy, "facetype": 1, "customface": "ddd", "status": 1, "clienttype": 1}]}
		 */

		int retcode = 0;
		int groupid = 0;
		UserInfo currentGroup = null;

		List<UserInfo> friends = UserSession.getInstance().friends;

		try {
			JsonReader reader = new JsonReader(new StringReader(data));

			reader.beginObject();
			while (reader.hasNext()) {
				String name = reader.nextName();
				if (name.equals("code")) {
					retcode = reader.nextInt();
				}else if (name.equals("groupid")) {
					groupid = reader.nextInt();
				} else if (name.equals("members") && reader.peek() != JsonToken.NULL) {

					for (UserInfo u : friends)
					{
						if (u.get_userid() == groupid) {
							currentGroup = u;
							break;
						}
					}

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
							}else if (nodename.equals("signature")) {
								u.set_signature(reader.nextString());
							}else if (nodename.equals("facetype")) {
								u.set_faceType(reader.nextInt());
							} else if (nodename.equals("customface")) {
								u.set_customFacePath(reader.nextString());
							} else{
								reader.skipValue();
							}
						}
						reader.endObject();
						if (currentGroup != null)
							currentGroup.groupMembers.add(u);
					}
					reader.endArray();
				} else{
					reader.skipValue();
				}

			}// end while-loop
			reader.endObject();
			reader.close();

		} catch (NumberFormatException e) {
			e.printStackTrace();
			LoggerFile.LogError("parse group member list error, data=", data);
			return;
		} catch (IllegalStateException e){
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
		msg.arg1 = retcode;
		BaseActivity.sendMessage(msg);
	}

	// 修改密码
	private void ansChangePasswd() {
		//try {
			//tms.User.ChangePasswd changepwa = tms.User.ChangePasswd.parseFrom(mRecvMsg.getbData());

			Message msg = new Message();
			//msg.obj = changepwa;
			msg.what = MegAsnType.ChagePwd;
			BaseActivity.sendMessage(msg);

//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}

	}

	// 接受好友消息
	private static void handleChatMsg(String data, int senderID, int targetID) {
        Message msg = new Message();
        msg.what = MsgType.msg_type_chat;
        //msg.obj = data;
        msg.arg1 = senderID;
        msg.arg2 = targetID;

        Context context = FlamingoApplication.getContext();
        NotificationManager manager = (NotificationManager)context.
                                        getSystemService(Context.NOTIFICATION_SERVICE);
        UserInfo userinfo = UserSession.getInstance().getUserInfoById(senderID);
        if (userinfo == null){
            LoggerFile.LogError("Unable to find user, senderID:" + senderID + ", targetID:"
                                + targetID + "msg:" + data);
            return;
        }

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
                    while(reader.hasNext()){
                        reader.beginObject();
                        while(reader.hasNext()){
                            String innerName = reader.nextName();
                            //注意：这里不能使用==，必须使用equals
                            if (innerName.equals("faceID")){
                                faceId = reader.nextInt();
                                json += "{\"faceID\":";
                                json += faceId;
                                json += "},";
                                notifySnapshot += "[表情]";
                            } else if (innerName.equals("msgText")){
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
                } else{
                    reader.skipValue();
                }
            }// end while-loop
            reader.endObject();
            reader.close();

        } catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse chat data error, data=", data);
        } catch (IllegalStateException e){
            LoggerFile.LogError("parse chat data error, data=", data);
            return;
        } catch (IOException e){
            LoggerFile.LogError("parse chat data error, data=", data);
            return;
        }

        //去掉最后一个多余的逗号
        json = json.substring(0, json.length() - 1);
        json += "]";

        msg.obj = json;

        //Bitmap senderHeadBmp = PictureUtil.getFriendHeadPic(context.getAssets(), userinfo);
        //BitmapDrawable bd = new BitmapDrawable(context.getResource(), senderHeadBmp);

        String senderName = userinfo.get_nickname();

        NetWorker.addSender(senderName);
        NetWorker.addNotificationCount();

        String notificationTitle = senderName;
        String contentText = notifySnapshot;
        int senderCount = NetWorker.getSenderCount();
        int notificationCount = NetWorker.getNotificationCount();
        if (senderCount == 1 && notificationCount > 1)
            notificationTitle = String.format("%s (%d条新消息)", senderName, notificationCount);
        else if (senderCount > 1) {
            notificationTitle = "Flamingo";
            contentText = String.format("有 %d 个联系人给您发过来%d条新消息", senderCount, notificationCount);
        }


        Intent intent = null;
        if (senderCount == 1) {
            intent = new Intent(context, ChattingActivity.class);
            intent.putExtra("nickname", senderName);
            intent.putExtra("userid", senderID);
            intent.putExtra("msgtexts", "");
            intent.putExtra("type", "");
        }
        else if (senderCount > 1) {
            intent = new Intent(context, MainActivity.class);
            ((FlamingoApplication)context).setTabIndex(TabbarEnum.MESSAGE);
        }


        PendingIntent pendingIntent = PendingIntent.getActivity(context, 0, intent, 0);

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

        //新增一条聊天消息
        BaseActivity.getChatMsgDb().insertChatMsg(MessageTextEntity.generateMsgID(), msgTime,
                                                  senderID,  targetID,
                                                  MessageTextEntity.CONTENT_TYPE_TEXT, json,
                                                  ClientType.CLIENT_TYPE_ANDROID, 0,
                                                 "");


        ChatMsgMgr.getInstance().increaseUnreadChatMsgCount(senderID);

        ChatSessionMgr.getInstance().updateSession(senderID, senderName, json, "");

        BaseActivity.sendMessage(msg);
	}

	// 发送消息
	private void ansSendmsg() {
//		try {
//			tms.User.TalkMsgAns tTalkMsgAns = tms.User.TalkMsgAns
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = tTalkMsgAns;
//			msg.what = MegAsnType.ToTalkingmsg;
//			BaseActivity.sendMessage(msg);
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 添加好友
	private void ansAddFriend() {
//		try {
//			tms.User.TargetsAddNotify tTargetsAddNotify = tms.User.TargetsAddNotify
//					.parseFrom(mRecvMsg.getbData());
//
//			int uTargetID = tTargetsAddNotify.getTargetsAdds().getUTargetID();
//			if (tTargetsAddNotify.getTargetsAdds().getUError() == 0) {
//
//				try {
//					List<NewFriendEntity> list = BaseActivity.getDb().findAll(
//							Selector.from(NewFriendEntity.class)
//									.where("uAccountID", "=", uAccountID)
//									.and(WhereBuilder.b("uTargetID", "=",
//											uTargetID)));
//					if (list != null && list.size() != 0) {
//						BaseActivity.getDb().delete(list.get(0));
//						NewFriendEntity friendInfo = new NewFriendEntity();
//						friendInfo.setNcmd(tTargetsAddNotify.getTargetsAdds()
//								.getNcmd().name());
//						friendInfo.setnFace(tTargetsAddNotify.getTargetInfos()
//								.getNFace());
//						friendInfo.setStrAccountNo(tTargetsAddNotify
//								.getTargetInfos().getStrAccountNo());
//						friendInfo.setStrNickName(tTargetsAddNotify
//								.getTargetInfos().getStrNickName()
//								.toStringUtf8());
//						friendInfo.setuTargetID(uTargetID);
//						friendInfo.setuAccountID(uAccountID);
//						BaseActivity.getDb().save(friendInfo);
//					} else {
//
//						NewFriendEntity friendInfo = new NewFriendEntity();
//						friendInfo.setNcmd(tTargetsAddNotify.getTargetsAdds()
//								.getNcmd().name());
//						friendInfo.setnFace(tTargetsAddNotify.getTargetInfos()
//								.getNFace());
//						friendInfo.setStrAccountNo(tTargetsAddNotify
//								.getTargetInfos().getStrAccountNo());
//						friendInfo.setStrNickName(tTargetsAddNotify
//								.getTargetInfos().getStrNickName()
//								.toStringUtf8());
//						friendInfo.setuTargetID(uTargetID);
//						friendInfo.setuAccountID(uAccountID);
//						BaseActivity.getDb().save(friendInfo);
//					}
//				} catch (DbException e1) {
//					e1.printStackTrace();
//				}
//
//			}
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	private void ansAddFriendnew() {

//		try {
//			tms.User.TargetsAdd tTargetsAdd = tms.User.TargetsAdd
//					.parseFrom(mRecvMsg.getbData());
//
//			Message msg = new Message();
//			msg.obj = tTargetsAdd;
//			msg.what = MegAsnType.FriendChange;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 查询添加好友
	private void ansTargetsQuery() {
//		try {
//			tms.User.TargetsQuery tTargetsQuery = tms.User.TargetsQuery
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = tTargetsQuery;
//			msg.what = MegAsnType.TargetsQuery;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 同步用户私有信息

	private void ansSysUserPrivateInfo() {
//		try {
//			tms.User.SysUserPrivateInfo sSysUserPrivateInfo = tms.User.SysUserPrivateInfo
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = sSysUserPrivateInfo;
//			msg.what = MegAsnType.UserPrivateInfo;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 同步好友信息
	private void ansSysUserPrivateInfoList() {
//		try {
//			tms.User.SysUserPrivateInfoList sSysUserPrivateInfolist = tms.User.SysUserPrivateInfoList
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = sSysUserPrivateInfolist;
//			msg.what = MegAsnType.UserPrivateInfoList;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	private void ansSysUserInfo() {
//		try {
//			tms.User.SysUserInfo sSysUserInfo = tms.User.SysUserInfo.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = sSysUserInfo;
//			msg.what = 100;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	private void ansSysTargetInfoList() {
//		try {
//			tms.User.SysTargetInfoList sSysTargetInfoList = tms.User.SysTargetInfoList
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = sSysTargetInfoList;
//			msg.what = MegAsnType.TargetInfo;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	private void ansUpdateUserInfo() {
//		try {
//			tms.User.UpdateUserInfo uUpdateUserInfo = tms.User.UpdateUserInfo
//					.parseFrom(mRecvMsg.getbData());
//			Message msg = new Message();
//			msg.obj = uUpdateUserInfo;
//			msg.what = 102;
//			BaseActivity.sendMessage(msg);
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 查询添加好友时返回的好友
	private void ansNotify() {
//		try {
//			tms.User.TargetsNotify tTargetsNotify = tms.User.TargetsNotify
//					.parseFrom(mRecvMsg.getbData());
//			if (tTargetsNotify.getTargetssCount() > 0) {
//				for (int i = 0; i < tTargetsNotify.getTargetssCount(); i++) {
//					TargetInfo targetinfo = tTargetsNotify.getTargetss(i)
//							.getTargetInfos();
//					List<TargetInfo> tis = new ArrayList<TargetInfo>();
//					tis.add(targetinfo);
//					Message msg = new Message();
//					msg.obj = tTargetsNotify;
//					msg.what = MegAsnType.TargetsNotify;
//					BaseActivity.sendMessage(msg);
//				}
//			} else {
//			}
//
//		} catch (InvalidProtocolBufferException e) {
//			e.printStackTrace();
//		}
	}

	// 好友列表
	private void ansSysMemberInfoList(TagServerMsg tagServerMsg) {
		try {
			tms.User.SysMemberInfoList sysMemberList = tms.User.SysMemberInfoList
					.parseFrom(tagServerMsg.getbData());

			Message msg = new Message();
			msg.obj = sysMemberList;
			msg.what = MegAsnType.UserInfoReq;
			BaseActivity.sendMessage(msg);

		} catch (InvalidProtocolBufferException e) {
			e.printStackTrace();
		}
	}

	//收发线程会同时调用这个函数，所以用锁保护起来
	public synchronized void closeConnection() {
		//if (socket == null)
		//	return;

//		try {
//			mDataInputStream.close();
//			mDataInputStream = null;
//
//			mDataOutputStream.close();
//			mDataOutputStream = null;
//
//			socket.close();
//			socket = null;
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

	public boolean isGroup(int userid) {
		return userid > 0x0FFFFFFF;
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