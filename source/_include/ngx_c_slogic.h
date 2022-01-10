#pragma once

#include "ngx_c_socket.h"

class CLogicSocket : public CSocket
{
public:
	CLogicSocket();
	virtual ~CLogicSocket() override;
	virtual bool Initialize() override;

public:

	void SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode);

	bool _HandleRegister(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);
	bool _HandleLogIn	(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);
	bool _HandlePing	(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);

	virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time); //心跳包检测函数

public:
	virtual void threadRecvProcFunc(char* pMsgBuf)override;
};