#ifndef _CONNECTION_POOL_H_
#define _CONNECTION_POOL_H_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"

using namespace std;

class connection_pool
{
public:
	MYSQL* GetConnection();               // 获取数据库连接
	bool ReleaseConnection(MYSQL* conn);  // 释放连接
	int GetFreeConn();                    // 获取连接
	void DestroyPool();                   // 销毁所有连接

	// 局部静态变量单例模式
	static connection_poll* GetInstance();

	void init(string url, int User, int PassWord, string DataBaseName, int Port, unsigned int MaxConn);

	connection_pool();
	~connection_pool();

private:
	locker lock;
	list<MYSQL*> connList; // 连接池
	sem reserve;

	unsigned int MaxConn;   // 最大连接数
	unsigned int CurConn;   // 当前已使用连接数
	unsigned int FreeConn;  // 当前空闲连接数

	string url;             // 主机地址
	string Port;            // 数据库端口号
	string User;            // 登录数据库的用户名
	string PassWord;        // 登录数据库的密码
	string DataBaseName;    // 使用的数据库名
};

class connectionRAII
{
public:
	// 双指针对 MYSQL* conn修改
	connectionRAII(MYSQL** con, connection_pool* connPool);
	~connectionRAII();
private:
	MYSQL* conRAII;
	connection_pool* poolRAII;
};

#endif // _CONNECTION_POOL_H_