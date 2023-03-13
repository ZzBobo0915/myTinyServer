#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

// 定义http响应的一些状态
const char* ok_200_title = "OK"; 
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

// 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
// 表示网站根目录
const char* doc_root = "/home/wucz/TinyServer/root";

//将表中的用户名和密码放入map
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool* connPool)
{
    // 先从连接池中取出一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqkcon(&mysql, connPool);

    // user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql);

    // 从表中检索完整的结果集
    MYSQL_RES * result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数据
    MYSQL_FIELD * fields = mysql_fetch_fields(result);

    // 从结果集中取下一行，将对应的用户名和尼玛存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

/* epoll 相关 */
// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核时间表注册读事件 ET模式下选择开启EPOLLONESHOT, listenfd不需要开
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    // 对端发送普通数据 触发EPOLLIN
    // 检测EPOLLRDHUP就可以直到是对方关闭
    // EPOLLET 设置ET
#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif // connfdET

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif // connfdLT

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif // listenfdET

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif // listenfdLT

    /* 针对connfd，ET模式下开启EPOLLONESHOT，因为我们希望每个socket在任意一个时刻都只被一个线程处理*/
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重制为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif // connfdET

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif // connfdLT
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
/* 结束epoll相关 */


int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址,然后再调用私有成员函数init()
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
	m_sockfd = sockfd;
	m_address = addr;
	addfd(m_epollfd, sockfd, true);
	m_user_count++;
	init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


// 从状态机，分析每一行内容
// 返回为行的读取状态
http::conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_cjecked_idx < m_read_idx; ++m_check_idx)
    {
        temp = m_read_buf[m_check_idx];
        // 如果temp为\r，则有可能会读取到完整行
        if (temp == '\r')
        {
            // 如果下一个字符到了buf结尾，说明接受不完整，需要继续接收
            if ((m_check_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            // 下一个字符是换行，将\r\n改为\0\0
            else if (m_read_buf[m_check_idx + 1] == '\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            // 如果都不符合，则返回语法错误
            return LINE_BAD;
        }
        // 如果当前字符是\n 也是可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接受完整，再次接受时会出现这种情况
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 没有找到\r或\n，继续接收
    return LINE_OPEN;
}

// 循环读取用户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;

#ifdef connfdLT
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    if (bytes_read <= 0)
        return false;
    m_read_idx += bytes_read;
    return true;

#endif // connfdLT

#ifdef connfdET
    // 非阻塞ET模式下，需要一次读完全部数据
    while (true)
    {
        // 从套接字接收数据，存储在m_read_buf缓冲区
        bytes_read = (m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            //read操作没数据可读程序不会阻塞等待会返回一个EAGAIN EWOULDBLOCK与EAGAIN等价
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        // 修改m_read_idx读取的字节数，便于下一次读取缓冲区数据使用
        m_read_idx += bytes_read;
    }
    return true;
#endif // connfdET

}

// 解析http请求行，获得请求方法、目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_requset_line(char* text)
{
    // 在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔
    // 请求行中最先含有空格和\t任一字符的位置并返回
    m_url = strpbrk(text, " \t");

    // 没有则报文格式有误
    if (!m_url)
        return BAD_REQUEST;
    // 将该位置改为\0，用于将前面提出来
    *m_url++ = '\0';

    // 取出数据，并且通过对比，确定请求方法
    char* method = text;
    // strcasecap忽略大小比较字符串
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else return BAD_REQUEST;

    // m_url此时跳过了第一个空格或\t，但是不知道后面还有没有
    // 将m_url向后偏移。通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    // strspn检索str1中第一个不存在于str2中出现的数组下标
    m_url += strspn(m_url, " \t");

    // 使用与判断请求方式相同的逻辑，判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    // 仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    // 对请求资源的前7个字符进行判断
    // 这里主要是有些报文的请求资源中会带http:// 需要对这种情况单独处理
    if (strcasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // strchr查找单个字符第一次出现的位置
        m_url = strchr(m_url, '/');
    }

    // 同样，还有https://的情况
    if (strcasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        // strchr查找单个字符第一次出现的位置
        m_url = strchr(m_url, '/');
    }

    // 大部分都不带上面那两个，一般就是/后直接访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求处理完毕，将主状态机转移处理请求头部
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息或解析空行
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    // 判断是头部还是空行
    if (text[0] == '\0')
    {
        // 如果请求数据长度为0，说明是POST，为0是GET
        if (m_content_length != 0)
        {
            // POST需要跳转到消息体处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // 下面都是解析各个字段
    // 头部连接字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    // 头部内容长度字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    // 头部HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    // 不符合请求报文头部内容格式
    else
    {
        //printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    // 判断buf中是否读取了消息体
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';

        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 将主从状态进行封装
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    // 对报文的每一行循环处理
    /***
        由于GET请求会直接将用户名和密码暴露在URL中，为了避免这个情况，需要改用POS请求
        将用户名和密码添加在报文中作为消息体进行封装
        在GET请求报文中，每一行都是\r\n结尾，所以对报文进行解析时，仅用从状态机的状态((line_status = parse_line()) == LINE_OK)就可以
        但是POST请求中，消息体末尾没有任何字符，所以不能使用从状态机的状态，这里转而使用主状态机的状态作为循环入口条件；
        解析完消息体后，报文的完整解析就完成了，但此时主状态机的状态还是CHECK_STATE_CONTENT，也就是说，符合循环入口条件，还会再次进入循环，这并不是我们所希望的；
        为此，增加了该语句，并在完成消息体解析后，将line_status状态变更为LINE_OPEN,此时可跳出循环完成解析
    ***/
    // parse_line为状态机的具体实现
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)
    {
        text = get_line();
        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // m_checked_idc表示状态机在m_read_buf中读取的位置
        m_start_line = m_check_idx;

        LOG_INFO("%s", text);
        Log::get_instance()->flush();

        // 主状态机的三种状态转移逻辑
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            // 解析请求行
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            // 解析请求头
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            //完整解析GET请求后，跳转到报文响应函数
            else if (ret == GET_REQUEST)
                return do_request();
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            // 解析消息体
            ret = parse_content(text);
            // 完整解析POST请求后，跳转到报文响应函数
            if (ret == GET_REQUEST)
                return do_request();
            // 解析完消息体即完成报文解析，避免再次循环，更新line_status
            line_status = LINE_POEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_requset()
{
    // 初始m_read_file赋值为网站根目录
    strcpy(m_read_file, doc_root);
    int len = strlen(doc_root);

    // 找到m_url中的/的位置
    // 指向strrchr所指向字符串中搜索最后一次出现字符的位置
    const char* p = strrchr(m_url, '/');

    //处理cgi，实现登录和注册校验
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }
    // 如果请求额资源为/0 就跳转注册页面
    if (*(p + 1) == '0')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        // 将网站目录和register.html拼接，更新到m_real_file
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 如果请求资源为/1，跳转登录界面
    else if (*(p + 1) == '1')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 请求资源为/5，POST请求，跳转/picture.html，即图片请求页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 如果请求资源为/7，POST请求，跳转/fans.html，即关注页面
    else if (*(p + 1) == '7')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 以上都不符合，就直接将url与网站目录拼接
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体中
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    // 判断权限，是否可读，不可读返回FORBIDDEN_REQUEST
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 判断文件类型，如果是目录，则返回BAD_REQUEST
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符，通过mmap映射到内存中，不共享
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    // 请求文件存在且可以访问
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 服务器主线程调用write将响应报文发送给浏览器端
bool http_conn::write()
{
    int temp = 0;

    // 若要发送的数据长度为0，表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        // writev:以iov[0],iov[1]..的顺序从各个缓冲区聚集输出到fd
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        // 第一个iovec的头部信息已经发完，发第二个
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write.idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        // 继续发送第一个头部信息数据
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            // 若请求为长连接(即还要继续连接)
            if (m_linger)
            {
                init();
                return true;
            }
            else
                return true;
        }
    }
}

/* add_response被下面几个函数调用来更新m_write_idx指针和缓冲区m_write_buf */
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    // 可变参数列表
    va_list arg_list;

    // 将变量初始化为传入参数
    va_start(arg_list, format);

    // 将数据format从可变列表中写入写缓冲区区，返回写入数据长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 判断写入长度与缓冲区大小的关系
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        // 清空arg_list
        va_end(arg_list);
        return false;
    }
    // 更新位置
    m_write_idx += len;
    // 清空
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();

    return true;
}

/* 添加状态行 */
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
/* 添加消息报头 */
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
/* 添加Content-Length */
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
/* 添加Content-Type，这里是html */
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
/* 添加Connection，通知浏览器端是保持连接还是关闭 */
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
/* 添加空行 */
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
/* 添加文本content */
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

// 根据do_request的返回状态，服务器子线程调用process_write向m_write_buf中写入响应报文
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    // 200
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        // 有请求资源存在
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向文件大小
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向mmap文件返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            // 如果请求资源为0，返回空白html文件
            const char* ok_string="<html><body></body></html>";
57          add_headers(strlen(ok_string));
58          if(!add_content(ok_string))
59              return false;
        }
    }
    default:
        return false;
    }
    // 除了FILE_REQUESTS状态，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

