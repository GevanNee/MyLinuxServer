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

	va_start(args, fmt); //ʹargsָ����ʼ�Ĳ���
	p = ngx_vslprintf(buf, last, fmt, args);
	va_end(args);        //�ͷ�args   
	return p;
}

u_char* ngx_snprintf(u_char* buf, size_t max, const char* fmt, ...)   //��printf()��ʽ���������Ƚϰ�ȫ��maxָ���˻���������λ��
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

	/*���ַ��������ǰ����ַ�������fmt������%10d��
	��ʾ���10��ʮ�������֣����Ǻ���Ĳ���ֻ��7������1234567��
	��ôǰ����������zero�ַ���䣬zero�ַ�һ����'0'����' '*/
	u_char zero; 

	uintptr_t width, sign, hex, frac_width, scale;

	int64_t		i64;   //����%d��Ӧ�Ŀɱ��
	uint64_t	ui64;  //����%ud��Ӧ�Ŀɱ�Σ���ʱ��Ϊ%f�ɱ�ε���������Ҳ�ǿ��Ե�
	u_char*		str;    //����%s��Ӧ�Ŀɱ��
	double		df;     //����%f��Ӧ�Ŀɱ��
	uint64_t	frac;  //%f�ɱ����,����%.2f�ȣ�ȡ��С�����ֵ�2λ������ݣ�

	while (*fmt != '\0' && buf < last)
	{
		if (*fmt == '%')
		{
			/*��ʼ����ʱ����*/
			zero = (u_char)(*++fmt == '0' ? '0' : ' ');
			width = 0;
			sign = 1;
			hex = 0;
			frac_width = 0;

			/*һ������%�����ַ��ĺ���*/
			/*һ.1) */
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

			/*�������ݺ���Ͳ���args�����ɶ�Ӧ�ַ��������ұ����ַ����ĸ�ʽ*/
			switch (*fmt)
			{
			case '%':
				*buf++ = '%';
				fmt++;
				continue; /*��ת�������whileѭ��*/

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
				width = 2 * sizeof(void*); /*%pֻ�ܰ�������������*/
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
						if (scale < uint_max / 10) /*��ֹ���*/
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

			/*���������ɵ��ַ������յõ��ĸ�ʽ���뵽buf��*/
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
		/*���������һ���Ż�����Ϊһ��ui64����������������32λ*/
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




