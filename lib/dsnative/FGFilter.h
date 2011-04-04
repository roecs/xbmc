/* 
 *  Copyright (C) 2009-2010 Team XBMC
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

#include "streams.h"
#include <list>

class CFGFilter
{
public:
  enum Type {
    NONE,
    FILE,
    INTERNAL,
    REGISTRY
  };

  CFGFilter(const CLSID& clsid, Type type, CStdStringA name = L"");
  CFGFilter(Type type) { m_type = type; };
  virtual ~CFGFilter() {};

  CLSID GetCLSID() {return m_clsid;}
  CStdStringW GetName() {return m_name;}
  Type GetType() const { return m_type; }

  void AddType(const GUID& majortype, const GUID& subtype);

  virtual HRESULT Create(IBaseFilter** ppBF) = 0;
protected:
  CLSID m_clsid;
  CStdStringA m_name;
  Type m_type;
  std::list<GUID> m_types;
};

class CFGFilterRegistry : public CFGFilter
{
protected:
  CStdStringA m_DisplayName;
  IMoniker* m_pMoniker;

  void ExtractFilterData(BYTE* p, UINT len);

public:
  CFGFilterRegistry(IMoniker* pMoniker);
  CFGFilterRegistry(CStdStringA DisplayName);
  CFGFilterRegistry(const CLSID& clsid);

  CStdStringA GetDisplayName() {return m_DisplayName;}
  IMoniker* GetMoniker() {return m_pMoniker;}

  HRESULT Create(IBaseFilter** ppBF);
};

template<class T>
class CFGFilterInternal : public CFGFilter
{
public:
  CFGFilterInternal(CStdStringW name = L"")
    : CFGFilter(__uuidof(T), INTERNAL, name) {}

  HRESULT Create(IBaseFilter** ppBF)
  {
    CheckPointer(ppBF, E_POINTER);

    HRESULT hr = S_OK;
    IBaseFilter* pBF = new T(NULL, &hr);
    if(FAILED(hr)) return hr;

    (*ppBF = pBF)->AddRef();
    pBF = NULL;

    return hr;
  }
};

class CFGFilterFile : public CFGFilter
{
protected:
  CStdStringA m_path;
  CStdStringA m_xFileType;
  CStdStringA m_internalName;
  HINSTANCE m_hInst;
  bool m_isDMO;
  CLSID m_catDMO;

public:
  CFGFilterFile(const CLSID& clsid, CStdStringA path, CStdStringW name = L"", CStdStringA filtername = "", CStdStringA filetype = "");
  

  HRESULT Create(IBaseFilter** ppBF);
  CStdStringA GetXFileType() { return m_xFileType; };
  CStdStringA GetInternalName() { return m_internalName; };
};



