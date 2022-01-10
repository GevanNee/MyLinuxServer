#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "ngx_c_memory.h"
#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

void CSocket::ngx_read_request_handler(lpngx_connection_t pConn)
{
	bool isflood = false; //是否flood攻击

	ssize_t reco = recvproc(pConn, pConn->precvbuf, pConn->irecvlen);
	if (reco == 0)
	{
		return;
	}

	/*开始收包，维护收包状态*/
	if (pConn->curStat == _PKG_HD_INIT)
	{
		if (reco == m_iLenPkgHeader) /*这里说明刚好收到包头*/
		{
			ngx_log_core(NGX_LOG_DEBUG, 0, "ngx_read_request_handler()中，正好收到包头!");
			ngx_wait_request_handler_proc_p1(pConn, isflood); //调用专门处理包头的函数去处理
		}
		else
		{
			//收到的包头不完整--我们不能预料每个包的长度，也不能预料各种拆包/粘包情况，所以收到不完整包头【也算是缺包】是很可能的；
			ngx_log_core(NGX_LOG_DEBUG, 0, "ngx_read_request_handler()中，收到的包不完整");
			pConn->curStat = _PKG_HD_RECVING;
			pConn->irecvlen -= reco;
			pConn->precvbuf += reco;
		}
	}
	else if (pConn->curStat == _PKG_HD_RECVING)
	{
		if (reco == pConn->irecvlen)
		{
			ngx_wait_request_handler_proc_p1(pConn, isflood);
		}
		else
		{
			pConn->irecvlen -= reco;
			pConn->precvbuf += reco;
		}
	}
	else if(pConn->curStat == _PKG_BD_INIT)
	{
		if (reco == pConn->irecvlen)
		{
			/*收到完整的包体*/
			if (m_floodAkEnable == 1)
			{
				isflood = TestFlood(pConn);
			}

			ngx_wait_request_handler_proc_plast(pConn, isflood);
		}
		else
		{
			pConn->curStat = _PKG_BD_RECVING;
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
		}
	}
	else if (pConn->curStat == _PKG_BD_RECVING)
	{
		if (pConn->irecvlen == reco)
		{
			//包体收完整了
			if (m_floodAkEnable == 1)
			{
				//Flood攻击检测是否开启
				isflood = TestFlood(pConn);
			}
			ngx_wait_request_handler_proc_plast(pConn, isflood);
		}
		else
		{
			//包体没收完整，继续收
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
		}
	}

	if (isflood == true)
	{
		zdClosesocketProc(pConn);
	}
}

void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn, bool& isflood)
{
	CMemory* p_memory = CMemory::GetInstance();

	LPCOMM_PKG_HEADER p_PkgHeader;
	p_PkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; //正好收到包头的时候，包头信息肯定是在dataHeadinfo里

	unsigned short e_pkgLen; //包头+包体的长度
	e_pkgLen = ntohs(p_PkgHeader->pkgLenth);
	
	/*DEBUG*/
	int e_crc = ntohl(p_PkgHeader->crc32);
	unsigned short msgCode = ntohs(p_PkgHeader->msgCode);
	ngx_log_core(NGX_LOG_DEBUG, 0, "ngx_wait_request_handler_proc_p1()中，e_pkgLen的值是%d, crc是%d, msgCode是%d", e_pkgLen, e_crc, msgCode);
	/*DEBUG*/

	if (e_pkgLen > (_PKG_MAX_LENGTH - 1000))
	{
		/*收到的包头里居然说包的长度>29000，绝逼是恶意包*/
		pConn->curStat = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else if (e_pkgLen < m_iLenPkgHeader)
	{
		//状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数

		pConn->curStat = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else /*合法包头*/
	{
		//我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；
		char* pTmpBuffer = (char*)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false); //分配内存【长度是 消息头长度  + 包头长度 + 包体长度】，最后参数先给false，表示内存不需要memset;        
		pConn->precvMemPointer = pTmpBuffer;  //内存开始指针

		//a)先填写消息头内容
		LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
		ptmpMsgHeader->pConn = pConn;
		ptmpMsgHeader->iCurrsequence = pConn->iCurrSequence; //收到包时的连接池中连接序号记录到消息头里来，以备将来用；
	
		//再填写包头内容
		pTmpBuffer += m_iLenMsgHeader;
		memcpy(pTmpBuffer, p_PkgHeader, m_iLenPkgHeader);

		if (e_pkgLen == m_iLenPkgHeader)
		{
			//该报文只有包头无包体【我们允许一个包只有包头，没有包体】
			//这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
			if (m_floodAkEnable == 1)
			{
				//Flood攻击检测是否开启
				isflood = TestFlood(pConn);
			}
			ngx_wait_request_handler_proc_plast(pConn, isflood);
		}
		else
		{
			pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
			pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
			pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
		}
	}

}

/*调用这个函数，说明pConn已经接收到了一个完整的数据包，*/
void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn, bool& isflood)
{
	/*把这段内存放到消息队列中*/

	if (isflood == false)
	{
		ngx_log_core(NGX_LOG_DEBUG, 0, "");
		g_threadpool.inMsgRecvQueueAndSignan(pConn->precvMemPointer); //入消息队列并触发线程处理消息
	}
	else
	{
		//对于有攻击倾向的恶人，先把他的包丢掉
		CMemory* p_memory = CMemory::GetInstance();
		p_memory->FreeMemory(pConn->precvMemPointer); //直接释放掉内存，根本不往消息队列入
	}

	pConn->precvMemPointer = NULL;
	pConn->curStat = _PKG_HD_INIT;     //收包状态机的状态恢复为原始态，为收下一个包做准备                    
	pConn->precvbuf = pConn->dataHeadInfo;  //设置好收包的位置
	pConn->irecvlen = m_iLenPkgHeader;  //设置好要接收数据的大小
	return;
}

