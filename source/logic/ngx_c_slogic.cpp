#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>


#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_c_slogic.h"
#include "ngx_c_crc32.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"
#include "ngx_logic_comm.h"

/*成员函数指针*/
typedef bool (CLogicSocket::*handler)(  lpngx_connection_t pConn,      //连接池中连接的指针
										LPSTRUC_MSG_HEADER pMsgHeader,  //消息头指针
										char *pPkgBody,                 //包体指针
										unsigned short iBodyLength);

CLogicSocket::CLogicSocket()
{
}

CLogicSocket::~CLogicSocket()
{
}

static handler statusHandler[] =
{
	 &CLogicSocket::_HandlePing,								//【0】：心跳包的实现
	NULL,														//【1】
	NULL,														//【2】
	NULL,														//【3】
	NULL,														//【4】

	//开始处理具体的业务逻辑
	&CLogicSocket::_HandleRegister,								//【5】注册
	&CLogicSocket::_HandleLogIn,								//【6】登录
	/*其他业务，待添加*/
};

#define AUTH_TOTAL_COMMANDS (sizeof(statusHandler)/sizeof(handler)) //业务函数的个数(包括null)

bool CLogicSocket::Initialize()
{
	/*本类的初始化工作。。。待添加*/
	/*先初始化一下父类*/
	return this->CSocket::Initialize(); //调用父类构造函数
}

void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode)
{
	CMemory* p_memory = CMemory::GetInstance();

	char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader, false);
	char* p_tmpbuf = p_sendbuf; //用来偏移

	memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
	p_tmpbuf += m_iLenMsgHeader; //指向包头位置

	LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;
	pPkgHeader->msgCode = htons(iMsgCode);
	pPkgHeader->pkgLenth = htons(m_iLenPkgHeader);
	pPkgHeader->crc32 = 0;

	msgSend(p_sendbuf);

	ngx_log_core(NGX_LOG_DEBUG, 0, "CLogicSocket::SendNoBodyPkgToClient()，构造了一个无包体的数据");

	return;
}

/*各种业务处理函数*/
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
#if 1
	//(1)首先判断包体的合法性
	if (pPkgBody == nullptr) //具体看客户端服务器约定，如果约定这个命令[msgCode]必须带包体，那么如果不带包体，就认为是恶意包，直接不处理    
	{
		return false;
	}

	int iRecvLen = sizeof(STRUCT_REGISTER);
	if (iRecvLen != iBodyLength) //发送过来的结构大小不对，认为是恶意包，直接不处理
	{
		return false;
	}

	//(2)对于同一个用户，可能同时发送来多个请求过来，造成多个线程同时为该 用户服务，比如以网游为例，用户要在商店中买A物品，又买B物品，而用户的钱 只够买A或者B，不够同时买A和B呢？
	   //那如果用户发送购买命令过来买了一次A，又买了一次B，如果是两个线程来执行同一个用户的这两次不同的购买命令，很可能造成这个用户购买成功了 A，又购买成功了 B
	   //所以，为了稳妥起见，针对某个用户的命令，我们一般都要互斥,我们需要增加临界的变量于ngx_connection_s结构中
	CLock lock(&pConn->logicPorcMutex); //凡是和本用户有关的访问都互斥

	//(3)取得了整个发送过来的数据
	LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
	p_RecvInfo->iType = ntohl(p_RecvInfo->iType);          //所有数值型,short,int,long,uint64_t,int64_t这种大家都不要忘记传输之前主机网络序，收到后网络转主机序
	p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 
	p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。 


	//(4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性，
	   //当前该玩家的状态是否适合收到这个数据等等【比如如果用户没登陆，它就不适合购买物品等等】
		//这里大家自己发挥，自己根据业务需要来扩充代码，老师就不带着大家扩充了。。。。。。。。。。。。
	//。。。。。。。。

	//(5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，这里我们就以给客户端也返回同样的 STRUCT_REGISTER 结构来举例    
	//LPSTRUCT_REGISTER pFromPkgHeader =  (LPSTRUCT_REGISTER)(((char *)pMsgHeader)+m_iLenMsgHeader);	//指向收到的包的包头，其中数据后续可能要用到
	LPCOMM_PKG_HEADER pPkgHeader;
	CMemory* p_memory = CMemory::GetInstance();
	CCRC32* p_crc32 = CCRC32::GetInstance();
	int iSendLen = sizeof(STRUCT_REGISTER);
	//a)分配要发送出去的包的内存

	//iSendLen = 65000; //unsigned最大也就是这个值
	char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);//准备发送的格式，这里是 消息头+包头+包体
	//b)填充消息头
	memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);                   //消息头直接拷贝到这里来
	//c)填充包头
	pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);    //指向包头
	pPkgHeader->msgCode = _CMD_REGISTER;	                        //消息代码，可以统一在ngx_logiccomm.h中定义
	pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
	pPkgHeader->pkgLenth = htons(m_iLenPkgHeader + iSendLen);        //整个包的尺寸【包头+包体尺寸】 
	//d)填充包体
	LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);	//跳过消息头，跳过包头，就是包体了
	//。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

	//e)包体内容全部确定好后，计算包体的crc32值
	pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char*)p_sendInfo, iSendLen);
	pPkgHeader->crc32 = htonl(pPkgHeader->crc32);

	//f)发送数据包
	msgSend(p_sendbuf);
