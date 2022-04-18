#include "AudioManager.h"
#include <cassert>
#include <algorithm>
#include <xaudio2fx.h>
#include <xapofx.h>
#include <xapo.h>
#include "WAVLoader.h"
#include "../Utility/utility.h"
#include "Effect/CreateEffect.h"

#pragma comment(lib,"xaudio2.lib")
#pragma comment(lib,"xapobase.lib")

AudioManager* AudioManager::instance_ = nullptr;

void AudioManager::Create(void)
{
	instance_ = new AudioManager();
}

AudioManager& AudioManager::GetInstance(void)
{
	return *instance_;
}

void AudioManager::Terminate(void)
{
	delete instance_;
}

void AudioManager::LoadSound(const std::string& filename, const std::string& key)
{
	std::string ext = GetExtension(filename);

	if (ext == "wav")
	{
		if (!wavLoader_->LoadWAVFile(filename))
		{
			return;
		}
	}
	else if (ext.empty())
	{
		if (!wavLoader_->LoadWAVFile(filename))
		{
			return;
		}
	}
	else
	{
		return;
	}
	filenameTable_.emplace(key, filename);
}

int AudioManager::CreateSubmix(std::initializer_list<int> outputHandles)
{
	SubmixVoice* subdata = new SubmixVoice;

	if (outputHandles.size() == 0u)
	{
		outputHandles = { RootSubmixHandle };
	}

	unsigned int stage = INT_MAX;

	for (auto& oh : outputHandles)
	{
		if ((oh & IdentifyMask) != SubmixIdentifyID) { delete subdata; return -1; }
		int h = (oh & SubmixHandleMask) >> SubmixHandleShift;
		if (h < 0 || h >= SubmixVoiceArrayMaxSize) { delete subdata; return -1; }
		if (!submix_[h]) { delete subdata; return -1; }

		stage = std::min(submix_[h]->stage_, stage);
	}

	if (stage == 0)
	{
		delete subdata; 
		return -1;
	}
	stage--;

	xaudioCore_->CreateSubmixVoice(&subdata->submixVoice_, masterVoiceDetails_.InputChannels,
		masterVoiceDetails_.InputSampleRate, XAUDIO2_VOICE_USEFILTER, stage, nullptr, nullptr);

	subdata->stage_ = stage;

	int index = submix_.Add(subdata);
	if (index == -1)
	{
		delete subdata;
		return -1;
	}

	subdata->handle_ = index;

	index = (index << SubmixHandleShift) + SubmixIdentifyID;

	for (auto& oh : outputHandles)
	{
		int h = (oh & SubmixHandleMask) >> SubmixHandleShift;
		AddSubmixOutputTarget(index, h);
	}

	return index;
}

int AudioManager::Play(const std::string& key, float volume)
{
	if (filenameTable_.find(key) == filenameTable_.end())
	{
		OutputDebugString(L"key not found");
		return -1;
	}

	HRESULT result;

	SourceVoice* srcdata = new SourceVoice();

	const auto& data = wavLoader_->GetWAVFile(filenameTable_.at(key));

	srcdata->waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
	srcdata->waveFormat_.nChannels = data.fmt_.channel_;
	srcdata->waveFormat_.nSamplesPerSec = data.fmt_.samplesPerSec_;
	srcdata->waveFormat_.nAvgBytesPerSec = data.fmt_.bytePerSec_;
	srcdata->waveFormat_.nBlockAlign = data.fmt_.blockAlign_;
	srcdata->waveFormat_.wBitsPerSample = data.fmt_.bitPerSample_;

	srcdata->buffer_.AudioBytes = data.dataSize_;
	srcdata->buffer_.pAudioData = data.data_;
	srcdata->buffer_.PlayBegin = 0;
	srcdata->buffer_.PlayLength = 0;
	srcdata->buffer_.LoopBegin = 0;
	srcdata->buffer_.LoopCount = 0;
	srcdata->buffer_.LoopLength = 0;
	srcdata->buffer_.Flags = XAUDIO2_END_OF_STREAM;

	result = xaudioCore_->CreateSourceVoice(&srcdata->sourceVoice_, &srcdata->waveFormat_,
		XAUDIO2_VOICE_USEFILTER, 4.0f);
	if (FAILED(result)) { delete srcdata; return -1; }

	result = srcdata->sourceVoice_->SubmitSourceBuffer(&srcdata->buffer_);
	if (FAILED(result)) { delete srcdata; return -1; }

	srcdata->vState_ = VoiceState::Playing;
	srcdata->sourceVoice_->Start();
	srcdata->sourceVoice_->SetVolume(volume);

	int index = source_.Add(srcdata);
	if (index == -1)
	{
		delete srcdata;
		return index;
	}

	srcdata->handle_ = index;

	AddSourceOutputTarget(index + SourceIdentifyID, RootSubmixHandle);

	return index + SourceIdentifyID;
}

