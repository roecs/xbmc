/*
 *      Copyright (C) 2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "ExtradataParser.h"
#include "GolombBuffer.h"
#define MARKER if(BitRead(1) != 1) {ASSERT(0); return 0;}
static const char kStartCode[4] = { 0x00, 0x00, 0x00, 0x01 };
// Sequence parameter set or picture parameter set
struct AVCParamSet 
{
  AVCParamSet(uint16_t length, const uint8_t *data) : mLength(length), mData(data) {}
  uint16_t mLength;
  const uint8_t *mData;
};

static void getNalUnitType(uint8_t byte, uint8_t* type) 
{

    // nal_unit_type: 5-bit unsigned integer
    *type = (byte & 0x1F);
}
static const uint8_t kNalUnitTypeSeqParamSet = 0x07;
static const uint8_t kNalUnitTypePicParamSet = 0x08;
static const uint8_t kNalUnitTypeAUD          = 0x09;

static const uint8_t *findNextStartCode(const uint8_t *data, size_t length)
{
  size_t bytesLeft = length;
  //Seek a startcode until the end of the data
  while (bytesLeft > 4  && memcmp("\x00\x00\x00\x01", &data[length - bytesLeft], 4))
    --bytesLeft;
  
  if (bytesLeft <= 4)
  {

        bytesLeft = 0; // Last parameter set
    }
    return &data[length - bytesLeft];
}

static const uint8_t *parseParamSet(const uint8_t *data, size_t length, size_t *paramSetLen)
{
  const uint8_t *nextStartCode = findNextStartCode(data, length);
    *paramSetLen = nextStartCode - data;
    if (*paramSetLen == 0)
  {

        
        return NULL;
    }

}

CExtradataParser::CExtradataParser(BYTE *pExtradata, uint32_t extra_len,int profile, int level)
  : CByteParser(pExtradata, extra_len)
{

  
#if 0
  int starthere;
  
  std::vector<int> nal_offset;
  std::vector<uint8_t> p;
  p.resize(extra_len);
  memcpy(&p.at(0),pExtradata,extra_len);
  size_t byteleft = extra_len;
  size_t paramSetLen = 0;
  const uint8_t *tmp = pExtradata;
  const uint8_t *nextStartCode = pExtradata;
  uint8_t type = 0;
  while (byteleft > 4 && !memcmp("\x00\x00\x00\x01", tmp, 4))
  {
    getNalUnitType(*(tmp + 4), &type);
    if (type == kNalUnitTypeSeqParamSet)
    { 
      
      
     nextStartCode = findNextStartCode(tmp + 4, byteleft - 4);

    }
    else if (type == kNalUnitTypePicParamSet)
    {
      nextStartCode = findNextStartCode(tmp + 4, byteleft - 4);

    }
    else if (type == kNalUnitTypeAUD)
    {
      nextStartCode = findNextStartCode(tmp + 4, byteleft - 4);
        
    }
    else
    {
      nextStartCode = findNextStartCode(tmp + 4, byteleft - 4);
    }
    byteleft -= nextStartCode - tmp;
    tmp = nextStartCode;

  }
  int res = 0;
  for (int i = 0; i < (p.size()-3);i++)
  {
    if (memcmp(kStartCode, &p.at(i),4) == 0)
    {
      if ((i+4) <= extra_len)
      {
        int prof = static_cast<int>(p.at(i+5));
        
        if (prof == profile)
        {
          p.erase(p.begin(),p.begin()+(i+4));
          p.insert(p.begin(),static_cast<BYTE>(level));
          p.insert(p.begin(),static_cast<BYTE>(0));
          break;
        }
      }
    }
  }
  
  
  for (int i = 0; i < (p.size()-3);i++)
  {
    if (memcmp(kStartCode, &p.at(i),4) == 0)
    {
      
      p.erase(p.begin()+i,p.begin()+(i+4));
      int countsps = p.size()-i;
      p.insert(p.end()-countsps,static_cast<BYTE>(0));
      p.insert(p.end()-countsps,countsps);
      


    }
  }
  int new_len = p.size();
  
  result = p;
  
  
  
  
#endif
}

CExtradataParser::~CExtradataParser()
{
}

void CExtradataParser::RemoveMpegEscapeCode(BYTE* dst, BYTE* src, int length)
{
	int		si=0;
	int		di=0;
	while(si+2<length) {
		//remove escapes (very rare 1:2^22)
		if(src[si+2]>3) {
			dst[di++]= src[si++];
			dst[di++]= src[si++];
		} else if(src[si]==0 && src[si+1]==0) {
			if(src[si+2]==3) { //escape
				dst[di++]= 0;
				dst[di++]= 0;
				si+=3;
				continue;
			} else { //next start code
				return;
			}
		}

		dst[di++]= src[si++];
	}
}

bool CExtradataParser::Read(avchdr& h, int len, CMediaType* pmt)
{
	__int64 endpos = Pos() + len; // - sequence header length

	DWORD	dwStartCode;

	while(Pos() < endpos+4 /*&& BitRead(32, true) == 0x00000001*/ && (!h.spslen || !h.ppslen)) {
		if (BitRead(32, true) != 0x00000001) {
			BitRead(8);
			continue;
		}
		__int64 pos = Pos();

		BitRead(32);
		BYTE id = BitRead(8);

		if((id&0x9f) == 0x07 && (id&0x60) != 0) {
			BYTE			SPSTemp[MAX_SPS];
			BYTE			SPSBuff[MAX_SPS];
			CGolombBuffer	gb (SPSBuff, MAX_SPS);
			__int64			num_units_in_tick;
			__int64			time_scale;
			long			fixed_frame_rate_flag;

			h.spspos = pos;

			// Manage H264 escape codes (see "remove escapes (very rare 1:2^22)" in ffmpeg h264.c file)
			ByteRead((BYTE*)SPSTemp, MAX_SPS);
			RemoveMpegEscapeCode (SPSBuff, SPSTemp, MAX_SPS);

			h.profile = (BYTE)gb.BitRead(8);
			gb.BitRead(8);
			h.level = (BYTE)gb.BitRead(8);

			gb.UExpGolombRead(); // seq_parameter_set_id

			if(h.profile >= 100) { // high profile
				if(gb.UExpGolombRead() == 3) { // chroma_format_idc
					gb.BitRead(1); // residue_transform_flag
				}

				gb.UExpGolombRead(); // bit_depth_luma_minus8
				gb.UExpGolombRead(); // bit_depth_chroma_minus8

				gb.BitRead(1); // qpprime_y_zero_transform_bypass_flag

				if(gb.BitRead(1)) // seq_scaling_matrix_present_flag
					for(int i = 0; i < 8; i++)
						if(gb.BitRead(1)) // seq_scaling_list_present_flag
							for(int j = 0, size = i < 6 ? 16 : 64, next = 8; j < size && next != 0; ++j) {
								next = (next + gb.SExpGolombRead() + 256) & 255;
							}
			}

			gb.UExpGolombRead(); // log2_max_frame_num_minus4

			UINT64 pic_order_cnt_type = gb.UExpGolombRead();

			if(pic_order_cnt_type == 0) {
				gb.UExpGolombRead(); // log2_max_pic_order_cnt_lsb_minus4
			} else if(pic_order_cnt_type == 1) {
				gb.BitRead(1); // delta_pic_order_always_zero_flag
				gb.SExpGolombRead(); // offset_for_non_ref_pic
				gb.SExpGolombRead(); // offset_for_top_to_bottom_field
				UINT64 num_ref_frames_in_pic_order_cnt_cycle = gb.UExpGolombRead();
				for(int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
					gb.SExpGolombRead();    // offset_for_ref_frame[i]
				}
			}

			gb.UExpGolombRead(); // num_ref_frames
			gb.BitRead(1); // gaps_in_frame_num_value_allowed_flag

			UINT64 pic_width_in_mbs_minus1 = gb.UExpGolombRead();
			UINT64 pic_height_in_map_units_minus1 = gb.UExpGolombRead();
			BYTE frame_mbs_only_flag = (BYTE)gb.BitRead(1);

			h.width = (pic_width_in_mbs_minus1 + 1) * 16;
			h.height = (2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;

			if (h.height == 1088) {
				h.height = 1080;    // Prevent blur lines
			}

			if (!frame_mbs_only_flag) {
				gb.BitRead(1);    // mb_adaptive_frame_field_flag
			}
			gb.BitRead(1);								// direct_8x8_inference_flag
			if (gb.BitRead(1)) {						// frame_cropping_flag
				gb.UExpGolombRead();					// frame_cropping_rect_left_offset
				gb.UExpGolombRead();					// frame_cropping_rect_right_offset
				gb.UExpGolombRead();					// frame_cropping_rect_top_offset
				gb.UExpGolombRead();					// frame_cropping_rect_bottom_offset
			}

			if (gb.BitRead(1)) {						// vui_parameters_present_flag
				if (gb.BitRead(1)) {					// aspect_ratio_info_present_flag
					if (255==(BYTE)gb.BitRead(8)) {	// aspect_ratio_idc)
						gb.BitRead(16);				// sar_width
						gb.BitRead(16);				// sar_height
					}
				}

				if (gb.BitRead(1)) {					// overscan_info_present_flag
					gb.BitRead(1);						// overscan_appropriate_flag
				}

				if (gb.BitRead(1)) {					// video_signal_type_present_flag
					gb.BitRead(3);						// video_format
					gb.BitRead(1);						// video_full_range_flag
					if(gb.BitRead(1)) {				// colour_description_present_flag
						gb.BitRead(8);					// colour_primaries
						gb.BitRead(8);					// transfer_characteristics
						gb.BitRead(8);					// matrix_coefficients
					}
				}
				if(gb.BitRead(1)) {					// chroma_location_info_present_flag
					gb.UExpGolombRead();				// chroma_sample_loc_type_top_field
					gb.UExpGolombRead();				// chroma_sample_loc_type_bottom_field
				}
				if (gb.BitRead(1)) {					// timing_info_present_flag
					num_units_in_tick		= gb.BitRead(32);
					time_scale				= gb.BitRead(32);
					fixed_frame_rate_flag	= gb.BitRead(1);

					// Trick for weird parameters (10x to Madshi)!
					if ((num_units_in_tick < 1000) || (num_units_in_tick > 1001)) {
						if  ((time_scale % num_units_in_tick != 0) && ((time_scale*1001) % num_units_in_tick == 0)) {
							time_scale			= (time_scale * 1001) / num_units_in_tick;
							num_units_in_tick	= 1001;
						} else {
							time_scale			= (time_scale * 1000) / num_units_in_tick;
							num_units_in_tick	= 1000;
						}
					}
					time_scale = time_scale / 2;	// VUI consider fields even for progressive stream : divide by 2!

					if (time_scale) {
						h.AvgTimePerFrame = (10000000I64*num_units_in_tick)/time_scale;
					}
				}
			}

			Seek(h.spspos+gb.GetPos());
		} else if((id&0x9f) == 0x08 && (id&0x60) != 0) {
			h.ppspos = pos;
		}

		BitByteAlign();

		dwStartCode = BitRead(32, true);
		while(Pos() < endpos+4 && (dwStartCode != 0x00000001) && (dwStartCode & 0xFFFFFF00) != 0x00000100) {
			BitRead(8);
			dwStartCode = BitRead(32, true);
		}

		if(h.spspos != 0 && h.spslen == 0) {
			h.spslen = Pos() - h.spspos;
		} else if(h.ppspos != 0 && h.ppslen == 0) {
			h.ppslen = Pos() - h.ppspos;
		}

	}

	if(!h.spspos || !h.spslen || !h.ppspos || !h.ppslen) {
		return(false);
	}

	if(!h.AvgTimePerFrame || !(
				(h.level == 10) || (h.level == 11) || (h.level == 12) || (h.level == 13) ||
				(h.level == 20) || (h.level == 21) || (h.level == 22) ||
				(h.level == 30) || (h.level == 31) || (h.level == 32) ||
				(h.level == 40) || (h.level == 41) || (h.level == 42) ||
				(h.level == 50) || (h.level == 51))) {
		return(false);
	}

	if(!pmt) {
		return(true);
	}

	{
		int extra = 2+h.spslen-4 + 2+h.ppslen-4;

		pmt->majortype = MEDIATYPE_Video;
		pmt->subtype = FOURCCMap('1CVA');
		//pmt->subtype = MEDIASUBTYPE_H264;		// TODO : put MEDIASUBTYPE_H264 to support Windows 7 decoder !
		pmt->formattype = FORMAT_MPEG2_VIDEO;
		int len = FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + extra;
		MPEG2VIDEOINFO* vi = (MPEG2VIDEOINFO*)new BYTE[len];
		memset(vi, 0, len);
		// vi->hdr.dwBitRate = ;
		vi->hdr.AvgTimePerFrame = h.AvgTimePerFrame;
		vi->hdr.dwPictAspectRatioX = h.width;
		vi->hdr.dwPictAspectRatioY = h.height;
		vi->hdr.bmiHeader.biSize = sizeof(vi->hdr.bmiHeader);
		vi->hdr.bmiHeader.biWidth = h.width;
		vi->hdr.bmiHeader.biHeight = h.height;
		vi->hdr.bmiHeader.biCompression = '1CVA';
		vi->dwProfile = h.profile;
		vi->dwFlags = 4; // ?
		vi->dwLevel = h.level;
		vi->cbSequenceHeader = extra;
		BYTE* p = (BYTE*)&vi->dwSequenceHeader[0];
		*p++ = (h.spslen-4) >> 8;
		*p++ = (h.spslen-4) & 0xff;
		Seek(h.spspos+4);
		ByteRead(p, h.spslen-4);
		p += h.spslen-4;
		*p++ = (h.ppslen-4) >> 8;
		*p++ = (h.ppslen-4) & 0xff;
		Seek(h.ppspos+4);
		ByteRead(p, h.ppslen-4);
		p += h.ppslen-4;
		pmt->SetFormat((BYTE*)vi, len);
		delete [] vi;
	}

	return(true);
}

