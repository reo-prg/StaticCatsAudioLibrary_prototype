#pragma once
#include "../EffectDefines.h"

struct EffectParams;
class CreateEffect
{
public:
	static void GenerateEffectInstance(EffectParams& param, AudioEffectType type, unsigned int channel);
private:
	static void CreateReverb(EffectParams& param);
	static void CreateVolumeMeter(EffectParams& param, unsigned int channel);
	static void CreateEcho(EffectParams& param);
	static void CreateEq(EffectParams& param);
	static void CreateMasteringLimiter(EffectParams& param);
	static void CreateFXReverb(EffectParams& param);

};

