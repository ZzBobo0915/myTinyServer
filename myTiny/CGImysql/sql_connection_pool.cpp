#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	this->CurConn = 0;
	this->FreeConn = 0;
}

connection_pool* connection_pool::GetInstance()
{
	// �ֲ���̬��������ģʽ
	static connection_pool connPool;
	return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn)
{
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DataBaseName;

	lock.lock();
	// ����MaxConn�����ݿ�����
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL* con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		connList.push_back(con);
		++FreeConn;
	}
	// �ź�����ʼ��Ϊ���������
	reserve = sem(FreeConn);
	this->MaxConn = MaxConn;
	lock.unlock();
}

// ������ʱ�������ݿ����ӳ��з���һ���������ӣ�����ʹ�úͿ���������
MYSQL* connection_pool::GetConnection()
{
	MYSQL* conn = NULL;
	if (connList.size() == 0)
		return NULL;
	// ȥ�����ӣ��ź���-1
	reserve.wait();
	lock.lock();
	conn = connList.front();
	connList.pop_front();
	--FreeConn;
	++CurConn;
	lock.unlock();
	return conn;
}
//�ͷŵ�ǰʹ�õ�����
bool connection_pool::ReleaseConnection(MYSQL* conn)
{
	if (conn == NULL)
		return false;
	lock.lock();
	connList.push_back(conn);
	++FreeConn;
	--CurConn;

	lock.unlock();
	reserve.post();
	return true;
}
//�������ݿ����ӳ�
void connection_pool::DestroyPool()
{
	lock.lock();
	if (connList.size() > 0)
	{
		// �������������ر����ݿ�����
		list<MYSQL*>::iterator it;
		for (it = connList.begin(); it != connList.end(); it++)
		{
			MYSQL* conn = *it;
			mysql_close(conn);
		}
		CurConn = 0;
		FreeConn = 0;
		connList.clear();
		lock.unlock();
	}
	lock.unlock();
}

int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

// �����ݿ����ӵĻ�ȡ���ͷ�ͨ��RAII���Ʒ�װ���� 
connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool) {
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
	poolRAII->ReleaseConnection(conRAII);
}