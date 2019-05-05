package org.hootina.platform.utils;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Iterator;
import java.util.Set;

/**
 * @desc   MD5加密
 * @author zhangyl
 */
public class Md5Util {
    /**
     * @param args
     * @return
     */
    private static String[] sortByPOPO(Set<String> args) {
        String[] ss = new String[args.size()];
        Iterator<String> iterator = args.iterator();
        int k = 0;
        while (iterator.hasNext()) {
            ss[k] = iterator.next();
            k++;
        }

        String tmp;
        for (int i = 0; i < ss.length; i++) {
            for (int j = i + 1; j < ss.length; j++) {
                if (orgpare(ss[i], ss[j]) > 0) {
                    tmp = ss[i];
                    ss[i] = ss[j];
                    ss[j] = tmp;
                }
            }
        }
        return ss;
    }

    private static String checkNull(String s) {
        return s == null ? "" : s;
    }

    // 十六位的md5加密
    private static String Md5(String plainText) {
        try {
            MessageDigest md = MessageDigest.getInstance("MD5");
            md.update(plainText.getBytes());
            byte b[] = md.digest();

            int i;

            StringBuffer buf = new StringBuffer("");
            for (int offset = 0; offset < b.length; offset++) {
                i = b[offset];
                if (i < 0)
                    i += 256;
                if (i < 16)
                    buf.append("0");
                buf.append(Integer.toHexString(i));
            }
            return buf.toString();
        } catch (NoSuchAlgorithmException e) {
            e.printStackTrace();
        }
        return "";
    }

    /**
     * 用来把小写变成大写
     */
    private static final int MASK = 0xFFDF;

    /**
     * 比较两个字符串大小
     *
     * @param o1
     * @param o2
     * @return
     */
    private static int orgpare(String o1, String o2) {
        int length1 = o1.length();
        int length2 = o2.length();
        int length = length1 > length2 ? length2 : length1;
        int c1, c2;
        int d1, d2;
        for (int i = 0; i < length; i++) {
            c1 = o1.charAt(i);
            c2 = o2.charAt(i);
            d1 = c1 & MASK;
            d2 = c2 & MASK;
            if (d1 > d2) {
                return 1;
            } else if (d1 < d2) {
                return -1;
            } else {
                if (c1 > c2) {
                    return 1;
                } else if (c1 < c2) {
                    return -1;
                }
            }
        }
        if (length1 > length2) {
            return 1;
        } else if (length1 < length2) {
            return -1;
        }
        return 0;
    }

    /**
     * 加密
     *
     * @param inputText
     * @param algorithmName
     * @return
     */
    private static String encrypt(String inputText) {
        if (inputText == null || "".equals(inputText.trim())) {
            throw new IllegalArgumentException("请输入要加密的内容");
        }
        String encryptText = null;
        try {
            MessageDigest m = MessageDigest.getInstance("MD5");
            m.update(inputText.getBytes("UTF-8"));
            byte[] s = m.digest();
            return hex(s);
        } catch (NoSuchAlgorithmException e) {
            e.printStackTrace();
        } catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }
        return encryptText;
    }

    // 返回十六进制字符串
    private static String hex(byte[] arr) {
        StringBuffer sb = new StringBuffer();
        for (int i = 0; i < arr.length; ++i) {
            sb.append(Integer.toHexString((arr[i] & 0xFF) | 0x100).substring(1,
                    3));
        }
        return sb.toString();
    }

    /**
     * 32位加密
     *
     * @param str
     * @return
     */
    public static String getMD5Str(String str) {
        MessageDigest messageDigest = null;

        try {
            messageDigest = MessageDigest.getInstance("MD5");

            messageDigest.reset();

            messageDigest.update(str.getBytes("UTF-8"));
        } catch (NoSuchAlgorithmException e) {
            System.out.println("NoSuchAlgorithmException caught!");
            System.exit(-1);
        } catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        byte[] byteArray = messageDigest.digest();

        StringBuffer md5StrBuff = new StringBuffer();

        for (int i = 0; i < byteArray.length; i++) {
            if (Integer.toHexString(0xFF & byteArray[i]).length() == 1)
                md5StrBuff.append("0").append(
                        Integer.toHexString(0xFF & byteArray[i]));
            else
                md5StrBuff.append(Integer.toHexString(0xFF & byteArray[i]));
        }

        return md5StrBuff.toString().toLowerCase();
    }

    /**
     * 对文件全文生成MD5摘要
     *
     * @param file要加密的文件
     * @return MD5摘要码
     */
    static char hexdigits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    public static String getMD5(File file) {

        FileInputStream fis = null;
        try {
            MessageDigest md = MessageDigest.getInstance("MD5");
            fis = new FileInputStream(file);
            byte[] buffer = new byte[2048];
            int length = -1;
            long s = System.currentTimeMillis();
            while ((length = fis.read(buffer)) != -1) {
                md.update(buffer, 0, length);
            }

            //32位加密
            byte[] b = md.digest();
            return byteToHexString(b);

            // 16位加密
            // return buf.toString().substring(8, 24);

        } catch (Exception ex) {
            ex.printStackTrace();
            return null;
        } finally {
            try {
                fis.close();
            } catch (IOException ex) {
                ex.printStackTrace();
            }
        }
    }

    /**
     * 把byte[]数组转换成十六进制字符串表示形式
     *
     * @param tmp 要转换的byte[]
     * @return 十六进制字符串表示形式
     */

    private static String byteToHexString(byte[] tmp) {
        String s;
        // 用字节表示就是 16 个字节
        char str[] = new char[16 * 2]; // 每个字节用 16 进制表示的话，使用两个字符，
        // 所以表示成 16 进制需要 32 个字符
        int k = 0; // 表示转换结果中对应的字符位置
        for (int i = 0; i < 16; i++) { // 从第一个字节开始，对 MD5 的每一个字节
            // 转换成 16 进制字符的转换
            byte byte0 = tmp[i]; // 取第 i 个字节
            str[k++] = hexdigits[byte0 >>> 4 & 0xf]; // 取字节中高 4 位的数字转换,
            // >>> 为逻辑右移，将符号位一起右移
            str[k++] = hexdigits[byte0 & 0xf]; // 取字节中低 4 位的数字转换
        }
        s = new String(str); // 换后的结果转换为字符串
        return s;
    }

}
