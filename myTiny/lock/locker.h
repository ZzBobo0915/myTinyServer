#ifndef _LOCKER_H_
#define _LOCKER_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量
class sem
{
public:
	sem()
	{
		// 初始化信号量，成功返回0，失败返回-1
		if (sem_init(&m_sem, 0, 0) != 0)
		{
			throw std::exception();
		}
	}
	sem(int nem)
	{
		if (sem_init(&m_sem, 0, num) != 0)
		{
			throw std::exception();
		}
	}
	~sem()
	{
		// 销毁信号量，成功返回0，失败返回-1
		sem_destroy(&m_sem);
	}
	bool wait()
	{
		// sem_wait类比lock 信号量大于0自减 等于0造成线程阻塞
		return sem_wait(&m_sem) == 0;
	}
	bool post()
	{
		// sem_post类比unlock 信号量自加，唤醒阻塞在信号量上的线程
		return sem_post(&m_sem) == 0;
	}

private:
	sem_t m_sem;
};

// 互斥锁
class locker
{
public:
	locker()
	{
		// 初始化互斥锁 成功返回0，失败返回-1
		if (pthread_mutex_init(&m_mutex, NULL) != 0)
		{
			throw std::exception();
		}
	}
	~locker()
	{
		// 销毁锁
		return pthread_mutex_destroy(&m_mutex);
	}
	bool lock()
	{
		// 上锁
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	bool unlock()
	{
		// 解锁
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	pthread_mutex_t* get()
	{
		return &m_mutex;
	}
private:
	pthread_mutex_t m_mutex;
};

// 条件变量
class cond
{
public:
	cond()
	{
		if (pthread_cond_init(&m_cond, NULL) != 0)
		{
			throw std::exception;
		}
	}
	~cond()
	{
		pthread_cond_destroy(&m_cond);
	}
	// 条件变量需要配合锁来使用
	bool wait(pthread_mutex_t *m_mutex)
	{
		int ret = 0;
		ret = pthread_cond_wait(&m_cond, m_mutex);
		return ret == 0;
	}
	/* 
		timespec:时间结构体，与timeval类似
		二者第一个参数为tv_sec，都为秒
		timespec第二个参数为tv_nsec，为纳秒数，timeval第二个参数为tv_usec，为毫秒数
	 */
	bool timewait(pthread_mutex_t* m_mutex, struct timespec t)
	{
		int ret = 0;
		ret = pothread_cond_wait(&m_cond, m_mutex, &t);
		return ret == 0;
	}
	// 发送信号，解除阻塞在条件变量上的某个线程
	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}
	// 发送信号，解除阻塞在条件变量上的所有线程
	bool boardcast()
	{
		return pthread_cond_boardcast(&m_cond) == 0;
	}

private:
	pthread_cond_t m_cond;
};

#endif // !_LOCKER_H_
