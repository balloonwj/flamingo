package org.hootina.platform.enums;

/**
 * @desc   好友操作类型
 * @author zhangyl
 * @date   20181124
 */
public class OperateFriendEnum {
    /*
    //type为1发出加好友申请 2 收到加好友请求(仅客户端使用) 3应答加好友 4删除好友请求 5应答删除好友
    //当type=3时，accept是必须字段，0对方拒绝，1对方接受
    cmd = 1005, seq = 0, {"userid": 9, "type": 1}
    cmd = 1005, seq = 0, {"userid": 9, "type": 2, "username": "xxx"}
    cmd = 1005, seq = 0, {"userid": 9, "type": 3, "username": "xxx", "accept": 1}

    //发送
    cmd = 1005, seq = 0, {"userid": 9, "type": 4}
    //应答
    cmd = 1005, seq = 0, {"userid": 9, "type": 5, "username": "xxx"}
    **/

    //加好友申请
    public static final int  OPERATE_FRIEND_SEND_APPLY     = 1;
    //收到加好友请求
    public static final int  OPERATE_FRIEND_RECV_APPLY     = 2;
    //应答加好友
    public static final int  OPERATE_FRIEND_RESPONSE_APPLY = 3;
    //删除好友请求
    public static final int  OPERATE_FRIEND_SEND_DELETE    = 4;
    //应答删除好友
    public static final int  OPERATE_FRIEND_RECV_DELETE    = 5;

    //当type=OPERATE_FRIEND_RESPONSE_APPLY时
    //拒绝
    public static final int  RESPONSE_APPLY_REFUSE         = 0;
    //同意
    public static final int  RESPONSE_APPLY_AGREE          = 1;
}