int AudioManager::PlayLoop(const std::string& key, float begin, 
	float length, unsigned int loopCount, float volume)
{
	if (filenameTable_.find(key) == filenameTable_.end())
	{
		OutputDebugString(L"key not found");
		return -1;
	}

	HRESULT result;

	SourceVoice* sdata = new SourceVoice();

	const auto& data = wavLoader_->GetWAVFile(filenameTable_.at(key));

	sdata->waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
	sdata->waveFormat_.nChannels = data.fmt_.channel_;
	sdata->waveFormat_.nSamplesPerSec = data.fmt_.samplesPerSec_;
	sdata->waveFormat_.nAvgBytesPerSec = data.fmt_.bytePerSec_;
	sdata->waveFormat_.nBlockAlign = data.fmt_.blockAlign_;
	sdata->waveFormat_.wBitsPerSample = data.fmt_.bitPerSample_;

	sdata->buffer_.AudioBytes = data.dataSize_;
	sdata->buffer_.pAudioData = data.data_;
	sdata->buffer_.PlayBegin = sdata->waveFormat_.nSamplesPerSec * begin;
	sdata->buffer_.PlayLength = sdata->waveFormat_.nSamplesPerSec * length;
	sdata->buffer_.LoopBegin = sdata->waveFormat_.nSamplesPerSec * begin;
	sdata->buffer_.LoopLength = sdata->waveFormat_.nSamplesPerSec * length;
	sdata->buffer_.LoopCount = loopCount;
	sdata->buffer_.Flags = XAUDIO2_END_OF_STREAM;

	result = xaudioCore_->CreateSourceVoice(&sdata->sourceVoice_, &sdata->waveFormat_, 
		XAUDIO2_VOICE_USEFILTER, 4.0f);
	if (FAILED(result)) { return -1; }

	result = sdata->sourceVoice_->SubmitSourceBuffer(&sdata->buffer_);
	if (FAILED(result)) { return -1; }

	sdata->vState_ = VoiceState::Playing;
	sdata->sourceVoice_->Start();
	sdata->sourceVoice_->SetVolume(volume);

	int index = source_.Add(sdata);
	if (index == -1)
	{
		delete sdata;
		return index;
	}

	sdata->handle_ = index;

	AddSourceOutputTarget(index + SourceIdentifyID, RootSubmixHandle);

	return index + SourceIdentifyID;
}

void AudioManager::PlayAgain(int handle)
{
	if (!SourceHandleIsValid(handle)) { return; }
	handle = handle & SourceHandleMask;

	auto& src = source_[handle];

	if (src->vState_ == VoiceState::Playing)
	{
		src->vState_ = VoiceState::Stop;
		src->sourceVoice_->Stop();
	}
	source_[handle]->sourceVoice_->FlushSourceBuffers();
	src->sourceVoice_->SubmitSourceBuffer(&src->buffer_);
}

void AudioManager::PlayAgain(int handle, float begin, float length)
{
	if (!SourceHandleIsValid(handle)) { return; }
	handle = handle & SourceHandleMask;

	auto& src = source_[handle];

	if (src->vState_ == VoiceState::Playing)
	{
		src->vState_ = VoiceState::Stop;
		src->sourceVoice_->Stop();
	}
	src->sourceVoice_->FlushSourceBuffers();

	src->buffer_.PlayBegin = src->waveFormat_.nSamplesPerSec * begin;
	src->buffer_.PlayLength = src->waveFormat_.nSamplesPerSec * length;

	src->sourceVoice_->SubmitSourceBuffer(&src->buffer_);
}

