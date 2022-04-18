#include "WAVLoader.h"
#include "../Utility/utility.h"
#include "../Window/DisplayException.h"

WAVLoader::WAVLoader()
{
}

WAVLoader::~WAVLoader()
{
	for (auto& w : wav_)
	{
		delete[] w.second.data_;
	}
}

bool SeekToFourCC(unsigned char* raw, const char fourcc[4], unsigned int& cursor, unsigned int filesize)
{
	while (true)
	{
		if (raw[cursor] == fourcc[0] && cursor <= (filesize - 4))
		{
			if (raw[cursor + 1] == fourcc[1] && raw[cursor + 2] == fourcc[2] && raw[cursor + 3] == fourcc[3])
			{
				cursor += 4;
				return true;
			}
		}
		cursor++;

		if (cursor >= filesize - 3)
		{
			return false;
		}
	}
	return false;
}

bool WAVLoader::LoadWAVFile(const std::string& filename)
{
	if (wav_.find(filename) != wav_.end())
	{
		return true;
	}
	FILE* fp;
	errno_t result = 0;

	WAVData data;

	result = fopen_s(&fp, filename.c_str(), "rb");
	if (result != 0)
	{
		std::wstring str = L"Oops!\n Audio resource " + StringToWString(filename) + L"\n is not found :(";
		DisplayException::DisplayError(str.c_str());
		return false;
	}

	try
	{
		// ファイル識別子チェック
		char fileidtf[4];
		fread_s(fileidtf, sizeof(char) * 4, sizeof(char), 4, fp);
		if (fileidtf[0] != 'R' || fileidtf[1] != 'I' || fileidtf[2] != 'F' || fileidtf[3] != 'F')
		{
			std::wstring str = L"Oops!\n RIFF Identifier is not found in " + StringToWString(filename);
			DisplayException::DisplayError(str.c_str());
			return false;
		}

		unsigned int filesize;
		fread_s(&filesize, sizeof(unsigned int), sizeof(unsigned int), 1, fp);
		data.fileSize_ = filesize;

		unsigned char* raw;
		unsigned int cursor = 0;
		raw = new unsigned char[filesize];
		fread_s(raw, sizeof(unsigned char) * filesize, sizeof(unsigned char), filesize, fp);
		fclose(fp);

		// RIFF識別子チェック
		char riffidtf[4];
		std::copy(&raw[cursor], &raw[cursor + 4], riffidtf);
		if (riffidtf[0] != 'W' || riffidtf[1] != 'A' || riffidtf[2] != 'V' || riffidtf[3] != 'E')
		{
			std::wstring str = L"Oops!\n WAVE Identifier is not found in " + StringToWString(filename);
			DisplayException::DisplayError(str.c_str());
			return false;
		}
		cursor += 4;

		if (!SeekToFourCC(raw, fmttag, cursor, filesize))
		{
			std::wstring str = L"Oops!\n fmt Identifier is not found in " + StringToWString(filename);
			DisplayException::DisplayError(str.c_str());
			return false;
		}

		std::copy(reinterpret_cast<FmtDesc*>(&raw[cursor]),
			reinterpret_cast<FmtDesc*>(&raw[cursor + sizeof(FmtDesc)]), &data.fmt_);
		cursor += sizeof(data.fmt_);

		if (!SeekToFourCC(raw, datatag, cursor, filesize))
		{
			std::wstring str = L"Oops!\n data Identifier is not found in " + StringToWString(filename);
			DisplayException::DisplayError(str.c_str());
			return false;
		}

		std::copy(reinterpret_cast<unsigned int*>(&raw[cursor]),
			reinterpret_cast<unsigned int*>(&raw[cursor + 4]), &data.dataSize_);
		cursor += sizeof(data.dataSize_);

		if (cursor + data.dataSize_ > filesize)
		{
			std::wstring str = L"Oops!\n data size is not length enough in " + StringToWString(filename);
			DisplayException::DisplayError(str.c_str());
			return false;
		}
		data.data_ = new unsigned char[data.dataSize_];
		std::copy_n(&raw[cursor], data.dataSize_, data.data_);

		wav_.emplace(filename, data);
		delete[] raw;
	}
	catch(std::bad_alloc)
	{
		DisplayException::DisplayError(L"Oops!\n Not enough memory :(");
		return false;
	}
	catch (...)
	{
		std::wstring str = L"Oops!\n Some happens in " + StringToWString(filename);
		DisplayException::DisplayError(str.c_str());
		return false;
	}
	return true;
}

const WAVData& WAVLoader::GetWAVFile(const std::string& filename)
{
	if (wav_.find(filename) == wav_.end())
	{
		OutputDebugString(L"wav file not load");
		return WAVData();
	}
	return wav_.at(filename);
}

void WAVLoader::DestroyWAVFile(const std::string& filename)
{
	if (wav_.find(filename) == wav_.end()) { return; }
	delete[] wav_.at(filename).data_;

	wav_.erase(filename);
}
