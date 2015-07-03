#pragma once

#include "mplayer_includes.h"	
#include "SoundTouch.h"

namespace caspar { namespace mplayer {

#define AD_Msg(lvl, x)	{if (debugLevel >= lvl) {mlg(x)}}
#define AD_SampleType	float

#define AD_DEFAULT_frameSamples				(int)2016
#define AD_DEFAULT_frameTimeNominalMs		(int)50
#define AD_DEFAULT_minDropIntervalMs		(int)75
#define AD_DEFAULT_maxDropTimeMs			(int)10
#define AD_DEFAULT_borderLatencyMs			(int)0
#define AD_DEFAULT_IgnoreDropRestSamples	(int)64

struct AudioDroperCtorParams
{
	//if disable audio will be passed bypassing Audio Droper
	bool enable;

	//in Hz
	int samplingRate; 

	//count of audio channels
	int channels;

	//count of samples to change tempo per one time.
	//it should be aliquot to input samples count in 
	//feedSamples function
	int frameSamples;

	//parameter for AudioTouch library. It's almost matches to frameSamples
	int frameTimeNominalMs;

	//Min interval between changes tempo (counting for input samples)
	int minDropIntervalMs;

	//max audio length redution per time
	int maxDropTimeMs;

	//it should be the half of latency noise (usually about 100-200 samples)
	int borderLatencyMs;

	//Sometimes AudioTouch library can't drop some samples 
	//(up to 64 samples). to prevent perpetual tries of drop 
	//these samples may be ignored and droped later in next drop request
	int IgnoreDropRestSamples;

	//Debug mode to determine clean latency noise
	bool noDropMode;

	//debugLevel 0 - quiet (except start information in ctor)
	//debugLevel 1 - only say about drops
	//debugLevel 2 - every frame show latency diagnostics
	//debugLevel 3 - every frame show wide latency diagnostics
	//debugLevel 4 - wide info about drop
	//debugLevel 5 - additional info every frame
	int debugLevel;

	AudioDroperCtorParams()
		: enable(true)
		, samplingRate(SAudioSamplingRate)
		, channels(SAudioChannels)
		, frameSamples(AD_DEFAULT_frameSamples)
		, frameTimeNominalMs(AD_DEFAULT_frameTimeNominalMs)
		, minDropIntervalMs(AD_DEFAULT_minDropIntervalMs)
		, maxDropTimeMs(AD_DEFAULT_maxDropTimeMs)
		, borderLatencyMs(AD_DEFAULT_borderLatencyMs)
		, IgnoreDropRestSamples(AD_DEFAULT_IgnoreDropRestSamples)
		, noDropMode(false)
		, debugLevel(0)
	{}
};

class AudioDroper
{
private:
	soundtouch::SoundTouch soundTouch;

	//parameters
	const int samplingRate;
	const int channels;
	const int frameSamples;
	const int frameTimeNominalMs;
	const int minDropIntervalSamples;
	const int maxDropCountSamples;
	const int borderLatencySamples;
	const int IgnoreDropRestSamples;
	const bool noDropMode;
	const int bufferInSize;
	const int bufferOutSize;

	//buffers
	AD_SampleType *bufferIn;
	AD_SampleType *bufferOut;

	int32_t *bufferInInternal;
	int32_t *bufferOutInternal;
	int32_t *auxBuf;
	int bufferInInternalSamples;
	int bufferOutInternalSamples;

	//stuff
	long samplesElapsed;
	int latencySamples;
	int latencyNoiseInterval;
	int latMax;
	int latMin;
	int putCnt;
	long diagCnt;
	int lastPutSamples;
	int lastGetSamples;
	int state;
	int currentSeq;
	float currentTempo;
	void diag();
	void defSoundTouchParams(int inputSamplesCount, int diff);
	void setSoundTouchParams(float tempo, int seq);

	void feedSamplesInternal(int32_t *buffer, int samplesCount);
	int getSamplesInternal(int32_t *buffer, int maxSamplesCount);


public:
	AudioDroper(AudioDroperCtorParams params);
	~AudioDroper();
	
	void feedSamples(int32_t *buffer, int samplesCount);
	int getSamples(int32_t *buffer, int maxSamplesCount);
	void dropSamples(int samplesCount);

	int getLatencySamples() const {return latencySamples; } 
	int getLatencyNoiseInterval() const {return latencyNoiseInterval; } //getLatencyNoiseInterval is actual in noDropMode
	inline long SamplesToMs(long samples) const {return (1000 * samples) / samplingRate;}
	inline long MsToSamples(long ms) const {return ((ms * (long)samplingRate)/1000);}
	int getBuf() {return  bufferInInternalSamples;}

	int debugLevel;
};	

}}