float AudioManager::GetProgress(int sourceHandle)
{
	if (!SourceHandleIsValid(sourceHandle)) { return 0.0f; }
	sourceHandle = sourceHandle & SourceHandleMask;
	auto& src = source_[sourceHandle];

	if (src->vState_ == VoiceState::Stop) { return 1.0f; }

	XAUDIO2_VOICE_STATE state;
	src->sourceVoice_->GetState(&state, 0);

	return (static_cast<float>(state.SamplesPlayed) / static_cast<float>(src->waveFormat_.nSamplesPerSec)) 
		/ (static_cast<float>(src->buffer_.AudioBytes) / static_cast<float>(src->waveFormat_.nAvgBytesPerSec));
}

void AudioManager::SetVolume(int handle, float volume)
{
	if (handle < 0) { return; }

	volume = std::clamp(volume, -XAUDIO2_MAX_VOLUME_LEVEL, XAUDIO2_MAX_VOLUME_LEVEL);

	int id = handle & IdentifyMask;
	if (id == SourceIdentifyID)
	{
		if (handle >= SourceVoiceArrayMaxSize) { return; }
		if (source_[handle])
		{
			source_[handle]->sourceVoice_->SetVolume(volume);
		}
	}
	else if (id == SubmixIdentifyID)
	{
		handle = (handle & SubmixHandleMask) >> SubmixHandleShift;
		if (handle >= SubmixVoiceArrayMaxSize) { return; }
		if (submix_[handle])
		{
			submix_[handle]->submixVoice_->SetVolume(volume);
		}
	}
}

void AudioManager::Continue(int handle)
{
	if (!SourceHandleIsValid(handle)) { return; }

	handle = handle & SourceHandleMask;

	if (source_[handle]->vState_ == VoiceState::Playing) { return; }

	source_[handle]->sourceVoice_->Start();
	source_[handle]->vState_ = VoiceState::Playing;
}

void AudioManager::Stop(int handle)
{
	if (!SourceHandleIsValid(handle)) { return; }

	handle = handle & SourceHandleMask;

	if (source_[handle]->vState_ == VoiceState::Stop) { return; }

	source_[handle]->sourceVoice_->Stop();
	source_[handle]->vState_ = VoiceState::Stop;
}

void AudioManager::Unload(const std::string& key)
{
	if (filenameTable_.find(key) == filenameTable_.end()) { return; }

	std::string ext = GetExtension(filenameTable_.at(key));

	if (ext == "wav")
	{
		wavLoader_->DestroyWAVFile(filenameTable_.at(key));
	}
	filenameTable_.erase(key);
}

void AudioManager::ContinueAll(void)
{
	for (const auto& h : source_.GetHandleList())
	{
		if (source_[h]->vState_ == VoiceState::Playing) { continue; }
		source_[h]->sourceVoice_->Start();
		source_[h]->vState_ = VoiceState::Playing;
	}
}

void AudioManager::StopAll(bool destroy)
{
	for (auto& h : source_.GetHandleList())
	{
		if (source_[h]->vState_ != VoiceState::Stop)
		{
			source_[h]->sourceVoice_->Stop();
		}
	}
	if (destroy)
	{
		source_.Clear();
	}
}

void AudioManager::DeleteHandle(int handle)
{
	if (handle < 0) { return; }

	int id = handle & IdentifyMask;

	if (id == SourceIdentifyID)
	{
		int dh = handle & SourceHandleMask;
		if (dh < SourceVoiceArrayMaxSize)
		{
			if (!source_[dh]) { return; }
			for (auto& s : source_[dh]->output_)
			{
				auto it = std::remove_if(s->sources_.begin(), s->sources_.end(),
					[&dh, this](SourceVoice* src) { return src == source_[dh].get(); });
				s->sources_.erase(it, s->sources_.end());

				RemoveSourceOutputTarget(handle, 
					(s->handle_ << SubmixHandleShift) + SubmixIdentifyID);
			}
			source_.Remove(dh);
		}
		return;
	}
	else if (id == SubmixIdentifyID)
	{
		int dh = (handle & SubmixHandleMask) >> SubmixHandleShift;
		if (dh > 0 && dh < SubmixVoiceArrayMaxSize)
		{
			if (!submix_[dh]) { return; }
			for (auto& o : submix_[dh]->output_)
			{
				auto oit = std::remove_if(o->input_.begin(), o->input_.end(),
					[&dh, this](SubmixVoice* sub) { return sub == submix_[dh].get(); });
				o->input_.erase(oit, o->input_.end());
			}

			for (auto& i : submix_[dh]->output_)
			{
				RemoveSubmixOutputTarget((i->handle_ << SubmixHandleShift) + SubmixIdentifyID, 
					handle);
			}

			for (auto& s : submix_[dh]->sources_)
			{
				RemoveSourceOutputTarget(s->handle_ + SourceIdentifyID, 
					handle);
			}

			submix_.Remove(dh);
		}
		return;
	}
}

