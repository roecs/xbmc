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


class CGolombBuffer
{
public:
  CGolombBuffer(BYTE* pBuffer, int nSize);

  UINT64      BitRead(int nBits, bool fPeek = false);
  UINT64      UExpGolombRead();
  INT64      SExpGolombRead();
  void      BitByteAlign();

  inline BYTE    ReadByte()  { return (BYTE) BitRead ( 8); };
  inline SHORT  ReadShort() { return (SHORT)BitRead (16); };
  inline DWORD  ReadDword() { return (DWORD)BitRead (32); };
  void      ReadBuffer(BYTE* pDest, int nSize);
  
  void      Reset();
  void      Reset(BYTE* pNewBuffer, int nNewSize);

  void      SetSize(int nValue) { m_nSize = nValue; };
  int       GetSize()      { return m_nSize; };
  int       RemainingSize() const { return m_nSize - m_nBitPos; };
  bool      IsEOF()        { return m_nBitPos >= m_nSize; };
  INT64      GetPos();
  BYTE*      GetBufferPos()    { return m_pBuffer + m_nBitPos; };

  void      SkipBytes(int nCount);

private :
  BYTE*    m_pBuffer;
  int      m_nSize;
  int      m_nBitPos;
  int      m_bitlen;
  INT64    m_bitbuff;
};
