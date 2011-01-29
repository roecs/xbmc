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




#include "PixelShaderCompiler.h"
#include "WindowingFactory.h"
#include "Log.h"
#include "windows.h"
#include <d3dx9shader.h>
CPixelShaderCompiler::CPixelShaderCompiler(bool fStaySilent)
{
#if 0
  HINSTANCE    hDll;
  CStdString d3dxversionpath;
  char lpPath[MAX_PATH+1];
  GetSystemDirectory(lpPath,MAX_PATH);

  int min_ver = D3DX_SDK_VERSION;
    int max_ver = D3DX_SDK_VERSION;
    

    if(D3DX_SDK_VERSION >= 42) {
      // August 2009 SDK (v42) is not compatible with older versions
      min_ver = 42;      
    } else {
      if(D3DX_SDK_VERSION > 33) {
        // versions between 34 and 41 have no known compatibility issues
        min_ver = 34;
      }  else {    
        // The minimum version that supports the functionality required by MPC is 24
        min_ver = 24;
  
        if(D3DX_SDK_VERSION == 33) {
          // The April 2007 SDK (v33) should not be used (crash sometimes during shader compilation)
          max_ver = 32;    
        }
      }
    }
    
    // load latest compatible version of the DLL that is available
    for (int i=max_ver; i>=min_ver; i--)
    {
      d3dxversionpath.Format("%s\\d3dx9_%d.dll", lpPath, i);
      
      CLog::Log(LOGINFO,"%s",d3dxversionpath.c_str());
      m_pD3DX.SetFile(d3dxversionpath);
      if (m_pD3DX.Load())
        break;
    }
  
  /*hDll = g_dsSettings.GetD3X9Dll();

  if(hDll)
  {
    m_pD3DXCompileShader = (D3DXCompileShaderPtr)GetProcAddress(hDll, "D3DXCompileShader");
    m_pD3DXDisassembleShader = (D3DXDisassembleShaderPtr)GetProcAddress(hDll, "D3DXDisassembleShader");
  }

  if(!fStaySilent)
  {
    if(!hDll)
    {
      CLog::Log(LOGERROR,"%s Cannot load D3DX9_xx.DLL, pixel shaders will not work.",__FUNCTION__);
    }
    else if(!m_pD3DXCompileShader || !m_pD3DXDisassembleShader) 
    {
      CLog::Log(LOGERROR,"%s Cannot find necessary function entry points in D3DX9_xx.DLL, pixel shaders will not work.",__FUNCTION__);
    }
  }*/
#endif
}

CPixelShaderCompiler::~CPixelShaderCompiler()
{
}

HRESULT CPixelShaderCompiler::CompileShader(
    LPCSTR pSrcData,
    LPCSTR pFunctionName,
    LPCSTR pProfile,
    DWORD Flags,
    IDirect3DPixelShader9** ppPixelShader,
    CStdString* disasm,
    CStdString* errmsg)
{
  /*if (!m_pD3DX.IsLoaded())
    return E_FAIL;*/

  HRESULT hr;

  Com::SmartPtr<ID3DXBuffer> pShader, pDisAsm, pErrorMsgs;
  
  hr = D3DXCompileShader(pSrcData, strlen(pSrcData), NULL, NULL, pFunctionName, pProfile, Flags, &pShader, &pErrorMsgs, NULL);

  if(FAILED(hr))
  {
    if(errmsg)
    {
      CStdStringA msg = "Unexpected compiler error";

      if(pErrorMsgs)
      {
        int len = pErrorMsgs->GetBufferSize();
        memcpy(msg.GetBufferSetLength(len), pErrorMsgs->GetBufferPointer(), len);
      }

      *errmsg = msg;
    }

    return hr;
  }

  if(ppPixelShader)
  {
    if(!g_Windowing.Get3DDevice()) return E_FAIL;
    hr = g_Windowing.Get3DDevice()->CreatePixelShader((DWORD*)pShader->GetBufferPointer(), ppPixelShader);
    if(FAILED(hr)) return hr;
  }

  if(disasm)
  {
    hr = D3DXDisassembleShader((DWORD*)pShader->GetBufferPointer(), FALSE, NULL, &pDisAsm);
    if(SUCCEEDED(hr) && pDisAsm) *disasm = CStdStringA((const char*)pDisAsm->GetBufferPointer());
  }

  return S_OK;
}