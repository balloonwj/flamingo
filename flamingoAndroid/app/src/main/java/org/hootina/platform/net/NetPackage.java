package org.hootina.platform.net;

/**
 * Created by zhangyl on 2018/3/19.
 */

public class NetPackage {
    public static final int PACKAGE_UNCOMPRESSED_FLAG = 0;
    public static final int PACKAGE_COMPRESSED_FLAG   = 1;

    public BinaryWriteStream    mWriteStream = new BinaryWriteStream();

    private int                  mCmd;
    private int                  mSeq;
    private String               mJson;
    private int                  mReserved1;
    private int                  mReserved2;

    public NetPackage(int cmd, int seq, String json){
        mWriteStream.writeInt32(cmd);
        mWriteStream.writeInt32(seq);
        mWriteStream.writeString(json);
        mWriteStream.flush();
    }

    public NetPackage(int cmd, int seq, String json, int arg1){
        mWriteStream.writeInt32(cmd);
        mWriteStream.writeInt32(seq);
        mWriteStream.writeString(json);
        mWriteStream.writeInt32(arg1);
        mWriteStream.flush();
    }

    public byte[] getBytes(){
        return mWriteStream.getBytesArray();
    }

    public int getBytesSize(){
        return mWriteStream.getSize();
    }
}
