#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>

#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

void CSocket::AddToTimerQueue(lpngx_connection_t pConn)
{
	CMemory* p_memory = CMemory::GetInstance();

	time_t futtime = time(nullptr);
	futtime += m_iWaitTime;

	CLock lock(&m_timequeueMutex);
	LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
	tmpMsgHeader->pConn = pConn;
	tmpMsgHeader->iCurrsequence = pConn->iCurrSequence;

	m_timerQueuemap.insert(std::make_pair(futtime, tmpMsgHeader));

	m_cur_size_++;  //计时队列尺寸+1

	m_timer_value_ = GetEarliestTime(); //计时队列头部时间值保存到m_timer_value_里
	return;
}

time_t  CSocket::GetEarliestTime()
{
	return m_timerQueuemap.begin()->first;
}

//从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥
LPSTRUC_MSG_HEADER CSocket::RemoveFirstTimer()
{
	if (m_cur_size_ <= 0)
	{
		return nullptr;
	}

	LPSTRUC_MSG_HEADER p_tmp = m_timerQueuemap.begin()->second;

	m_timerQueuemap.erase(m_timerQueuemap.begin());
	--m_cur_size_;

	return p_tmp;
}

/*拿出一个*/
LPSTRUC_MSG_HEADER CSocket::GetOverTimeTimer(time_t cur_time)
{
	if (m_cur_size_ == 0)
	{
		return nullptr;
	}

	CMemory* p_memory = CMemory::GetInstance();

	/*取得红黑树上头节点的值*/
	time_t earliesttime = GetEarliestTime();
	if (earliesttime <= cur_time)
	{
		
		LPSTRUC_MSG_HEADER ptmp = RemoveFirstTimer(); //删除头节点，并且拿到头节点的value值

		if (/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick != 1)  //能调用到本函数第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
		{
			/*这个分支是虽然超时但是不踢出时的分支，要重新把节点加回来*/

			//因为下次超时的时间我们也依然要判断，所以还要把这个节点加回来        
			time_t newinqueutime = cur_time + (m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(sizeof(STRUC_MSG_HEADER), false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;
			m_timerQueuemap.insert(std::make_pair(newinqueutime, tmpMsgHeader)); //自动排序 小->大			
			m_cur_size_++;
		}

		if (m_cur_size_ > 0)
		{
			m_timer_value_ = GetEarliestTime(); //保存一下新的头节点的值
		}
		return ptmp;
	}

	return nullptr;

}

/*将指定tcp连接从事件队列里抠出去*/
void CSocket::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;
	CMemory* p_memory = CMemory::GetInstance();

	CLock lock(&m_timequeueMutex);

	//因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
	pos = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for (; pos != posend; ++pos)
	{
		if (pos->second->pConn == pConn)
		{
			p_memory->FreeMemory(pos->second);  //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_; //减去一个元素，必然要把尺寸减少1个;								
			goto lblMTQM;
		}
	}
	if (m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}
	return;
}

/*清理时间队列中所有内容*/
void CSocket::clearAllFromTimerQueue()
{
	if (m_cur_size_ <= 0)
	{
		return;
	}

	CMemory* p_memory = CMemory::GetInstance();
	auto pos = m_timerQueuemap.begin();
	auto posEnd = m_timerQueuemap.end();
	for (; pos != posEnd; ++pos)
	{
		p_memory->FreeMemory(pos->second);
		--m_cur_size_;
	}
	m_timerQueuemap.clear();
}

/*时间队列监视和处理线程，处理到期不发心跳包的用户踢出的线程*/
void* CSocket::ServerTimerQueueMonitorThread(void* threadData)
{
	ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
	CSocket* pSocketObj = pThread->_pThis;

	time_t absolute_time, cur_time;
	while (g_stopEvent == 0)
	{
		if (pSocketObj->m_cur_size_ != 0)
		{
			//时间队列中最近发生事情的时间放到 absolute_time里；
			absolute_time = pSocketObj->m_timer_value_; //这个可是省了个互斥，十分划算
			cur_time = time(nullptr);

			int err;
			if (cur_time > absolute_time)
			{
				std::list<LPSTRUC_MSG_HEADER> m_lsIdleList; //保存要处理的内容
				LPSTRUC_MSG_HEADER result;

				err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
				if (err != 0)
				{
					ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!", err);//有问题，要及时报告
				}
				
				while ((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL) //一次性的把所有超时节点都拿过来
				{
					m_lsIdleList.push_back(result);
				}//end while

				err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex);
				if (err != 0)
				{
					ngx_log_stderr(err, "CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);//有问题，要及时报告                
				}

				LPSTRUC_MSG_HEADER tmpmsg;
				while (!m_lsIdleList.empty())
				{
					tmpmsg = m_lsIdleList.front();
					m_lsIdleList.pop_front();
					pSocketObj->procPingTimeOutChecking(tmpmsg, cur_time); //这里需要检查心跳超时问题
				} //end while(!m_lsIdleList.empty())

			}
		}
		usleep(500 * 1000); 
	}

	return nullptr;
}

void CSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{

}
