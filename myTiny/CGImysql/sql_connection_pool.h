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
	MYSQL* GetConnection();               // ��ȡ���ݿ�����
	bool ReleaseConnection(MYSQL* conn);  // �ͷ�����
	int GetFreeConn();                    // ��ȡ����
	void DestroyPool();                   // ������������

	// �ֲ���̬��������ģʽ
	static connection_poll* GetInstance();

	void init(string url, int User, int PassWord, string DataBaseName, int Port, unsigned int MaxConn);

	connection_pool();
	~connection_pool();

private:
	locker lock;
	list<MYSQL*> connList; // ���ӳ�
	sem reserve;

	unsigned int MaxConn;   // ���������
	unsigned int CurConn;   // ��ǰ��ʹ��������
	unsigned int FreeConn;  // ��ǰ����������

	string url;             // ������ַ
	string Port;            // ���ݿ�˿ں�
	string User;            // ��¼���ݿ���û���
	string PassWord;        // ��¼���ݿ������
	string DataBaseName;    // ʹ�õ����ݿ���
};

class connectionRAII
{
public:
	// ˫ָ��� MYSQL* conn�޸�
	connectionRAII(MYSQL** con, connection_pool* connPool);
	~connectionRAII();
private:
	MYSQL* conRAII;
	connection_pool* poolRAII;
};

#endif // _CONNECTION_POOL_H_