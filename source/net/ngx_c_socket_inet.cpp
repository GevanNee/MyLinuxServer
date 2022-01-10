#include <sys/socket.h>
#include <arpa/inet.h>

#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_macro.h"


/*将参数1的地址信息写到参数3里*/
size_t CSocket::ngx_sock_ntop(struct sockaddr* sa, int port, u_char* text, size_t len)
{
    struct sockaddr_in* sin;
    u_char* p;

    switch (sa->sa_family)
    {
    case AF_INET:
        sin = (struct sockaddr_in*)sa;
        p = (u_char*)&sin->sin_addr;
        if (port)  //端口信息也组合到字符串里
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud:%d", p[0], p[1], p[2], p[3], ntohs(sin->sin_port)); //返回的是新的可写地址
        }
        else //不需要组合端口信息到字符串中
        {
            p = ngx_snprintf(text, len, "%ud.%ud.%ud.%ud", p[0], p[1], p[2], p[3]);
        }
        return (p - text);
        break;
    default:
        return 0;
        break;
    }
    return 0;
}