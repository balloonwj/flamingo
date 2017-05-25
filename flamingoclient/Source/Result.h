#pragma once

//包含各种状态值
enum LOGIN_RESULT
{
	LOGIN_SUCCESSFUL = 1,	//登录成功
	LOGIN_CANNOT_CONNECT,	//连接不上服务器
	LOGIN_INVALID_ACCOUNT,	//无效的用户名或密码
	LOGIN_FAILED			//登录失败
};

enum REGISTER_RESULT
{
	REGISTER_SUCCESSFUL = 1,	//注册成功
	REGISTER_EXIST,				//已经被注册过
	REGISTER_FAILED				//注册失败
};
