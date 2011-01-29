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

#define INITGUID
#include "stdafx.h"

#include "dsallocator.h"

#include <math.h>

#include <mfidl.h>
#include <Mfapi.h>
#include <mferror.h>
#include <evr9.h>
#include "StdString.h"
#include "SmartPtr.h"

typedef HRESULT (__stdcall *FCT_MFCreateVideoSampleFromSurface)(IUnknown* pUnkSurface, IMFSample** ppSample);
typedef HRESULT (__stdcall *FCT_MFCreateDXSurfaceBuffer)(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);
//HRESULT MFCreateDXSurfaceBuffer(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);

FCT_MFCreateVideoSampleFromSurface ptrMFCreateVideoSampleFromSurface;
FCT_MFCreateDXSurfaceBuffer ptrMFCreateDXSurfaceBuffer;

// Guid to tag IMFSample with DirectX surface index
static const GUID GUID_SURFACE_INDEX = { 0x30c8e9f6, 0x415, 0x4b81, { 0xa3, 0x15, 0x1, 0xa, 0xc6, 0xa9, 0xda, 0x19 } };

void DebugPrint(const wchar_t *format, ... )
{
  CStdStringW strData;
  strData.reserve(16384);

  va_list va;
  va_start(va, format);
  strData.FormatV(format, va);
  va_end(va);

  OutputDebugString(strData.c_str());
  if( strData.Right(1) != L"\n" )
    OutputDebugString(L"\n");

}

#pragma pack(push, 1)
template<int texcoords>
struct MYD3DVERTEX
{
  float x, y, z, rhw;
  struct
  {
    float u, v;
  } t[texcoords];
};
template<>
struct MYD3DVERTEX<0> 
{
  float x, y, z, rhw; 
  DWORD Diffuse;
};
#pragma pack(pop)

template<int texcoords>
static void AdjustQuad(MYD3DVERTEX<texcoords>* v, double dx, double dy)
{
  double offset = 0.5;

  for(int i = 0; i < 4; i++)
  {
    v[i].x -= (float) offset;
    v[i].y -= (float) offset;
    
    for(int j = 0; j < max(texcoords-1, 1); j++)
    {
      v[i].t[j].u -= (float) (offset*dx);
      v[i].t[j].v -= (float) (offset*dy);
    }

    if(texcoords > 1)
    {
      v[i].t[texcoords-1].u -= (float) offset;
      v[i].t[texcoords-1].v -= (float) offset;
    }
  }
}

template<int texcoords>
static HRESULT TextureBlt(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<texcoords> v[4], D3DTEXTUREFILTERTYPE filter = D3DTEXF_LINEAR)
{
  if(!pD3DDev)
    return E_POINTER;

  DWORD FVF = 0;
  switch(texcoords)
  {
    case 1: FVF = D3DFVF_TEX1; break;
    case 2: FVF = D3DFVF_TEX2; break;
    case 3: FVF = D3DFVF_TEX3; break;
    case 4: FVF = D3DFVF_TEX4; break;
    case 5: FVF = D3DFVF_TEX5; break;
    case 6: FVF = D3DFVF_TEX6; break;
    case 7: FVF = D3DFVF_TEX7; break;
    case 8: FVF = D3DFVF_TEX8; break;
    default: return E_FAIL;
  }

  HRESULT hr;

  //Those are needed to avoid conflict with the xbmc gui
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );

  hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); 
  hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); 
  hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED); 

  for(int i = 0; i < texcoords; i++)
  {
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MAGFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MINFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MIPFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | FVF);

  MYD3DVERTEX<texcoords> tmp = v[2]; 
  v[2] = v[3]; 
  v[3] = tmp;
  hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));

  for(int i = 0; i < texcoords; i++)
  {
    pD3DDev->SetTexture(i, NULL);
  }

  return S_OK;
}

static HRESULT DrawRect(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<0> v[4])
{
  if(!pD3DDev)
    return E_POINTER;

  HRESULT hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  hr = pD3DDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); 
  hr = pD3DDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); 
  //D3DRS_COLORVERTEX 
  hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

  hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE |
    D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);
  hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX0 | D3DFVF_DIFFUSE);

  MYD3DVERTEX<0> tmp = v[2];
  v[2] = v[3];
  v[3] = tmp;
  hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));  

  return S_OK;
}

