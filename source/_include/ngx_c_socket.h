#pragma once
#include <stdint.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <semaphore.h>
#include <atomic>
#include <list>
#include <vector>
#include <map>

#include "ngx_comm.h"

#define NGX_LISTEN_BACKLOG 511 //listen的第二个参数大小，代表listen就绪队列的大小
#define NGX_MAX_EVENTS	   512 //epoll_wait一次性最多接受这么多事件

typedef struct ngx_connection_s      ngx_connection_t, * lpngx_connection_t;
typedef struct ngx_listening_s       ngx_listening_t, * lpngx_listening_t;
typedef class  CSocket               CSocket;

typedef void (CSocket::*ngx_event_handler_pt)(lpngx_connection_t c); //定义成员函数指针

/*描述一个监听对象*/
struct ngx_listening_s
{
	int				port;
	int				fd;
	lpngx_connection_t connection; //该监听对象对应的连接对象
};

/*用来描述一个用于通信的TCP连接*/
struct ngx_connection_s
{
	ngx_connection_s();
	virtual ~ngx_connection_s();
	void GetOneToUse();
	void PutOneToFree();

	int						fd;
	lpngx_listening_t		listening; //如果这个链接分配给了监听套接字，这个变量就指向该监听套接字对应的ngx_listening_t的地址。

	unsigned				instance : 1;  //【位域】失效位标志：0是有效，1是失效【官方nginx提供】
	uint64_t				iCurrSequence;	//一个序号，每次分配时+1，
	struct sockaddr			s_sockaddr;
	char					addr_text[20];  //ip地址的文本信息，比如192.168.1.1

	uint8_t					r_ready;		//读准备好标记
	uint8_t					w_ready;		//写准备号标记

	ngx_event_handler_pt	r_handler;		//读事件的相关处理办法
	ngx_event_handler_pt	w_handler;		//写事件

	uint32_t				events;			//epoll里的events

	/*收包相关*/
	unsigned char			curStat;						//收包状态
	char					dataHeadInfo[_DATA_BUFSIZE_];
	char*					precvbuf;						/*接收缓冲区的头指针，用来判定包头或者包体是否接收完整*/
	unsigned int			irecvlen;						//要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码
	char*					precvMemPointer;				//new出来的用于收包的内存首地址，释放用的

	pthread_mutex_t			logicPorcMutex;					

	/*发包相关*/
	std::atomic<int>		iThrowSendCount;				//用这个变量标记发送缓冲区是否满了
	char*					psendMemPointer;				//释放用的,此指针指向发送buf的头，也就是消息头+包头+包体，但是消息头不用发送！
	char*					psendbuf;						//发送缓冲区的头指针
	unsigned int			isendlen;						//要写的数据的长度

	bool					ifNewRecvMem;					//标志位，如果成功收到包头，就要分配内存开始保存消息头+包头+包体的内容，这个标记为true表示分配过内存，这个内存需要手动释放。
	char*					pNewMemPointer;					//new出来的内存的首地址,需要和ifNewRecvMem配对使用

	/*回收资源相关*/
	time_t					inRecyTime;						//本对象入到资源回收站里的时间

	/*心跳包有关*/
	time_t					lastPingTime;					//上次ping的时间[上次心跳包的事件]

	/*网络安全相关*/
	uint64_t				FloodkickLastTime;				//Floos攻击上次收到包的时间
	int						FloodAttackCount;

	std::atomic<int>		iSendCount;						//发送队列中有的数据条目数,如果client只发不收，则可能造成此数过大，提出处理。

	/*暂时没用，因为用了std::list*/
	lpngx_connection_t		next;							//指针，指向下一个本类型的对象，把空闲连接池串起来做成单向链表。
};

/*消息头，收到包之后再加一个头，用来记录包的额外信息*/
typedef struct _STRUC_MSG_HEADER
{
	lpngx_connection_t	pConn;         //记录对应的链接
	uint64_t			iCurrsequence; //收到数据包时记录对应连接的序号，将来能用于比较是否连接已经作废用
	/*其他信息*/
}STRUC_MSG_HEADER, * LPSTRUC_MSG_HEADER;

class CSocket
{
public:
	CSocket();
	virtual ~CSocket();
	virtual bool Initialize();				//父进程调用的函数
	virtual bool Initialize_subprocess();	//子进程调用的函数
	virtual void Shutdown_subproc();

	void printTDInfo();						//打印统计信息

public:
	virtual void threadRecvProcFunc(char* pMsgBuf);	//处理客户端请求的函数
	virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time); //心跳包检测函数

public:
	int ngx_epoll_init();

	int ngx_epoll_process_events(int timer);

	int ngx_epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpngx_connection_t pConn);

protected:
	//数据发送相关
	void msgSend(char* psendbuf);					//把数丢进发送队列中
	void zdClosesocketProc(lpngx_connection_t p_Conn); //主动关闭一个连接时要用的善后处理操作。

