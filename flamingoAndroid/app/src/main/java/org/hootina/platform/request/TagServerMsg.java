package org.hootina.platform.request;

import org.hootina.platform.utils.NetDataTypeTransform;

public class TagServerMsg {

	private static final int NMsgType = 4;
	public byte[] byteArrayData;
	private NetDataTypeTransform mDataTypeTransform = new NetDataTypeTransform();

	private int nMsgType;
	private byte nCmdID;
	private byte[] bData;

	public byte[] getByteArrayData() {
		return byteArrayData;
	}

	public TagServerMsg(int nMsgType, byte nCmdID, byte[] bData) {
		this.nMsgType = nMsgType;
		this.nCmdID = nCmdID;
		this.bData = bData;
		byteArrayData = new byte[NMsgType+1+bData.length + 1];

		int index = 0;
		byte[] NMsgType = mDataTypeTransform.IntToByteArray(nMsgType);
		System.arraycopy(NMsgType, 0, byteArrayData, index, NMsgType.length);
		index += NMsgType.length;

		byteArrayData[index] = nCmdID;
		index += 1;

		byte[] BData = bData;//mDataTypeTransform.StringToByteArray(bData);
		System.arraycopy(BData, 0, byteArrayData, index, BData.length);
		index += BData.length;
	}

	public TagServerMsg(byte[] dataArray) {
		int index = 0;
		byteArrayData = dataArray;

		byte[] foruNMsgType = new byte[NMsgType];
		System.arraycopy(dataArray, index, foruNMsgType, 0, foruNMsgType.length);
		nMsgType = mDataTypeTransform.ByteArrayToInt(foruNMsgType);
		index += foruNMsgType.length;

		nCmdID = dataArray[index];
		index += 1;
		if(dataArray.length - index-1>=0){
		bData=new byte[dataArray.length - index-1];
		System.arraycopy(dataArray, index, bData, 0, bData.length);}

	}

	public int getnMsgType() {
		return nMsgType;
	}

	public void setnMsgType(int nMsgType) {
		this.nMsgType = nMsgType;
	}

	public byte getnCmdID() {
		return nCmdID;
	}

	public void setnCmdID(byte nCmdID) {
		this.nCmdID = nCmdID;
	}

	public byte[] getbData() {
		return bData;
	}

	public void setbData(byte[] bData) {
		this.bData = bData;
	}



}
