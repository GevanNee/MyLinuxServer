#include "ngx_c_memory.h"
#include <string.h>

CMemory* CMemory::m_instance = nullptr;

void* CMemory::AllocMemory(int memCount, bool isSetZero)
{
	void* tmpData = (void*)new char[memCount];
	if (isSetZero)
	{
		memset(tmpData, 0, memCount);
	}
	return tmpData;
}

void CMemory::FreeMemory(void* point)
{
	delete[]((char*)point);
}