DsAllocator::DsAllocator(IDSInfoCallback *pCallback)
  : m_pCallback(pCallback)
{
  
  
  HMODULE hlib=NULL;
  hlib = LoadLibrary(L"evr.dll");
  ptrMFCreateVideoSampleFromSurface = (FCT_MFCreateVideoSampleFromSurface)GetProcAddress(hlib,"MFCreateVideoSampleFromSurface");
  ptrMFCreateDXSurfaceBuffer = (FCT_MFCreateDXSurfaceBuffer)GetProcAddress(hlib,"MFCreateDXSurfaceBuffer");
  UINT resetToken = 0;
  HRESULT hr = DXVA2CreateDirect3DDeviceManager9(&resetToken, &m_pD3DDevManager);
  hr = m_pD3DDevManager->ResetDevice(m_pCallback->GetD3DDev(), resetToken);
  surfallocnotify=NULL;
  refcount=1;
  inevrmode=false;
  m_pMixer=NULL;
  mediasink=NULL;
  m_pClock=NULL;
  m_pMediaType=NULL;
  endofstream=false;
  ResetSyncOffsets();
  m_mutex = new CMutex();
}

DsAllocator::~DsAllocator() 
{
  CleanupSurfaces();
  SAFE_RELEASE(m_mutex);
}

void DsAllocator::CleanupSurfaces() 
{
  Lock();
  while(fullevrsamples.size()>0)
  {
    IMFSample *sample=fullevrsamples.front();
    fullevrsamples.pop();
    sample->Release();
  }
  while(emptyevrsamples.size()>0)
  {
    IMFSample *sample=emptyevrsamples.front();
    emptyevrsamples.pop();
    sample->Release();
  }

  for (int i=0;i<m_pSurfaces.size();i++) 
  {
    SAFE_RELEASE(m_pTextures[i]);
    SAFE_RELEASE(m_pSurfaces[i]);
  }
  Unlock();
}

HRESULT STDMETHODCALLTYPE DsAllocator::InitializeDevice(DWORD_PTR userid,VMR9AllocationInfo* allocinf,DWORD*numbuf)
{
  if (!surfallocnotify) return S_FALSE;

  CleanupSurfaces();
  Lock();
  m_pSurfaces.resize(*numbuf);
  HRESULT hr= surfallocnotify->AllocateSurfaceHelper(allocinf,numbuf,&m_pSurfaces.at(0));
  vheight=allocinf->dwHeight;
  vwidth=allocinf->dwWidth;
  Unlock();
  return hr;
}

void DsAllocator::LostDevice(IDirect3DDevice9 *d3ddev, IDirect3D9* d3d)
  {
  if (!surfallocnotify) return ;
  CleanupSurfaces();
  Lock();
  HMONITOR hmon=d3d->GetAdapterMonitor(D3DADAPTER_DEFAULT);
  surfallocnotify->ChangeD3DDevice(d3ddev,hmon);
  Unlock();

}

