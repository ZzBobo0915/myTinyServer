#ifndef _LOCKER_H_
#define _LOCKER_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// �ź���
class sem
{
public:
	sem()
	{
		// ��ʼ���ź������ɹ�����0��ʧ�ܷ���-1
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
		// �����ź������ɹ�����0��ʧ�ܷ���-1
		sem_destroy(&m_sem);
	}
	bool wait()
	{
		// sem_wait���lock �ź�������0�Լ� ����0����߳�����
		return sem_wait(&m_sem) == 0;
	}
	bool post()
	{
		// sem_post���unlock �ź����Լӣ������������ź����ϵ��߳�
		return sem_post(&m_sem) == 0;
	}

private:
	sem_t m_sem;
};

// ������
class locker
{
public:
	locker()
	{
		// ��ʼ�������� �ɹ�����0��ʧ�ܷ���-1
		if (pthread_mutex_init(&m_mutex, NULL) != 0)
		{
			throw std::exception();
		}
	}
	~locker()
	{
		// ������
		return pthread_mutex_destroy(&m_mutex);
	}
	bool lock()
	{
		// ����
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	bool unlock()
	{
		// ����
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	pthread_mutex_t* get()
	{
		return &m_mutex;
	}
private:
	pthread_mutex_t m_mutex;
};

// ��������
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
	// ����������Ҫ�������ʹ��
	bool wait(pthread_mutex_t *m_mutex)
	{
		int ret = 0;
		ret = pthread_cond_wait(&m_cond, m_mutex);
		return ret == 0;
	}
	/* 
		timespec:ʱ��ṹ�壬��timeval����
		���ߵ�һ������Ϊtv_sec����Ϊ��
		timespec�ڶ�������Ϊtv_nsec��Ϊ��������timeval�ڶ�������Ϊtv_usec��Ϊ������
	 */
	bool timewait(pthread_mutex_t* m_mutex, struct timespec t)
	{
		int ret = 0;
		ret = pothread_cond_wait(&m_cond, m_mutex, &t);
		return ret == 0;
	}
	// �����źţ�������������������ϵ�ĳ���߳�
	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}
	// �����źţ�������������������ϵ������߳�
	bool boardcast()
	{
		return pthread_cond_boardcast(&m_cond) == 0;
	}

private:
	pthread_cond_t m_cond;
};

#endif // !_LOCKER_H_
