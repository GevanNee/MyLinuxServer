#include <string.h>
#include "ngx_func.h"

//消除字符串首尾的空格
void trim(char* string)
{
    Rtrim(string);
    Ltrim(string);
}

void Rtrim(char* string)
{
    if (string == nullptr)
    {
        return;
    }
    size_t lenth = strlen(string);
    if (lenth <= 0)
    {
        return;
    }

    while (string[lenth - 1] == ' ')
    {
        string[lenth - 1] = '\0';
        lenth--;
    }
}

void Ltrim(char* string)
{
    if (string == nullptr)
    {
        return;
    }

    char* p_tmp = string;
    while ((*p_tmp) == ' ' && (*p_tmp) != '\0')
    {
        p_tmp += 1;
    }

    if ((*p_tmp) == '\0')
    {
        string[0] = '\0';
    }
    char* p_tmp2 = string;
    while ((*p_tmp) != '\0')
    {
        (*p_tmp2) = (*p_tmp);
        p_tmp2++;
        p_tmp++;
    }
    (*p_tmp2) = '\0';
    return;
}