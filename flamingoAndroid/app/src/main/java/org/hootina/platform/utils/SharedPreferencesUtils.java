package org.hootina.platform.utils;

import java.util.Map;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;

/**
 * SharedPreferences工具类：用于保存数据
 * 
 */
public class SharedPreferencesUtils {

	/**
	 * 保存在手机里的文件名
	 */
	public static final String FILE_NAME = "org.org.hootina";

	/**
	 * 保存数据
	 * 
	 * @param context
	 *            上下文
	 * @param key
	 *            key
	 * @param obj
	 *            value
	 */
	public static void put(Context context, String key, Object obj) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);
		Editor editor = sp.edit();

		if (obj instanceof Boolean) {
			editor.putBoolean(key, (Boolean) obj);
		} else if (obj instanceof Float) {
			editor.putFloat(key, (Float) obj);
		} else if (obj instanceof Integer) {
			editor.putInt(key, (Integer) obj);
		} else if (obj instanceof Long) {
			editor.putLong(key, (Long) obj);
		} else {
			editor.putString(key, (String) obj);
		}
		editor.commit();
	}

	/**
	 * 获取指定的数据
	 * 
	 * @param context
	 * @param key
	 * @param defaultObj
	 * @return
	 */
	public static Object get(Context context, String key, Object defaultObj) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);

		if (defaultObj instanceof Boolean) {
			return sp.getBoolean(key, (Boolean) defaultObj);
		} else if (defaultObj instanceof Float) {
			return sp.getFloat(key, (Float) defaultObj);
		} else if (defaultObj instanceof Integer) {
			return sp.getInt(key, (Integer) defaultObj);
		} else if (defaultObj instanceof Long) {
			return sp.getLong(key, (Long) defaultObj);
		} else if (defaultObj instanceof String) {
			return sp.getString(key, (String) defaultObj);
		}
		return null;
	}

	/**
	 * 删除指定的数据
	 * 
	 * @param key
	 */
	public static void remove(Context context, String key) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);
		Editor editor = sp.edit();
		editor.remove(key);
		editor.commit();
	}

	/**
	 * 返回所有的键值对
	 * 
	 * @return
	 */
	public static Map<String, ?> getAll(Context context) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);
		Map<String, ?> map = sp.getAll();
		return map;
	}

	/**
	 * 清除所有数据
	 * 
	 * @param context
	 */
	public static void clear(Context context) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);
		Editor editor = sp.edit();
		editor.clear();
		editor.commit();
	}

	/**
	 * 检查是否存在此key对应的数据
	 * 
	 * @param context
	 * @param key
	 * @return
	 */
	public static boolean contains(Context context, String key) {
		SharedPreferences sp = context.getSharedPreferences(FILE_NAME,
				context.MODE_PRIVATE);
		return sp.contains(key);
	}

}