void AudioManager::Update(void)
{
	for (auto& s : source_.GetHandleList())
	{
		XAUDIO2_VOICE_STATE state;
		source_[s]->sourceVoice_->GetState(&state, 0);
		if (state.BuffersQueued == 0 && source_[s]->vState_ == VoiceState::Playing)
		{
			source_[s]->sourceVoice_->Stop();
			source_[s]->vState_ = VoiceState::Stop;
		}
	}
}

void AudioManager::AddSourceOutputTarget(int sourceHandle, int targetHandle)
{
	if ((sourceHandle & IdentifyMask) != SourceIdentifyID) { return; }
	if ((targetHandle & IdentifyMask) != SubmixIdentifyID) { return; }

	int sHandle = sourceHandle & SourceHandleMask;
	if (sHandle < 0 || sHandle > SourceVoiceArrayMaxSize) { return; }
	int tHandle = (targetHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (tHandle < 0 || tHandle > SubmixVoiceArrayMaxSize) { return; }

	auto& src = source_[sHandle];
	auto& tgt = submix_[tHandle];

	if (!src || !tgt)
	{
		return;
	}

	if (targetHandle != RootSubmixHandle)
	{
		RemoveSourceOutputTarget(sourceHandle, RootSubmixHandle);
	}

	src->send_.emplace_back(XAUDIO2_SEND_DESCRIPTOR{ 0, tgt->submixVoice_ });
	src->output_.emplace_back(tgt.get());
	tgt->sources_.emplace_back(src.get());

	XAUDIO2_VOICE_SENDS snd = { src->send_.size(), src->send_.data() };
	src->sourceVoice_->SetOutputVoices(&snd);
}

void AudioManager::AddSubmixOutputTarget(int submixHandle, int targetHandle)
{
	if ((submixHandle & IdentifyMask) != SubmixIdentifyID) { return; }
	if ((targetHandle & IdentifyMask) != SubmixIdentifyID) { return; }

	int sHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (sHandle < 0 || sHandle > SubmixVoiceArrayMaxSize) { return; }
	int tHandle = (targetHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (tHandle < 0 || tHandle > SubmixVoiceArrayMaxSize) { return; }

	if (sHandle == tHandle) { return; }
	auto& sub = submix_[sHandle];
	auto& tgt = submix_[tHandle];

	if (!sub || !tgt)
	{
		return;
	}

	if (targetHandle != RootSubmixHandle)
	{
		RemoveSourceOutputTarget(submixHandle,
			RootSubmixHandle);
	}

	sub->send_.emplace_back(XAUDIO2_SEND_DESCRIPTOR{ 0, tgt->submixVoice_ });
	sub->output_.emplace_back(tgt.get());
	tgt->input_.emplace_back(sub.get());

	XAUDIO2_VOICE_SENDS snd = 
	{ sub->send_.size(), 
		sub->send_.data() };

	sub->submixVoice_->SetOutputVoices(&snd);
}

void AudioManager::RemoveSourceOutputTarget(int sourceHandle, int targetHandle)
{
	if ((sourceHandle & IdentifyMask) != SourceIdentifyID) { return; }
	if ((targetHandle & IdentifyMask) != SubmixIdentifyID) { return; }

	int sHandle = sourceHandle & SourceHandleMask;
	if (sHandle < 0 || sHandle > SourceVoiceArrayMaxSize) { return; }
	int tHandle = (targetHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (tHandle < 0 || tHandle > SubmixVoiceArrayMaxSize) { return; }

	auto& src = source_[sHandle];
	auto& tgt = submix_[tHandle];

	if (!src || !tgt)
	{
		return;
	}

	auto it1 = std::remove_if(src->output_.begin(), src->output_.end(),
		[&tgt](SubmixVoice* sub) { return sub == tgt.get(); });

	// not found
	if (it1 == src->output_.end()) { return; }

	src->output_.erase(it1, src->output_.end());

	auto it2 = std::remove_if(src->send_.begin(), src->send_.end(),
		[&tgt](XAUDIO2_SEND_DESCRIPTOR& desc)
		{ return desc.pOutputVoice == tgt->submixVoice_; });
	src->send_.erase(it2, src->send_.end());

	auto it3 = std::remove_if(tgt->sources_.begin(), tgt->sources_.end(),
		[&src](SourceVoice* sr) { return sr == src.get(); });
	tgt->sources_.erase(it3, tgt->sources_.end());

	if (src->send_.size() == 0)
	{
		AddSourceOutputTarget(sourceHandle, RootSubmixHandle);
	}
	XAUDIO2_VOICE_SENDS snd = { src->send_.size(), src->send_.data() };
	src->sourceVoice_->SetOutputVoices(&snd);
}

void AudioManager::RemoveSubmixOutputTarget(int submixHandle, int targetHandle)
{
	if ((submixHandle & IdentifyMask) != SubmixIdentifyID) { return; }
	if ((targetHandle & IdentifyMask) != SubmixIdentifyID) { return; }

	int sHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (sHandle < 0 || sHandle > SubmixVoiceArrayMaxSize) { return; }
	int tHandle = (targetHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (tHandle < 0 || tHandle > SubmixVoiceArrayMaxSize) { return; }

	if (sHandle == tHandle) { return; }
	auto& sub = submix_[sHandle];
	auto& tgt = submix_[tHandle];

	if (!sub || !tgt)
	{
		return;
	}

	auto it1 = std::remove_if(sub->output_.begin(), sub->output_.end(),
		[&tgt](SubmixVoice* sub) { return sub == tgt.get(); });

	// not found
	if (it1 == sub->output_.end()) { return; }
	
	sub->output_.erase(it1, sub->output_.end());

	auto it2 = std::remove_if(sub->send_.begin(), sub->send_.end(),
		[&tgt](XAUDIO2_SEND_DESCRIPTOR& desc)
		{ return desc.pOutputVoice == tgt->submixVoice_; });
	sub->send_.erase(it2, sub->send_.end());

	auto it3 = std::remove_if(tgt->input_.begin(), tgt->input_.end(),
		[&sub](SubmixVoice* sr) { return sr == sub.get(); });
	tgt->input_.erase(it3, tgt->input_.end());

	if (sub->send_.size() == 0)
	{
		AddSubmixOutputTarget(submixHandle, RootSubmixHandle);
	}
	XAUDIO2_VOICE_SENDS snd = { sub->send_.size(), sub->send_.data() };
	sub->submixVoice_->SetOutputVoices(&snd);
}

void AudioManager::SetFilter(int handle, XAUDIO2_FILTER_TYPE type, float frequency, float danping)
{
	frequency = std::clamp(frequency, 0.0f, XAUDIO2_MAX_FILTER_FREQUENCY);
	danping = std::clamp(danping, 0.0f, XAUDIO2_MAX_FILTER_ONEOVERQ);
	XAUDIO2_FILTER_PARAMETERS filter_ = { type, frequency, danping };
	
	int id = handle & IdentifyMask;
	if (id == SourceIdentifyID)
	{
		handle = handle & SourceHandleMask;
		if (handle < 0 || handle >= SourceVoiceArrayMaxSize) { return; }
		if (!source_[handle]) { return; }
		source_[handle]->sourceVoice_->SetFilterParameters(&filter_);
	}
	else if (id == SubmixIdentifyID)
	{
		handle = (handle & SubmixHandleMask) >> SubmixHandleShift;
		if (handle < 0 || handle >= SubmixVoiceArrayMaxSize) { return; }
		if (!submix_[handle]) { return; }
		submix_[handle]->submixVoice_->SetFilterParameters(&filter_);
	}
}

int AudioManager::AddEffect(int handle, AudioEffectType type, bool active, int insertPosition)
{
	if (!SubmixHandleIsValid(handle)) { return -1; }
	int hd = (handle & SubmixHandleMask) >> SubmixHandleShift;

	EffectParams param = {};

	CreateEffect::GenerateEffectInstance(param, type, masterVoiceDetails_.InputChannels);

	param.type_ = type;

	if (insertPosition == -1)
	{
		submix_[hd]->efkDesc_.emplace_back(XAUDIO2_EFFECT_DESCRIPTOR
			{ param.pEffect_, active, masterVoiceDetails_.InputChannels });
		submix_[hd]->efkParam_.emplace_back(param);
		insertPosition = submix_[hd]->efkDesc_.size() - 1;
	}
	else
	{
		submix_[hd]->efkDesc_.emplace(submix_[hd]->efkDesc_.begin() + insertPosition,
			XAUDIO2_EFFECT_DESCRIPTOR{ param.pEffect_, active,
			masterVoiceDetails_.InputChannels });
		submix_[hd]->efkParam_.emplace(submix_[hd]->efkParam_.begin(), param);
	}
	XAUDIO2_EFFECT_CHAIN chain = { submix_[hd]->efkDesc_.size(),
		submix_[hd]->efkDesc_.data() };

	submix_[hd]->submixVoice_->SetEffectChain(nullptr);
	submix_[hd]->submixVoice_->SetEffectChain(&chain);

	return insertPosition;
}

void AudioManager::SetReverbParameter(const XAUDIO2FX_REVERB_I3DL2_PARAMETERS& param, int submixHandle, int effectIndex)
{
	XAUDIO2FX_REVERB_PARAMETERS p = {};
	ReverbConvertI3DL2ToNative(&param, &p);
	SetReverbParameter(p, submixHandle, effectIndex);
}

void AudioManager::SetReverbParameter(const XAUDIO2FX_REVERB_PARAMETERS& param, int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= submix_[submixHandle]->efkDesc_.size()) { return; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::Reverb);
		if (effectIndex < 0) { return; }
	}

	HRESULT result;

	result = submix_[submixHandle]->submixVoice_->
		SetEffectParameters(effectIndex, &param, sizeof(param));
	if (FAILED(result)) { OutputDebugStringA("SetEffectParameter is failed\n"); }
}

void AudioManager::SetEchoParameter(float strength, float delay, float reverb, int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= submix_[submixHandle]->efkDesc_.size()) { return; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::Echo);
		if (effectIndex < 0) { return; }
	}

	HRESULT result;

	FXECHO_PARAMETERS param = { strength, delay, reverb };
	result = submix_[submixHandle]->submixVoice_->
		SetEffectParameters(effectIndex, &param, sizeof(param));
	if (FAILED(result)) { OutputDebugStringA("SetEffectParameter is failed\n"); }
}

