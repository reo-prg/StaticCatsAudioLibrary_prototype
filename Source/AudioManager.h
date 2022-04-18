#pragma once
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <xapofx.h>
#include <array>
#include <initializer_list>
#include <list>
#include <string>
#include <memory>
#include <unordered_map>
#include "EffectDefines.h"
#include "../Utility/HandleArray.h"

#define AudioIns AudioManager::GetInstance()

constexpr size_t SourceVoiceArrayMaxSize = 1024;
constexpr size_t SubmixVoiceArrayMaxSize = 256;

constexpr int SourceHandleMask = 0x0000ffff;
constexpr int SubmixHandleMask = 0x00ff0000;

constexpr int IdentifyMask = 0x70000000;
constexpr int SourceIdentifyID = 0x10000000;
constexpr int SubmixIdentifyID = 0x20000000;

constexpr int SubmixHandleShift = 16;

constexpr int RootSubmixHandle = SubmixIdentifyID + 0;

constexpr unsigned int RootProcessingStage = 128;

struct SubmixVoice;
struct SourceVoice;

enum class VoiceState
{
	Playing,
	Stop,
};

class WAVLoader;
class AudioManager
{
public:
	static void Create();
	static AudioManager& GetInstance(void);
	static void Terminate(void);

	void LoadSound(const std::string& filename, const std::string& key);

	int CreateSubmix(std::initializer_list<int> outputHandles = { RootSubmixHandle });

	int Play(const std::string& key, float volume = 1.0f);
	int PlayLoop(const std::string& key, float begin, float length, unsigned int loopCount, float volume = 1.0f);
	void PlayAgain(int handle);
	void PlayAgain(int handle, float begin, float length);

	float GetProgress(int sourceHandle);
	
	void SetVolume(int handle, float volume);
	void Continue(int handle);
	void Stop(int handle);
	void Unload(const std::string& key);
	void ContinueAll(void);
	void StopAll(bool destroy);
	void DeleteHandle(int handle);

	void Update(void);

	void AddSourceOutputTarget(int sourceHandle, int targetHandle);
	void AddSubmixOutputTarget(int sourceHandle, int targetHandle);

	void RemoveSourceOutputTarget(int sourceHandle, int targetHandle);
	void RemoveSubmixOutputTarget(int submixHandle, int targetHandle);

	void SetFilter(int handle, XAUDIO2_FILTER_TYPE type, float frequency, float danping);

	int AddEffect(int handle, AudioEffectType type, bool active, int insertPosition = -1);

	void SetReverbParameter(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS& param, int submixHandle, int effectIndex = -1);
	void SetReverbParameter(const XAUDIO2FX_REVERB_PARAMETERS& param, int submixHandle, int effectIndex = -1);
	void SetEchoParameter(float strength, float delay, float reverb, int submixHandle, int effectIndex = -1);
	void SetEqualizerParameter(const FXEQ_PARAMETERS& param, int submixHandle, int effectIndex = -1);
	void SetMasteringLimiterParameter(int release, float loudness, int submixHandle, int effectIndex = -1);
	void SetFXReverbParameter(float diffuse, float roomsize, int submixHandle, int effectIndex = -1);

	XAUDIO2FX_VOLUMEMETER_LEVELS* GetVolumeMeterParameter(int submixHandle, int effectIndex = -1);
private:
	AudioManager();
	AudioManager(const AudioManager&) = delete;
	AudioManager operator=(const AudioManager&) = delete;
	~AudioManager();

	static AudioManager* instance_;

	void Initialize(void);

	bool SourceHandleIsValid(int handle);
	bool SubmixHandleIsValid(int handle);

	int FindEffect(int handle, AudioEffectType type);

	std::unique_ptr<WAVLoader> wavLoader_;

	IXAudio2* xaudioCore_;
	IXAudio2MasteringVoice* masterVoice_;
	XAUDIO2_VOICE_DETAILS masterVoiceDetails_ = {};

	std::unordered_map<std::string, std::string> filenameTable_;

	HandleArray<SourceVoice, SourceVoiceArrayMaxSize> source_;

	HandleArray<SubmixVoice, SubmixVoiceArrayMaxSize> submix_;
};

struct SourceVoice
{
	SourceVoice() = default;
	~SourceVoice()
	{
		if (sourceVoice_ != nullptr)
		{
			sourceVoice_->DestroyVoice();
		}
	}

	WAVEFORMATEX waveFormat_;
	XAUDIO2_BUFFER buffer_;
	IXAudio2SourceVoice* sourceVoice_ = nullptr;
	VoiceState vState_;

	int handle_;

	std::vector<XAUDIO2_SEND_DESCRIPTOR> send_;
	std::vector<SubmixVoice*> output_;
};

struct EffectParams
{
	~EffectParams()
	{
		if (type_ == AudioEffectType::VolumeMeter)
		{
			XAUDIO2FX_VOLUMEMETER_LEVELS* level = reinterpret_cast<XAUDIO2FX_VOLUMEMETER_LEVELS*>(param_);
			delete[] level->pPeakLevels;
			delete[] level->pRMSLevels;
			delete param_;

			pEffect_->Release();
		}
	}

	AudioEffectType type_;
	
	IUnknown* pEffect_;
	void* param_;
};

struct SubmixVoice
{
	SubmixVoice() = default;
	~SubmixVoice()
	{
		if (submixVoice_ != nullptr)
		{
			submixVoice_->DestroyVoice();
		}
	}

	IXAudio2SubmixVoice* submixVoice_ = nullptr;

	std::vector<XAUDIO2_SEND_DESCRIPTOR> send_;

	std::vector<SubmixVoice*> input_;
	std::vector<SubmixVoice*> output_;

	std::vector<SourceVoice*> sources_;

	std::vector<XAUDIO2_EFFECT_DESCRIPTOR> efkDesc_;
	std::vector<EffectParams> efkParam_;

	int handle_;
	unsigned int stage_;
};
