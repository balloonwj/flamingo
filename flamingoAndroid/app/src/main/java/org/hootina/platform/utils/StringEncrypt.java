package org.hootina.platform.utils;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class StringEncrypt {

	/**
	 * 对字符串加密,加密算法使用MD5,SHA-1,SHA-256,默认使用SHA-256
	 * 
	 * @param strSrc
	 *            要加密的字符串
	 * @param encName
	 *            加密类型
	 * @return
	 */
	public static String encrypt(String strSrc, String encName) {
		MessageDigest md = null;
		String strDes = null;

		byte[] bt = strSrc.getBytes();
		try {
			if (encName == null || encName.equals("")) {
				encName = "SHA-256";
			}
			md = MessageDigest.getInstance(encName);
			md.update(bt);
			strDes = bytes2Hex(md.digest());
		} catch (NoSuchAlgorithmException e) {
			return null;
		}
		return strDes;
	}

	private static String bytes2Hex(byte[] bts) {
		String des = "";
		String tmp = null;
		for (int i = 0; i < bts.length; i++) {
			tmp = (Integer.toHexString(bts[i] & 0xFF));
			if (tmp.length() == 1) {
				des += "0";
			}
			des += tmp;
		}
		return des;
	}
}
