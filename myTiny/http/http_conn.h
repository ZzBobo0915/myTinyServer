#ifndef _HTTPCONN_H_
#define _HTTPCONN_H_

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

class http_conn
{
public:
	static const int FILENAME_LEN = 200;    // 设置读取文件的名称m_real_file
	static const int READ_BUFFER_SIZE = 2048;   // 设置读缓冲区的大小
	static const int WRITE_BUFFER_SIZE = 1024;  // 设置写缓冲区的大小
	// 报文的请求状态，目前只有GET和POST
	enum METHOD  
	{
		GET = 0,
		POST
	};
	// 主状态机的状态
	enum CHECK_STATE
	{
		CHECK_STATE_REQUESTLINE = 0,  // 解析请求行
		CHECK_STATE_HEADER,           // 解析请求头
		CHECK_STATE_CONTENT           // 解析消息体，仅用于解析POST请求
	};
	// 报文解析的结果
	enum HTTP_CODE
	{
		NO_REQUEST,         // 请求不完整，跳转主线程继续监测读事件
		GET_REQUEST,        // 获得了完整的请求，接着调用do_request完成资源映射
		BAD_REQUEST,        // HTTP请求报文有误，跳转process_write完成响应报文
		NO_RESOURCE,        // 请求资源不存在，跳转process_write完成响应报文
		FORBIDDEN_REQUEST,  // 请求资源禁止访问，跳转process_write完成响应报文
		FILE_REQUEST,       // 资源正常访问，跳转process_write完成响应报文
		INTERNAL_ERROR,     // 服务器内部错误，一般不会触发
		CLOSED_CONNECTION
	};
	// 从状态机的状态
	enum LINE_STATUS
	{
		LINE_OK = 0,  // 完全读取一行
		LINE_BAD,     // 报文语法错误
		LINE_OPEN     // 接受不完整
	};

public:
	http_conn() {}
	~http_conn() {}

public:
	// 初始化套接字地址
	void init(int sockfd, const sockaddr_in& addr);
	// 关闭http连接
	void close_conn(bool real_close = true);
	// 各个子线程通过process对任务进行处理
	void process();
	// 读浏览器端发来的全部内容
	bool read_once();
	// 响应报文写入数据
	bool write();

	sockaddr_in* get_address()
	{
		return &m_address;
	}
	//同步线程初始化数据库读取表
	void initmysql_result();
	// CGI使用线程池初始化数据库表
	void initresultFile(connection_pool* connPool);

private:
	void init();

	// 从m_read_buf，并处理请求报文
	HTTP_CODE process_read();
	// 主状态机解析请求报文的请求行数据
	HTTP_CODE parse_requset_line(char* text);
	// 主状态机解析请求报文的请求头数据
	HTTP_CODE parse_headers(char* text);
	// 主状态机解析请求报文的请求主体数据
	HTTP_CODE parse_content(char* text);

	// 向m_write_buf中写入响应报文
	bool process_write();
	// 生成响应报文
	HTTP_CODE do_requset();

	// m_start_line是已经解析的字符
	// getline()用于将指针向后偏移，指向未处理的字符
	char* get_line() { return m_read_buf + m_start_line; }

	// 从状态机 读取一行，分析是请求报文的哪个部分
	LINE_STATUS parse_line();

	// 释放mmap映射的内存空间
	void unmap();

	// 根据响应报文格式，生成对应部分，以下函数均由de_requset调用
	bool add_response(cont char* format, ...);
	bool add_content(const char* format, ...);
	bool add_status_line(int status, const char*title);
	bool add_headers(int content_length);
	bool add_content_type();
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	static int m_epollfd;
	static int m_user_count;
	MYSQL* mysql;

private:
	int m_sockfd;
	sockaddr_in m_address;

	// 存储读取的请求报文数据
	char m_read_buf[READ_BUFFER_SIZE];
	// 缓冲区中m_read_buf中数据最后一个字节的下一个位置
	int m_read_idx;
	// m_read_buf读取的位置
	int m_check_idx;
	// m_read_buf中已经解析的字符个数
	int m_start_line;

	// 存储发出的响应报文数据
	char m_write_buf[WRITE_BUFFER_SIZE];
	// 指示buffer中的长度
	int m_write_idx;

	// 主状态机的状态
	CHECK_STATE m_check_state;
	// 请求方法
	METHOD m_method;

	/* 以下解析请求报文对应的字段变量 */
	char m_real_file[FILENAME_LEN];  
	char* m_url;                     // url
 	char* m_version;                 // http版本
	char* m_host;				     // host域名
	char m_content_length;			 // content长度
	bool m_linger;                   // 是否保持连接

	char* m_file_address;			 // 读取服务器上的文件地址
	struct stat m_file_stat;         // 复制文件状态用
	struct iovec m_iv[2];            // io向量机制iovec
	int m_iv_count;
	int cgi;                         // 是否启用POST
	char* m_string;                  // 存储请求头数据
	int bytes_to_send;               // 剩余发送字节数
	int bytes_have_send;            // 已发送字节数

};


#endif // _HEEPCONN_H_