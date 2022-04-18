#pragma once
#include <unordered_map>
#include <xaudio2.h>
#include <string>

struct FmtDesc
{
	unsigned int chunkSize_;
	unsigned short formatType_;
	unsigned short channel_;
	unsigned int samplesPerSec_;
	unsigned int bytePerSec_;
	unsigned short blockAlign_;
	unsigned short bitPerSample_;
};

struct WAVData
{
	unsigned int fileSize_;
	FmtDesc fmt_;
	unsigned int dataSize_;
	unsigned char* data_;
};

class WAVLoader
{
public:
	WAVLoader();
	~WAVLoader();
	bool LoadWAVFile(const std::string& filename);
	const WAVData& GetWAVFile(const std::string& filename);
	void DestroyWAVFile(const std::string& filename);
private:
	std::unordered_map<std::string, WAVData> wav_;

	static constexpr char fmttag[4] = { 'f', 'm', 't', ' ' };
	static constexpr char datatag[4] = { 'd', 'a', 't', 'a' };
};

