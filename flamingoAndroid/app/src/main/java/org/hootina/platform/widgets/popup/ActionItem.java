package org.hootina.platform.widgets.popup;

import android.content.Context;

/**
 * @author yangyu
 *	���������������ڲ���������Ʊ����ͼ�꣩
 */
public class ActionItem {
	
	//�����ı�����
	public CharSequence mTitle;
	
	public ActionItem( CharSequence title){
		
		this.mTitle = title;
	}
	
	public ActionItem(Context context, int titleId){
		this.mTitle = context.getResources().getText(titleId);
	}
	
	public ActionItem(Context context, CharSequence title) {
		this.mTitle = title;
	}
}