ssize_t CSocket::recvproc(lpngx_connection_t pConn, char* buf, ssize_t buflen)
{
	ssize_t n = recv(pConn->fd, buf, buflen, 0);

	if (n == 0)
	{
		zdClosesocketProc(pConn);
		return -1;
	}
	else if (n < 0)
	{
		/*按理来说LT模式下不该收到这个错误*/
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			ngx_log_core(NGX_LOG_WARN, errno , "CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK居然成立？？？");
			return -1;
		}

		/*这个错误说明阻塞函数被信号中断，可以不用处理(nginx里也没处理)*/
		if (errno == EINTR)
		{
			ngx_log_core(NGX_LOG_WARN, errno, "CSocekt::recvproc()中errno == EINTR, accept的阻塞被信号中断了"); //LT模式不应该出现这个问题
			return -1; //返回就行，不用处理
		}

		/*以上的错误可以认为发生时正常的，不会对服务器造成影响*/

		/*以下的错误说明有异常事件发生，我们需要关闭该连接，并且回收资源*/
		if (errno == ECONNRESET)
		{
			/*这个错误说明收到了RST，可能是客户端直接杀进程导致的，没有进行四次挥手*/
			ngx_log_core(NGX_LOG_INFO, errno, "CSocekt::recvproc()中errno == ECONNRESET");

			/*应该是一个常规错误，因为客户端杀进程是一个常规操作，服务器死不死跟我客户端有什么关系？？？？？*/

			//do something
			//完善中...
		}
		else
		{
			if (errno == EBADF)// #define EBADF   9 /* Bad file descriptor */
			{
				ngx_log_core(NGX_LOG_INFO, errno, "CSocekt::recvproc()中errno == ECONNRESET");

				/*书上说多线程可能会干掉socket导致这个错误发生？？不懂*/
			}
			else
			{
				ngx_log_core(NGX_LOG_ERROR, errno, "CSocekt::recvproc()中errno == ECONNRESET");
			}
		}
		zdClosesocketProc(pConn);
		return -1;
	}

	return n;
}

void CSocket::ngx_write_request_handler(lpngx_connection_t pConn)
{
	CMemory* p_memory = CMemory::GetInstance();

	ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);

	if (sendsize > 0 && sendsize != pConn->isendlen)
	{
		//没有全部发送完毕，数据只发出去了一部分，那么发送到了哪里，剩余多少，继续记录，方便下次sendproc()时使用
		pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;
		return;
	}

	if (sendsize == -1)
	{
		//这不太可能，可以发送数据时通知我发送数据，我发送时你却通知我发送缓冲区满？
		ngx_log_core(NGX_LOG_ERROR, errno, "CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志，别的先不干啥
		return;
	}

	if (sendsize > 0 && sendsize == pConn->isendlen) //成功发送完毕，做个通知是可以的；
	{
		//如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；
		if (ngx_epoll_oper_event(
			pConn->fd,          //socket句柄
			EPOLL_CTL_MOD,      //事件类型，这里是修改【因为我们准备减去写通知】
			EPOLLOUT,           //标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
			1,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
			pConn               //连接池中的连接
		) == -1)
		{
			ngx_log_core(NGX_LOG_ERROR, errno, "CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
		}
	}

	/*走到这里表明数据发送完毕，或者对端断开连接(n == 0)，需要释放写缓冲区的内存*/

	p_memory->FreeMemory(pConn->psendMemPointer); //
	pConn->psendMemPointer = nullptr;
	--pConn->iThrowSendCount; //这个值恢复了，触发下面这一行的信号量才有意义
	if (sem_post(&m_semEventSendQueue) == -1)
	{
		ngx_log_core(NGX_LOG_ERROR, 0, "CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");
	}

	return;
}

ssize_t CSocket::sendproc(lpngx_connection_t c, char* buf, ssize_t size)
{
	ssize_t n;
	while (1)
	{
		n = send(c->fd, buf, size, 0);

		if (n > 0)
		{
			return n;
		}

		if (n == 0)
		{
			return 0;
		}
		
		/* 以下就是 n < 0 */
		if (errno == EAGAIN)
		{
			/*内核缓冲区满了*/
			return -1;
		}

		if (errno == EINTR)
		{
			//信号导致的
			//nginx中也没处理
			ngx_log_stderr(errno, "CSocekt::sendproc()中send()失败.");  //打印个日志看看啥时候出这个错误
			continue;
		}
		else
		{
			//代表有其他未预见的错误
			return -2;
		}
	}
}

//消息处理线程主函数，专门处理各种接收到的TCP消息， 
//pMsgBuf：发送过来的消息缓冲区，消息本身是自解释的，通过包头可以计算整个包长
//消息本身格式【消息头+包头+包体】
void CSocket::threadRecvProcFunc(char* pMsgBuf)
{
	//这是一个虚函数，让子类去实现
	return;
}