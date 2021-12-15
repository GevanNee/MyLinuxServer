#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ngx_func.h"
#include "ngx_macro.h"

static u_char* ngx_sprintf_num(u_char* buf, u_char* last, uint64_t ui64, u_char zero, uintptr_t hex, uintptr_t width);

u_char* ngx_slprintf(u_char* buf, u_char* last, const char* fmt, ...)
{
	va_list   args; 
	u_char* p;

	va_start(args, fmt); //使args指向起始的参数
	p = ngx_vslprintf(buf, last, fmt, args);
	va_end(args);        //释放args   
	return p;
}

u_char* ngx_snprintf(u_char* buf, size_t max, const char* fmt, ...)   //类printf()格式化函数，比较安全，max指明了缓冲区结束位置
{
	u_char* p;
	va_list   args;

	va_start(args, fmt);
	p = ngx_vslprintf(buf, buf + max, fmt, args);
	va_end(args);
	return p;
}

u_char* ngx_vslprintf(u_char* buf, u_char* last, const char* fmt, va_list args)
{
	/*debug*/
	u_char* debug_buf = buf;
	/*debug end*/

	/*该字符用来填充前面的字符，比如fmt里遇到%10d，
	表示输出10个十进制数字，但是后面的参数只有7个数字1234567，
	那么前面三个就用zero字符填充，zero字符一般是'0'或者' '*/
	u_char zero; 

	uintptr_t width, sign, hex, frac_width, scale;

	int64_t		i64;   //保存%d对应的可变参
	uint64_t	ui64;  //保存%ud对应的可变参，临时作为%f可变参的整数部分也是可以的
	u_char*		str;    //保存%s对应的可变参
	double		df;     //保存%f对应的可变参
	uint64_t	frac;  //%f可变参数,根据%.2f等，取得小数部分的2位后的内容；

	while (*fmt != '\0' && buf < last)
	{
		if (*fmt == '%')
		{
			/*初始化临时变量*/
			zero = (u_char)(*++fmt == '0' ? '0' : ' ');
			width = 0;
			sign = 1;
			hex = 0;
			frac_width = 0;

			/*一，解析%后面字符的含义*/
			/*一.1) */
			while (*fmt >= '0' && *fmt <= '9')
			{
				width = width * 10 + (*fmt - '0');
				fmt++;
			}

			for (;;)
			{
				switch (*fmt)
				{
				case 'u':
					sign = 0;
					++fmt;
					continue;
				case 'x':
					hex = 1;
					++fmt;
					continue;
				case 'X':
					hex = 2;
					++fmt;
					continue;
				case '.':
					++fmt;
					while (*fmt >= '0' || *fmt <= '9')
					{
						frac_width = frac_width * 10 + (*fmt - '0');
						++fmt;
					}
					break;
				default:
					break;
				}
				break;
			}

			/*二，根据含义和参数args，生成对应字符串，并且保存字符串的格式*/
			switch (*fmt)
			{
			case '%':
				*buf++ = '%';
				fmt++;
				continue; /*跳转到最外层while循环*/

			case 'd':
				if (sign)
				{
					i64 = (int64_t)va_arg(args, int);
				}
				else
				{
					ui64 = (uint64_t)va_arg(args, int);
				}
				break;

			case 'L':
				if(sign)
				{
					i64 = va_arg(args, int64_t);
				}
				else
				{
					ui64 = va_arg(args, uint64_t);
				}
				break;

			case 'i':
				if (sign)
				{
					i64 = (int64_t)va_arg(args, intptr_t);
				}
				else
				{
					ui64 = (uint64_t)va_arg(args, uintptr_t);
				}
				break;

			case 'p':
				ui64 = (uintptr_t)va_arg(args, void*);
				sign = 0;
				hex = 2;
				zero = '0';
				width = 2 * sizeof(void*); /*%p只能按照这个长度输出*/
				break;

			case 'f':
				df = va_arg(args, double);
				if (df < 0)
				{
					*buf++ = '-';
					df = -df;
				}

				ui64 = (uint64_t)df;
				frac = 0;

				if (frac_width)
				{
					scale = 1;
					uintptr_t uint_max = (intptr_t(0) - 1);
					for (uintptr_t i = frac_width; i > 0; --i)
					{
						if (scale < uint_max / 10) /*防止溢出*/
						{
							scale *= 10;
						}
					}

					frac = (uint64_t)((df - (double)ui64)* scale + 0.5);

					if (frac == scale)
					{
						++ui64;
						frac = 0;
					}
				}

				buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width);

				if (frac_width != 0)
				{
					if (buf < last)
					{
						*buf++ = '.';
					}
					buf = ngx_sprintf_num(buf, last, frac, zero, hex, width);
				}
				
				continue;

			case 's':
				str = va_arg(args, u_char*);
				while (*str && buf < last)
				{
					*buf++ = *str++;
				}
				fmt++;
				continue;

			case 'P':
				i64 = (int64_t)va_arg(args, pid_t);
				sign = 1;

				break;

			default:
				*buf++ = *fmt++;
				continue;
			}

			/*三，把生成的字符串按照得到的格式输入到buf中*/
			if (sign != 0)
			{
				if (i64 < 0)
				{
					*buf++ = '-';
					ui64 = (uint64_t)(-i64);
				}
				else
				{
					ui64 = i64;
				}
			}
			buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width);
			fmt++;
		}
		else
		{
			*buf++ = *fmt++;
		}
	}
	return buf;
}

u_char* ngx_sprintf_num(u_char* buf, u_char* last, uint64_t ui64, u_char zero, uintptr_t hexadecimal, uintptr_t width)
{
	u_char* p;
	u_char temp[NGX_INT64_LEN + 1];
	/*debug*/
	size_t tempSize = sizeof(temp) / sizeof(u_char);
	/*debug end*/
	static u_char hex[] = "0123456789abcdef";
	static u_char HEX[] = "0123456789ABCDEF";

	p = temp + NGX_INT64_LEN;

	if (hexadecimal == 0)
	{
		/*这里可以做一个优化，因为一般ui64传进来的数不超过32位*/
		do 
		{
			*--p = (u_char)(ui64 % 10 + '0');
			ui64 /= 10;
		} 
		while (ui64 > 0);
	}
	else if (hexadecimal == 1)
	{
		do 
		{ 
			*--p = hex[(uint32_t)(ui64 & 0xf)];
		}
		while(ui64 >> 4);
	}
	else if (hexadecimal == 2)
	{
		do
		{
			*--p = HEX[(uint32_t)(ui64 & 0xf)];
		} while (ui64 >> 4);
	}
	else
	{
		hexadecimal = 0;
		do
		{
			*--p = (u_char)(ui64 % 10 + '0');
			ui64 /= 10;
		} 
		while (ui64 > 0);
	}
	size_t len = (temp + NGX_INT64_LEN) - p;

	while (width > len++ && buf < last)
	{
		*buf++ = zero;
	}

	len = (temp + NGX_INT64_LEN) - p;

	if ((buf + len) >= last)
	{
		len = last - buf;
	}
	buf = (u_char*)memcpy(buf, p, len) + len;
	return buf;
}




