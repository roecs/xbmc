#pragma once

/*
 *      Copyright (C) 2005-2008 Team XBMC
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

#include "DVDVideoCodec.h"
#include "DllDSNative.h"

#include "windowingfactory.h" //g_Windowing
#include "application.h"

extern HWND g_hWnd;

class CBaseFilter;
struct DSVideoOutputData;
class CDVDVideoCodecDirectshow 
  : public CDVDVideoCodec,
    public IDSInfoCallback
{
public:
  CDVDVideoCodecDirectshow();

  virtual ~CDVDVideoCodecDirectshow();
  virtual HWND GetWindow() { return g_hWnd; }
  virtual IDirect3DDevice9* GetD3DDev() { return g_Windowing.Get3DDevice(); }
  virtual __int64 GetTime() { return g_application.GetTime(); }
  virtual __int64 GetTotalTime() { return g_application.GetTotalTime(); }
  virtual void LogCallback(int loglevel, const char *format, ...){ CLog::Log(loglevel,format); }
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  
  virtual int Decode(BYTE* pData, int iSize, double dts, double pts, int flags);
  virtual void Reset();
  virtual bool GetPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual bool GetUserData(DVDVideoUserData* pDvdVideoUserData);
  virtual void FrameReady(int number);

  virtual void SetDropState(bool bDrop);
  virtual const char* GetName() { return "DirectShow"; }
protected:

  DllDSNative m_dllDsNative;
  bool  m_requireResync;
  DSVideoCodec *codec;
  DSVideoOutputData* m_pCurrentData;
  int current_surface_index;
  int number_of_frame_ready;
  
  int buffersize;
  BITMAPINFOHEADER *bih;//bitmap header for the inputpin
  BITMAPINFOHEADER *bihout;//bitmap header for the outputpin
  double m_pts;
  double m_dts;
  unsigned int m_wait_timeout;
 
};
