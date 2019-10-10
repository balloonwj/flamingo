#pragma once

enum EMysqlError
{
    EME_ERROR = -1,
    EME_OK,
    EME_NOT_EXIST,
    EME_EXIST,
};

class CDatabaseMysql;

class IMysqlTask
{
public:
    IMysqlTask(void) {};
    virtual ~IMysqlTask(void) {};

public:
	virtual void execute(CDatabaseMysql* poConn) = 0;
    virtual void reply() = 0;
    virtual void release() { delete this; };
};


