#include "stdafx.h"
#include "mywavfile.h"
#include "sys/stat.h"
#include "math.h"
#include "fstream"

MyWavFile::MyWavFile()
{
    StdWaveHeader.push_back(0x52);  //"RIFF"
    StdWaveHeader.push_back(0x49);
    StdWaveHeader.push_back(0x46);
    StdWaveHeader.push_back(0x46);

    StdWaveHeader.push_back(0x00);  //<Total Chunk Size>
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x57);  //"WAVE"
    StdWaveHeader.push_back(0x41);
    StdWaveHeader.push_back(0x56);
    StdWaveHeader.push_back(0x45);

    //========================Subchunk1 (format)=========================
    StdWaveHeader.push_back(0x66);  //"fmt "
    StdWaveHeader.push_back(0x6D);
    StdWaveHeader.push_back(0x74);
    StdWaveHeader.push_back(0x20);

    StdWaveHeader.push_back(0x10);  //Subchunk1 size = 16
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x01);  // PCM format (1)
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x02);  // Channels count = 2
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x80);  // Sample rate = 48000 Hz (samples per second)
    StdWaveHeader.push_back(0xBB);
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x00);  // Byte rate = 192000 byte/sec (48000 Hz)
    StdWaveHeader.push_back(0xEE);
    StdWaveHeader.push_back(0x02);
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x04);  //BlockAlign = 4
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x10);  //BitsPerSample = 16  (2 bytes)
    StdWaveHeader.push_back(0x00);

    StdWaveHeader.push_back(0x64);  //"data"
    StdWaveHeader.push_back(0x61);
    StdWaveHeader.push_back(0x74);
    StdWaveHeader.push_back(0x61);

    StdWaveHeader.push_back(0x00);  //<SubChunk2 size>
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);
    StdWaveHeader.push_back(0x00);
}

bool MyWavFile::fromData(ByteArray data, bool do_check)
{
    samples.clear();
	spos = 0;

    if (data.size() < StdWaveHeader.size())
    {
        cout << "no data" << endl;
        return false;
    }

    //read header
    unsigned int smpl_cnt = 0;
    for (unsigned int i = 0; i < StdWaveHeader.size(); i++)
    {
        unsigned char d = data[i];
        switch (i)
        {
            case 40:
                smpl_cnt |= d;
                break;
            case 41:
                smpl_cnt |= d << 8;
                break;
            case 42:
                smpl_cnt |= d << 16;
                break;
            case 43:
                smpl_cnt |= d << 24;
                break;

            case 4:
            case 5:
            case 6:
            case 7:
                break;

            default:
                if (do_check)
                {
                    if (StdWaveHeader[i] != (unsigned char)data[i])
                    {
                        cout << "incorrect header!" << endl;
                        cout << "header - data" << endl;
                        for (unsigned int k = 0; k < StdWaveHeader.size(); k++)
                        {
                            unsigned char h = StdWaveHeader[k];
                            unsigned char d = data[k];
                            cout << h << " " << d << endl;
                        }
                        return false;
                    }
                }
                break;
        }
    }
    smpl_cnt /= 4;

    //read data
    for (unsigned int i = StdWaveHeader.size(); i < data.size(); i += 4)
    {
        if (i + 3 >= data.size())
        {
            if (do_check)
            {
                cout << "incorrect data align!" << endl;
                return false;
            }
            else break;
        }

        unsigned char d0 = data[i];
        unsigned char d1 = data[i+1];
     //   unsigned char d2 = data[i+2];
//        unsigned char d3 = data[i+3];

        int16_t val = d0 | (d1 << 8);
        samples.push_back(val);
    }

    if (do_check)
    {
        if (samples.size() != smpl_cnt)
        {
            cout << "incorrect smpl count!" << endl;
            cout << "from header = " << smpl_cnt << endl;
            cout << "in fact = " << samples.size()  << endl;
            return false;
        }
    }

    return true;
}

#define ReadSize 4096
bool MyWavFile::loadFromFile(string filename, bool do_check)
{
    ByteArray data;
    FILE *f = fopen(filename.c_str(), "rb");
    if (!f)
    {
        cout << "cannot open file" << endl;
        return false;
    }


    char buf[ReadSize];
    while (true)
    {
        unsigned int count = fread(buf, 1, ReadSize, f);
        for (unsigned int i = 0; i < count; i++)
        {
            data.push_back(buf[i]);
        }

        if (count != ReadSize) break;
    }
    fclose(f);

    return fromData(data, do_check);
}

ByteArray MyWavFile::toData()
{
    ByteArray res;
    int ct;

    //Write header
    for (unsigned int i = 0; i < StdWaveHeader.size(); i++)
    {
        switch (i)
        {
            case 4:
                ct = (samples.size() * 4) + 36;
                res.push_back(ct & 0xFF);
                res.push_back((ct >> 8) & 0xFF);
                res.push_back((ct >> 16) & 0xFF);
                res.push_back((ct >> 24) & 0xFF);
                break;
            case 5:
            case 6:
            case 7:
                break;

            case 40:
                ct = (samples.size() * 4);
                res.push_back(ct & 0xFF);
                res.push_back((ct >> 8) & 0xFF);
                res.push_back((ct >> 16) & 0xFF);
                res.push_back((ct >> 24) & 0xFF);
                break;
            case 41:
            case 42:
            case 43:
                break;

            default:
                res.push_back( StdWaveHeader[i] );
                break;
        }
    }


    //Write data (stereo, diplucate)
    for (unsigned int i = 0; i < samples.size(); i++)
    {
        int16_t val = samples[i];
        res.push_back(val & 0xFF);         //channel 1
        res.push_back((val >> 8) & 0xFF);
        res.push_back(val & 0xFF);         //channel 2
        res.push_back((val >> 8) & 0xFF);
    }

    return res;
}

bool MyWavFile::saveToFile(string filename)
{
    FILE *f = fopen(filename.c_str(),"wb");
    if (!f) return false;
    ByteArray data = toData();
    fwrite(&data[0], data.size(), 1, f);
    fclose(f);
    return true;
}

void MyWavFile::makeTestSine()
{
    samples.clear();
    for (int i = 0; i < 44100; i++)
    {
        double t = (double)i / 44100;
        double A = 10000;
        double arg = 2*3.14*t * 440;
        double dval = A * ( sin(arg) );
//        dval = round(dval);

        samples.push_back((int16_t)dval);
    }
}


void MyWavFile::seek_samples(int n)
{
	spos = n;
}

int MyWavFile::read_samples(float *buf, unsigned int smpl_count)
{
	int cnt = 0;
	for (unsigned int i = 0; (i < smpl_count) && (spos + i < samples.size()); i++)
	{
		buf[i*2] = (float)samples[spos+i];
		buf[i*2 + 1] = (float)samples[spos+i];
		cnt++;
	}
	spos += cnt;
	return cnt;
}

int MyWavFile::write_samples(float *buf, int smpl_count)
{
	int cnt = 0;
	for (int i = 0; i < smpl_count; i++)
	{
		samples.push_back((int16_t)buf[i*2]);
		//samples.push_back(i);
		cnt++;
	}

	return cnt;
}