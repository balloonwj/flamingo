package org.hootina.platform.net;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.Socket;

/**
 * @desc    文件传输辅助类
 * @author  zhangyl 2018.10.09
 */
public class FileServerConnectionHelper {
    public Socket           mSocket;
    public DataInputStream  mDataInputStream;
    public DataOutputStream mDataOutputStream;

    public FileServerConnectionHelper() {

    }
}
