#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
	static Log* get_instance()
	{
		static Log instance;
		return &instance;
	}

	// �첽д��־���з���������˽�з���async_write_log()
	static void* flush_log_thread(void* args)
	{
		Log::get()->async_write_log();
	}
	// ��ѡ��Ĳ�������־�ļ�����־��������С����������Լ����־����
	bool init(const char* file_name, int log_buf_size = 8192, int spilt_lines = 5000000, int max_queue_size = 0);
	// ��������ݰ��ձ�׼��ʽ����
	void write_log(int level, const char* format, ...);
	// ǿ��ˢ�»�����
	void flush(void);
private:
	char dir_name[128];                // ·����
	char log_name[128];                // log�ļ���
	int m_split_lines;                 // ��־�������
	int m_log_buf_size;                // ��־��������С
	long long m_count;                 // ��־������¼
	int m_today;					   // ������ļ�����¼��ǰ�¼�����һ��
	FILE* m_fp;                        // ��log���ļ�ָ��
	char* buf;                         // ���������
	block_queue<string>* m_log_queue;  // ��������
	bool m_is_async;                   // �Ƿ�ͬ�����
	locker m_mutex;                   
};

// �����ĸ��궨���������ļ�ʹ�ã����ڲ�ͬ���͵���־���
#define LOG_DEBUG(format, ...) LOG::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif // _LOG_H_