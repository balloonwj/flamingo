package org.hootina.platform.utils;

public class ByteUtil {
	public static byte[] byteMerger(byte[] byte_1, byte[] byte_2) {
		if (byte_1 == null) {
			if (byte_2 == null) {
				return null;
			} else {
				return byte_2;
			}
		}

		if (byte_2 == null) {

			return byte_1;
		}
		byte[] byte_3 = new byte[byte_1.length + byte_2.length];
		System.arraycopy(byte_1, 0, byte_3, 0, byte_1.length);
		System.arraycopy(byte_2, 0, byte_3, byte_1.length, byte_2.length);
		return byte_3;
	}
}