void AudioManager::SetEqualizerParameter(const FXEQ_PARAMETERS& param, int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= submix_[submixHandle]->efkDesc_.size()) { return; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::Equalizer);
		if (effectIndex < 0) { return; }
	}

	HRESULT result;
	result = submix_[submixHandle]->submixVoice_->
		SetEffectParameters(effectIndex, &param, sizeof(param));
	if (FAILED(result)) { OutputDebugStringA("SetEffectParameter is failed\n"); }
}

void AudioManager::SetMasteringLimiterParameter(int release, float loudness, int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= submix_[submixHandle]->efkDesc_.size()) { return; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::MasteringLimiter);
		if (effectIndex < 0) { return; }
	}

	HRESULT result;

	FXMASTERINGLIMITER_PARAMETERS param = { release, loudness };
	result = submix_[submixHandle]->submixVoice_->
		SetEffectParameters(effectIndex, &param, sizeof(param));
	if (FAILED(result)) { OutputDebugStringA("SetEffectParameter is failed\n"); }
}

void AudioManager::SetFXReverbParameter(float diffuse, float roomsize, int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= static_cast<int>(submix_[submixHandle]->efkDesc_.size())) { return; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::FXReverb);
		if (effectIndex < 0) { return; }
	}

	HRESULT result;

	FXREVERB_PARAMETERS param = { diffuse, roomsize };
	result = submix_[submixHandle]->submixVoice_->
		SetEffectParameters(effectIndex, &param, sizeof(param));
	if (FAILED(result)) 
	{ 
		OutputDebugStringA("SetEffectParameter is failed\n");
	}
}

