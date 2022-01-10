#pragma once

class CCRC32
{
private:
	CCRC32();
public:
	~CCRC32();
private:
	static CCRC32* m_instance;

public:
	static CCRC32* GetInstance();

	class CGarhuishou
	{
	public:
		~CGarhuishou();
	};
	//-------
public:

	void  Init_CRC32_Table();
	//unsigned long Reflect(unsigned long ref, char ch); // Reflects CRC bits in the lookup table
	unsigned int Reflect(unsigned int ref, char ch); // Reflects CRC bits in the lookup table

	//int   Get_CRC(unsigned char* buffer, unsigned long dwSize);
	int   Get_CRC(unsigned char* buffer, unsigned int dwSize);

public:
	//unsigned long crc32_table[256]; // Lookup table arrays
	unsigned int crc32_table[256]; // Lookup table arrays
};