HRESULT STDMETHODCALLTYPE DsAllocator::TerminateDevice(DWORD_PTR userid)
{
  CleanupSurfaces();

  return S_OK;
}
HRESULT STDMETHODCALLTYPE DsAllocator::GetSurface(DWORD_PTR userid,DWORD surfindex,DWORD surfflags, IDirect3DSurface9** surf)
{
  if (surfindex>=m_pSurfaces.size()) return E_FAIL;
  if (surf==NULL) return E_POINTER;

  Lock();
  m_pSurfaces[surfindex]->AddRef();
  *surf=m_pSurfaces[surfindex];
  Unlock();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DsAllocator::AdviseNotify(IVMRSurfaceAllocatorNotify9* allnoty)
{
  Lock();
  surfallocnotify=allnoty;
  IDirect3D9 *d3d;
  IDirect3DDevice9 *d3ddev = m_pCallback->GetD3DDev();
  //OK lets set the direct3d object from the osd
  //d3ddev=((OsdWin*)Osd::getInstance())->getD3dDev();
  d3ddev->GetDirect3D(&d3d);
  HMONITOR hmon = (d3d->GetAdapterMonitor(D3DADAPTER_DEFAULT));
  HRESULT hr = surfallocnotify->SetD3DDevice(d3ddev,hmon);
  Unlock();
  return S_OK;//hr
}


HRESULT STDMETHODCALLTYPE DsAllocator::StartPresenting(DWORD_PTR userid)
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::StopPresenting(DWORD_PTR userid)
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::PresentImage(DWORD_PTR userid,VMR9PresentationInfo* presinf)
{
  //TODO
  return S_OK;

}

HRESULT STDMETHODCALLTYPE DsAllocator::QueryInterface(REFIID refiid,void ** obj)
{
  if (obj==NULL) return E_POINTER;

  if (refiid==IID_IVMRSurfaceAllocator9)
  {
    *obj=static_cast<IVMRSurfaceAllocator9*>(this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IVMRImagePresenter9)
  {
    *obj=static_cast<IVMRImagePresenter9*>(this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IMFVideoDeviceID)
  {
    *obj=static_cast<IMFVideoDeviceID*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IMFTopologyServiceLookupClient )
  {
    *obj=static_cast<IMFTopologyServiceLookupClient*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IQualProp )
  {
    *obj=static_cast<IQualProp*> (this);
    AddRef();
    return S_OK;
  }  else if (refiid==IID_IMFGetService)
  {
    *obj=static_cast<IMFGetService*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IDirect3DDeviceManager9) 
  {
    if (m_pD3DDevManager)
{
      return m_pD3DDevManager->QueryInterface(refiid,obj);
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  return E_NOINTERFACE;
}



ULONG STDMETHODCALLTYPE  DsAllocator::AddRef()
{
  return InterlockedIncrement(&refcount);
}

ULONG STDMETHODCALLTYPE DsAllocator::Release()
{
  ULONG ref=0;
  ref=InterlockedDecrement(&refcount);
  if (ref==NULL)
  {
    delete this; //Commit suicide
  }
  return ref;
}

HRESULT STDMETHODCALLTYPE  DsAllocator::GetDeviceID(IID *pDid)
{
  if (pDid==NULL)
    return E_POINTER;

  *pDid=__uuidof(IDirect3DDevice9);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::InitServicePointers(IMFTopologyServiceLookup *plooky)
{
  if (!plooky) return E_POINTER;
  Lock();
  inevrmode=true;
  /* get all interfaces we need*/

  DWORD dwobjcts=1;
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_MIXER_SERVICE,
    __uuidof(IMFTransform),(void**)&m_pMixer, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMediaEventSink),(void**)&mediasink, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL, 0, MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMFClock),(void**)&m_pClock,&dwobjcts);


  Unlock();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::ReleaseServicePointers()
{
  Lock();
  inevrmode=false;
  /* TODO Set RenderState , sample type etc.*/

  //((OsdWin*)Osd::getInstance())->setExternalDriving(NULL,0,0);

  if (m_pMixer) m_pMixer->Release();
  m_pMixer=NULL;

  if (mediasink) mediasink->Release();
  mediasink=NULL;

  if (m_pClock) m_pClock->Release();
  m_pClock=NULL;
  if (m_pMediaType) m_pMediaType->Release();
  m_pMediaType=NULL;

  Unlock();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::GetService(const GUID &guid,const IID &iid,LPVOID *obj)
{
  if (guid==MR_VIDEO_ACCELERATION_SERVICE)
  {
    if (m_pD3DDevManager)
    {
      return m_pD3DDevManager->QueryInterface (__uuidof(IDirect3DDeviceManager9), (void**) obj);
    }
    else
    {
      return E_NOINTERFACE;
    }

  } 
  else if (guid==MR_VIDEO_RENDER_SERVICE)
  {
    return QueryInterface(iid,obj);
  } 
  else
  {
    return E_NOINTERFACE;
  }
}


void DsAllocator::GetEVRSamples()
{
  Lock();
  MFCLOCK_STATE clockstate;
  MFT_OUTPUT_DATA_BUFFER  Buffer;
  if (m_pClock) 
    m_pClock->GetState(0, &clockstate);
  //MessageBox(0,"get samples","samples",0);
  
  if (m_pClock && clockstate==MFCLOCK_STATE_STOPPED && fullevrsamples.size()>0)
  {
    Unlock();
    return;
  }

  while (emptyevrsamples.size()>0)
  {

    MFT_OUTPUT_DATA_BUFFER outdatabuffer;
    ZeroMemory(&outdatabuffer,sizeof(outdatabuffer));
    outdatabuffer.pSample=emptyevrsamples.front();
    DWORD status=0;
    LONGLONG starttime,endtime;
    MFTIME dummy;
    starttime=0;
    endtime=0;

    if (m_pClock) 
    {
      m_pClock->GetCorrelatedTime(0,&starttime,&dummy);
      if (lastdelframe) CalcJitter( (starttime-lastdelframe)/10000);
      lastdelframe=starttime;
    }
    if (!m_pMixer)
    {
      Unlock();
      return;
    }
    HRESULT hr=m_pMixer->ProcessOutput(0,1,&outdatabuffer,&status);
    

    if (hr==MF_E_TRANSFORM_NEED_MORE_INPUT)
    {
      if (endofstream)
      {

        endofstream=false;
        mediasink->Notify(EC_COMPLETE,(LONG_PTR) S_OK,0);

      }
      break;
    }
    else if (hr==MF_E_TRANSFORM_STREAM_CHANGE)
    {
      if (m_pMediaType) m_pMediaType->Release();
      m_pMediaType=NULL;
      break;
    } 
    else if (hr==MF_E_TRANSFORM_TYPE_NOT_SET)
    {
      if (m_pMediaType) m_pMediaType->Release();
      m_pMediaType=NULL;
      RenegotiateEVRMediaType();
      break;
    } 
    else if (hr==S_OK)
    {
      
      LONGLONG prestime=0;
      hr=outdatabuffer.pSample->GetSampleTime(&prestime);
      m_gPtr = outdatabuffer.pSample;
      outdatabuffer.pSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&currentsurfaceindex);
      //OutputDebugString(L"Got EVR Sample %lld",prestime);
      IMFSample *temp=emptyevrsamples.front();
      emptyevrsamples.pop();
      


      fullevrsamples.push(temp);
      //OutputDebugString(L"got evr sample %d, %d",
      //	emptyevrsamples.size(),fullevrsamples.size());
      if (m_pClock)
{
        m_pClock->GetCorrelatedTime(0,&endtime,&dummy);
        LONGLONG delay=endtime-starttime;
        mediasink->Notify( EC_PROCESSING_LATENCY,(LONG_PTR)&delay,0);
      }
    }
  else break;

  }
  Unlock();
}

HRESULT STDMETHODCALLTYPE DsAllocator::ProcessMessage(MFVP_MESSAGE_TYPE mess,ULONG_PTR mess_para)
{
  switch (mess)
  {
    case MFVP_MESSAGE_FLUSH:
    {
      //OutputDebugString(L"EVR Message MFVP_MESSAGE_FLUSH received");
      FlushEVRSamples(); 
    }
    break;
    case MFVP_MESSAGE_INVALIDATEMEDIATYPE: 
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_INVALIDATEMEDIATYPE received");
      if (m_pMediaType) 
        m_pMediaType->Release();
      m_pMediaType=NULL;
      RenegotiateEVRMediaType();
    }
    break;
    case MFVP_MESSAGE_PROCESSINPUTNOTIFY: 
    {
      //OutputDebugString(L"EVR Message MFVP_MESSAGE_PROCESSINPUTNOTIFY received");
      GetEVRSamples();
    } 
    break;
    case MFVP_MESSAGE_BEGINSTREAMING:
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_BEGINSTREAMING received");
      ResetSyncOffsets();
      //((OsdWin*)Osd::getInstance())->setExternalDriving(this,vwidth,vheight);
      //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_started);//No need to do this causes misbehaviout
      endofstream=false;
                     
    }
    break;
    case MFVP_MESSAGE_ENDSTREAMING:
      {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_ENDSTREAMING received");
      //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_off);
      //((OsdWin*)Osd::getInstance())->setExternalDriving(NULL,vwidth,vheight);
      //FlushEVRSamples();
      //if (m_pMediaType) m_pMediaType->Release();
      //m_pMediaType=NULL;
                    } break;
    case MFVP_MESSAGE_ENDOFSTREAM: 
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_ENDOFSTREAM received");
      //MessageBox(0,"endofstream","endofstream",0);
      endofstream=true;
    } 
    break;
    case MFVP_MESSAGE_STEP: 
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_STEP received");
      //((OsdWin*)Osd::getInstance())->setExternalDriving(this,vwidth,vheight);
      //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_pause);
      //MessageBox(0,"steppy","steppy",0);
      FlushEVRSamples(/*LOWORD(mess_para)*/); //Message sending, has to be done after compeletion
    } 
    break;
    case MFVP_MESSAGE_CANCELSTEP:
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_CANCELSTEP received");
      //???
    } 
    break;
    default:
      OutputDebugString(L"DsAllocator::ProcessMessage unhandled \n");
      OutputDebugString(L"");
  };
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::OnClockStart(MFTIME systime,LONGLONG startoffset)
{
  //((OsdWin*)Osd::getInstance())->SetEVRTimes(systime,startoffset);
  timeBeginPeriod(1);

  if (PRESENTATION_CURRENT_POSITION!=startoffset) 
    FlushEVRSamples();
  //((OsdWin*)Osd::getInstance())->setExternalDriving(this,vwidth,vheight);
   OutputDebugString(L"OnClockStart");
  //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_started);
  GetEVRSamples();

  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::OnClockStop(MFTIME systime)
{
  timeEndPeriod(1);

  //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_off);
  
   OutputDebugString(L"OnClockStop");
  //((OsdWin*)Osd::getInstance())->setExternalDriving(NULL,vwidth,vheight);
  FlushEVRSamples();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::OnClockPause(MFTIME systime)
{
  timeEndPeriod(1);
  //((OsdWin*)Osd::getInstance())->setExternalDriving(this,vwidth,vheight);
  
   OutputDebugString(L"OnClockPause");
  //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_pause);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::OnClockRestart(MFTIME systime)
{
  //((OsdWin*)Osd::getInstance())->setExternalDriving(this,vwidth,vheight);
  
   OutputDebugString(L"OnClockRestart");
  //((OsdWin*)Osd::getInstance())->SetEVRStatus(OsdWin::EVR_pres_started);

  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::OnClockSetRate(MFTIME systime,float rate)
{
  timeBeginPeriod(1);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::GetCurrentMediaType(IMFVideoMediaType **mtype)
{ 
  OutputDebugString(L"mediatype\n");
  if (!mtype) 
    return E_POINTER;
  Lock();
  if (!m_pMediaType)
  {
    Unlock();
    *mtype=NULL;
    return MF_E_NOT_INITIALIZED;
  }
  HRESULT hr=m_pMediaType->QueryInterface(IID_IMFVideoMediaType,(void**)mtype);
  Unlock();
  return hr;
}

void DsAllocator::RenegotiateEVRMediaType()
{
  if (!m_pMixer)
  {
    OutputDebugString(L"Cannot renegotiate without transform!");
    return ;
  }
  bool gotcha=false;
  DWORD index=0;


  while (!gotcha)
  {
    IMFMediaType *mixtype=NULL;
    HRESULT hr;
    if (hr=m_pMixer->GetOutputAvailableType(0,index++,&mixtype)!=S_OK)
    {
      DebugPrint(L"No more types availiable from EVR %d !",hr);
      break;
    }

    //Type check
    BOOL compressed;
    mixtype->IsCompressedFormat(&compressed); 
    if (compressed)
    {
      mixtype->Release();
      continue;
    }
    UINT32 helper;
    mixtype->GetUINT32(MF_MT_INTERLACE_MODE,&helper);
    if (helper!=MFVideoInterlace_Progressive)
    {
      OutputDebugString(L"Skip media type interlaced!");
      mixtype->Release();
      continue;
    }
    GUID temp;
    mixtype->GetMajorType(&temp);
    if (temp!=MEDIATYPE_Video)
  {
      OutputDebugString(L"Skip media type no video!");
      mixtype->Release();
      continue;
    }
    if(m_pMixer->SetOutputType(0,mixtype,MFT_SET_TYPE_TEST_ONLY)!=S_OK) 
    {
      OutputDebugString(L"Skip media type test failed!");
      mixtype->Release();
      continue;
    }
    //Type is ok!

    gotcha=true;

    Lock();
    if (m_pMediaType) 
      m_pMediaType->Release();
    m_pMediaType=NULL;

    m_pMediaType=mixtype;
    AllocateEVRSurfaces();
    Unlock();

    hr=m_pMixer->SetOutputType(0,mixtype,0);



    if (hr!=S_OK) 
    {
      Lock();
      if (m_pMediaType) 
        m_pMediaType->Release();
      m_pMediaType=NULL;
      gotcha=false;

      Unlock();
    }


    DebugPrint(L"Output type set! %d",hr);
  }
  if (!gotcha)
    OutputDebugString(L"No suitable output type!");



}

void DsAllocator::AllocateEVRSurfaces()
{

  LARGE_INTEGER temp64;
  m_pMediaType->GetUINT64(MF_MT_FRAME_SIZE, (UINT64*)&temp64);
  vwidth=temp64.HighPart;
  vheight=temp64.LowPart;
  GUID subtype;
  subtype.Data1=D3DFMT_X8R8G8B8;
  m_pMediaType->GetGUID(MF_MT_SUBTYPE,&subtype);
  D3DFORMAT format=(D3DFORMAT)subtype.Data1;
  DebugPrint(L"Surfaceformat is %d, width %d, height %d",format,vwidth,vheight);
  format=D3DFMT_X8R8G8B8;

  CleanupSurfaces();
  Lock();

  m_pSurfaces.resize(10);
  m_pTextures.resize(10);
  for (int i=0;i<10;i++)
  {
    HRESULT hr;
    LPDIRECT3DSURFACE9 surf;
    LPDIRECT3DTEXTURE9 text;
    hr = m_pCallback->GetD3DDev()->CreateTexture(vwidth, vheight, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,&m_pTextures[i],NULL);
    //hr = m_pCallback->GetD3DDev()->CreateRenderTarget(vwidth,vheight,format, D3DMULTISAMPLE_NONE,0,FALSE,&surfy,NULL);
    if (SUCCEEDED(hr))
    {
      if (FAILED(m_pTextures[i]->GetSurfaceLevel(0, &m_pSurfaces[i])))
      {
        //assert(0);
      }
      
    }
    else
    {
      m_pSurfaces[i]=NULL;
    }
  }
  
  


  for (int i=0;i<m_pSurfaces.size();i++)
  {
    if (m_pSurfaces[i]!=NULL) 
    {
      IMFSample *sample=NULL;
      ptrMFCreateVideoSampleFromSurface(m_pSurfaces[i],&sample);
      sample->SetUINT32 (GUID_SURFACE_INDEX, i);
      
      if (sample) 
        emptyevrsamples.push(sample);
    }
  }
  Unlock();

}

void DsAllocator::FlushEVRSamples()
{
  Lock();
  while(fullevrsamples.size()>0)
  {
    IMFSample *sample=fullevrsamples.front();
    fullevrsamples.pop();
    emptyevrsamples.push(sample);
  }
  Unlock();
}
#define FAC 1

void DsAllocator::GetNextSurface(IMFMediaBuffer **pBuf,DWORD *waittime)
{
  
  //*surf=NULL;
  *waittime=10;
  if (fullevrsamples.size()==0) 
    GetEVRSamples();
  Lock();
  //OutputDebugString(L"Enter Get Next Surface");
  
  while (fullevrsamples.size()>0)
  {
    //MessageBox(0,"Got a sample","got a sample",0);
    IMFSample *sample=fullevrsamples.front();
    LONGLONG prestime=0;
    MFTIME   systime=0;
    LONGLONG currenttime=0;

    HRESULT hr=sample->GetSampleTime(&prestime);
    if (hr==S_OK) 
    {
      if (m_pClock) m_pClock->GetCorrelatedTime(0,&currenttime,&systime);
    }
    LONGLONG delta=prestime-currenttime; 

     

    if (delta<-10000*20 && false) 
    { //SkipIT
      LONGLONG latency=-delta;
      //mediasink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
      LARGE_INTEGER helper;
      helper.QuadPart=-delta;
      DebugPrint(L"skip 1 frame %d %d prestime %lld",helper.LowPart,helper.HighPart,prestime);
      CalcSyncOffsets(delta/10000LL);
      //	emptyevrsamples.size(),fullevrsamples.size());
      fullevrsamples.pop();
      emptyevrsamples.push(sample);
      framesdropped++;
      continue;
    }

    if (delta<10000*20 || !m_pClock )
    {
      *waittime=0;
      IMFMediaBuffer* buffy=NULL;
      //MessageBox(0,"its showtime","showtimw",0);
      CalcSyncOffsets(delta/10000LL);
      framesdrawn++;
      //hr = sample->CopyToBuffer(pBuf); <--- better with?
      
      hr=sample->GetBufferByIndex(0,&buffy);

      if (hr!=S_OK) 
      { //SkipIT
        fullevrsamples.pop();
        emptyevrsamples.push(sample);
        continue;
      }
      *pBuf = buffy;
      (*pBuf)->AddRef();
      //return the buf
      break;
      hr=sample->GetBufferByIndex(0,&buffy);
      //LARGE_INTEGER helper;
      //helper.QuadPart=-delta;
      //OutputDebugString(L"Paint 1 frame %d %d, frames  %d prestime %lld",
       //   helper.LowPart,helper.HighPart,fullevrsamples.size(),prestime);
      if (hr!=S_OK) 
      { //SkipIT
        fullevrsamples.pop();
        emptyevrsamples.push(sample);
        continue;
      }
      
      IMFGetService* service;
      hr=buffy->QueryInterface(IID_IMFGetService,(void**)&service);
      buffy->Release();
      if (hr!=S_OK) 
      { //SkipIT
        fullevrsamples.pop();
        emptyevrsamples.push(sample);
        continue;
      }
      LPDIRECT3DSURFACE9 tempsurf;
      hr=service->GetService(MR_BUFFER_SERVICE,IID_IDirect3DSurface9 ,(void**) &tempsurf);
      service->Release();
      if (hr!=S_OK) 
      { //SkipIT
        fullevrsamples.pop();
        emptyevrsamples.push(sample);
        continue;
      }
      //*surf=tempsurf;
      break;
    }
    else 
    {
      *waittime=delta/10000-10;
      //*surf=NULL;
      break;
    }     
  }

  Unlock();

}

void DsAllocator::DiscardSurfaceandgetWait(DWORD *waittime)
{
  //OutputDebugString(L"Discard surface and get Wait");
  GetEVRSamples();
  //OutputDebugString(L"Discard surface and get Wait2");
  Lock();
  if (fullevrsamples.size()==0)
  {
    *waittime=0;
    Unlock();
    return;
  }
  IMFSample *sample=fullevrsamples.front();
  fullevrsamples.pop();
  emptyevrsamples.push(sample);
  *waittime=0;

  while (fullevrsamples.size()>0) 
  {

    IMFSample *sample=fullevrsamples.front();
    LONGLONG prestime=0;
    MFTIME   systime=0;
    LONGLONG currenttime=0;

    HRESULT hr=sample->GetSampleTime(&prestime);
    if (hr==S_OK) 
    {
      if (m_pClock) m_pClock->GetCorrelatedTime(0,&currenttime,&systime);
    }
    LONGLONG delta=prestime-currenttime;


    if (delta<-10000*20 )
{ //SkipIT
       LONGLONG latency=-delta;
       // mediasink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
      //LARGE_INTEGER helper;
      //helper.QuadPart=-delta;
      //OutputDebugString(L"skip 1 frame %d %d time %lld",helper.LowPart,helper.HighPart,prestime);
      CalcSyncOffsets(delta/10000LL);
      //	emptyevrsamples.size(),fullevrsamples.size());
      fullevrsamples.pop();
      emptyevrsamples.push(sample);
      framesdropped++;
      continue;
    }

    *waittime=min(delta/10000/2-10,1); 

    break;
  }
  Unlock();

}
void DsAllocator::ResetSyncOffsets()
{
  for (int i=0;i<n_stats;i++)
  {
    sync_offset[i]=0;
    jitter_offset[i]=0;
  }
  framesdrawn=0;
  lastdelframe=0;
  framesdropped=0;
  avg_sync_offset=0;
  dev_sync_offset=0;
  jitter=0;
  sync_pos=0;
  jitter_pos=0;
  avgfps=0;
}

void DsAllocator::CalcSyncOffsets(int sync)
{
  sync_offset[sync_pos]=sync;
  sync_pos=(sync_pos +1)%n_stats;

  double mean_value=0;
  for (int i=0;i<n_stats;i++)
  {
    mean_value+=sync_offset[i];
  }
  mean_value/=(double) n_stats;
  double std_dev=0;
  for (int i=0;i<n_stats;i++)
  {
    double temp_dev=(mean_value-(double)sync_offset[i]);
    std_dev+=temp_dev*temp_dev;
  }
  std_dev/=(double)n_stats;
  avg_sync_offset=mean_value;
  dev_sync_offset=sqrt(std_dev);
}

void DsAllocator::CalcJitter(int jitter)
{
  jitter_offset[jitter_pos]=jitter;
  jitter_pos=(jitter_pos +1)%n_stats;
  

  double mean_value=0;
  for (int i=0;i<n_stats;i++)
  {
    mean_value+=jitter_offset[i];
  }
  mean_value/=(double) n_stats;
  avgfps=1000./mean_value*100.;
  double std_dev=0;
  for (int i=0;i<n_stats;i++)
  {
    double temp_dev=(mean_value-(double)jitter_offset[i]);
    std_dev+=temp_dev*temp_dev;
  }
  std_dev/=(double)n_stats;
  jitter=sqrt(std_dev);
}


HRESULT STDMETHODCALLTYPE DsAllocator::get_FramesDrawn(int *val)
{
  *val=framesdrawn;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::get_AvgFrameRate(int *val)
{
  *val=avgfps;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::get_Jitter(int *val)
{
  *val=jitter;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::get_AvgSyncOffset(int *val)
{
  *val=avg_sync_offset;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::get_DevSyncOffset(int *val)
{
  *val=dev_sync_offset;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DsAllocator::get_FramesDroppedInRenderer(int *val)
{
  *val=framesdropped;
  return S_OK;
}


IPaintCallback* DsAllocator::AcquireCallback()
{
  
  return static_cast<IPaintCallback*>(this);
  
}

bool DsAllocator::AcceptMoreData()
{
  DWORD status;
  if (S_OK == m_pMixer->GetInputStatus(0, &status))
  {
     if (status & MFT_INPUT_STATUS_ACCEPT_DATA)
       return true;
  }
  
  return false;
}
bool DsAllocator::SurfaceReady()
{
  DWORD status;
  if (S_OK == m_pMixer->GetOutputStatus(&status))
  {
    if (status & MFT_OUTPUT_STATUS_SAMPLE_READY)
      return true;
    
  }
  
    return false;
}
bool DsAllocator::GetFreeSample(IMFSample** ppSample)
{
  CAutoLock lock(&m_SampleQueueLock);
  if (emptyevrsamples.size() > 0)
  {
    *ppSample = emptyevrsamples.front();
    emptyevrsamples.pop();

    return true;
  }
  return false;
}

void DsAllocator::Render(const RECT& dst, IDirect3DSurface9* target)
{
  HRESULT hr = S_OK;;
  LONGLONG prestime = 0;
  MFTIME   systime = 0;
  LONGLONG currenttime = 0;
  DWORD waittime = 10;
  if (!m_pCallback)
    return;
  LPDIRECT3DDEVICE9 pDevice = m_pCallback->GetD3DDev();
  IMFSample *sample = NULL;
  
  
  GetEVRSamples();

  while (fullevrsamples.size()>0) 
  {
    //MessageBox(0,"Got a sample","got a sample",0);
    sample=fullevrsamples.front();


    hr=sample->GetSampleTime(&prestime);
    m_iLastSampleDuration = m_iLastSampleTime - prestime;
    m_iLastSampleTime = prestime;
    LONGLONG sample_duration;
    hr=sample->GetSampleDuration(&sample_duration);

    if (hr==S_OK)
    {
      if (m_pClock) 
        m_pClock->GetCorrelatedTime(0,&currenttime,&systime);
    }

    LONGLONG delta=prestime-currenttime; 

    if (delta<-10000*20 && false) 
    { //SkipIT
      LONGLONG latency=-delta;
      //mediasink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
      LARGE_INTEGER helper;
      helper.QuadPart=-delta;
      
      CalcSyncOffsets(delta/10000LL);
      //if failed to get the index just put it in the free surface list
      fullevrsamples.pop();
      emptyevrsamples.push(sample);
      framesdropped++;
      return;
    }

    if (1)//delta<10000*20 || !m_pClock )
    {
      waittime=0;
      IMFMediaBuffer* buffy=NULL;
      
      CalcSyncOffsets(delta/10000LL);
      framesdrawn++;
 
      if (FAILED(sample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&currentsurfaceindex)))
      {
        //if failed to get the index just put it in the free surface list
        fullevrsamples.pop();
        emptyevrsamples.push(sample);
        return;
      }
      
      break;
    } 
    else 
    {
      waittime=delta/10000-10;
      break;
    }     
  }

  if ((currentsurfaceindex < 0 )&& (currentsurfaceindex > m_pSurfaces.size()))
    return;
  if (m_pSurfaces.at(currentsurfaceindex))
  {
    Vector v[4];
    v[0] = Vector(dst.left, dst.top, 0);
    v[1] = Vector(dst.right, dst.top, 0);
    v[2] = Vector(dst.left, dst.bottom, 0);
    v[3] = Vector(dst.right, dst.bottom, 0);
    int centerx,centery;
    centerx = (dst.right-dst.left)/2;
    centery = (dst.bottom-dst.top)/2;
    Vector center(centerx, centery, 0);
    XForm xform;
    xform = XForm(Ray(Vector(0, 0, 0), Vector()), Vector(1, 1, 1), false);
    int l = (int)(Vector(dst.right-dst.left, dst.top - dst.bottom, 0).Length()*1.5f)+1;

    for(ptrdiff_t i = 0; i < 4; i++)
    {
      v[i] = xform << (v[i] - center);
      v[i].z = v[i].z / l + 0.5f;
      v[i].x /= v[i].z*2;
      v[i].y /= v[i].z*2;
      v[i] += center;
    }
    
    D3DSURFACE_DESC desc;
    m_pTextures[currentsurfaceindex]->GetLevelDesc(0,&desc);
    const float dx = 1.0f/(float)desc.Width;
    const float dy = 1.0f/(float)desc.Height;
    const float tx0 = (float) 0;
    const float tx1 = (float) vwidth;
    const float ty0 = (float) 0;
    const float ty1 = (float) vheight;
    float fConstData[][4] = {{dx*0.5f, dy*0.5f, 0, 0}, {dx, dy, 0, 0}, {dx, 0, 0, 0}, {0, dy, 0, 0}};
    hr = pDevice->SetPixelShaderConstantF(0, (float*)fConstData, 4);
/*CWinRenderer::CropSource(vs.SrcRect, vs.DstRect, desc);*/
    MYD3DVERTEX<1> vv[] =
  {
    {v[0].x, v[0].y, v[0].z, 1.0f/v[0].z,  tx0, ty0},
    {v[1].x, v[1].y, v[1].z, 1.0f/v[1].z,  tx1, ty0},
    {v[2].x, v[2].y, v[2].z, 1.0f/v[2].z,  tx0, ty1},
    {v[3].x, v[3].y, v[3].z, 1.0f/v[3].z,  tx1, ty1},
  };

  AdjustQuad(vv, 1.0, 1.0);
  hr = pDevice->SetTexture(0,m_pTextures[currentsurfaceindex]);
  hr = TextureBlt(pDevice, vv, D3DTEXF_POINT);
    
    //vheight
    //vwidth
    //hr = pDevice->StretchRect(m_pSurfaces[currentsurfaceindex], NULL, target, &rdst, D3DTEXF_POINT);
  if (FAILED(hr))
  {
    hr = pDevice->StretchRect(m_pSurfaces[currentsurfaceindex], NULL, target, NULL, D3DTEXF_NONE);
    //OutputDebugStringA("DSAllocator Failed to Render Target with EVR");
  }
  Lock();
  if (fullevrsamples.size()==0)
  {
    waittime=0;
    Unlock();
    return;
  }
  IMFSample *sample=fullevrsamples.front();
  fullevrsamples.pop();
  emptyevrsamples.push(sample);
  waittime=0;
    //DiscardSurfaceandgetWait(&waittime);
  }
  else
  {
    OutputDebugStringA("DSAllocator Failed to get surface from sample");
  }
  
  Unlock();
  
}