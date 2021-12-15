#pragma once
#include <vector>

typedef struct ConfigItem
{
    char key[50];
    char value[200];
}ConfigItem, * LPConfigItem;

class ngx_c_conf
{
public:
    ~ngx_c_conf();

public:
    bool load(const char* configFileName);
    int getInt(const char* key, const int defaultValue);
    const char* getString(const char* key);
    static ngx_c_conf* getInstance();

public:
    class releaser {
    public:
        ~releaser();
    };

private:
    ngx_c_conf();
    static ngx_c_conf* m_instance;
    std::vector<LPConfigItem> m_ConfigItemList;
};