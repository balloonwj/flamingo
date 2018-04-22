package org.hootina.platform.utils;

public class FastClickUtil {
	private static long lastClickTime = 0;

	public synchronized static boolean isFastClick(int delayTime) {
		long time = System.currentTimeMillis();
		if (time - lastClickTime < delayTime) {
			return true;
		}
		lastClickTime = time;
		return false;
	}
}
