package org.hootina.platform.adapters;

import java.util.List;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.SparseArray;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.enums.OnlineType;

import com.nostra13.universalimageloader.core.DisplayImageOptions;
import com.nostra13.universalimageloader.core.ImageLoader;
import com.nostra13.universalimageloader.core.assist.ImageScaleType;

/**
 * @author Administrator 所有adapter类的父类
 *
 * @param <T>
 */
public abstract class BaseAdapter<T> extends android.widget.BaseAdapter {
	private List<T> list;
	// convertView
	private View view;
	private LayoutInflater layoutInflater;

	protected FlamingoApplication _flamingoApplication;

	protected Context context;

	protected ImageLoader imageLoader;
	protected DisplayImageOptions options;
	protected DisplayImageOptions.Builder builder;

	/**
	 * 构造方法
	 * 
	 * @param context
	 * @param list
	 */
	public BaseAdapter(Context context, List<T> list) {
		this.list = list;
		this.context = context;
		layoutInflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);

		imageLoader = ImageLoader.getInstance();
		builder = new DisplayImageOptions.Builder();
		options = builder.showImageOnLoading(R.drawable.touming) // 设置图片在下载期间显示的图片
		        .showImageForEmptyUri(R.drawable.touming)// 设置图片Uri为空或是错误的时候显示的图片
		        .showImageOnFail(R.drawable.touming) // 设置图片加载/解码过程中错误时候显示的图片
		        .cacheInMemory(true)// 设置下载的图片是否缓存在内存中
		        .cacheOnDisk(true)// 设置下载的图片是否缓存在SD卡中
		        .imageScaleType(ImageScaleType.IN_SAMPLE_POWER_OF_2)// 设置图片缩放方式
		        .bitmapConfig(Bitmap.Config.RGB_565)// 设置图片的解码类型
		        .resetViewBeforeLoading(false)// 设置图片在下载前是否重置，复位
		        // .displayer(new RoundedBitmapDisplayer(0))// 是否设置为圆角，弧度为多少
		        // .displayer(new FadeInBitmapDisplayer(0))// 是否图片加载好后渐入的动画时间
		        .build();// 构建完成

