#pragma once

#include <vector>
#include <string>
#include <stdio.h>
#include <iostream>


using namespace std;
#define int16_t short
typedef vector<unsigned char> ByteArray;
typedef vector<int16_t> Frame;


//PCM, 16-bit, 44100 Hz.
//Stereo, but chanels are same
class MyWavFile
{
private:
    ByteArray StdWaveHeader;
	int spos;

public:
    MyWavFile();

    vector< int16_t > samples;

    bool fromData(ByteArray data, bool do_check = true);
    bool loadFromFile(string filename, bool do_check = true);

    ByteArray toData();
    bool saveToFile(string filename);

    void makeTestSine();

	
	void seek_samples(int n);
	int read_samples(float *buf, unsigned int smpl_count);
	int write_samples(float *buf, int smpl_count);
};
