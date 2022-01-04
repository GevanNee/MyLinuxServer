#pragma once

#define _PKG_MAX_LENGTH		30000 //每个包的最大长度(包头 + 包体)

/*定义收包的状态*/
#define _PKG_HD_INIT		0 //初始，接收包头之前
#define _PKG_HD_RECVING		1 //包头还没接收完
#define _PKG_BD_INIT		2 //包头接收完了，还没开始接收包体
#define _PKG_BD_RECVING		3 //接收包体中。。。
#define _PKG_RV_FINISHED	4 //包体接受完，暂时用不到，因为接受完一般回到0状态

#define _DATA_BUFSIZE_		20 //接收包头，并把包头存下来用于解析。这个数字就是存包头的缓冲区的大小，这个大小需要大于下面的结构体大小

#pragma pack (1) //定死1字节对齐，因为不同编译器对齐不一样
/*包头结构*/
typedef struct _COMM_PKG_HERDER
{
	unsigned short	pkgLenth;	/*报文的总长度：包头+包体*/

	unsigned short	msgCode;	/*描述消息的类型*/
	int				crc32;		/*包体的CRC校验*/
}COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;
#pragma pack()