package org.hootina.platform.request;

import org.hootina.platform.utils.NetDataTypeTransform;

public class Msg {
//	public ProtocolHead protocolHead;

	public long reserve1;
	public long reserve2;

	private  static final int Reserve1Len = 8;
	private  static final int Reserve2Len = 8;
	private  static final int CommandLen = 16;
	public byte[] byteArrayData = new byte[CommandLen];
	public NetDataTypeTransform mDataTypeTransform = new NetDataTypeTransform();;

	public byte[] getByteArrayData() {
		return byteArrayData;
	}

	public Msg() {

	}

	public Msg( int reserve1, int reserve2) {
		this.reserve1 = reserve1;
		this.reserve2 = reserve2;
		int index = 0;
		byte[] reserve1byte = mDataTypeTransform.IntToByteArray(reserve1);
		System.arraycopy(reserve1byte, 0, byteArrayData, index, reserve1byte.length);
		index += reserve1byte.length;
		
		byte[] reserve2byte = mDataTypeTransform.IntToByteArray(reserve1);
		System.arraycopy(reserve2byte, 0, byteArrayData, index, reserve2byte.length);
		index += reserve2byte.length;
		
		
	}

	public Msg(byte[] dataArray) {
		int index = 0;
        System.arraycopy(dataArray,0, byteArrayData,0,CommandLen);  
        byte[] forreserve1 = new byte[Reserve1Len];  
        System.arraycopy(dataArray,index,forreserve1,0,forreserve1.length);  
        reserve1=mDataTypeTransform.ByteArrayToInt(forreserve1);  
        index += forreserve1.length;
        
        byte[] forreserve2 = new byte[Reserve2Len];  
        System.arraycopy(dataArray,index,forreserve1,0,forreserve2.length);  
        reserve2=mDataTypeTransform.ByteArrayToInt(forreserve2);  
        index += forreserve2.length;
		
		
	}


	public long getReserve1() {
		return reserve1;
	}

	public void setReserve1(int reserve1) {
		this.reserve1 = reserve1;
	}

	public long getReserve2() {
		return reserve2;
	}

	public void setReserve2(int reserve2) {
		this.reserve2 = reserve2;
	}

}
