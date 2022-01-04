#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

void ngx_init_setproctitle()
{
	gp_envmem = new char[g_envneedmem];
	memset(gp_envmem, 0, g_envneedmem);

	char* p = gp_envmem;
	for (int i = 0; environ[i]; i++)
	{
		size_t size = strlen(environ[i]) + 1;
		strcpy(p, environ[i]);
		environ[i] = p;
		p += size;
	}
	return;
}

void ngx_setproctitle(const char* title)
{
	size_t titleLenth = strlen(title);
	size_t maxlenth = g_argvneedmem + g_envneedmem;
	
	if (titleLenth >= maxlenth)
	{
		ngx_log_core(NGX_LOG_ERROR, 0, "Set title failed, the lenth of title exceeds the limit!");
		return;
	}

	size_t leftMemorySize = maxlenth - titleLenth;

	g_os_argv[1] = nullptr; //?

	char* p = g_os_argv[0];
	strcpy(*g_os_argv, title);
	p += titleLenth;
	
	memset(p, 0, leftMemorySize);
}