XAUDIO2FX_VOLUMEMETER_LEVELS* AudioManager::GetVolumeMeterParameter(int submixHandle, int effectIndex)
{
	if (!SubmixHandleIsValid(submixHandle)) { return nullptr; }
	submixHandle = (submixHandle & SubmixHandleMask) >> SubmixHandleShift;
	if (effectIndex >= submix_[submixHandle]->efkDesc_.size()) { return nullptr; }

	if (effectIndex < 0)
	{
		effectIndex = FindEffect(submixHandle, AudioEffectType::FXReverb);
		if (effectIndex < 0) { return nullptr; }
	}

	auto& sub = submix_[submixHandle];
	sub->submixVoice_->GetEffectParameters(effectIndex, sub->efkParam_[effectIndex].param_,
		sizeof(XAUDIO2FX_VOLUMEMETER_LEVELS));
	return reinterpret_cast<XAUDIO2FX_VOLUMEMETER_LEVELS*>(sub->efkParam_[effectIndex].param_);
}

AudioManager::AudioManager():xaudioCore_(nullptr), masterVoice_(nullptr)
{
	Initialize();
}

AudioManager::~AudioManager()
{
	//for (auto& src : sources_)
	//{
	//	src.reset();
	//}

	//for (auto& s : submixs_)
	//{
	//	s.second->submixVoice_->DestroyVoice();
	//}
	//submixs_.clear();

	source_.Clear();
	submix_.Clear();

	if (masterVoice_)
	{
		masterVoice_->DestroyVoice();
		masterVoice_ = nullptr;
	}
	
	xaudioCore_->Release();
}

