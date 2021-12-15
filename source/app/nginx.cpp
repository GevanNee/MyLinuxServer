#include <iostream>
#include <cstring>
#include <vector>
#include <time.h>
#include <sys/time.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_global.h"

using namespace std;

void freeResource()
{
	if (ngx_log.fd > 0)
	{
		close(ngx_log.fd);
		ngx_log.fd = -1;
	}
	
}

void timeTest()
{
	u_char* last;
	u_char  errstr[20 + 1];   //这个+1也是我放入进来的，本函数可以参考ngx_log_stderr()函数的写法；

	memset(errstr, 0, sizeof(errstr));
	last = errstr + 20;

	struct timeval   tv;
	struct tm        tm;
	time_t           sec;   //秒
	u_char* p;    //指向当前要拷贝数据到其中的内存位置
	va_list          args;

	memset(&tv, 0, sizeof(struct timeval));
	memset(&tm, 0, sizeof(struct tm));

	//gettimeofday(&tv, NULL);     //获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】        
	tv.tv_sec + 1;
	sec = tv.tv_sec;             //秒
	localtime_r(&sec, &tm);      //把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
	tm.tm_mon++;                 //月份要调整下正常
	tm.tm_year += 1900;          //年份要调整下才正常

	printf("%d-%d-%d", tm.tm_year, tm.tm_mon, tm.tm_mday);
}

void configTest()
{
	char str[500] = "    12   ";

	trim(str);

	ngx_c_conf* conf = ngx_c_conf::getInstance();
	if (conf == nullptr)
	{
		cout << "初始化失败" << endl;
	}

	if (!conf->load("./ngx_conf.conf"))
	{
		cout << "加载文件失败" << endl;
	}
	cout << conf->getInt("listenport", 12322) << endl;
}

void logTest()
{
	ngx_log_init();
	u_char myName[10]{ "LDY" };
	ngx_log_stderr(errno, "myName = %s, myAge = %d", myName, 25);
}


int main()
{
	timeTest();
	
	//logTest();
	//getpid();
	//freeResource();
	return 0; 
}