bool CExtradataParser::NextMPEGStartCode(BYTE &code)
{
  BitByteAlign();
  DWORD dw = (DWORD)-1;
  do
	{
		if(!Remaining()) return false;
		dw = (dw << 8) | (BYTE)BitRead(8);
	}
	while((dw&0xffffff00) != 0x00000100);
  code = (BYTE)(dw&0xff);
  return true;
}

uint8_t CExtradataParser::ParseMPEGSequenceHeader(BYTE *pTarget)
{
  BYTE id = 0;
  while(Remaining() && id != 0xb3)
  {
    if(!NextMPEGStartCode(id))
      return 0;
  }

  if(id != 0xb3) 
    return 0;

  uint32_t shpos = Pos() - 4;
  BitRead(12); // Width
  BitRead(12); // Height
  BitRead(4); // AR
  BitRead(4); // FPS
  BitRead(18); // Bitrate
  MARKER;
  BitRead(10); // VBV
  BitRead(1); // Constrained Flag
  // intra quantisizer matrix
  if(BitRead(1)) {
    for (uint8_t i = 0; i < 64; i++) {
      BitRead(8);
    }
  }
  // non-intra quantisizer matrix
  if(BitRead(1)) {
    for (uint8_t i = 0; i < 64; i++) {
      BitRead(8);
    }
  }
  
  uint8_t shlen = Pos() - shpos;

  uint32_t shextpos = 0;
  uint8_t shextlen = 0;

  if(NextMPEGStartCode(id) && id == 0xb5) { // sequence header ext
    shextpos = Pos() - 4;
    
    int startcode = BitRead(4); // Start Code Id; TODO: DIfferent start code ids mean different length of da2a
    ASSERT(startcode == 1);
    
    BitRead(1); // Profile Level Escape
    BitRead(3); // Profile
    BitRead(4); // Level
    BitRead(1); // Progressive
    BitRead(2); // Chroma
    BitRead(2); // Width Extension
    BitRead(2); // Height Extension
    BitRead(12); // Bitrate Extension
    MARKER;
    BitRead(8); // VBV Buffer Size Extension
    BitRead(1); // Low Delay
    BitRead(2); // FPS Extension n
    BitRead(5); // FPS Extension d

    shextlen = Pos() - shextpos;
  }

  memcpy(pTarget, Start()+shpos, shlen);
  if (shextpos) {
    memcpy(pTarget+shlen, Start()+shextpos, shextlen);
  }
  return shlen + shextlen;
}
