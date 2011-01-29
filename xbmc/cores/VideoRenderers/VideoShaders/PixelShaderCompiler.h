/* 
 *  Copyright (C) 2003-2006 Gabest
 *  http://www.gabest.org
 *
 *  Copyright (C) 2005-2010 Team XBMC
 *  http://www.xbmc.org
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
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include <d3dx9shader.h>

#include "cores/dvdplayer/DVDCodecs/Video/DllDSNative.h"
#include "lib\dsnative\smartptr.h"
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"

class CPixelShaderCompiler
{
public:
  CPixelShaderCompiler(bool fStaySilent = false);
  virtual ~CPixelShaderCompiler();

  HRESULT CompileShader(
        LPCSTR pSrcData,
        LPCSTR pFunctionName,
        LPCSTR pProfile,
        DWORD Flags,
        IDirect3DPixelShader9** ppPixelShader,
        CStdString* disasm = NULL,
        CStdString* errmsg = NULL);
protected:
  DllD3X9 m_pD3DX;
};
