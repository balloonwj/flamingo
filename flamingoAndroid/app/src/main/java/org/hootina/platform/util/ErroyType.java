package org.hootina.platform.util;
/*
 *@ 错误类型
 */
public interface ErroyType {
	public static final int error_ok = 0;//没有错误
	public static final int error_username = 1;//账号不存在
	public static final int	error_password = 2;//密码错误
	public static final int	error_netcard = 3;////网卡绑定错误,请联系管理员3
	public static final int	error_expiration = 4;//帐号已过期,请联系管理员4
	public static final int	error_disabled = 5;//帐号被禁用,请联系管理员5
	public static final int	error_database = 6;//数据库操作失败6
	public static final int	error_access = 7;//权限不够,请联系管理员7
	public static final int	error_fileread = 8;//文件读取失败,请稍候重试8
	public static final int	error_system = 9;//系统异常,请联系管理员9
	public static final int	error_nodata = 10;//没有数据需要传输10
	public static final int	error_remotesrv = 11;//远程服务未启动,请联系管理员11
	public static final int	error_version = 12;//版本错误,系统不支持12
	public static final int	error_netword = 13;//网络通讯失败13
	public static final int	error_filewrite = 14;///文件写入失败14
	public static final int	error_vdalrun = 15;//代码编译/执行失败15
	public static final int	error_registered = 16;//已注册16
    public static final int	error_invalidusername = 17;// 无效用户名17
    public static final int	error_exists = 18;// 已存在18
    public static final int	error_applied = 19;//
	public static final int	error_agreed = 20;//
	public static final int	error_refuse = 21;//
	public static final int	error_fail = 22;//
	public static final int	error_checksum = 23;//
	public static final int	error_max = 24;//
}
