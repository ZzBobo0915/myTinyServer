#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

/***
    CGImysql:校验程序，负责用户数据与数据库数据对比
    http:实现HTTP协议连接、销毁等
    lock:封装锁、信号量
    log:日志
    root:html、图片、视频
    test_presure:压力测试
    threadpool:线程池
    timer:定时器
***/

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

#define SYNLOG  // 同步写日志
//#define ASYNLOG  // 异步写日志

#define listenfdLT  // LT触发阻塞
//#define listenfdET  // ET触发非阻塞

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

//设置定时器相关参数
static int pipefd[2];             // 定时器管道
static sort_timer_lst timer_lst;  // 定时器容器列表
static int epollfd = 0;

/*
    信号处理函数中仅仅通过管道发送信号值
    不处理信号对应的逻辑
    缩短异步执行时间
    减少对主程序的影响
*/
void sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断后可再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    // 创建sigaction结构体
    struct sigaction sa;
    menset(&sa, '\0', sizeof(sa));

    // 信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restrat)
        sa.sa_flags |= SA_RESATRT;
    // 将信号添加到信号集
    sigfillset(&sa.sa_mask);
    // 执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 设置定时器处理函数
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data* user_data)
{
    // 删除非活动链接在socket上的注册事件
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    // 关闭文件描述符
    close(user_data->sockfd);
    // 减少连接数
    http_conn::m_user_count--;

    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[]) 
{
    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[1]));
        return 1;
    }

    int port = argv[1];

    // 创建数据库连接池



    // 创建线程池
    threadpool<http_conn>* pool = NULL;
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }

    // 主线程创建MAX_FD个http对象
    http_conn* users = new http_conn(MAX_FD);

    // 创建建通socket的文件描述符
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    // 创建listenfd的TCP/TP的socket地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    // SO_REUSEADDR 允许端口被重复使用
    int flag = 1;
    setsocket(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 绑定listenfd与地址
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(apollfd != -1);

    // 将listenfd注册在事件表中，当listen到新的客户连接时，listenfd变为就绪事件
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    // 创建管道，用于定时器
    // socketpair创建一对无名的、相互连接的套接字 
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);            // 设置写端非阻塞
    addfd(epollfd, pipefd[0], false);     // 注册，设置读端为非阻塞

    addsig(SIGALRM, sig_handler, false);  // 数秒后返回一个SIGALRM信号
    addsig(SIGTERM, sig_handler, false);  // 终止进程信号
    // 循环条件
    bool stop_server = false;

    // 创建定时器容器链表
    client_data* users_timer = new client_data[MAX_FD]; 

    // 超时标记
    bool timeout = false;
    // 每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);

    while (!stop_server)
    {
        /* 主线程调用epoll_wait等待一组文件描述符上的事件，并将当前所有就绪的epoll_event复制到events数组中 */
        /* int epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout); */
        /*              事件表(红黑树)  返回参数，从内核复制到events  本次返回的最大数目   最多阻塞事件ms，0为不阻塞,-1为阻塞  */
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        // EINTR出现中断错误，write过程中遇到终端就会返回-1并且设置errno为EINTR
        if (number < -0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        // 对所有就绪事件进行处理
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            // 处理新的到的客户链接
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_len);
                if (connfd < 0)
                {
                    LOG_ERROR("%s errno is:%d", "accept error", errno);
                    continue;
                }
                // 超过最大连接数
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                // 初始化connfd套接字地址
                users[connfd].init(connfd, client_address);

                // 初始化client_data的数据
                // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;;
                util_timer* timer = new util_timer;       // 创建定时器临时变量
                timer->user_data = &users_timer[connfd];  // 设置定时器连接资源
                timer->cb_func = cb_func;                 // 设置回调函数
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;       // 设置绝对超时时间
                users_timer[connfd].timer = timer;        // 创建该连接对应的定时器，初始化为前述临时变量
                timer_lst.add_timer(timer);               // 将定时器加入链表中

#endif // listenfdLT

#ifdef listenfdET
                // ET模式下需要循环一直接受数据直到接受完毕
                while (1)
                {
                    int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
                    if (connfd < 0)
                    {
                        LOG_ERROR("%s:errno is %d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_user_count >= MAXFD)
                    {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    // 初始化client_data数据
                    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer* timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif // listenfdET
            }
            /* 出现异常，处理异常事件，关闭客户端连接并删除定时器 */
            else if (event[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务端关闭连接，移除对应的定时器
                util_timer* timer = users_timer[sockfd].timer;
                tiemr->cb_func(&user_timer[sockfd]);
                if (timer)
                    timer_lst.del_timer(timer);
            }
            // 处理定时器信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                // 收到SIGALRM信号，timeout设置为true
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                // 创建定时器临时变量，从对应的定时器中取出
                util_timer* timer = users_timer[sockfd].timer;
                // 读取浏览器端发来的全部数据
                if (user[sockfd].read_once())
                {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    // 若检测到读时间，将该事件放入请求队列
                    pool->append(users + sockfd);

                    // 若有数据传输，则定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;

                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();

                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    // 服务端关闭连接，移除对应的定时器
                    timer->cb_func(&user_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if (event[i].events & EPOLLPUT)
            {
                util_timer* timer = users_timer[sockfd].timer;
                // 响应报文写入文件
                if (users[sockfd].write())
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    // 服务器端关闭连接，移除对应的定时器
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        // 处理定时器，由于定时器是非必须事件收到信号并不是立马处理，完成读写事件后再处理
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;

}