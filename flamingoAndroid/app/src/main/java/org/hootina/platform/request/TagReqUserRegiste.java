package org.hootina.platform.request;

import org.hootina.platform.utils.NetDataTypeTransform;

public class TagReqUserRegiste {
	private static final int szAccountNoLen = 64;
	private static final int szNickNameLen = 64;
	private static final int szPasswdLen = 64;
	private static final int CommandLen = 192;
	private String szAccountNo;
	private String szNickName;
	private String szPasswd;
	public byte[] byteArrayData = new byte[CommandLen];
	private NetDataTypeTransform mDataTypeTransform = new NetDataTypeTransform();

	public byte[] getByteArrayData() {
		return byteArrayData;
	}

	public TagReqUserRegiste() {

	}

	public TagReqUserRegiste(String szAccountNo, String szNickName,
			String szPasswd) {
		this.szAccountNo = szAccountNo;
		this.szNickName = szNickName;
		this.szPasswd = szPasswd;

		int index = 0;
		byte[] szAccountNobyte = mDataTypeTransform
				.StringToByteArray(szAccountNo);
		System.arraycopy(szAccountNobyte, 0, byteArrayData, index,
				szAccountNobyte.length);

		index += 64;
		byte[] szNickNamebyte = mDataTypeTransform
				.StringToByteArray(szNickName);
		System.arraycopy(szNickNamebyte, 0, byteArrayData, index,
				szNickNamebyte.length);

		index += 64;
		byte[] szPasswdbyte = mDataTypeTransform.StringToByteArray(szPasswd);
		System.arraycopy(szPasswdbyte, 0, byteArrayData, index, szPasswdbyte.length);

		index += 64;
	}

	public TagReqUserRegiste(byte[] dataArray) {
		int index = 0;
		System.arraycopy(dataArray, 0, byteArrayData, 0, CommandLen);
		byte[] forszAccountNo = new byte[szAccountNoLen];
		System.arraycopy(dataArray, index, forszAccountNo, 0, forszAccountNo.length);
		szAccountNo = mDataTypeTransform.ByteArraytoString(forszAccountNo,
				forszAccountNo.length);
		index += forszAccountNo.length;

		byte[] forszNickName = new byte[szNickNameLen];
		System.arraycopy(dataArray, index, forszNickName, 0, forszNickName.length);
		szNickName = mDataTypeTransform.ByteArraytoString(forszNickName,
				forszNickName.length);
		index += forszNickName.length;

		byte[] forszPasswd = new byte[szPasswdLen];
		System.arraycopy(dataArray, index, forszPasswd, 0, forszPasswd.length);
		szPasswd = mDataTypeTransform.ByteArraytoString(forszPasswd,
				forszPasswd.length);
		index += forszPasswd.length;

	}

	public String getSzAccountNo() {
		return szAccountNo;
	}

	public void setSzAccountNo(String szAccountNo) {
		this.szAccountNo = szAccountNo;
	}

	public String getSzNickName() {
		return szNickName;
	}

	public void setSzNickName(String szNickName) {
		this.szNickName = szNickName;
	}

	public String getSzPasswd() {
		return szPasswd;
	}

	public void setSzPasswd(String szPasswd) {
		this.szPasswd = szPasswd;
	}

}
