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

#pragma once

#include "byteparser.h"
#include <vector>

#define MAX_SPS			256			// Max size for a SPS packet

class CExtradataParser :
  protected CByteParser
{
public:
  CExtradataParser(BYTE *pExtradata, uint32_t extra_len,int profile, int level);
  ~CExtradataParser();

  struct avchdr 
  {
    BYTE profile, level;
    unsigned int width, height;
    __int64 spspos, spslen;
    __int64 ppspos, ppslen;
    __int64 AvgTimePerFrame;

    avchdr() {
      spspos = 0;
      spslen = 0;
      ppspos = 0;
      ppslen = 0;
      AvgTimePerFrame = 0;
    }
  };
  void RemoveMpegEscapeCode(BYTE* dst, BYTE* src, int length);
  bool Read(avchdr& h, int len, CMediaType* pmt = NULL);

  uint8_t ParseMPEGSequenceHeader(BYTE *pTarget);

  std::vector<BYTE> getextradata(){return result;}
  avchdr GetAvchdr() 
  {
    CMediaType pmt;
    pmt.InitMediaType();
    Read(avch,m_dwLen, &pmt);

    return avch;
  }
protected:
  avchdr avch;
  std::vector<BYTE> result;
  int sps_count;
  int pps_count;
  bool NextMPEGStartCode(BYTE &code);
};
