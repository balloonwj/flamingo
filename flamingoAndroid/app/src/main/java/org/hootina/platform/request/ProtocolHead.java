package org.hootina.platform.request;

import org.hootina.platform.utils.NetDataTypeTransform;

public class ProtocolHead {
	private static final int Time_checkLen = 4;
	private static final int Req_ans_orgmandLen = 4;
	private static final int Source_sizeLen = 4;
	private static final int Package_size = 4;
	private static final int Self_paramkLen = 4;
	private static final int CommandLen = 20;
	public byte[] byteArrayData = new byte[CommandLen];
	private NetDataTypeTransform mDataTypeTransform = new NetDataTypeTransform();

	public int time_check;;
	public int req_ans_orgmand;
	public int source_size;
	public int package_size;;
	public int self_param;

	public byte[] getByteArrayData() {
		return byteArrayData;
	}

	public ProtocolHead(int time_check, int req_ans_orgmand, int source_size,
			int package_size, int self_param) {
		this.time_check = time_check;
		this.req_ans_orgmand = req_ans_orgmand;
		this.source_size = source_size;
		this.package_size = package_size;
		this.self_param = self_param;
		int index = 0;
		byte[] Time_checkbyte = mDataTypeTransform.IntToByteArray(time_check);
		System.arraycopy(Time_checkbyte, 0, byteArrayData, index,
				Time_checkbyte.length);
		index += Time_checkbyte.length;

		byte[] Req_ans_orgmand = mDataTypeTransform
				.IntToByteArray(req_ans_orgmand);
		System.arraycopy(Req_ans_orgmand, 0, byteArrayData, index,
				Req_ans_orgmand.length);
		index += Req_ans_orgmand.length;

		byte[] Source_size = mDataTypeTransform.IntToByteArray(source_size);
		System.arraycopy(Source_size, 0, byteArrayData, index,
				Source_size.length);
		index += Source_size.length;

		byte[] Package_size = mDataTypeTransform.IntToByteArray(package_size);
		System.arraycopy(Package_size, 0, byteArrayData, index,
				Package_size.length);
		index += Package_size.length;

		byte[] Self_param = mDataTypeTransform.IntToByteArray(self_param);
		System.arraycopy(Self_param, 0, byteArrayData, index, Self_param.length);
		index += Self_param.length;

	}

	public ProtocolHead(byte[] dataArray) {
		int index = 0;
		System.arraycopy(dataArray, 0, byteArrayData, 0, CommandLen);

		byte[] fortime_check = new byte[Time_checkLen];
		System.arraycopy(dataArray, index, fortime_check, 0,
				fortime_check.length);
		time_check = mDataTypeTransform.ByteArrayToInt(fortime_check);
		index += fortime_check.length;

		byte[] forreq_ans_orgmand = new byte[Req_ans_orgmandLen];
		System.arraycopy(dataArray, index, forreq_ans_orgmand, 0,
				forreq_ans_orgmand.length);
		req_ans_orgmand = mDataTypeTransform.ByteArrayToInt(forreq_ans_orgmand);
		index += forreq_ans_orgmand.length;

		byte[] forsource_size = new byte[Source_sizeLen];
		System.arraycopy(dataArray, index, forsource_size, 0,
				forsource_size.length);
		source_size = mDataTypeTransform.ByteArrayToInt(forsource_size);
		index += forsource_size.length;

		byte[] forpackage_size = new byte[Package_size];
		System.arraycopy(dataArray, index, forpackage_size, 0,
				forpackage_size.length);
		package_size = mDataTypeTransform.ByteArrayToInt(forpackage_size);
		index += forpackage_size.length;

		byte[] forself_param = new byte[Self_paramkLen];
		System.arraycopy(dataArray, index, forself_param, 0,
				forself_param.length);
		self_param = mDataTypeTransform.ByteArrayToInt(forself_param);
		index += forself_param.length;

	}

	public int getTime_check() {
		return time_check;
	}

	public void setTime_check(int time_check) {
		this.time_check = time_check;
	}

	public int getReq_ans_orgmand() {
		return req_ans_orgmand;
	}

	public void setReq_ans_orgmand(int req_ans_orgmand) {
		this.req_ans_orgmand = req_ans_orgmand;
	}

	public int getSource_size() {
		return source_size;
	}

	public void setSource_size(int source_size) {
		this.source_size = source_size;
	}

	public int getPackage_size() {
		return package_size;
	}

	public void setPackage_size(int package_size) {
		this.package_size = package_size;
	}

	public int getSelf_param() {
		return self_param;
	}

	public void setSelf_param(int self_param) {
		this.self_param = self_param;
	}
}
