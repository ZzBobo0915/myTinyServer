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

	// 异步写日志公有方法，调用私有方法async_write_log()
	static void* flush_log_thread(void* args)
	{
		Log::get()->async_write_log();
	}
	// 可选择的参数有日志文件，日志缓冲区大小，最大行数以及最长日志条列
	bool init(const char* file_name, int log_buf_size = 8192, int spilt_lines = 5000000, int max_queue_size = 0);
	// 将输出内容按照标准格式整理
	void write_log(int level, const char* format, ...);
	// 强制刷新缓冲区
	void flush(void);
private:
	char dir_name[128];                // 路径名
	char log_name[128];                // log文件名
	int m_split_lines;                 // 日志最大行数
	int m_log_buf_size;                // 日志缓冲区大小
	long long m_count;                 // 日志行数记录
	int m_today;					   // 按天份文件，记录当前事件是哪一天
	FILE* m_fp;                        // 打开log的文件指针
	char* buf;                         // 输出的内容
	block_queue<string>* m_log_queue;  // 阻塞队列
	bool m_is_async;                   // 是否同步标记
	locker m_mutex;                   
};

// 下面四个宏定义在其他文件使用，用于不同类型的日志输出
#define LOG_DEBUG(format, ...) LOG::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif // _LOG_H_