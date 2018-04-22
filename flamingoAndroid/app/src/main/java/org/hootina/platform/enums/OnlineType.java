package org.hootina.platform.enums;

/**
 *@desc   用户客户端版本在线类型
 *@author zhangyl
 *@date   20180403
 */

public class OnlineType {
    //为了和其他版本代码保持一致，风格就这样吧
    public final static int online_type_offline         = 0;   //离线
    public final static int online_type_pc_online       = 1;   //电脑在线
    public final static int online_type_pc_invisible    = 2;   //电脑隐身
    public final static int online_type_android_cellular= 3;   //android 3G/4G/5G在线
    public final static int online_type_android_wifi    = 4;   //android wifi在线
    public final static int online_type_ios             = 5;   //ios 在线
    public final static int online_type_mac             = 6;   //MAC在线
}