void AudioManager::Initialize(void)
{
	HRESULT result;

	wavLoader_.reset(new WAVLoader());
	
	// IXAudio2オブジェクトの作成
	result = XAudio2Create(&xaudioCore_);
	assert(SUCCEEDED(result));

	// デバッグの設定
#ifdef _DEBUG
	XAUDIO2_DEBUG_CONFIGURATION debugc = {};
	debugc.TraceMask = XAUDIO2_LOG_WARNINGS;
	debugc.BreakMask = XAUDIO2_LOG_ERRORS;
	xaudioCore_->SetDebugConfiguration(&debugc);
#endif

	// マスタリングボイスの作成
	result = xaudioCore_->CreateMasteringVoice(&masterVoice_, XAUDIO2_DEFAULT_CHANNELS,
		XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, nullptr);
	assert(SUCCEEDED(result));

	masterVoice_->GetVoiceDetails(&masterVoiceDetails_);


	SubmixVoice* sm = new SubmixVoice();
	result = xaudioCore_->CreateSubmixVoice(&sm->submixVoice_, masterVoiceDetails_.InputChannels,
		masterVoiceDetails_.InputSampleRate, XAUDIO2_VOICE_USEFILTER, RootProcessingStage, nullptr, nullptr);
	assert(SUCCEEDED(result));
	int hd = submix_.Add(sm);
	sm->handle_ = hd;
	sm->stage_ = RootProcessingStage;
}

bool AudioManager::SourceHandleIsValid(int handle)
{
	if ((handle & IdentifyMask) != SourceIdentifyID) { return false; }
	handle = handle & SourceHandleMask;
	if (handle < 0 || handle >= SourceVoiceArrayMaxSize) { return false; }
	if (!source_[handle]) { return false; }
	return true;
}

bool AudioManager::SubmixHandleIsValid(int handle)
{
	if ((handle & IdentifyMask) != SubmixIdentifyID) { return false; }
	handle = (handle & SubmixHandleMask) >> SubmixHandleShift;
	if (handle < 0 || handle >= SubmixVoiceArrayMaxSize) { return false; }
	if (!submix_[handle]) { return false; }

	return true;
}

int AudioManager::FindEffect(int handle, AudioEffectType type)
{
	auto& p = submix_[handle]->efkParam_;
	int ret;
	for (ret = p.size() - 1; ret >= 0; ret--)
	{
		if (p[ret].type_ == type)
		{
			break;
		}
	}

	if (ret < 0)
	{
		ret = -1;
	}

	return ret;
}