		_flamingoApplication = (FlamingoApplication) context.getApplicationContext();

	}

	public List<T> getList() {
		return list;
	}

	public void setList(List<T> list) {
		this.list = list;
	}

	/**
	 * 清空列表
	 */
	public void clear() {
		this.list.clear();
		notifyDataSetChanged();
	}

	/**
	 * 按列表添加
	 * 
	 * @param list
	 */
	public void addAll(List<T> list) {
		if (list != null) {
			this.list.addAll(list);
			notifyDataSetChanged();
		}
	}

	/**
	 * 添加单条
	 * 
	 * @param T
	 */
	public void add(T t) {
		if (t != null) {
			this.list.add(t);
			notifyDataSetChanged();
		}
	}

	@Override
	public int getCount() {
		return list == null ? 0 : list.size();
	}

	@Override
	public T getItem(int position) {
		return list.get(position);
	}

	@Override
	public long getItemId(int position) {
		return position;
	}

	@Override
	public View getView(int position, View convertView, ViewGroup parent) {

		if (null == convertView) {
			convertView = layoutInflater.inflate(getContentView(), null);
		}
		// 为全局变量赋值，供下面使用
		view = convertView;

		onInitView(convertView, position);
		return convertView;
	}

	public abstract int getContentView();

	public abstract void onInitView(View view, int position);

	/**
	 * 通过id获取控件
	 * 
	 * @param id
	 *            控件的id
	 * @return 返回View的子类
	 */
	@SuppressWarnings("unchecked")
	protected <E extends View> E get(View view, int id) {
		SparseArray<View> viewHolder = (SparseArray<View>) view.getTag();
		if (null == viewHolder) {
			viewHolder = new SparseArray<View>();
			view.setTag(viewHolder);
		}
		View childView = viewHolder.get(id);
		if (null == childView) {
			childView = view.findViewById(id);
			viewHolder.put(id, childView);
		}
		return (E) childView;
	}

	/**
	 * 显示网络图片, 可重新配置options
	 */
	protected void displayImage(int id, String imageUrl, float screenWidthScale, float heightWidthScale) {
		if (imageUrl == null || id < 0) {
			return;
		}

		ImageView iv = get(view, id);

		ViewGroup.LayoutParams params = iv.getLayoutParams();
		params.width = (int) (screenWidthScale * _flamingoApplication.getScreenWidth());
		params.height = (int) (params.width * heightWidthScale);
		iv.setLayoutParams(params);
		
		if(imageUrl != null)
		{
			Bitmap bitmap = BitmapFactory.decodeFile(imageUrl);
			iv.setImageBitmap(bitmap);
		}
		else
		{
			iv.setImageBitmap(null);
		}

		//imageLoader.displayImage(imageUrl, iv, options);
	}

	// --------------------------------------------------
	// -------------------控件基本设置-------------------------------
	// --------------------------------------------------

	/**
	 * 设置控件文字
	 * 
	 * @param id
	 *            控件ID
	 * @param str
	 *            要设置的文字
	 */
	protected void setText(int id, String str) {
		if (str == null || view == null || id <= 0) {
			return;
		}
		// 所有需要设置文字的控件都继承自textview
		TextView tv = get(view, id);
		tv.setText(str);
	}

	/**
	 * 设置控件文字
	 *
	 * @param id
	 *            控件ID
	 * @param str
	 *            要设置的文字
	 */
	protected void setText2(int id, int onlineType, String signature) {
		if (view == null || id <= 0) {
			return;
		}
		// 所有需要设置文字的控件都继承自textview
		String strOnlineType = "[离线]";
		if (onlineType == OnlineType.online_type_pc_online)
			strOnlineType = "[电脑在线]";
		else if (onlineType == OnlineType.online_type_android_cellular)
			strOnlineType = "[4G在线]";
		else if (onlineType == OnlineType.online_type_android_wifi)
			strOnlineType = "[WIFI在线]";
		else if (onlineType == OnlineType.online_type_ios)
			strOnlineType = "[Iphone在线]";
		else if (onlineType == OnlineType.online_type_mac)
			strOnlineType = "[MAC在线]";

		String str = strOnlineType + " " + signature;

		TextView tv = get(view, id);
		tv.setText(str);
	}

	/**
	 * 设置控件文字
	 *
	 * @param id
	 *            控件ID
	 * @param color
	 *            要设置的文字颜色
	 */
	protected void setTextColor(int id, int color) {
		if (view == null || id <= 0) {
			return;
		}
		TextView tv = get(view, id);
		tv.setTextColor(color);
	}

	/**
	 * 设置背景
	 * 
	 * @param id
	 *            控件ID
	 * @param res
	 *            背景图片或颜色
	 */
	protected void setBackground(int id, int res) {
		if (res <= 0 || view == null || id <= 0) {
			return;
		}

		get(view, id).setBackgroundResource(res);

	}

	/**
	 * 设置图片
	 * 
	 * @param id
	 *            控件ID
	 * @param res
	 *            图片
	 */
	protected void setImageResource(int id, int res) {
		if (res <= 0 || view == null || id <= 0) {
			return;
		}

		ImageView iv = get(view, id);
		iv.setImageResource(res);

	}

	/**
	 * 设置是否可见
	 * 
	 * @param id
	 *            控件ID
	 * @param state
	 *            View.VISIBLE View.GONE View.INVISIBLE
	 */
	protected void setVisibility(int id, int state) {
		if (view == null || id <= 0) {
			return;
		}

		get(view, id).setVisibility(state);

	}

	/**
	 * 设置点击监听器
	 * 
	 * @param id
	 *            控件ID
	 */
	protected void setOnClickListener(int id, int position, OnClickListener listener) {
		if (listener == null || view == null || id <= 0) {
			return;
		}

		View v = get(view, id);
		v.setTag(position);

		v.setOnClickListener(listener);

	}

	/**
	 * 设置padding
	 */
	protected void setPadding(int id, int left, int top, int right, int bottom) {
		get(view, id).setPadding(left, top, right, bottom);
	}

}
