/*
* Copyright 2013 Sveriges Television AB http://casparcg.com/
*
* This file is part of CasparCG (www.casparcg.com).
*
* CasparCG is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* CasparCG is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
*
* Author: Robert Nagy, ronag89@gmail.com
*/

#pragma once



namespace caspar { namespace mplayer {


//video
#define SWidth					(1920)
#define SHeight					(1080)
#define SPixFmt					PIX_FMT_YUV420P
#define MIN_FRAME_TIME			0.01f //i.e. max frame fate 100
#define MAX_FRAME_TIME			2.00f //i.e  min frame fate 0.5

//audio
#define SAudioSamplingRate		48000 
#define SAudioChannels			2
#define SAudioSampleFmt			AV_SAMPLE_FMT_S32

//pipes
#define READ_PIPE_WAIT_TOUT		12000
#define CONNECT_PIPE_TOUT		20000
#define PIPE_BUF_SIZE			0x400000
#define AUDIO_PIPE_SIZE			0x400000
#define VIDEO_PIPE_SIZE			0x400000

//buffers
#define SCacheSize					2048
#define SYNC_FRAMES					10 //must be >= 3

//other
#define READ_MPLAYER_STDOUT

void init(void);
bool is_url_address(std::wstring str);
}}