#endif
	return true;
}

bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
	return false;
}

bool CLogicSocket::_HandlePing(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
	return false;
}

void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
	CMemory* p_memory = CMemory::GetInstance();

	if (tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrSequence) //此连接没断
	{
		lpngx_connection_t p_Conn = tmpmsg->pConn;

		if (/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick == 1)  //能调用到本函数第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
		{
			//到时间直接踢出去的需求
			zdClosesocketProc(p_Conn);
		}
		else if ((cur_time - p_Conn->lastPingTime) > (m_iWaitTime * 3 + 10)) //超时踢的判断标准就是 每次检查的时间间隔*3，超过这个时间没发送心跳包，就踢【大家可以根据实际情况自由设定】
		{
			//踢出去【如果此时此刻该用户正好断线，则这个socket可能立即被后续上来的连接复用  如果真有人这么倒霉，赶上这个点了，那么可能错踢，错踢就错踢】            
			//ngx_log_stderr(0,"时间到不发心跳包，踢出去!");   //感觉OK
			zdClosesocketProc(p_Conn);
		}

		p_memory->FreeMemory(tmpmsg);//内存要释放
	}
	else //此连接断了
	{
		p_memory->FreeMemory(tmpmsg);//内存要释放
	}
	return;

}

/*线程内部函数，由线程调用*/
/*MsgBuf:消息头 + 包头 + 包体*/
void CLogicSocket::threadRecvProcFunc(char* pMsgBuf)
{
	LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;
	LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + m_iLenMsgHeader);

	void* pPkgBody;
	unsigned short pkglen = ntohs(pPkgHeader->pkgLenth);

	if (pkglen == m_iLenPkgHeader) //只有包头没有包体
	{
		if (pPkgHeader->crc32 != 0) //没有包体的包crc是0才对
		{
			return; //丢弃
		}

		pPkgBody = nullptr;
	}
	else
	{
		pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);
		pPkgBody = (void*)(pMsgBuf + m_iLenPkgHeader + m_iLenMsgHeader);

		int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char*)pPkgBody, pkglen - m_iLenMsgHeader);
	}

	unsigned short imsgCode = ntohs(pPkgHeader->msgCode);
	lpngx_connection_t p_Conn = pMsgHeader->pConn;
	if (p_Conn->iCurrSequence != pMsgHeader->iCurrsequence)   //该连接池中连接以被其他tcp连接【其他socket】占用，这说明原来的 客户端和本服务器的连接断了，这种包直接丢弃不理
	{
		ngx_log_core(NGX_LOG_INFO, 0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d, 发现过期连接"); //发现过期连接
		return; //丢弃不理这种包了【客户端断开了】
	}

	/*判断消息码是否在可用范围内*/
	if (imsgCode >= AUTH_TOTAL_COMMANDS) //无符号数不可能<0
	{
		ngx_log_core(NGX_LOG_ERROR, 0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d, 消息码不对!", imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
		return; //丢弃不理这种包【恶意包或者错误包】
	}

	/*判断是否存在消息的处理函数*/
	if (statusHandler[imsgCode] == nullptr) 
	{
		ngx_log_core(NGX_LOG_ERROR, 0, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!", imsgCode);
		return;  //没有相关的处理函数
	}

	/*到这里算是正确的包*/
	(this->*(statusHandler[pPkgHeader->msgCode]))(pMsgHeader->pConn, pMsgHeader, (char*)pPkgBody, pPkgHeader->pkgLenth - m_iLenMsgHeader);

	return;
}
