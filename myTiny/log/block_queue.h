#ifndef _BLOCK_QUEUE_H_
#define _BLOCK_QUEUE_H_

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

// 当队列为空时，从队列中获取元素的线程将会被挂起；当队列是满时，往队列里添加元素的线程将会挂起。
template<class T>
class block_queue
{
public:
	// 初始化私有成员
	block_queue(int max_size = 1000)
	{
		if (max_size <= 0)
			exit(-1);
		m_max_size = max_size;
		m_array = new T[max_size];
		m_size = 0;
		m_front = -1;
		m_back = -1;
	}

	void clear()
	{
		m_mutex.lock();
		m_size = 0;
		m_front = -1;
		m_back = -1;
		m_mutex.unlock();
	}

	~block_queue()
	{
		m_mutex.lock();
		if (m_array != NULL)
			delete[] m_array;
		m_mutex.unlock();
	}

	bool full()
	{
		m_mutex.lock();
		if (m_size >= m_max_size)
		{
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}

	bool empty()
	{
		m_mutex.lock();
		if (m_size == 0)
		{
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}

	bool front(T& value)
	{
		m_mutex.lock();
		if (m_size == 0)
		{
			m_mutex.unlock();
			return false;
		}
		value = m_array[m_front];
		m_mutex.unlock();
		return true;
	}

	bool back(T& value)
	{
		m_mutex.lock();
		if (m_size == 0)
		{
			m_mutex.unlock();
			return false;
		}
		value = m_array[m_back];
		m_mutex.unlock();
		return true;
	}

	int size()
	{
		int tmp = 0;

		m_mutex.lock();
		tmp = m_size;

		m_mutex.unlock();
		return tmp;
	}

	int max_size()
	{
		int tmp = 0;

		m_mutex.lock();
		tmp = m_max_size;

		m_mutex.unlock();
		return tmp;
	}
	//往队列添加元素，需要将所有使用队列的线程先唤醒
	//当有元素push进队列,相当于生产者生产了一个元素
	//若当前没有线程等待条件变量,则唤醒无意义
	bool push(const T& item)
	{
		m_mutex.lock();
		if (m_size >= m_max_size)
		{
			m_cond.boardcast();
			m_mutex.unlock();
			return false;
		}
		m_back = (m_back + 1) % m_max_size;
		m_array[m_back] = item;

		m_size++;
		m_cond.boardcast();
		m_mutex.unlock();
		return true;
	}
	//pop时,如果当前队列没有元素,将会等待条件变量
	bool pop(T& item)
	{
		m_mutex.lock();
		// 多个消费者应该用while
		while (m_size <= 0)
		{
			if (!m_cond.wait(m_mutex.get()))
			{
				m_mutex.unlock();
				return false;
			}
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[front];
		m_size--;
		return true;
	}
	// 超时处理的pop
	bool pop(T& item, int ms_timeout)
	{
		struct tiemspec t = { 0, 0 };
		struct timeval now = { 0, 0 };
		gettimeofday(&now, NULL);
		m_mutex.lock();
		if (m_size <= 0)
		{
			t.tv_sec = now.tv_sec + ms_timeout / 1000;
			t.tv_nsec = (ms_timeout % 1000) * 1000;
			if (!m_cond.wait(m_mutex.get(), t))
			{
				m_mutex.unlock();
				return false;
			}
		}
		if (m_size <= 0)
		{
			m_mutex.unlock();
			return false;
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}

private:
	locker m_mutex;
	cond m_cond;

	T* m_array;
	int m_size;
	int m_max_size;
	int m_fron;
	int m_back;
};

#endif // _BLOCK_QUEUE_H_