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

#include <evr9.h>
#include "StdString.h"
#include "SmartPtr.h"
#include <map>
#include "utils\TimeUtils.h"
// === Helper functions
#define CheckHR(exp) {if(FAILED(hr = exp)) return hr;}
#define PaintInternal() {assert(0);}
//evr
typedef HRESULT (__stdcall *FCT_MFCreateVideoSampleFromSurface)(IUnknown* pUnkSurface, IMFSample** ppSample);
typedef HRESULT (__stdcall *FCT_MFCreateDXSurfaceBuffer)(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);
  // AVRT.dll
typedef HANDLE  (__stdcall *PTR_AvSetMmThreadCharacteristicsW)(LPCWSTR TaskName, LPDWORD TaskIndex);
typedef BOOL  (__stdcall *PTR_AvSetMmThreadPriority)(HANDLE AvrtHandle, AVRT_PRIORITY Priority);
typedef BOOL  (__stdcall *PTR_AvRevertMmThreadCharacteristics)(HANDLE AvrtHandle);
//HRESULT MFCreateDXSurfaceBuffer(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);

FCT_MFCreateVideoSampleFromSurface ptrMFCreateVideoSampleFromSurface;
FCT_MFCreateDXSurfaceBuffer ptrMFCreateDXSurfaceBuffer;
PTR_AvSetMmThreadCharacteristicsW       pfAvSetMmThreadCharacteristicsW;
PTR_AvSetMmThreadPriority               pfAvSetMmThreadPriority;
PTR_AvRevertMmThreadCharacteristics     pfAvRevertMmThreadCharacteristics;
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

#define TRACE_EVR

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
  
  // Load Vista specifics DLLs
  hlib = LoadLibrary (L"AVRT.dll");
  pfAvSetMmThreadCharacteristicsW    = hlib ? (PTR_AvSetMmThreadCharacteristicsW)  GetProcAddress (hlib, "AvSetMmThreadCharacteristicsW") : NULL;
  pfAvSetMmThreadPriority        = hlib ? (PTR_AvSetMmThreadPriority)      GetProcAddress (hlib, "AvSetMmThreadPriority") : NULL;
  pfAvRevertMmThreadCharacteristics  = hlib ? (PTR_AvRevertMmThreadCharacteristics)  GetProcAddress (hlib, "AvRevertMmThreadCharacteristics") : NULL;

  surfallocnotify=NULL;
  refcount=1;
  inevrmode=false;
  m_pMixer=NULL;
  m_pSink=NULL;
  m_pClock=NULL;
  m_pMediaType=NULL;
  endofstream=false;
  ResetSyncOffsets();
  
  m_mutex = new CMutex();
  ResetStats();
  m_nRenderState        = Shutdown;
  //m_fUseInternalTimer      = false;
  m_LastSetOutputRange    = -1;
  m_bPendingRenegotiate    = false;
  m_bPendingMediaFinished    = false;
  m_bWaitingSample      = false;
  m_pCurrentDisplaydSample  = NULL;
  m_nStepCount        = 0;
  //m_dwVideoAspectRatioMode  = MFVideoARMode_PreservePicture;
  //m_dwVideoRenderPrefs    = (MFVideoRenderPrefs)0;
  //m_BorderColor        = RGB (0,0,0);
  m_bSignaledStarvation    = false;
  m_StarvationClock      = 0;
  //m_pOuterEVR          = NULL;
  m_LastScheduledSampleTime  = -1;
  m_LastScheduledUncorrectedSampleTime = -1;
  m_MaxSampleDuration      = 0;
  m_LastSampleOffset      = 0;
  ZeroMemory(m_VSyncOffsetHistory, sizeof(m_VSyncOffsetHistory));
  m_VSyncOffsetHistoryPos = 0;
  m_bLastSampleOffsetValid  = false;
}

DsAllocator::~DsAllocator() 
{
  CleanupSurfaces();
  SAFE_RELEASE(m_mutex);
}

