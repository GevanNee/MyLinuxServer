#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"

static u_char error_levels[][20]
{
	{"stderr"},
	{"emergency"},
	{"alert"},
	{"critical"},
	{"error"},
	{"warn"},
	{"notice"},
	{"infomation"},
	{"debug"},
};

ngx_log_t ngx_log;

void ngx_log_init()
{
	u_char* log_name = nullptr;
	ngx_c_conf* p_config = ngx_c_conf::getInstance();

	log_name = (u_char*)p_config->getString("log");
	if (log_name == nullptr)
	{
		log_name = (u_char*)NGX_ERROR_LOG_PATH;
	}

	ngx_log.log_level = p_config->getInt("loglevel", NGX_LOG_NOTICE);
	if (ngx_log.log_level > 8 || ngx_log.log_level < 0)
	{
		ngx_log.log_level = NGX_LOG_NOTICE;
	}

	ngx_log.fd = open((const char*)log_name, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (ngx_log.fd == -1)
	{
		ngx_log_stderr(errno, "");
		ngx_log.fd = STDERR_FILENO;
	}
}

/*把errno的信息保存到一段指定的内存里。*/
/*返回值：日志字符串的末尾*/
u_char* ngx_log_errno(u_char* buf, u_char* last, int err)
{
	/*左字符串*/
	char leftString[10]{ 0 };
	sprintf(leftString, "(%d: ", err);
	size_t leftLenth = strlen(leftString);

	/*主信息*/
	char* errorInfo = strerror(err);
	size_t infoLenth = strlen(errorInfo);
	
	/*右字符串*/
	char rightString[] = " ) ";
	size_t rightLenth = strlen(rightString);

	if (buf + leftLenth + infoLenth + rightLenth < last)
	{
		buf = (u_char*)memcpy(buf, leftString, leftLenth) + leftLenth;
		buf = (u_char*)memcpy(buf, errorInfo, infoLenth) + infoLenth;
		buf = (u_char*)memcpy(buf, rightString, rightLenth) + rightLenth;
	}

	return buf;
}



/*把日志打印到stderr里，非常重要的信息才需要调用这个函数*/
/* */
void ngx_log_stderr(int err, const char * fmt, ...)
{
	va_list args;
	u_char errorString[NGX_MAX_ERROR_STR + 1]{0}; /*+1可能更保险一点*/
	u_char* p;
	u_char* last = errorString + NGX_MAX_ERROR_STR;

	p = (u_char*)memcpy(errorString, "nginx: ", 7) + 7;
	
	va_start(args, fmt);
	p = ngx_vslprintf(p, last, fmt, args);
	va_end(args);
	

	if (err != 0)
	{
		p = ngx_log_errno(p, last, err);
	}
	/*位置不够也要强行插入换行符*/
	if (p >= (last - 1))
	{
		p = last - 2;
	}
	*p++ = '\n';

	write(STDERR_FILENO, errorString, p - errorString);

	if (ngx_log.fd > STDERR_FILENO)
	{
		/*往日志文件里同步信息*/
	}

	return;
}




void ngx_log_core(int level, int err, const char* fmt, ...)
{
	va_list args;
	u_char* p;
	u_char* last;
	u_char errstr[NGX_MAX_ERROR_STR + 1];

	memset(errstr, 0, sizeof(errstr));
	last = errstr + NGX_MAX_ERROR_STR;

	/*时间操作*/
	struct timeval   timeValue;
	struct tm		 tm;
	time_t			 second;

	memset(&timeValue, 0, sizeof(struct timeval));
	memset(&tm, 0, sizeof(struct tm));

	gettimeofday(&timeValue, nullptr);

	second = timeValue.tv_sec;
	localtime_r(&second, &tm); /*带_r的是线程安全，但是有性能问题，还有死锁问题*/
	/*解决方法： https://blog.csdn.net/pengzhouzhou/article/details/87095635 */
	tm.tm_year += 1900;
	tm.tm_mon++;

	/*组合出一个时间字符串，比如：2021/11/08 05:30:00*/
	u_char currentTimestr[40] = {0};
	ngx_slprintf(currentTimestr, currentTimestr + 40, "%4d/%02d/%02d %02d:%02d:%02d", 
				tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	p = (u_char*)memcpy(errstr, currentTimestr, strlen((const char*)currentTimestr)) + strlen((const char*)currentTimestr);

	
		//localtime_r()
	//write(ngx_log.fd, buf, size);
}
