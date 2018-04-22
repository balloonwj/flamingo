package org.hootina.platform.result;

import android.os.Message;

public class NetReqQueue {
	public Message[] queue = new Message[100];
	public int nReadIndex = 0;
	public int nWriteIndex = 0;
	
	// UI 线程访问 
	public void Push(Message msg)
	{
		if(nWriteIndex + 1 != nReadIndex)
		{
			queue[nWriteIndex] = msg;
			nWriteIndex = (nWriteIndex + 1) % 100;
		}
	}
	
	// 网络线程访问
	public Message Pop()
	{
		if(nReadIndex != nWriteIndex)
		{
			Message msg =  queue[nReadIndex];
			queue[nReadIndex] = null;
			nReadIndex = (nReadIndex + 1) % 100;
			return msg;
		}
		else
		{
			return null;
		}
	}
}
