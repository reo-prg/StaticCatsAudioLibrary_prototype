#include "CreateEffect.h"
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <xapofx.h>
#include "../AudioManager.h"

void CreateEffect::GenerateEffectInstance(EffectParams& param, AudioEffectType type, unsigned int channel)
{
	switch (type)
	{
	case AudioEffectType::Reverb:
		CreateEffect::CreateReverb(param);
		break;
	case AudioEffectType::VolumeMeter:
		CreateEffect::CreateVolumeMeter(param, channel);
		break;
	case AudioEffectType::Echo:
		CreateEffect::CreateEcho(param);
		break;
	case AudioEffectType::Equalizer:
		CreateEffect::CreateEq(param);
		break;
	case AudioEffectType::MasteringLimiter:
		CreateEffect::CreateMasteringLimiter(param);
		break;
	case AudioEffectType::FXReverb:
		CreateEffect::CreateFXReverb(param);
		break;
	default:
		break;
	}
}

void CreateEffect::CreateReverb(EffectParams& param)
{
	XAudio2CreateReverb(&param.pEffect_);
}

void CreateEffect::CreateVolumeMeter(EffectParams& param, unsigned int channel)
{
	XAudio2CreateVolumeMeter(&param.pEffect_);

	XAUDIO2FX_VOLUMEMETER_LEVELS* level = new XAUDIO2FX_VOLUMEMETER_LEVELS();

	level->pPeakLevels = new float[channel];
	level->pRMSLevels = new float[channel];
	level->ChannelCount = channel;
	param.param_ = level;
}

void CreateEffect::CreateEcho(EffectParams& param)
{
	CreateFX(__uuidof(FXEcho), &param.pEffect_);
}

void CreateEffect::CreateEq(EffectParams& param)
{
	CreateFX(__uuidof(FXEQ), &param.pEffect_);
}

void CreateEffect::CreateMasteringLimiter(EffectParams& param)
{
	CreateFX(__uuidof(FXMasteringLimiter), &param.pEffect_);
}

void CreateEffect::CreateFXReverb(EffectParams& param)
{
	CreateFX(__uuidof(FXReverb), &param.pEffect_);
}
