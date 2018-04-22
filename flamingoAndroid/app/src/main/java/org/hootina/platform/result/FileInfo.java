package org.hootina.platform.result;

public class FileInfo {
	private String filename;
	public  String localFileName;
	private int size;
	private int offset;
	
	byte[] data;
	
	public String getName(){
		return filename;
	}
	
	public void setName(String name) {
		filename = name;
	}
	
	public int getSize(){
		return size;
	}
	
	public void setSize(int s) {
		size = s;
	}

	public byte[] getData(){
		return data;
	}
	
	public void setData(byte[] oo)
	{
		data = oo;
	}
}
