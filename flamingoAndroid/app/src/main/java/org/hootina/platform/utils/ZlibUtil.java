package org.hootina.platform.utils;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

import static android.R.attr.data;

/**
 * Created by zhangyl on 2018/3/17.
 */

public final class ZlibUtil {
    /**
     * 解压数据
     *
     * @param gzipStr
     * @return
     */
//    public static String decompress(String gzipStr) {
//        if (gzipStr.equals("")) {
//            return null;
//        }
//        byte[] t = AbBase64.decode(gzipStr);
//
//        Inflater decompresser = new Inflater();
//        decompresser.setInput(t, 0, t.length);
//        // 对byte[]进行解压，同时可以要解压的数据包中的某一段数据，就好像从zip中解压出某一个文件一样。
//        byte[] result = new byte[t.length];
//        try {
//            int resultLength = decompresser.inflate(result); // 返回的是解压后的的数据包大小，
//        } catch (DataFormatException e) {
//            e.printStackTrace();
//        }
//        decompresser.end();
//        return new String(result);
//
//    }
//
//    //压缩
//    public static String compress(String data) {
//        try {
//            ByteArrayOutputStream bos = new ByteArrayOutputStream();
//            DeflaterOutputStream zos = new DeflaterOutputStream(bos);
//            zos.write(data.getBytes());
//            zos.close();
//            return AbBase64.encode(bos.toByteArray());
//        } catch (Exception ex) {
//            ex.printStackTrace();
//        }
//        return "ZIP_ERR";
//    }

    public static String decompressString(byte[] bytes) {
        //Decompress the bytes // 开始解压,
        Inflater decompresser = new Inflater();
        decompresser.setInput(bytes, 0, bytes.length);
        //对byte[]进行解压，同时可以要解压的数据包中的某一段数据，就好像从zip中解压出某一个文件一样。
        byte[] result = new byte[bytes.length];
        int resultLength = 0;
        //返回的是解压后的的数据包大小
        try {
            resultLength  = decompresser.inflate(result);
        }catch (DataFormatException e){
            return "";
        }

        decompresser.end();
        if (resultLength <= 0)
            return "";

        return new String(result, 0, resultLength);
    }

    public static byte[] decompressBytes(byte[] data){
        byte[] output = new byte[0];
        Inflater decompresser = new Inflater();
        decompresser.reset();
        //设置当前输入解压
        decompresser.setInput(data, 0, data.length);
        ByteArrayOutputStream o = new ByteArrayOutputStream(data.length);
        try {
            byte[] buf = new byte[1024];
            while (!decompresser.finished()) {
                int i = decompresser.inflate(buf);
                o.write(buf, 0, i);
            }
            output = o.toByteArray();
        } catch (Exception e) {
            output = data;
            e.printStackTrace();
        } finally {
            try {
                o.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        decompresser.end();
        return output;
    }
}
