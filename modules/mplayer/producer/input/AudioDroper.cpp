#include "stdafx.h"
#include "AudioDroper.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <iterator>

namespace caspar { namespace mplayer {

using namespace std;
using namespace soundtouch;

AudioDroper::AudioDroper(AudioDroperCtorParams params)
	: samplingRate(params.samplingRate)
	, channels(params.channels)
	, frameSamples(params.frameSamples)
	, frameTimeNominalMs(params.frameTimeNominalMs)
	, minDropIntervalSamples(MsToSamples(params.minDropIntervalMs))
	, maxDropCountSamples(MsToSamples(params.maxDropTimeMs))
	, borderLatencySamples(MsToSamples(params.borderLatencyMs))
	, IgnoreDropRestSamples(params.IgnoreDropRestSamples)
	, noDropMode(params.noDropMode)
	, bufferInSize(params.frameSamples * params.channels)
	, bufferOutSize(params.frameSamples * params.channels * 10)
	, samplesElapsed(0)
	, latencySamples(0)
	, putCnt(0)
	, diagCnt(0)
	, lastPutSamples(0)
	, lastGetSamples(0)
	, state(0)
	, currentSeq(-1)
	, currentTempo(-1)
{
	debugLevel = params.debugLevel;

	soundTouch.setSampleRate(samplingRate);
	soundTouch.setChannels(channels);
	
	bufferIn = new AD_SampleType[bufferInSize];
	bufferOut = new AD_SampleType[bufferOutSize];
	

	bufferInInternal = new int32_t[bufferInSize*10];
	bufferOutInternal = new int32_t[bufferOutSize*10];
	auxBuf = new int32_t[bufferInSize*10];
	bufferInInternalSamples = 0;
	bufferOutInternalSamples = 0;

	

	AD_Msg(0, "AudioDroper parameters:" << "\n" <<
	"samplingRate: " << samplingRate << "\n" <<
	"channels: " << channels << "\n" << 
	"frameSamples: " << frameSamples << " (" << SamplesToMs(frameSamples) << " ms)" << "\n" <<
	"frameTimeNominalMs: " << frameTimeNominalMs << " (" << MsToSamples(frameTimeNominalMs) << " smpl)" << "\n" <<
	"minDropIntervalMs: " << params.minDropIntervalMs << " (" << MsToSamples(params.minDropIntervalMs) << " smpl)" << "\n" << 
	"maxDropTimeMs: " << params.maxDropTimeMs << " (" << MsToSamples(params.maxDropTimeMs) << " smpl)" << "\n" << 
	"borderLatencyMs: " << params.borderLatencyMs << " (" << MsToSamples(params.borderLatencyMs) << " smpl)" << "\n" << 
	"IgnoreDropRestSamples: " << IgnoreDropRestSamples << " (" << SamplesToMs(IgnoreDropRestSamples) << " ms)" << "\n" <<
	"noDropMode: " << noDropMode << "\n" <<
	"debugLevel: " << debugLevel << "\n"
	);

	setSoundTouchParams(1.0f, frameTimeNominalMs);
}

AudioDroper::~AudioDroper()
{
	delete[] bufferIn;
	delete[] bufferOut;

	delete[] bufferInInternal;
	delete[] bufferOutInternal;

	delete[] auxBuf;
}

enum AD_STATE
{
	ST_STD = 0,
	ST_ACCUMULATION,
	ST_WAIT
};

void AudioDroper::defSoundTouchParams(int inputSamplesCount, int diff)
{
	switch (state)
	{
	case ST_STD:	//std mode: wait for start dropping passing. Drop is allowed
		if ((diff > IgnoreDropRestSamples) && (!noDropMode))
		{
			int dropSamples = (diff > maxDropCountSamples) ? maxDropCountSamples : diff;
			int dropMs = SamplesToMs(dropSamples);
			if (dropMs == 0) dropMs = 1;
			float tempoScale = (float)frameSamples / float(frameSamples - dropSamples);
			int seqMs = dropMs < frameTimeNominalMs ? frameTimeNominalMs - dropMs : 1;

			setSoundTouchParams(tempoScale, seqMs);
			AD_Msg(1, "AudioDroper is trying to drop " << dropSamples << " samples");

			if (inputSamplesCount >= frameSamples)
			{
				if (minDropIntervalSamples) 
				{
					AD_Msg(4, "enough samples -> AudioDroper is going to ST_WAIT");
					samplesElapsed = 0;
					state = ST_WAIT;
				} else AD_Msg(4, "no minDropIntervalSamples -> AudioDroper will not wait");
			}
			else //need more samples
			{
				AD_Msg(4, "not enough samples -> AudioDroper is going to ST_ACCUMULATION");
				samplesElapsed = inputSamplesCount;
				state = ST_ACCUMULATION;
			}
		}
		else
		{
			AD_Msg(5, "AudioDroper: bypassing clean sound");
			setSoundTouchParams(1.0f, frameTimeNominalMs);
		}
		break;

	case ST_ACCUMULATION:	//Accumulation new samples in dropping passing mode
		samplesElapsed += inputSamplesCount;
		AD_Msg(4, "AudioDroper accumulation: samplesElapsed = " << samplesElapsed);

		if (samplesElapsed >= frameSamples)
		{
			if (minDropIntervalSamples) 
			{
				AD_Msg(4, "enough samples -> AudioDroper is going to ST_WAIT");
				samplesElapsed = 0;
				state = ST_WAIT;
			}
			else
			{
				AD_Msg(4, "no minDropIntervalSamples -> AudioDroper will not wait");
				state = ST_STD;
			}
		}	
		break;

	case ST_WAIT:	//Only clean passing. Drop is not allowed
		setSoundTouchParams(1.0f, frameTimeNominalMs);

		samplesElapsed += inputSamplesCount;
		AD_Msg(5, "AudioDroper wait: samplesElapsed = " << samplesElapsed);

		if (samplesElapsed >= minDropIntervalSamples)
		{
			AD_Msg(4, "AudioDroper is going to ST_STD");
			state = ST_STD;
		}
		break;
	}
}

void AudioDroper::setSoundTouchParams(float tempo, int seq)
{
	soundTouch.setSetting(SETTING_SEQUENCE_MS, seq);
	soundTouch.setTempo(tempo);	

	currentSeq = seq;
	currentTempo = tempo;
	AD_Msg(4, "setSoundTouchParams: tempo = " << tempo << "; seq = " << seq);

	/*if ((currentSeq != seq) || (currentTempo != tempo))
	{



	}*/
}

void AudioDroper::feedSamples(int32_t *buffer, int samplesCount)
{
	memcpy((void*)&bufferInInternal[bufferInInternalSamples*channels], buffer, samplesCount*channels*sizeof(int32_t));
	bufferInInternalSamples += samplesCount;

	//AD_Msg(4, "External feed " << samplesCount << ". Now there is " << bufferInInternalSamples << "smpl");
	
	if (bufferInInternalSamples >= frameSamples)
	{
		feedSamplesInternal(bufferInInternal, frameSamples);
	
		int readcnt = getSamplesInternal(&bufferOutInternal[bufferOutInternalSamples*channels], ((bufferOutSize*10) / channels));
		bufferOutInternalSamples += readcnt;

		int rest = 	bufferInInternalSamples - frameSamples;
		if (rest > 0)
		{
			memcpy(auxBuf, &bufferInInternal[frameSamples*channels], rest*channels*sizeof(int32_t));
			memcpy(bufferInInternal, auxBuf, rest*channels*sizeof(int32_t));
		}
		bufferInInternalSamples = rest;
	}
}

int AudioDroper::getSamples(int32_t *buffer, int maxSamplesCount)
{
	memcpy(buffer, bufferOutInternal, bufferOutInternalSamples*channels*sizeof(int32_t));
	int res = bufferOutInternalSamples;
	bufferOutInternalSamples = 0;
	return res;
}


void AudioDroper::feedSamplesInternal(int32_t *buffer, int samplesCount)
{
	lastPutSamples = samplesCount;
	int diff = -borderLatencySamples - latencySamples;

	AD_Msg(5, "_______________________________________");
	AD_Msg(5, "AudioDroper feed: inputCnt = " << samplesCount << "; lat = " << latencySamples << "; diff = " << diff);
	defSoundTouchParams(samplesCount, diff);
	

	

	for (int i = 0; i < samplesCount*channels; i++) 
	{
		bufferIn[i] = (AD_SampleType)buffer[i];
	}
	setSoundTouchParams(currentTempo, currentSeq);
	soundTouch.putSamples(bufferIn, samplesCount);
	latencySamples += samplesCount;

	if (++putCnt != 1)
		AD_Msg(0, "AudioDroper warning: uneven calls of feedSamples/getSamples. You have to call getSamples after each feedSamples");
}


int AudioDroper::getSamplesInternal(int32_t *buffer, int maxSamplesCount)
{
	int readCnt = (maxSamplesCount*channels > bufferOutSize) ? (bufferOutSize / channels) : maxSamplesCount;

	int nSamples = 0;
	int totalCnt = 0;
	do 
	{
		nSamples = soundTouch.receiveSamples(bufferOut, readCnt);

		for (int i = 0; i < nSamples*channels; i++)
			buffer[totalCnt*channels + i] = (int32_t)bufferOut[i];

		totalCnt += nSamples;
		
	} while ((nSamples != 0) && (totalCnt < maxSamplesCount));
	latencySamples -= totalCnt;
	lastGetSamples = totalCnt;
	
	if (--putCnt == 0) 
		diag();
	else
		AD_Msg(0, "AudioDroper warning: uneven calls of feedSamples/getSamples. You have to call getSamples after each feedSamples");

	return totalCnt;
}

#define DIAG_WAIT_NOISE_TIMES 100
void AudioDroper::diag()
{
	diagCnt++;

	AD_Msg(2, "AudioDroper frame: " << lastPutSamples << " -> " << lastGetSamples << " (latency = " << latencySamples << ")");

	if (diagCnt == DIAG_WAIT_NOISE_TIMES)
	{
		latMax = latencySamples;
		latMin = latencySamples;
		latencyNoiseInterval = 0;
	}
	else if (diagCnt > DIAG_WAIT_NOISE_TIMES)
	{
		if (latencySamples > latMax) latMax = latencySamples;
		if (latencySamples < latMin) latMin = latencySamples;
		latencyNoiseInterval = latMax - latMin;

		AD_Msg(3, "AudioDroper latency min = " << latMin << " max = " << latMax << " max-min = " << latencyNoiseInterval);
	}
}

void AudioDroper::dropSamples(int samplesCount)
{
	if (!noDropMode)
	{
		latencySamples -= samplesCount;
		AD_Msg(1, "AudioDroper: external command to drop " << samplesCount << " samples");
	}
}


}}
