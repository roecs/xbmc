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
//class CRect;
#include "d3d9.h"
#include "DVDCodecs\Video\DVDVideoCodec.h"
class IPaintCallback
{
public:
  virtual ~IPaintCallback() {};

  virtual void Render(const RECT& dst, IDirect3DSurface9* target, int index) = 0;
  virtual bool WaitOutput(unsigned int msec) = 0;
  virtual bool GetD3DSurfaceFromScheduledSample(int *surface_index) = 0;
  virtual int GetReadySample() = 0;
  virtual bool GetPicture(DVDVideoPicture *pDvdVideoPicture) = 0;
};

