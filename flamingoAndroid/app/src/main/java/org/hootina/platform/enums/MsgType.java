package org.hootina.platform.enums;

/**
 * @desc   通信协议号定义
 * @author zhangyl
 * @date   2017.08.18
 */

//为了和服务器保持一致，这里所有常量都使用下划线连接的小写形式
public class MsgType {
    public static final int msg_type_unknown          = 0;
    public static final int msg_networker_disconnect  = 100;
    public static final int msg_type_heartbeart       = 1000;
    public static final int msg_type_register         = 1001;
    public static final int msg_type_login            = 1002;
    public static final int msg_type_getfriendlist    = 1003;
    public static final int msg_type_finduser         = 1004;
    public static final int msg_type_operatefriend    = 1005;
    public static final int msg_type_userstatuschange = 1006;
    public static final int msg_type_updateuserinfo   = 1007;
    public static final int msg_type_modifypassword   = 1008;
    public static final int msg_type_creategroup      = 1009;
    public static final int msg_type_getgroupmembers  = 1010;
    public static final int msg_type_chat             = 1100;        //单聊消息
    public static final int msg_type_multichat        = 1101;        //群发消息
    public static final int msg_type_kickuser         = 1102;        //被踢下线

    //错误码
    public static final int ERROR_CODE_SUCCESS               = 0;
    public static final int ERROR_CODE_UNKNOWNFAILED         = 1;
    public static final int ERROR_CODE_REGISTERFAILED        = 100;
    public static final int ERROR_CODE_REGISTERED            = 101;
    public static final int ERROR_CODE_UNREGISTER            = 102;
    public static final int ERROR_CODE_INCORRECTPASSWORD     = 103;
    public static final int ERROR_CODE_UPDATEUSERINFOFAILED  = 104;
    public static final int ERROR_CODE_MODIFYPASSWORDFAILED  = 105;
    public static final int ERROR_CODE_CREATEGROUPFAILED     = 106;
}
