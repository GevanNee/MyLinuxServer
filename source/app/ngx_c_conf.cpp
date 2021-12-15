#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <unistd.h>
#include <string.h>
#include "ngx_c_conf.h"
#include "ngx_func.h"

ngx_c_conf* ngx_c_conf::m_instance = nullptr;

ngx_c_conf* ngx_c_conf::getInstance()
{
    if (m_instance == nullptr)
    {
        if (m_instance == nullptr)
        {
            m_instance = new ngx_c_conf();
            static releaser r;
            return m_instance;
        }
    }
    return m_instance;
}

ngx_c_conf::~ngx_c_conf()
{
    std::vector<LPConfigItem>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); pos++)
    {
        delete (*pos);
    }
    m_ConfigItemList.clear();
    return;
}

ngx_c_conf::ngx_c_conf()
{

}

bool ngx_c_conf::load(const char* configFileName)
{
    FILE* fp;
    fp = fopen(configFileName, "r");
    if (fp == nullptr)
    {
        return false;
    }

    char linebuf[501];
    while (!feof(fp))
    {
        if (fgets(linebuf, 500, fp) == nullptr)
        {
            continue;
        }

        if (linebuf[0] == '\0')
        {
            continue;
        }

        if (linebuf[0] == ';' || linebuf[0] == '#' || linebuf[0] == ' ' || linebuf[0] == '\t' || linebuf[0] == '\n')
        {
            continue;
        }

        //Çå³ý×Ö·û´®Ä©Î²µÄÌØÊâ·ûºÅ
        while (strlen(linebuf) > 0)
        {
            if (linebuf[strlen(linebuf) - 1] == '\t' || linebuf[strlen(linebuf) - 1] == '\n' || linebuf[strlen(linebuf) - 1] == ' ')
            {
                linebuf[strlen(linebuf) - 1] = '\0';
            }
            else
            {
                break;
            }
        }
        if (strlen(linebuf) == 0)
        {
            continue;
        }

        if (linebuf[0] == '[')
        {
            continue;
        }

        char* ptmp = strchr(linebuf, '=');
        if (ptmp != nullptr)
        {
            LPConfigItem p_confItem = new ConfigItem;
            memset(p_confItem, 0, sizeof(ConfigItem));

            strncpy(p_confItem->key, linebuf, (int)(ptmp - linebuf));
            strcpy(p_confItem->value, ptmp + 1);

            trim(p_confItem->key);
            trim(p_confItem->value);

            m_ConfigItemList.push_back(p_confItem);
        }
    }

    fclose(fp);
    return true;
}

int ngx_c_conf::getInt(const char* key, const int defaultValue)
{
    for (auto itr : m_ConfigItemList)
    {
        if (strcasecmp((*itr).key, key) == 0)
        {
            return atoi((*itr).value);
        }
    }
    return defaultValue;
}

const char* ngx_c_conf::getString(const char* key)
{
    for (auto itr: m_ConfigItemList)
    {
        if (strcasecmp((*itr).key, key) == 0)
        {
            return (*itr).value;
        }
    }
    return nullptr;
}

ngx_c_conf::releaser::~releaser()
{
    {
        if (m_instance != nullptr)
        {
            delete m_instance;
            m_instance == nullptr;
        }
    }
}
