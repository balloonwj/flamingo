package org.hootina.platform.net;

import android.content.Context;
import android.os.Handler;
import android.util.Log;

import org.hootina.platform.request.Msg;
import org.hootina.platform.result.FileInfo;
import org.hootina.platform.result.NetReqQueue;
import org.hootina.platform.utils.PictureUtil;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.Socket;
import java.util.List;

public class NetWorkerTwo extends Thread {
//	private final String TAG = "NetWorkerTwo";
//	//final static String IP = "192.168.0.86";
//	static String IP = "192.168.0.86";
//    static int PORT = 25261;
//	Socket socket;
//	DataInputStream dis;
//	DataOutputStream dos;
//	Context mContext;
//	protected final byte connect = 1;
//	protected final byte running = 2;
//	protected byte state = connect;
//
//	private boolean onWork = true;
//	private Msg msg;
//	private boolean connectedAlready = true;
//
//	private List<FileInfo> downloadingFiles;
//
//	public NetReqQueue netqueue = new NetReqQueue();
//
//	public Handler mHandler;
//
//	public NetWorkerTwo(Context context) {
//		mContext = context;
//	}
//
//	public NetWorkerTwo() {
//
//	}
//
//	public void run() {
//		try {
//			if (socket != null)
//				socket.close();
//			if (dis != null)
//				dis.close();
//			if (dos != null)
//				dos.close();
//			onWork = true;
//			state = connect;
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
//		while (onWork) {
//			switch (state) {
//			case connect:
//				connect();
//				break;
//			case running:
//				receiveMsg();
//				break;
//			default:
//				Log.i(TAG, "寮傚父锛�");
//				break;
//			}
//		}
//	}
//
//	private synchronized void connect() {
//		// 杩炴帴鏈嶅姟鍣ㄧ
//
//		// 杩炴帴鏈嶅姟鍣ㄧ
//		try {
//			socket = new Socket(IP, PORT);
//			sleep(500);
//			if (socket != null) {
//				state = running;
//				PictureUtil.clear();
//
//				dis = new DataInputStream(new BufferedInputStream(
//						socket.getInputStream()));
//				dos = new DataOutputStream(new BufferedOutputStream(
//						socket.getOutputStream()));
//				 connectedAlready = true;
//			}
//		} catch (Exception e) {
//			connectedAlready = false;
//			Log.i(TAG, "NetWorker connect() 寮傚父锛�" + e.toString());
//			state = connect;
//			if (socket != null) {
//				try {
//					socket.close();
//				} catch (IOException e1) {
//					e1.printStackTrace();
//				}
//			}
//			System.out.println(e.getMessage() + "//杈撳嚭寮傚父淇℃伅");
//			return;
//		}
//	}
//
//	private int readmsg(byte[] buffer) throws IOException {
//		int ret = 0;
//		while (true) {
//			ret += dis.read(buffer, ret, buffer.length - ret);
//			if (ret >= buffer.length || ret <= 0) {
//				return ret;
//			}
//		}
//	}
//
//	// 鎺ユ敹娑堟伅
//	public synchronized void receiveMsg() {
//		try {
//			do {
//				if (dis == null) {
//					break;
//				}
//
//				byte[] headbuffer = new byte[20];
//				int byteOffset = 0;
//
//				int ret = readmsg(headbuffer);
//				if(ret <= 0 || ret > headbuffer.length)
//				{
//					setOnWork(true);
//					Log.i("net error", "read failed");
//					return;
//				}
//
//				protocolHead = new ProtocolHead(headbuffer);
//
//				if (protocolHead.source_size <= 0
//						|| protocolHead.source_size > 512 * 1024) {
//					break;
//				}
//
//				byte[] buffer = new byte[protocolHead.source_size];
//
//				ret = readmsg(buffer);
//				if(ret <= 0 || ret > buffer.length)
//				{
//					setOnWork(true);
//					Log.i("net error", "read failed");
//					return;
//				}
//
//				int msglen = protocolHead.source_size - 16;
//				if (msglen <= 0) {
//					break;
//				}
//
//				byte[] msgbuffer = new byte[msglen];
//				System.arraycopy(buffer, 16, msgbuffer, 0, msglen);
//				int type = protocolHead.getReq_ans_orgmand();
////				switch (type) {
////				case MsgType.ProtobufMsg: // 鏈嶅姟鍣ㄥ唴閮ㄦ秷鎭�
////					mRecvMsg = new TagServerMsg(msgbuffer);
////					//handleTagServerMsg(mRecvMsg);
////					break;
////				}
//
//				return;
//			} while (false);
//
//			Log.i(TAG, "NetWorker receiveMsg() 寮傚父锛�");
//		} catch (Exception e) {
//			connectedAlready = false;
//			state = connect;
//			setOnWork(true);
//
//		}
//	}
//
//	public void sendFileUpInfo(String name, String strChecksum, int uDownsize, long uFilesize) {
////		try {
////			byte[] data = new byte[2048];
////			msg = new Msg(0, 0);
////			tms.User.FileUpInfo.Builder fileUpInfo = tms.User.FileUpInfo
////					.newBuilder();
////			fileUpInfo.setStrName(com.google.protobuf.ByteString
////					.copyFromUtf8(name));
////			fileUpInfo.setStrChecksum(strChecksum);
////			fileUpInfo.setUDownsize(uDownsize);
////			fileUpInfo.setUFilesize((int) uFilesize);
////
////			tagServerMsg = new TagServerMsg(tms.Base.cmd.n_FileUpInfo_VALUE,
////					(byte) 0, fileUpInfo.build().toByteArray());
////
////			protocolHead = new ProtocolHead(0, 1002,
////					msg.getByteArrayData().length
////							+ tagServerMsg.getByteArrayData().length, 0, 1);
////
////			int index = 0;
////			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
////					protocolHead.getByteArrayData().length);
////			index += protocolHead.getByteArrayData().length;
////			System.arraycopy(msg.getByteArrayData(), 0, data, index,
////					msg.getByteArrayData().length);
////			index += msg.getByteArrayData().length;
////
////			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
////					tagServerMsg.getByteArrayData().length);
////			index += tagServerMsg.getByteArrayData().length;
////			if (dos != null) {
////				dos.write(data, 0, index);
////				dos.flush();
////			}
////		} catch (IOException e) {
////			e.printStackTrace();
////		}
//
//	}
//
//	public void sendFileLoadData(int uError, int uFilesize, int uOffset,
//								 int uDownsize, int uTmmodified, byte[] szData) {
////		try {
////			int len = 0;
////			if(szData != null)
////			{
////				len = szData.length;
////			}
////			byte[] data = new byte[len + 512];
////			msg = new Msg(0, 0);
////			// tms.User.FileUp
////
////			tms.User.FileLoadData.Builder fileLoadData = tms.User.FileLoadData
////					.newBuilder();
////			fileLoadData.setUError(uError);
////			fileLoadData.setUFilesize(uFilesize);
////			fileLoadData.setUOffset(uOffset);
////			fileLoadData.setUDownsize(uDownsize);
////			fileLoadData.setUTmmodified(uTmmodified);
////			if (szData != null) {
////				fileLoadData.setSzData(com.google.protobuf.ByteString
////						.copyFrom(szData));
////			}
////			tagServerMsg = new TagServerMsg(tms.Base.cmd.n_FileLoadData_VALUE,
////					(byte) 0, fileLoadData.build().toByteArray());
////
////			protocolHead = new ProtocolHead(0, 1002,
////					msg.getByteArrayData().length
////							+ tagServerMsg.getByteArrayData().length, 0, 1);
////
////			int index = 0;
////			System.arraycopy(protocolHead.getByteArrayData(), 0, data, index,
////					protocolHead.getByteArrayData().length);
////			index += protocolHead.getByteArrayData().length;
////			System.arraycopy(msg.getByteArrayData(), 0, data, index,
////					msg.getByteArrayData().length);
////			index += msg.getByteArrayData().length;
////
////			System.arraycopy(tagServerMsg.getByteArrayData(), 0, data, index,
////					tagServerMsg.getByteArrayData().length);
////			index += tagServerMsg.getByteArrayData().length;
////			if (dos != null) {
////				dos.write(data, 0, index);
////				dos.flush();
////			}
////		} catch (IOException e) {
////			e.printStackTrace();
////		}
//
//	}
//
//	public void sendFileLoadInfo(byte[] strName, int uOffset, int uDownsize,
//			int uLocalsize, int uLocaltime, int bVersion) {
//		try {
//			byte[] data = new byte[uDownsize + 512];
//			msg = new Msg(0, 0);
//			// tms.User.FileUp
//
//			//tms.User.FileLoadInfo.Builder fileLoadInfo = tms.User.FileLoadInfo.newBuilder();
//			//fileLoadInfo.setStrName(com.google.protobuf.ByteString.copyFrom(strName));
//			//fileLoadInfo.setUOffset(uOffset);
//			//fileLoadInfo.setULocalsize(uLocalsize);
//			//fileLoadInfo.setULocaltime(uLocaltime);
//			//fileLoadInfo.setBVersion(bVersion);
//			//tagServerMsg = new TagServerMsg(tms.Base.cmd.n_FileLoadInfo_VALUE,
//			//		(byte) 0, fileLoadInfo.build().toByteArray());
//
//			protocolHead = new ProtocolHead(0, 1002,
//					msg.getByteArrayData().length
//							+ tagServerMsg.getByteArrayData().length, 0, 1);
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
//			if (dos != null) {
//				dos.write(data, 0, index);
//				dos.flush();
//			}
//		} catch (IOException e) {
//			e.printStackTrace();
//		}
//
//	}
//
//	public void setOnWork(boolean onWork) {
//		this.onWork = onWork;
//	}
//	//选择服务器
//	public void changgeserver(String iP,int pORT) {
//		connectedAlready = false;
//		state = connect;
//		IP=iP;
//		PORT=pORT;
//		}
//	public boolean writeBuf(byte[] data) {
//		int length = data.length;
//		try {
//			dos.write(data);
//		} catch (IOException e) {
//			e.printStackTrace();
//			return false;
//		}
//		return true;
//	}

}