void DsAllocator::ResetStats()
{
  m_pcFrames      = 0;
  m_nDroppedUpdate  = 0;
  m_pcFramesDrawn    = 0;
  m_piAvg        = 0;
  m_piDev        = 0;
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
  
  inevrmode=true;
  /* get all interfaces we need*/

  DWORD dwobjcts=1;
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_MIXER_SERVICE,
    __uuidof(IMFTransform),(void**)&m_pMixer, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMediaEventSink),(void**)&m_pSink, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL, 0, MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMFClock),(void**)&m_pClock,&dwobjcts);
  StartWorkerThreads();

  
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::ReleaseServicePointers()
{
  Lock();
  inevrmode=false;
  /* TODO Set RenderState , sample type etc.*/

  //((OsdWin*)Osd::getInstance())->setExternalDriving(NULL,0,0);

  
  m_pMixer = NULL;
  m_pSink = NULL;
  m_pClock = NULL;
  m_pMediaType = NULL;
  //if (m_pSink) m_pSink->Release();
  //if (m_pMixer) m_pMixer->Release();
  //if (m_pClock) m_pClock->Release();
  //if (m_pMediaType) m_pMediaType->Release();
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
        m_pSink->Notify(EC_COMPLETE,(LONG_PTR) S_OK,0);

      }
      break;
    }
    else if (hr==MF_E_TRANSFORM_STREAM_CHANGE)
    {
      //if (m_pMediaType) m_pMediaType->Release();
      m_pMediaType=NULL;
      break;
    } 
    else if (hr==MF_E_TRANSFORM_TYPE_NOT_SET)
    {
      //if (m_pMediaType) m_pMediaType->Release();
      m_pMediaType=NULL;
      RenegotiateEVRMediaType();
      break;
    } 
    else if (hr==S_OK)
    {
      
      LONGLONG prestime=0;
      hr=outdatabuffer.pSample->GetSampleTime(&prestime);
      m_gPtr = outdatabuffer.pSample;
      outdatabuffer.pSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&m_nCurSurface);
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
        m_pSink->Notify( EC_PROCESSING_LATENCY,(LONG_PTR)&delay,0);
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
      m_bPendingRenegotiate = true;
      while (*((volatile bool *)&m_bPendingRenegotiate))
        Sleep(1);
    }
    break;
    case MFVP_MESSAGE_PROCESSINPUTNOTIFY: 
    { 
    } 
    break;
    case MFVP_MESSAGE_BEGINSTREAMING:
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_BEGINSTREAMING received");
      ResetStats();  
    }
    break;
    case MFVP_MESSAGE_ENDSTREAMING:
      OutputDebugString(L"EVR Message MFVP_MESSAGE_ENDSTREAMING received");
      break;
    case MFVP_MESSAGE_ENDOFSTREAM: 
    {
      OutputDebugString(L"EVR Message MFVP_MESSAGE_ENDOFSTREAM received");
      m_bPendingMediaFinished = true;
      endofstream=true;
    } 
    break;
    case MFVP_MESSAGE_STEP:
    {
      // Request frame step the param is the number of frame to step
      CLog::Log(LOGINFO, "EVR Message MFVP_MESSAGE_STEP %i", mess_para);
      FlushEVRSamples(/*LOWORD(mess_para)*/); //Message sending, has to be done after compeletion
    } 
    break;
    case MFVP_MESSAGE_CANCELSTEP:
    {
      CLog::Log(LOGINFO, "EVR Message MFVP_MESSAGE_CANCELSTEP received");
      //???
    } 
    break;
    default:
      CLog::Log(LOGINFO, "DsAllocator::ProcessMessage unhandled");
  };
  return S_OK;
}
// IMFClockStateSink
STDMETHODIMP DsAllocator::OnClockStart(MFTIME hnsSystemTime,  int64_t llClockStartOffset)
{
  m_nRenderState    = Started;

  TRACE_EVR ("EVR: OnClockStart  hnsSystemTime = %I64d,   llClockStartOffset = %I64d\n", hnsSystemTime, llClockStartOffset);
  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;

  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockStop(MFTIME hnsSystemTime)
{
  TRACE_EVR ("EVR: OnClockStop  hnsSystemTime = %I64d\n", hnsSystemTime);
  m_nRenderState    = Stopped;

  m_ModeratedClockLast = -1;
  m_ModeratedTimeLast = -1;
  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockPause(MFTIME hnsSystemTime)
{
  TRACE_EVR ("EVR: OnClockPause  hnsSystemTime = %I64d\n", hnsSystemTime);
  if (!m_bSignaledStarvation)
    m_nRenderState    = Paused;

  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;
  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockRestart(MFTIME hnsSystemTime)
{
  m_nRenderState  = Started;

  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;
  TRACE_EVR ("EVR: OnClockRestart  hnsSystemTime = %I64d\n", hnsSystemTime);

  return S_OK;
}


STDMETHODIMP DsAllocator::OnClockSetRate(MFTIME hnsSystemTime, float flRate)
{
  ASSERT (FALSE);
  return E_NOTIMPL;
}

void ModerateFloat(double& Value, double Target, double& ValuePrim, double ChangeSpeed)
{
  double xbiss = (-(ChangeSpeed)*(ValuePrim) - (Value-Target)*(ChangeSpeed*ChangeSpeed)*0.25f);
  ValuePrim += xbiss;
  Value += ValuePrim;
}

int64_t DsAllocator::GetClockTime(int64_t PerformanceCounter)
{
  int64_t       llClockTime;
  MFTIME        nsCurrentTime;
  if (! m_pClock)
    return 0;

  m_pClock->GetCorrelatedTime(0, &llClockTime, &nsCurrentTime);
  DWORD Characteristics = 0;
  m_pClock->GetClockCharacteristics(&Characteristics);
  MFCLOCK_STATE State;
  m_pClock->GetState(0, &State);

  if (!(Characteristics & MFCLOCK_CHARACTERISTICS_FLAG_FREQUENCY_10MHZ))
  {
    MFCLOCK_PROPERTIES Props;
    if (m_pClock->GetProperties(&Props) == S_OK)
      llClockTime = (llClockTime * 10000000) / Props.qwClockFrequency; // Make 10 MHz

  }
  int64_t llPerf = PerformanceCounter;
//  return llClockTime + (llPerf - nsCurrentTime);
  double Target = llClockTime + (llPerf - nsCurrentTime) * m_ModeratedTimeSpeed;

  bool bReset = false;
  if (m_ModeratedTimeLast < 0 || State != m_LastClockState || m_ModeratedClockLast < 0)
  {
    bReset = true;
    m_ModeratedTimeLast = llPerf;
    m_ModeratedClockLast = llClockTime;
  }

  m_LastClockState = State;

  double TimeChange = (double) llPerf - m_ModeratedTimeLast;
  double ClockChange = (double) llClockTime - m_ModeratedClockLast;

  m_ModeratedTimeLast = llPerf;
  m_ModeratedClockLast = llClockTime;

  if (bReset)
  {
    m_ModeratedTimeSpeed = 1.0;
    m_ModeratedTimeSpeedPrim = 0.0;
    ZeroMemory(m_TimeChangeHistory, sizeof(m_TimeChangeHistory));
    ZeroMemory(m_ClockChangeHistory, sizeof(m_ClockChangeHistory));
    m_ClockTimeChangeHistoryPos = 0;
  }
  if (TimeChange)
  {
    int Pos = m_ClockTimeChangeHistoryPos % 100;
    int nHistory = std::min(m_ClockTimeChangeHistoryPos, 100);
    ++m_ClockTimeChangeHistoryPos;
    if (nHistory > 50)
    {
      int iLastPos = (Pos - (nHistory)) % 100;
      if (iLastPos < 0)
        iLastPos += 100;

      double TimeChange = llPerf - m_TimeChangeHistory[iLastPos];
      double ClockChange = llClockTime - m_ClockChangeHistory[iLastPos];

      double ClockSpeedTarget = ClockChange / TimeChange;
      double ChangeSpeed = 0.1;
      if (ClockSpeedTarget > m_ModeratedTimeSpeed)
      {
        if (ClockSpeedTarget / m_ModeratedTimeSpeed > 0.1)
          ChangeSpeed = 0.1;
        else
          ChangeSpeed = 0.01;
      }
      else 
      {
        if (m_ModeratedTimeSpeed / ClockSpeedTarget > 0.1)
          ChangeSpeed = 0.1;
        else
          ChangeSpeed = 0.01;
      }
      ModerateFloat(m_ModeratedTimeSpeed, ClockSpeedTarget, m_ModeratedTimeSpeedPrim, ChangeSpeed);
//      m_ModeratedTimeSpeed = TimeChange / ClockChange;
    }
    m_TimeChangeHistory[Pos] = (double) llPerf;
    m_ClockChangeHistory[Pos] = (double) llClockTime;
  }

  return (int64_t) Target;
}

// IBaseFilter delegate
bool DsAllocator::GetState( DWORD dwMilliSecsTimeout, FILTER_STATE *State, HRESULT &_ReturnValue)
{
  CAutoLock lock(&m_SampleQueueLock);

  if (m_bSignaledStarvation)
  {
    
    unsigned int nSamples = std::max(m_pSurfaces.size() / 2, 1U);
    if ((m_ScheduledSamples.GetCount() < nSamples || m_LastSampleOffset < -m_rtTimePerFrame*2) /*&& !g_bNoDuration*/)
    {      
      *State = (FILTER_STATE)Paused;
      _ReturnValue = VFW_S_STATE_INTERMEDIATE;
      return true;
    }
    m_bSignaledStarvation = false;
  }
  return false;
}

// IQualProp
STDMETHODIMP DsAllocator::get_FramesDroppedInRenderer(int *pcFrames)
{
  *pcFrames  = m_pcFrames;
  return S_OK;
}
STDMETHODIMP DsAllocator::get_FramesDrawn(int *pcFramesDrawn)
{
  *pcFramesDrawn = m_pcFramesDrawn;
  return S_OK;
}
STDMETHODIMP DsAllocator::get_AvgFrameRate(int *piAvgFrameRate)
{
  *piAvgFrameRate = (int)(m_fAvrFps * 100);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_Jitter(int *iJitter)
{
  *iJitter = (int)((m_fJitterStdDev/10000.0) + 0.5);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_AvgSyncOffset(int *piAvg)
{
  *piAvg = (int)((m_fSyncOffsetAvr/10000.0) + 0.5);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_DevSyncOffset(int *piDev)
{
  *piDev = (int)((m_fSyncOffsetStdDev/10000.0) + 0.5);
  return S_OK;
}


// IMFRateSupport
STDMETHODIMP DsAllocator::GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate)
{
  // TODO : not finished...
  *pflRate = 0;
  return S_OK;
}
    
STDMETHODIMP DsAllocator::GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate)
{
  HRESULT    hr = S_OK;
  float    fMaxRate = 0.0f;

  CAutoLock lock(this);

  CheckPointer(pflRate, E_POINTER);
  CheckHR(CheckShutdown());
    
  // Get the maximum forward rate.
  fMaxRate = GetMaxRate(fThin);

  // For reverse playback, swap the sign.
  if (eDirection == MFRATE_REVERSE)
    fMaxRate = -fMaxRate;

  *pflRate = fMaxRate;

  return hr;
}
    
STDMETHODIMP DsAllocator::IsRateSupported(BOOL fThin, float flRate, float *pflNearestSupportedRate)
{
    // fRate can be negative for reverse playback.
    // pfNearestSupportedRate can be NULL.

    CAutoLock lock(this);

    HRESULT hr = S_OK;
    float   fMaxRate = 0.0f;
    float   fNearestRate = flRate;   // Default.

  CheckPointer (pflNearestSupportedRate, E_POINTER);
    CheckHR(hr = CheckShutdown());

    // Find the maximum forward rate.
    fMaxRate = GetMaxRate(fThin);

    if (fabsf(flRate) > fMaxRate)
    {
        // The (absolute) requested rate exceeds the maximum rate.
        hr = MF_E_UNSUPPORTED_RATE;

        // The nearest supported rate is fMaxRate.
        fNearestRate = fMaxRate;
        if (flRate < 0)
        {
            // For reverse playback, swap the sign.
            fNearestRate = -fNearestRate;
        }
    }

    // Return the nearest supported rate if the caller requested it.
    if (pflNearestSupportedRate != NULL)
        *pflNearestSupportedRate = fNearestRate;

    return hr;
}


float DsAllocator::GetMaxRate(BOOL bThin)
{
  float   fMaxRate    = FLT_MAX;  // Default.
  UINT32  fpsNumerator  = 0, fpsDenominator = 0;
  UINT    MonitorRateHz  = 0; 

  if (!bThin && (m_pMediaType != NULL))
  {
    // Non-thinned: Use the frame rate and monitor refresh rate.
        
    // Frame rate:
    MFGetAttributeRatio(m_pMediaType, MF_MT_FRAME_RATE, 
      &fpsNumerator, &fpsDenominator);

    // Monitor refresh rate:
    MonitorRateHz = m_RefreshRate; // D3DDISPLAYMODE

    if (fpsDenominator && fpsNumerator && MonitorRateHz)
    {
      // Max Rate = Refresh Rate / Frame Rate
      fMaxRate = (float)MulDiv(
        MonitorRateHz, fpsDenominator, fpsNumerator);
    }
  }
  return fMaxRate;
}

void DsAllocator::CompleteFrameStep(bool bCancel)
{
  if (m_nStepCount > 0)
  {
    if (bCancel || (m_nStepCount == 1)) 
    {
      m_pSink->Notify(EC_STEP_COMPLETE, bCancel ? TRUE : FALSE, 0);
      m_nStepCount = 0;
    }
    else
      m_nStepCount--;
  }
}

void DsAllocator::SetDSMediaType(CMediaType mt)
{
  if (mt.formattype==FORMAT_VideoInfo)
  {
    m_rtTimePerFrame = ((VIDEOINFOHEADER*)mt.pbFormat)->AvgTimePerFrame;
    m_bInterlaced = false;
  }
  else if (mt.formattype==FORMAT_VideoInfo2)
  {
    m_rtTimePerFrame = ((VIDEOINFOHEADER2*)mt.pbFormat)->AvgTimePerFrame;
    m_bInterlaced = (((VIDEOINFOHEADER2*)mt.pbFormat)->dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0;
  }
  else if (mt.formattype==FORMAT_MPEGVideo)
  {
    m_rtTimePerFrame = ((MPEG1VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
    m_bInterlaced = false;
  }
  else if (mt.formattype==FORMAT_MPEG2Video)
  {
    m_rtTimePerFrame = ((MPEG2VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
    m_bInterlaced = (((MPEG2VIDEOINFO*)mt.pbFormat)->hdr.dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0;
  }

  if (m_rtTimePerFrame == 0) 
    m_rtTimePerFrame = 417166;
  m_fps = (float)(10000000.0 / m_rtTimePerFrame);
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
    //if (m_pMediaType) m_pMediaType->Release();
    m_pMediaType=NULL;

    m_pMediaType=mixtype;
    AllocateEVRSurfaces();
    Unlock();

    hr=m_pMixer->SetOutputType(0,mixtype,0);



    if (hr!=S_OK) 
    {
      Lock();
      //if (m_pMediaType) m_pMediaType->Release();
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
      //m_pSink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
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
       // m_pSink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
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

    *waittime = std::min((LONGLONG)delta/10000/2-10, (LONGLONG)1); 

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

void DsAllocator::Render(const RECT& dst, IDirect3DSurface9* target)
{
  HRESULT hr = S_OK;;
  LONGLONG prestime = 0;
  MFTIME   systime = 0;
  LONGLONG currenttime = 0;
  DWORD waittime = 10;
  if (!m_pCallback || endofstream)
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
      //m_pSink->Notify(EC_SAMPLE_LATENCY,(LONG_PTR) &latency,0);
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
 
      if (FAILED(sample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&m_nCurSurface)))
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

  if ((m_nCurSurface < 0 )&& (m_nCurSurface > m_pSurfaces.size()))
    return;
  if (m_pSurfaces.at(m_nCurSurface))
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
    m_pTextures[m_nCurSurface]->GetLevelDesc(0,&desc);
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
  hr = pDevice->SetTexture(0,m_pTextures[m_nCurSurface]);
  hr = TextureBlt(pDevice, vv, D3DTEXF_POINT);
    
    //vheight
    //vwidth
    //hr = pDevice->StretchRect(m_pSurfaces[m_nCurSurface], NULL, target, &rdst, D3DTEXF_POINT);
  if (FAILED(hr))
  {
    hr = pDevice->StretchRect(m_pSurfaces[m_nCurSurface], NULL, target, NULL, D3DTEXF_NONE);
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

DWORD WINAPI DsAllocator::GetMixerThreadStatic(LPVOID lpParam)
{
  DsAllocator*    pThis = (DsAllocator*) lpParam;
  pThis->GetMixerThread();
  return 0;
}

void DsAllocator::GetMixerThread()
{
  HANDLE        hEvts[]    = { m_hEvtQuit};
  bool        bQuit    = false;
    TIMECAPS      tc;
  DWORD        dwResolution;
  DWORD        dwUser = 0;
  DWORD        dwTaskIndex  = 0;

  timeGetDevCaps(&tc, sizeof(TIMECAPS));
  dwResolution = std::min( std::max(tc.wPeriodMin, (UINT) 0), tc.wPeriodMax);
  dwUser    = timeBeginPeriod(dwResolution);

  while (!bQuit)
  {
    DWORD dwObject = WaitForMultipleObjects (_countof(hEvts), hEvts, FALSE, 1);
    switch (dwObject)
    {
    case WAIT_OBJECT_0 :
      bQuit = true;
      break;
    case WAIT_TIMEOUT :
      {
        bool bDoneSomething = false;
        {
          CAutoLock AutoLock(&m_ImageProcessingLock);
          bDoneSomething = GetImageFromMixer();
        }
        if (m_rtTimePerFrame == 0 && bDoneSomething)
        {
          //CAutoLock lock(this);
          //CAutoLock lock2(&m_ImageProcessingLock);
          //CAutoLock cRenderLock(&m_RenderLock);

          // Use the code from VMR9 to get the movie fps, as this method is reliable.
#if 0 //Don't need this part because its done when the connection is done
          Com::SmartPtr<IPin>      pPin;
          CMediaType        mt;
          if (
            SUCCEEDED (m_pOuterEVR->FindPin(L"EVR Input0", &pPin)) &&
            SUCCEEDED (pPin->ConnectionMediaType(&mt)) )
          {
            ExtractAvgTimePerFrame (&mt, m_rtTimePerFrame);

            m_bInterlaced = ExtractInterlaced(&mt);

          }
#endif
          // If framerate not set by Video Decoder choose 23.97...
          if (m_rtTimePerFrame == 0) 
            m_rtTimePerFrame = 417166;

          m_fps = (float)(10000000.0 / m_rtTimePerFrame);
#if 0 //Done in dvdplayervideo
          if (!g_renderManager.IsConfigured())
          {
            g_renderManager.Configure(m_NativeVideoSize.cx, m_NativeVideoSize.cy, m_AspectRatio.cx, m_AspectRatio.cy, m_fps,
              CONF_FLAGS_FULLSCREEN);
            CLog::Log(LOGDEBUG, "%s Render manager configured (FPS: %f)", __FUNCTION__, m_fps);
          }
#endif
        }

      }
      break;
    }
  }

  timeEndPeriod (dwResolution);
//  if (pfAvRevertMmThreadCharacteristics) pfAvRevertMmThreadCharacteristics (hAvrt);
}

bool DsAllocator::GetImageFromMixer()
{
  MFT_OUTPUT_DATA_BUFFER    Buffer;
  HRESULT                   hr = S_OK;
  DWORD                     dwStatus;
  REFERENCE_TIME            nsSampleTime;
  int64_t                   llClockBefore = 0;
  int64_t                   llClockAfter  = 0;
  int64_t                   llMixerLatency;
  UINT                      dwSurface;
  bool                      bDoneSomething = false;

  while (SUCCEEDED(hr))
  {
    Com::SmartPtr<IMFSample>    pSample;

    if (FAILED (GetFreeSample (&pSample)))
    {
      m_bWaitingSample = true;
      break;
    }

    memset (&Buffer, 0, sizeof(Buffer));
    Buffer.pSample  = pSample;
    pSample->GetUINT32 (GUID_SURFACE_INDEX, &dwSurface);

    {
      llClockBefore = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
      hr = m_pMixer->ProcessOutput (0 , 1, &Buffer, &dwStatus);
      llClockAfter = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
    }

    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) 
    {
      MoveToFreeList (pSample, false);
      break;
    }

    if (m_pSink) 
    {
      //CAutoLock autolock(this); We shouldn't need to lock here, m_pSink is thread safe
      llMixerLatency = llClockAfter - llClockBefore;
      m_pSink->Notify (EC_PROCESSING_LATENCY, (LONG_PTR)&llMixerLatency, 0);
    }

    pSample->GetSampleTime (&nsSampleTime);
    REFERENCE_TIME        nsDuration;
    pSample->GetSampleDuration (&nsDuration);
    //Dont give a fuck about the tearing test
    /*if (AfxGetMyApp()->m_fTearingTest)
    {
      RECT    rcTearing;
      
      rcTearing.left    = m_nTearingPos;
      rcTearing.top    = 0;
      rcTearing.right    = rcTearing.left + 4;
      rcTearing.bottom  = m_NativeVideoSize.cy;
      m_pD3DDev->ColorFill (m_pVideoSurface[dwSurface], &rcTearing, D3DCOLOR_ARGB (255,255,0,0));

      rcTearing.left  = (rcTearing.right + 15) % m_NativeVideoSize.cx;
      rcTearing.right  = rcTearing.left + 4;
      m_pD3DDev->ColorFill (m_pVideoSurface[dwSurface], &rcTearing, D3DCOLOR_ARGB (255,255,0,0));
      m_nTearingPos = (m_nTearingPos + 7) % m_NativeVideoSize.cx;
    }  */

    int64_t TimePerFrame = m_rtTimePerFrame;
    TRACE_EVR ("EVR: Get from Mixer : %d  (%I64d) (%I64d)\n", dwSurface, nsSampleTime, TimePerFrame!=0?nsSampleTime/TimePerFrame:0);

    MoveToScheduledList (pSample, false);
    bDoneSomething = true;
    if (m_rtTimePerFrame == 0)
      break;
  }

  return bDoneSomething;
}


void DsAllocator::StartWorkerThreads()
{
  DWORD    dwThreadId;

  if (m_nRenderState == Shutdown)
  {
    m_hEvtQuit    = CreateEvent (NULL, TRUE, FALSE, NULL);
    m_hEvtFlush    = CreateEvent (NULL, TRUE, FALSE, NULL);

    m_hThread    = ::CreateThread(NULL, 0, PresentThread, (LPVOID)this, 0, &dwThreadId);
    SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
    m_hGetMixerThread = ::CreateThread(NULL, 0, GetMixerThreadStatic, (LPVOID)this, 0, &dwThreadId);
    SetThreadPriority(m_hGetMixerThread, THREAD_PRIORITY_HIGHEST);

    m_nRenderState    = Stopped;
    TRACE_EVR ("EVR: Worker threads started...\n");
  }
}

void DsAllocator::StopWorkerThreads()
{
  if (m_nRenderState != Shutdown)
  {
    SetEvent (m_hEvtFlush);
    m_bEvtFlush = true;
    SetEvent (m_hEvtQuit);
    m_bEvtQuit = true;

    m_drawingIsDone.Set();

    if ((m_hThread != INVALID_HANDLE_VALUE) && (WaitForSingleObject (m_hThread, 10000) == WAIT_TIMEOUT))
    {
      ASSERT (FALSE);
      TerminateThread (m_hThread, 0xDEAD);
    }
    if ((m_hGetMixerThread != INVALID_HANDLE_VALUE) && (WaitForSingleObject (m_hGetMixerThread, 10000) == WAIT_TIMEOUT))
    {
      ASSERT (FALSE);
      TerminateThread (m_hGetMixerThread, 0xDEAD);
    }

    if (m_hThread     != INVALID_HANDLE_VALUE) CloseHandle (m_hThread);
    if (m_hGetMixerThread     != INVALID_HANDLE_VALUE) CloseHandle (m_hGetMixerThread);
    if (m_hEvtFlush     != INVALID_HANDLE_VALUE) CloseHandle (m_hEvtFlush);
    if (m_hEvtQuit     != INVALID_HANDLE_VALUE) CloseHandle (m_hEvtQuit);

    m_bEvtFlush = false;
    m_bEvtQuit = false;


    TRACE_EVR ("EVR: Worker threads stopped...\n");
  }
  m_nRenderState = Shutdown;
}

DWORD WINAPI DsAllocator::PresentThread(LPVOID lpParam)
{
  DsAllocator*    pThis = (DsAllocator*) lpParam;
  pThis->RenderThread();
  return 0;
}

void DsAllocator::RenderThread()
{
  HANDLE        hAvrt;
  DWORD         dwTaskIndex   = 0;
  HANDLE        hEvts[]       = { m_hEvtQuit, m_hEvtFlush};
  bool          bQuit         = false;
  TIMECAPS      tc;
  DWORD         dwResolution;
  MFTIME        nsSampleTime;
  int64_t       llClockTime;
  DWORD         dwUser = 0;
  DWORD         dwObject;

  
  // Tell Vista Multimedia Class Scheduler we are a playback thretad (increase priority)
  if (pfAvSetMmThreadCharacteristicsW)  hAvrt = pfAvSetMmThreadCharacteristicsW (L"Playback", &dwTaskIndex);
  if (pfAvSetMmThreadPriority)      pfAvSetMmThreadPriority (hAvrt, AVRT_PRIORITY_HIGH /*AVRT_PRIORITY_CRITICAL*/);

    timeGetDevCaps(&tc, sizeof(TIMECAPS));
    dwResolution = std::min(std::max(tc.wPeriodMin, (UINT) 0), tc.wPeriodMax);
    dwUser    = timeBeginPeriod(dwResolution);

  int NextSleepTime = 1;
  while (!bQuit)
  {
    int64_t  llPerf = 0;//CTimeUtils::GetPerfCounter();
    if (NextSleepTime == 0)
      NextSleepTime = 1;
    dwObject = WaitForMultipleObjects (_countof(hEvts), hEvts, FALSE, std::max(NextSleepTime < 0 ? 1 : NextSleepTime, 0));
    if (NextSleepTime > 1)
      NextSleepTime = 0;
    else if (NextSleepTime == 0)
      NextSleepTime = -1;      
    switch (dwObject)
    {
    case WAIT_OBJECT_0 :
      bQuit = true;
      break;
    case WAIT_OBJECT_0 + 1 :
      // Flush pending samples!
      FlushSamples();
      m_bEvtFlush = false;
      ResetEvent(m_hEvtFlush);
      m_drawingIsDone.Reset();
      TRACE_EVR ("EVR: Flush done!\n");
      break;

    case WAIT_TIMEOUT :

      if ((m_LastSetOutputRange != -1 && m_LastSetOutputRange != (0/*(CEVRRendererSettings *)g_dsSettings.pRendererSettings)->outputRange*/) || m_bPendingRenegotiate))
      {
        FlushSamples();
        RenegotiateEVRMediaType();
        m_bPendingRenegotiate = false;
      }

      {
        Com::SmartPtr<IMFSample>    pMFSample;
        int64_t  llPerf = 0;//CTimeUtils::GetPerfCounter();
        int                         nSamplesLeft = 0;
        if (SUCCEEDED (GetScheduledSample(&pMFSample, nSamplesLeft)))
        { 
          m_pCurrentDisplaydSample = pMFSample;

          bool bValidSampleTime = true;
          HRESULT hGetSampleTime = pMFSample->GetSampleTime (&nsSampleTime);
          if (hGetSampleTime != S_OK || nsSampleTime == 0)
          {
            bValidSampleTime = false;
          }
          // We assume that all samples have the same duration
          int64_t SampleDuration = 0; 
          pMFSample->GetSampleDuration(&SampleDuration);

          TRACE_EVR("EVR: RenderThread ==>> Presenting surface %d  (%I64d)\n", m_nCurSurface, nsSampleTime);

          bool bStepForward = false;

          if (m_nStepCount < 0)
          {
            // Drop frame
            TRACE_EVR ("EVR: Dropped frame\n");
            m_pcFrames++;
            bStepForward = true;
            m_nStepCount = 0;
          }
          else if (m_nStepCount > 0)
          {
            pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&m_nCurSurface);
            ++m_OrderedPaint;
            //if (!g_bExternalSubtitleTime)
              //__super::SetTime (g_tSegmentStart + nsSampleTime);
            
            PaintInternal();

            m_nDroppedUpdate = 0;
            CompleteFrameStep (false);
            bStepForward = true;
          }
          else if ((m_nRenderState == Started))
          {
            int64_t CurrentCounter = 0;//CTimeUtils::GetPerfCounter();
            // Calculate wake up timer
            if (!m_bSignaledStarvation)
            {
              llClockTime = GetClockTime(CurrentCounter);
              m_StarvationClock = llClockTime;
            }
            else
            {
              llClockTime = m_StarvationClock;
            }

            if (!bValidSampleTime)
            {
              // Just play as fast as possible
              bStepForward = true;
              pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&m_nCurSurface);
              ++m_OrderedPaint;
              //if (!g_bExternalSubtitleTime)
              //  __super::SetTime (g_tSegmentStart + nsSampleTime);
              
              //PaintInternal();

            }
            else
            {
              int64_t TimePerFrame = (int64_t) (GetFrameTime() * 10000000);
              int64_t DrawTime = (int64_t) ((m_PaintTime) * 0.9 - 20000); // 2 ms offset
              //if (!s.iVMR9VSync)
                DrawTime = 0;

              int64_t SyncOffset = 0;
              int64_t VSyncTime = 0;
              int64_t TimeToNextVSync = -1;
              bool bVSyncCorrection = false;
              double DetectedRefreshTime;
              double DetectedScanlinesPerFrame;
              double DetectedScanlineTime;
              int DetectedRefreshRatePos;
              {
                CAutoLock Lock(&m_RefreshRateLock);  
                DetectedRefreshTime = m_DetectedRefreshTime;
                DetectedRefreshRatePos = m_DetectedRefreshRatePos;
                DetectedScanlinesPerFrame = m_DetectedScanlinesPerFrame;
                DetectedScanlineTime = m_DetectedScanlineTime;
              }

              if (DetectedRefreshRatePos < 20 || !DetectedRefreshTime || !DetectedScanlinesPerFrame)
              {
                DetectedRefreshTime = 1.0/m_RefreshRate;
                DetectedScanlinesPerFrame = m_ScreenSize.cy;
                DetectedScanlineTime = DetectedRefreshTime / double(m_ScreenSize.cy);
              }

#if 0
              if (g_dsSettings.pRendererSettings->vSync)
              {
                bVSyncCorrection = true;
                double TargetVSyncPos = GetVBlackPos();
                double RefreshLines = DetectedScanlinesPerFrame;
                double ScanlinesPerSecond = 1.0/DetectedScanlineTime;
                double CurrentVSyncPos = fmod(double(m_VBlankStartMeasure) + ScanlinesPerSecond * ((CurrentCounter - m_VBlankStartMeasureTime) / 10000000.0), RefreshLines);
                double LinesUntilVSync = 0;
                //TargetVSyncPos -= ScanlinesPerSecond * (DrawTime/10000000.0);
                //TargetVSyncPos -= 10;
                TargetVSyncPos = fmod(TargetVSyncPos, RefreshLines);
                if (TargetVSyncPos < 0)
                  TargetVSyncPos += RefreshLines;
                if (TargetVSyncPos > CurrentVSyncPos)
                  LinesUntilVSync = TargetVSyncPos - CurrentVSyncPos;
                else
                  LinesUntilVSync = (RefreshLines - CurrentVSyncPos) + TargetVSyncPos;
                double TimeUntilVSync = LinesUntilVSync * DetectedScanlineTime;
                TimeToNextVSync = (int64_t) (TimeUntilVSync * 10000000.0);
                VSyncTime = (int64_t) (DetectedRefreshTime * 10000000.0);

                int64_t ClockTimeAtNextVSync = llClockTime + (int64_t) ((TimeUntilVSync * 10000000.0) * m_ModeratedTimeSpeed);
  
                SyncOffset = (nsSampleTime - ClockTimeAtNextVSync);

//                if (SyncOffset < 0)
//                  TRACE_EVR("EVR: SyncOffset(%d): %I64d     %I64d     %I64d\n", m_nCurSurface, SyncOffset, TimePerFrame, VSyncTime);
              }
              else
#endif
              SyncOffset = (nsSampleTime - llClockTime);
              
              //int64_t SyncOffset = nsSampleTime - llClockTime;
              TRACE_EVR ("EVR: SyncOffset: %I64d SampleFrame: %I64d ClockFrame: %I64d\n", SyncOffset, TimePerFrame!=0 ? nsSampleTime/TimePerFrame : 0, TimePerFrame!=0 ? llClockTime /TimePerFrame : 0);
              if (SampleDuration > 1 && !m_DetectedLock)
                TimePerFrame = SampleDuration;

              int64_t MinMargin;
              if (m_FrameTimeCorrection && 0)
                MinMargin = 15000;
              else
                MinMargin = 15000 + std::min((int64_t) (m_DetectedFrameTimeStdDev), 20000i64);
              int64_t TimePerFrameMargin = (int64_t) std::min((__int64) (TimePerFrame * 0.11), std::max((__int64) (TimePerFrame * 0.02), MinMargin));
              int64_t TimePerFrameMargin0 = (int64_t) TimePerFrameMargin/2;
              int64_t TimePerFrameMargin1 = 0;

              if (m_DetectedLock && TimePerFrame < VSyncTime)
                VSyncTime = TimePerFrame;

              if (m_VSyncMode == 1)
                TimePerFrameMargin1 = -TimePerFrameMargin;
              else if (m_VSyncMode == 2)
                TimePerFrameMargin1 = TimePerFrameMargin;

              m_LastSampleOffset = SyncOffset;
              m_bLastSampleOffsetValid = true;

              int64_t VSyncOffset0 = 0;
              bool bDoVSyncCorrection = false;
              if ((SyncOffset < -(TimePerFrame + TimePerFrameMargin0 - TimePerFrameMargin1)) && nSamplesLeft > 0) // Only drop if we have something else to display at once
              {
                // Drop frame
                TRACE_EVR ("EVR: Dropped frame\n");
                m_pcFrames++;
                bStepForward = true;
                ++m_nDroppedUpdate;
                NextSleepTime = 0;
//                VSyncOffset0 = (-SyncOffset) - VSyncTime;
                //VSyncOffset0 = (-SyncOffset) - VSyncTime + TimePerFrameMargin1;
                //m_LastPredictedSync = VSyncOffset0;
                bDoVSyncCorrection = false;
              }
              else if (SyncOffset < TimePerFrameMargin1)
              {

                if (bVSyncCorrection)
                {
//                  VSyncOffset0 = -SyncOffset;
                  VSyncOffset0 = -SyncOffset;
                  bDoVSyncCorrection = true;
                }

                // Paint and prepare for next frame
                TRACE_EVR ("EVR: Normalframe\n");
                m_nDroppedUpdate = 0;
                bStepForward = true;
                pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&m_nCurSurface);
                m_LastFrameDuration = nsSampleTime - m_LastSampleTime;
                m_LastSampleTime = nsSampleTime;
                m_LastPredictedSync = VSyncOffset0;

                ++m_OrderedPaint;

#if 0
                if (!g_bExternalSubtitleTime)
                  __super::SetTime (g_tSegmentStart + nsSampleTime);
#endif

                PaintInternal();
                
                NextSleepTime = 0;
                m_pcFramesDrawn++;
              }
              else
              {
                if (TimeToNextVSync >= 0 && SyncOffset > 0)
                {
                  NextSleepTime = (int) ((TimeToNextVSync)/10000) - 2;
                }
                else
                  NextSleepTime = (int) ((SyncOffset)/10000) - 2;

                if (NextSleepTime > TimePerFrame)
                  NextSleepTime = 1;

                if (NextSleepTime < 0)
                  NextSleepTime = 0;
                NextSleepTime = 1;
                TRACE_EVR ("EVR: Delay\n");
              }

              if (bDoVSyncCorrection)
              {
                //int64_t VSyncOffset0 = (((SyncOffset) % VSyncTime) + VSyncTime) % VSyncTime;
                int64_t Margin = TimePerFrameMargin;

                int64_t VSyncOffsetMin = 30000000000000;
                int64_t VSyncOffsetMax = -30000000000000;
                for (int i = 0; i < 5; ++i)
                {
                  VSyncOffsetMin = std::min(m_VSyncOffsetHistory[i], VSyncOffsetMin);
                  VSyncOffsetMax = std::max(m_VSyncOffsetHistory[i], VSyncOffsetMax);
                }

                m_VSyncOffsetHistory[m_VSyncOffsetHistoryPos] = VSyncOffset0;
                m_VSyncOffsetHistoryPos = (m_VSyncOffsetHistoryPos + 1) % 5;

//                int64_t VSyncTime2 = VSyncTime2 + (VSyncOffsetMax - VSyncOffsetMin);
                //VSyncOffsetMin; = (((VSyncOffsetMin) % VSyncTime) + VSyncTime) % VSyncTime;
                //VSyncOffsetMax = (((VSyncOffsetMax) % VSyncTime) + VSyncTime) % VSyncTime;

                TRACE_EVR("EVR: SyncOffset(%d, %d): %8I64d     %8I64d     %8I64d     %8I64d\n", m_nCurSurface, m_VSyncMode,VSyncOffset0, VSyncOffsetMin, VSyncOffsetMax, VSyncOffsetMax - VSyncOffsetMin);

                if (m_VSyncMode == 0)
                {
                  // 23.976 in 60 Hz
                  if (VSyncOffset0 < Margin && VSyncOffsetMax > (VSyncTime - Margin))
                  {
                    m_VSyncMode = 2;
                  }
                  else if (VSyncOffset0 > (VSyncTime - Margin) && VSyncOffsetMin < Margin)
                  {
                    m_VSyncMode = 1;
                  }
                }
                else if (m_VSyncMode == 2)
                {
                  if (VSyncOffsetMin > (Margin))
                  {
                    m_VSyncMode = 0;
                  }
                }
                else if (m_VSyncMode == 1)
                {
                  if (VSyncOffsetMax < (VSyncTime - Margin))
                  {
                    m_VSyncMode = 0;
                  }
                }
              }

            }
          }
          m_pCurrentDisplaydSample = NULL;
          if (bStepForward)
          {
            MoveToFreeList(pMFSample, true);
            CheckWaitingSampleFromMixer();
            m_MaxSampleDuration = std::max(SampleDuration, m_MaxSampleDuration);
          }
          else
            MoveToScheduledList(pMFSample, true);
        }
        else if (m_bLastSampleOffsetValid && m_LastSampleOffset < -10000000) // Only starve if we are 1 seconds behind
        {
          if (m_nRenderState == Started)
          {
            m_pSink->Notify(EC_STARVATION, 0, 0);
            m_bSignaledStarvation = true;
          }
        }
        //GetImageFromMixer();
      }
//      else
//      {        
//        TRACE_EVR ("EVR: RenderThread ==>> Flush before rendering frame!\n");
//      }

      break;
    }
  }

  timeEndPeriod (dwResolution);
  if (pfAvRevertMmThreadCharacteristics) pfAvRevertMmThreadCharacteristics (hAvrt);
}

/*Sample stuff*/
void DsAllocator::RemoveAllSamples()
{
  CAutoLock AutoLock(&m_ImageProcessingLock);

  FlushSamples();
  m_ScheduledSamples.Clear();
  m_FreeSamples.Clear();

  {
    m_DisplaydSampleQueueLock.Lock();
    while (!m_pCurrentDisplaydSampleQueue.empty())
      m_pCurrentDisplaydSampleQueue.pop();
    m_DisplaydSampleQueueLock.Unlock();
  }

  m_LastScheduledSampleTime = -1;
  m_LastScheduledUncorrectedSampleTime = -1;
  //ASSERT(m_nUsedBuffer == 0);
  m_nUsedBuffer = 0;
}

HRESULT DsAllocator::GetFreeSample(IMFSample** ppSample)
{
  CAutoLock lock(&m_SampleQueueLock);
  HRESULT    hr = S_OK;

  if (m_FreeSamples.GetCount() > 1)  // <= Cannot use first free buffer (can be currently displayed)
  {
    InterlockedIncrement (&m_nUsedBuffer);
    //*ppSample = m_FreeSamples.RemoveHead().Detach();
    m_FreeSamples.RemoveFront(ppSample);
  }
  else
    hr = MF_E_SAMPLEALLOCATOR_EMPTY;

  return hr;
}


HRESULT DsAllocator::GetScheduledSample(IMFSample** ppSample, int &_Count)
{
  CAutoLock lock(&m_SampleQueueLock);
  HRESULT    hr = S_OK;

  _Count = m_ScheduledSamples.GetCount();
  if (_Count > 0)
  {
    //*ppSample = m_ScheduledSamples.RemoveHead().Detach();
    m_ScheduledSamples.RemoveFront(ppSample);
    --_Count;
  }
  else
    hr = MF_E_SAMPLEALLOCATOR_EMPTY;

  return hr;
}


void DsAllocator::MoveToFreeList(IMFSample* pSample, bool bTail)
{
  CAutoLock lock(&m_SampleQueueLock);
  InterlockedDecrement (&m_nUsedBuffer);
  if (m_bPendingMediaFinished && m_nUsedBuffer == 0)
  {
    m_bPendingMediaFinished = false;
    m_pSink->Notify (EC_COMPLETE, 0, 0);
  }
  if (bTail)
    m_FreeSamples.InsertBack(pSample);
  else
    m_FreeSamples.InsertFront(pSample);
}


void DsAllocator::MoveToScheduledList(IMFSample* pSample, bool _bSorted)
{

  if (_bSorted)
  {
    CAutoLock lock(&m_SampleQueueLock);
    m_ScheduledSamples.InsertFront(pSample);
  }
  else
  {

    CAutoLock lock(&m_SampleQueueLock);

    double ForceFPS = 0.0;
//    double ForceFPS = 59.94;
//    double ForceFPS = 23.976;
    if (ForceFPS != 0.0)
      m_rtTimePerFrame = (int64_t) (10000000.0 / ForceFPS);
    int64_t Duration = m_rtTimePerFrame;
    int64_t PrevTime = m_LastScheduledUncorrectedSampleTime;
    int64_t Time;
    int64_t SetDuration;
    pSample->GetSampleDuration(&SetDuration);
    pSample->GetSampleTime(&Time);
    m_LastScheduledUncorrectedSampleTime = Time;

    m_bCorrectedFrameTime = false;

    int64_t Diff2 = PrevTime - (int64_t) (m_LastScheduledSampleTimeFP*10000000.0);
    int64_t Diff = Time - PrevTime;
    if (PrevTime == -1)
      Diff = 0;
    if (Diff < 0)
      Diff = -Diff;
    if (Diff2 < 0)
      Diff2 = -Diff2;
    if (Diff < m_rtTimePerFrame*8 && m_rtTimePerFrame && Diff2 < m_rtTimePerFrame*8) // Detect seeking
    {
      int iPos = (m_DetectedFrameTimePos++) % 60;
      int64_t Diff = Time - PrevTime;
      if (PrevTime == -1)
        Diff = 0;
      m_DetectedFrameTimeHistory[iPos] = Diff;
    
      if (m_DetectedFrameTimePos >= 10)
      {
        int nFrames = std::min(m_DetectedFrameTimePos, 60);
        int64_t DectedSum = 0;
        for (int i = 0; i < nFrames; ++i)
        {
          DectedSum += m_DetectedFrameTimeHistory[i];
        }

        double Average = double(DectedSum) / double(nFrames);
        double DeviationSum = 0.0;
        for (int i = 0; i < nFrames; ++i)
        {
          double Deviation = m_DetectedFrameTimeHistory[i] - Average;
          DeviationSum += Deviation*Deviation;
        }
    
        double StdDev = sqrt(DeviationSum/double(nFrames));

        m_DetectedFrameTimeStdDev = StdDev;

        double DetectedRate = 1.0/ (double(DectedSum) / (nFrames * 10000000.0) );

        double AllowedError = 0.0003;

        static double AllowedValues[] = {60.0, 59.94, 50.0, 48.0, 47.952, 30.0, 29.97, 25.0, 24.0, 23.976};

        int nAllowed = sizeof(AllowedValues) / sizeof(AllowedValues[0]);
        for (int i = 0; i < nAllowed; ++i)
        {
          if (fabs(1.0 - DetectedRate / AllowedValues[i]) < AllowedError)
          {
            DetectedRate = AllowedValues[i];
            break;
          }
        }

        m_DetectedFrameTimeHistoryHistory[m_DetectedFrameTimePos % 500] = DetectedRate;

        class CAutoInt
        {
        public:

          int m_Int;

          CAutoInt()
          {
            m_Int = 0;
          }
          CAutoInt(int _Other)
          {
            m_Int = _Other;
          }

          operator int () const
          {
            return m_Int;
          }

          CAutoInt &operator ++ ()
          {
            ++m_Int;
            return *this;
          }
        };

        std::map<double,CAutoInt> Map;
        for( int i=0;i<500;++i )
        {
          std::map<double,CAutoInt>::iterator it;
          it = Map.find(m_DetectedFrameTimeHistoryHistory[i]);
          if (it == Map.end())
            Map.insert(std::make_pair(m_DetectedFrameTimeHistoryHistory[i],1));
          else
            ++Map[m_DetectedFrameTimeHistoryHistory[i]];
        }

        std::map<double,CAutoInt>::iterator it=Map.begin();
        double BestVal = 0.0;
        int BestNum = 5;
        while (it != Map.end())
        {
          if ((*it).second > BestNum && (*it).first != 0.0)
          {
            BestNum = (*it).second;
            BestVal = (*it).first;
          }
          ++it;
        }
        
        /*CMap<double, double, CAutoInt, CAutoInt> Map;

        for (int i = 0; i < 500; ++i)
        {
          ++Map[m_DetectedFrameTimeHistoryHistory[i]];
        }

        POSITION Pos = Map.GetStartPosition();
        double BestVal = 0.0;
        int BestNum = 5;
        while (Pos)
        {
          double Key;
          CAutoInt Value;
          Map.GetNextAssoc(Pos, Key, Value);
          if (Value.m_Int > BestNum && Key != 0.0)
          {
            BestNum = Value.m_Int;
            BestVal = Key;
          }
        }*/

        m_DetectedLock = false;
        for (int i = 0; i < nAllowed; ++i)
        {
          if (BestVal == AllowedValues[i])
          {
            m_DetectedLock = true;
            break;
          }
        }
        if (BestVal != 0.0)
        {
          m_DetectedFrameRate = BestVal;
          m_DetectedFrameTime = 1.0 / BestVal;
        }
      }

      int64_t PredictedNext = PrevTime + m_rtTimePerFrame;
      int64_t PredictedDiff = PredictedNext - Time;
      if (PredictedDiff < 0)
        PredictedDiff = -PredictedDiff;

      if (m_DetectedFrameTime != 0.0 
        //&& PredictedDiff > 15000 
        && m_DetectedLock /*&& ((CEVRRendererSettings *)g_dsSettings.pRendererSettings)->enableFrameTimeCorrection*/)
      {
        double CurrentTime = Time / 10000000.0;
        double LastTime = m_LastScheduledSampleTimeFP;
        double PredictedTime = LastTime + m_DetectedFrameTime;
        if (fabs(PredictedTime - CurrentTime) > 0.0015) // 1.5 ms wrong, lets correct
        {
          CurrentTime = PredictedTime;
          Time = (int64_t) (CurrentTime * 10000000.0);
          pSample->SetSampleTime(Time);
          pSample->SetSampleDuration((int64_t) (m_DetectedFrameTime * 10000000.0));
          m_bCorrectedFrameTime = true;
          m_FrameTimeCorrection = 30;
        }
        m_LastScheduledSampleTimeFP = CurrentTime;
      }
      else
        m_LastScheduledSampleTimeFP = Time / 10000000.0;
    }
    else
    {
      m_LastScheduledSampleTimeFP = Time / 10000000.0;
      if (Diff > m_rtTimePerFrame*8)
      {
        // Seek
        m_bSignaledStarvation = false;
        m_DetectedFrameTimePos = 0;
        m_DetectedLock = false;
      }
    }

    TRACE_EVR("EVR: Time: %f %f %f\n", Time / 10000000.0, SetDuration / 10000000.0, m_DetectedFrameRate);
    if (!m_bCorrectedFrameTime && m_FrameTimeCorrection)
      --m_FrameTimeCorrection;

#if 0
    if (Time <= m_LastScheduledUncorrectedSampleTime && m_LastScheduledSampleTime >= 0)
      PrevTime = m_LastScheduledSampleTime;

    m_bCorrectedFrameTime = false;
    if (PrevTime != -1 && (Time >= PrevTime - ((Duration*20)/9) || Time == 0) || ForceFPS != 0.0)
    {
      if (Time - PrevTime > ((Duration*20)/9) && Time - PrevTime < Duration * 8 || Time == 0 || ((Time - PrevTime) < (Duration / 11)) || ForceFPS != 0.0)
      {
        // Error!!!!
        Time = PrevTime + Duration;
        pSample->SetSampleTime(Time);
        pSample->SetSampleDuration(Duration);
        m_bCorrectedFrameTime = true;
        TRACE_EVR("EVR: Corrected invalid sample time\n");
      }
    }
    if (Time+Duration*10 < m_LastScheduledSampleTime)
    {
      // Flush when repeating movie
      FlushSamplesInternal();
    }
#endif

#if 0
    static int64_t LastDuration = 0;
    int64_t SetDuration = m_rtTimePerFrame;
    pSample->GetSampleDuration(&SetDuration);
    if (SetDuration != LastDuration)
    {
      TRACE_EVR("EVR: Old duration: %I64d New duration: %I64d\n", LastDuration, SetDuration);
    }
    LastDuration = SetDuration;
#endif
    m_LastScheduledSampleTime = Time;

    m_ScheduledSamples.InsertBack(pSample);

  }
}



void DsAllocator::FlushSamples()
{
  CAutoLock        lock(this);
  CAutoLock        lock2(&m_SampleQueueLock);
  
  FlushSamplesInternal();
  m_LastScheduledSampleTime = -1;
}

void DsAllocator::FlushSamplesInternal()
{
  while (m_ScheduledSamples.GetCount() > 0)
  {
    Com::SmartPtr<IMFSample>    pMFSample;

    //pMFSample = m_ScheduledSamples.RemoveHead();
    m_ScheduledSamples.RemoveFront(&pMFSample);
    MoveToFreeList (pMFSample, true);
  }

  m_LastSampleOffset      = 0;
  m_bLastSampleOffsetValid  = false;
  m_bSignaledStarvation = false;
}
