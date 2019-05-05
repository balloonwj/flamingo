package org.hootina.platform.net;

/**
 * @desc 二进制协议编码类，组成C++结构体
 * @author zhangyl
 * @date 2017.08.18
 * @version 1.0
 */

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public class BinaryWriteStream {

    private String _str;
    private List<Byte> _list = new ArrayList<Byte>();
    public List<Byte> _list2 = new ArrayList<Byte>();
    private byte[] _bytes = null;

    //byte 数组与 int 的相互转换
    public static int byteArrayToInt(byte[] b) {
        return b[3] & 0xFF |
                (b[2] & 0xFF) << 8 |
                (b[1] & 0xFF) << 16 |
                (b[0] & 0xFF) << 24;
    }

    public static byte[] intToByteArray(int a) {
        return new byte[]{
                (byte) ((a >> 24) & 0xFF),
                (byte) ((a >> 16) & 0xFF),
                (byte) ((a >> 8) & 0xFF),
                (byte) (a & 0xFF)
        };
    }

    public static int intToLittleEndian(int n) {
        byte[] b = new byte[4];
        b[0] = (byte) (n & 0xff);
        b[1] = (byte) (n >> 8 & 0xff);
        b[2] = (byte) (n >> 16 & 0xff);
        b[3] = (byte) (n >> 24 & 0xff);

        int k = (int) (b[0] << 24) + (int) (b[1] << 16) + (int) (b[2] << 16) + (int) b[3];
        return k;
    }

    //
    public static long longToLittleEndian(long n) {
        byte[] b = new byte[8];
        b[0] = (byte) (n & 0xff);
        b[1] = (byte) (n >> 8 & 0xff);
        b[2] = (byte) (n >> 16 & 0xff);
        b[3] = (byte) (n >> 24 & 0xff);
        b[4] = (byte) (n >> 32 & 0xff);
        b[5] = (byte) (n >> 40 & 0xff);
        b[6] = (byte) (n >> 48 & 0xff);
        b[7] = (byte) (n >> 56 & 0xff);

        long k = (long) (b[0] << 56) + (long) (b[1] << 48) + (long) (b[2] << 40) + (long) b[3] << 32
                + (long) (b[4] << 24) + (long)(b[5] << 16) + (long)(b[6] << 8) + (long)(b[7]);
        return k;
    }

    public BinaryWriteStream() {
        //Byte[] pkgHeader = new Byte[6];
        //_str += pkgHeader;
    }

    byte[] intToBigEndian(int n) {
        byte[] b = new byte[4];
        b[3] = (byte) (n & 0xff);
        b[2] = (byte) (n >> 8 & 0xff);
        b[1] = (byte) (n >> 16 & 0xff);
        b[0] = (byte) (n >> 24 & 0xff);

        //int k = (int)(b[0] << 24) + (int)(b[1] << 16) + (int)(b[2] << 16) + (int)b[3];
        return b;
    }

    int compressInteger(int i, byte[] buf) {
        int len = 0;
        for (int a = 4; a >= 0; a--) {
            byte c;
            c = (byte) (i >> (a * 7) & 0x7f);
            if (c == 0x00 && len == 0)
                continue;

            if (a == 0)
                c &= 0x7f;
            else
                c |= 0x80;
            buf[len] = c;
            len++;
        }
        if (len == 0) {
            len++;
            buf[0] = 0;
        }

        return len;
    }

    public boolean writeString(String data) {
        if (data == null)
            return false;

        byte[] buf = new byte[5];
        int orgpressLen = compressInteger(data.length(), buf);

        for (int i = 0; i < orgpressLen; ++i) {
            _list.add(buf[i]);
        }

        byte[] strData;
        try{
            strData = data.getBytes("UTF-8");
        } catch (java.io.UnsupportedEncodingException e){
            strData = null;
        }

        for (int j = 0; j < strData.length; ++j) {
            _list.add(strData[j]);
        }

        return true;
    }

    public boolean writeBytes(byte[] data) {
        if (data == null)
            return false;

        int datalength = 0;
        datalength = data.length;

        byte[] buf = new byte[5];
        int orgpressLen = compressInteger(datalength, buf);

        for (int i = 0; i < orgpressLen; ++i) {
            _list.add(buf[i]);
        }

        for (int j = 0; j < datalength; ++j) {
            _list.add(data[j]);
        }

        return true;
    }

    public int getSize() {
        if (_bytes != null)
            return _bytes.length;
        else
            return 0;
    }

    public boolean writeInt32(int i) {
        //int bigEndian = ;
        byte[] tmp = intToBigEndian(i);
        for (int n = 0; n < tmp.length; ++n) {
            _list.add(tmp[n]);
        }

        return true;
    }

    public boolean writeInt64(long value) {
        //byte[] int64str = new byte[128];
        String strTemp = String.format("%d", value);
        writeString(strTemp);

        return true;
    }

    public String getData() {
        return _str;
    }

    public void flush() {
        int length = _list.size();
        Byte[] pkgHeader = new Byte[6];
        pkgHeader[3] = (byte) (length & 0xff);
        pkgHeader[2] = (byte) (length >> 8 & 0xff);
        pkgHeader[1] = (byte) (length >> 16 & 0xff);
        pkgHeader[0] = (byte) (length >> 24 & 0xff);
        for (int i = 0; i < pkgHeader.length; ++i) {
            _list2.add(pkgHeader[i]);
        }

        Iterator<Byte> firstIter = _list.iterator();
        while (firstIter.hasNext()) {
            _list2.add(firstIter.next());
        }

        int bytesLength = _list2.size();
        _bytes = new byte[bytesLength];

        for (int k = 0; k < bytesLength; ++k) {
            Byte xb = _list2.get(k);
            if (xb == null)
                _bytes[k] = 0;
            else
                _bytes[k] = (byte) xb;
            //System.out.println(k);
            //++i;
        }
    }

    public byte[] getBytesArray() {
        return _bytes;
    }

}