private:
	void ReadConf();	//用于读取配置项
	bool ngx_open_listening_sockets();	//监听必须的端口
	void ngx_close_listening_sockets();	//关闭监听套接字
	bool set_nonblocking(int sockfd);

	/*一些连接处理函数*/
	void ngx_event_accept(lpngx_connection_t oldc); //建立新的连接
	void ngx_read_request_handler(lpngx_connection_t pConn); //设置数据来的时候的处理函数
	void ngx_write_request_handler(lpngx_connection_t pConn); //设置数据写的时候的处理函数
	void ngx_close_connection(lpngx_connection_t pConn); //通用连接关闭， 资源用这个函数释放

	ssize_t recvproc(lpngx_connection_t pConn, char* buf, ssize_t buflen); //接收从客户端传来的数据的专用函数

	void ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn, bool &isflood); //接收完整包头后的处理函数
	void ngx_wait_request_handler_proc_plast(lpngx_connection_t pConnm, bool& isflood); // 收到一个完整的包后的处理

	void clearMsgSendQueue();	//清空发送消息队列

	ssize_t sendproc(lpngx_connection_t c, char* buf, ssize_t size); //将数据发送到客户端

	///*获取对端信息*/
	size_t ngx_sock_ntop(struct sockaddr* sa, int port, u_char* text, size_t len); //根据参数一的信息，获得地址字符，返回值是字符串的长度

	/*连接池相关*/
	void initconnection(); //初始化连接池
	void clearconnection(); //回收连接池
	lpngx_connection_t ngx_get_connection(int isock); //从连接池中获取一个空闲连接
	void ngx_free_connection(lpngx_connection_t c); //归还参数c代表的连接到连接池中

	//和时间相关的函数
	void    AddToTimerQueue(lpngx_connection_t pConn);                    //设置踢出时钟(向map表中增加内容)
	time_t  GetEarliestTime();                                            //从multimap中取得最早的时间返回去
	LPSTRUC_MSG_HEADER RemoveFirstTimer();                                //从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
	LPSTRUC_MSG_HEADER GetOverTimeTimer(time_t cur_time);                  //根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点      
	void DeleteFromTimerQueue(lpngx_connection_t pConn);                  //把指定用户tcp连接从timer表中抠出去
	void clearAllFromTimerQueue();                                        //清理时间队列中所有内容

	/*回收相关*/
	void inRecyConnectQueue(lpngx_connection_t pConn);

	/*网络安全相关*/
	bool TestFlood(lpngx_connection_t pConn);						//测试是否flood攻击

	//线程相关函数
	static void* ServerSendQueueThread(void* threadItem);                 //专门用来发送数据的线程
	static void* ServerRecyConnectionThread(void* threadItem);            //专门用来回收连接的线程
	static void* ServerTimerQueueMonitorThread(void* threadItem);         //时间队列监视线程，处理到期不发心跳包的用户踢出的线程
protected:
	//一些和网络通讯有关的成员变量
	size_t								m_iLenPkgHeader;                       //sizeof(COMM_PKG_HEADER);		
	size_t								m_iLenMsgHeader;                       //sizeof(STRUC_MSG_HEADER);

	//时间相关
	int									m_ifTimeOutKick;                       //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当配置文件中Sock_WaitTimeEnable = 1时，本项才有用 
	int									m_iWaitTime;                           //多少秒检测一次是否 心跳超时，只有当配置文件中Sock_WaitTimeEnable = 1时，本项才有用	

private:
	struct ThreadItem
	{
		pthread_t		_Handle;                                              //线程句柄
		CSocket*		_pThis;                                              //记录线程池的指针	
		bool			ifrunning;                                            //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

		//构造函数
		ThreadItem(CSocket* pthis) :_pThis(pthis), ifrunning(false) {}
		//析构函数
		~ThreadItem() {}
	};

	int									m_worker_connection;			//epoll连接的最大项数
	int									m_ListenPortCount;				//所监听的端口数量
	int									m_epollhandle;					//epollcreat返回的句柄

	std::list<lpngx_connection_t>		m_connectionList;				//连接池
	std::list<lpngx_connection_t>		m_freeConnectionList;			//空闲连接池

	std::atomic<int>					m_total_connection_n;			//连接池的总连接数
	std::atomic<int>					m_free_connection_n;			//连接池的空闲连接数
	pthread_mutex_t						m_connectionMutex;				//用来互斥m_freeconnectionList，m_connectionList
	pthread_mutex_t						m_recyconnqueueMutex;			//连接回收队列的互斥量

	std::list<lpngx_connection_t>		m_recyconnectionList;			//将要释放的连接放这里
	std::atomic<int>					m_totol_recyconnection_n;		//待释放连接队列大小
	int									m_RecyConnectionWaitTime;		//等待这么些秒后才回收连接

	std::vector<lpngx_listening_t>		m_ListenSocketList;				//监听套接字队列
	struct epoll_event					m_events[NGX_MAX_EVENTS];		//用于epoll_wait()中的传出参数

	//消息队列
	std::list<char*>					m_MsgSendQueue;					//发送数据消息队列
	std::atomic<int>					m_iSendMsgQueueCount;			//发消息队列大小

	//多线程相关
	std::vector<ThreadItem*>			m_threadVector;					//线程 容器，容器里就是各个线程了 	
	pthread_mutex_t						m_sendMessageQueueMutex;		//发消息队列互斥量 
	sem_t								m_semEventSendQueue;			//处理发消息线程相关的信号量
	
	//时间相关
	int									m_ifkickTimeCount;				//是否开启踢人时钟，1：开启   0：不开启		
	pthread_mutex_t						m_timequeueMutex;				//和时间队列有关的互斥量
	std::multimap<time_t, LPSTRUC_MSG_HEADER>   m_timerQueuemap;		//时间队列	
	size_t								m_cur_size_;					//时间队列的尺寸
	time_t								m_timer_value_;					//当前计时队列头部时间值

	/*在线用户*/
	std::atomic<int>					m_onlineUserCount;				//当前在线用户统计

	//网络安全相关
	int									m_floodAkEnable;				//Flood攻击检测是否开启,1：开启   0：不开启
	unsigned int						m_floodTimeInterval;			//表示每次收到数据包的时间间隔
	int									m_floodKickCount;				//累积多少次踢出此人

	//统计用途
	time_t								m_lastprintTime;				//上次打印统计信息的时间(10秒钟打印一次)
	int									m_iDiscardSendPkgCount;			//丢弃的发送数据包数量
};