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
  
  
  
  

}

CExtradataParser::~CExtradataParser()
{
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
