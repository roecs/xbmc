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
#include "threads\Thread.h"
// === Helper functions
#define CheckHR(exp) {if(FAILED(hr = exp)) return hr;}
//#define PaintInternal() {assert(0);}
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

#define TRACE_EVR(x) CLog::DebugLog(x)

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

////////////////////////////////////////////////////////////////////////////////////////////
class CEvrMixerThread : public CThread
{
public:
  CEvrMixerThread(IMFTransform* mixer, IMediaEventSink* sink);
  virtual ~CEvrMixerThread();

  unsigned int        GetReadyCount(void);
  unsigned int        GetFreeCount(void);
  IMFSample*          ReadyListPop(void);
  void                FreeListPush(IMFSample* pBuffer);
  bool                WaitOutput(unsigned int msec);

protected:
  bool                ProcessOutput(void);
  virtual void        Process(void);

  Com::CSyncPtrQueue<IMFSample> m_FreeList;
  Com::CSyncPtrQueue<IMFSample> m_ReadyList;

  Com::SmartPtr<IMFTransform>       m_pMixer;
  Com::SmartPtr<IMediaEventSink>    m_pSink;
  unsigned int                      m_timeout;
  bool                m_format_valid;
  int                 m_width;
  int                 m_height;
  uint64_t            m_timestamp;
  uint64_t            m_PictureNumber;
  uint8_t             m_color_space;
  unsigned int        m_color_range;
  unsigned int        m_color_matrix;
  int                 m_interlace;
  bool                m_framerate_tracking;
  uint64_t            m_framerate_cnt;
  double              m_framerate_timestamp;
  double              m_framerate;
  int                 m_aspectratio_x;
  int                 m_aspectratio_y;
  CEvent              m_ready_event;
};

CEvrMixerThread::CEvrMixerThread(IMFTransform* mixer, IMediaEventSink* sink) :
  CThread(),
  m_pMixer(mixer),
  m_pSink(sink),
  m_timeout(20),
  m_format_valid(false),
  m_framerate_tracking(false),
  m_framerate_cnt(0),
  m_framerate_timestamp(0.0),
  m_framerate(0.0)
{
}

CEvrMixerThread::~CEvrMixerThread()
{

  while(m_ReadyList.Count())
    m_ReadyList.Pop()->Release();
  while(m_FreeList.Count())
    m_FreeList.Pop()->Release();
  m_pMixer = NULL;
  m_pSink = NULL;
}

unsigned int CEvrMixerThread::GetReadyCount(void)
{
  return m_ReadyList.Count();
}

unsigned int CEvrMixerThread::GetFreeCount(void)
{
  return m_FreeList.Count();
}

IMFSample* CEvrMixerThread::ReadyListPop(void)
{
  IMFSample *pBuffer = m_ReadyList.Pop();
  return pBuffer;
}

void CEvrMixerThread::FreeListPush(IMFSample* pBuffer)
{
  m_FreeList.Push(pBuffer);
  pBuffer->AddRef();
}

bool CEvrMixerThread::WaitOutput(unsigned int msec)
{
  return m_ready_event.WaitMSec(msec);
}

bool CEvrMixerThread::ProcessOutput(void)
{
  HRESULT     hr = S_OK;
  DWORD       dwStatus = 0;
  LONGLONG    mixerStartTime = 0, mixerEndTime = 0, llMixerLatency = 0;
  MFTIME      systemTime = 0;
  
  
  if (m_FreeList.Count() == 0)
    return false;

  MFT_OUTPUT_DATA_BUFFER dataBuffer;
  ZeroMemory(&dataBuffer, sizeof(dataBuffer));
  IMFSample *pSample = NULL;

  
          // Get next output buffer from the free list
  pSample = m_FreeList.Pop();
  dataBuffer.pSample = pSample;
  mixerStartTime = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
  hr = m_pMixer->ProcessOutput (0 , 1, &dataBuffer, &dwStatus);
  mixerEndTime = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
  if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
  {
    m_FreeList.Push(pSample);
    return false;
  }
  if (m_pSink)
  {
    llMixerLatency = mixerStartTime - mixerEndTime;
    m_pSink->Notify (EC_PROCESSING_LATENCY, (LONG_PTR)&llMixerLatency, 0);
  }
  m_ReadyList.Push(pSample);
}

void CEvrMixerThread::Process(void)
{
  DWORD res;
  // decoder is primed so now calls in DtsProcOutputXXCopy will block
  while (!m_bStop)
  {
    if (SUCCEEDED(m_pMixer->GetOutputStatus(&res)))
    {
      if (res & MFT_OUTPUT_STATUS_SAMPLE_READY)
        break;
    }
    
    Sleep(10);

  }
  CLog::Log(LOGINFO, "%s: CEvrMixerThread Started...", __FUNCTION__);
  while (!m_bStop)
  {
    if (SUCCEEDED(m_pMixer->GetOutputStatus(&res)&&(res & MFT_OUTPUT_STATUS_SAMPLE_READY)))
      ProcessOutput();
    else
      Sleep(1);

  }
}

DsAllocator::DsAllocator(IDSInfoCallback *pCallback)
  : m_pCallback(pCallback),
    m_pMixerThread(NULL)
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
  m_nUsedBuffer = 0;
  m_iLastSampleTime = 0;
  m_ClockTimeChangeHistoryPos = 0;
  m_ModeratedTimeSpeed = 0;
  m_ModeratedTimeSpeedPrim = 0;
	m_ModeratedTimeSpeedDiff = 0;
  m_OrderedPaint = 0;
  m_PaintTime = 0;
  m_PaintTimeMin = 0;
  m_PaintTimeMax = 0;
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
  for (size_t i=0;i<m_pSurfaces.size();i++) 
  {
    SAFE_RELEASE(m_pTextures[i]);
    SAFE_RELEASE(m_pSurfaces[i]);
  }
  Unlock();
}

HRESULT STDMETHODCALLTYPE DsAllocator::InitializeDevice(DWORD_PTR userid,VMR9AllocationInfo* allocinf,DWORD*numbuf)
{
  if (!surfallocnotify) return S_FALSE;
  FlushSamples();
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

HRESULT STDMETHODCALLTYPE DsAllocator::ProcessMessage(MFVP_MESSAGE_TYPE mess,ULONG_PTR mess_para)
{
  switch (mess)
  {
    case MFVP_MESSAGE_FLUSH:
      /*SetEvent(m_hEvtFlush);
      m_bEvtFlush = true;
      m_drawingIsDone.Set();*/
      CLog::DebugLog("EVR: MFVP_MESSAGE_FLUSH");
      if (m_pMixerThread)
        m_pMixerThread->StopThread(true);
      FlushSamples();

      /*while (WaitForSingleObject(m_hEvtFlush, 1) == WAIT_OBJECT_0);*/
    break;
    case MFVP_MESSAGE_INVALIDATEMEDIATYPE: 
    {
      m_bPendingRenegotiate = true;
      FlushSamples();
      RenegotiateEVRMediaType();
      //while (*((volatile bool *)&m_bPendingRenegotiate))
      //  Sleep(1);
    }
    break;
    case MFVP_MESSAGE_PROCESSINPUTNOTIFY: 
    {
      GetImageFromMixer();
    } 
    break;
    case MFVP_MESSAGE_BEGINSTREAMING:
    {
      CLog::DebugLog("EVR Message MFVP_MESSAGE_BEGINSTREAMING received");
      ResetStats();  
    }
    break;
    case MFVP_MESSAGE_ENDSTREAMING:
      CLog::DebugLog("EVR Message MFVP_MESSAGE_ENDSTREAMING received");
      if (m_pMixerThread)
      {
        while(m_BusyList.Count())
          m_pMixerThread->FreeListPush( m_BusyList.Pop() );

        m_pMixerThread->StopThread();
        delete m_pMixerThread;
        m_pMixerThread = NULL;
      }
      break;
    case MFVP_MESSAGE_ENDOFSTREAM: 
    {
     CLog::DebugLog("EVR Message MFVP_MESSAGE_ENDOFSTREAM received");

      m_bPendingMediaFinished = true;
      endofstream=true;
    } 
    break;
    case MFVP_MESSAGE_STEP:
    {
      // Request frame step the param is the number of frame to step
      CLog::Log(LOGINFO, "EVR Message MFVP_MESSAGE_STEP %i", mess_para);
      m_nStepCount = mess_para;
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

  CLog::DebugLog("EVR: OnClockStart  hnsSystemTime = %I64d,   llClockStartOffset = %I64d", hnsSystemTime, llClockStartOffset);
  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;

  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockStop(MFTIME hnsSystemTime)
{
  CLog::DebugLog("EVR: OnClockStop  hnsSystemTime = %I64d", hnsSystemTime);
  m_nRenderState    = Stopped;

  m_ModeratedClockLast = -1;
  m_ModeratedTimeLast = -1;
  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockPause(MFTIME hnsSystemTime)
{
  CLog::DebugLog("EVR: OnClockPause  hnsSystemTime = %I64d", hnsSystemTime);
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
  CLog::DebugLog("EVR: OnClockRestart  hnsSystemTime = %I64d", hnsSystemTime);

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
  
  
  
  m_pMixerThread->Create();


}

int DsAllocator::GetReadySample()
{ 
  return m_pMixerThread->GetReadyCount();
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
  CLog::DebugLog("Surfaceformat is %d, width %d, height %d",format,vwidth,vheight);
  format=D3DFMT_X8R8G8B8;

  RemoveAllSamples();
  m_pMixerThread = new CEvrMixerThread(m_pMixer, m_pSink);
  //CleanupSurfaces();

  m_pSurfaces.resize(10);
  m_pTextures.resize(10);
  for (int i=0;i<10;i++)
  {
    HRESULT hr;
    hr = m_pCallback->GetD3DDev()->CreateTexture(vwidth, vheight, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,&m_pTextures[i],NULL);
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
  
  

  HRESULT hr = S_OK;
  for (size_t i=0;i<m_pSurfaces.size();i++)
  {
    if (m_pSurfaces[i]!=NULL) 
    {
      IMFSample* pMFSample = NULL;
      hr = ptrMFCreateVideoSampleFromSurface(m_pSurfaces[i],&pMFSample);
      pMFSample->SetUINT32 (GUID_SURFACE_INDEX, i);
      
      if (SUCCEEDED (hr))
      {
        pMFSample->SetUINT32 (GUID_SURFACE_INDEX, i);
        m_pMixerThread->FreeListPush(pMFSample);
      }
      ASSERT(SUCCEEDED (hr));
    }
  }

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

bool DsAllocator::WaitOutput(unsigned int msec)
{
  return m_ready_event.WaitMSec(msec);
}

bool DsAllocator::GetD3DSurfaceFromScheduledSample(int *surface_index)
{
  
  Com::SmartPtr<IMFSample>    pMFSample;
  int                         nSamplesLeft = 0;
  CAutoSingleLock lock(m_section);
  if (SUCCEEDED (GetScheduledSample(&pMFSample, nSamplesLeft)))
  {
    HRESULT hr = pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)surface_index);
    m_BusySamples.InsertBack(pMFSample);
    
    
    return true;
  }
  else
  return false;
}

void DsAllocator::Render(const RECT& dst, IDirect3DSurface9* target, int index)
{
  HRESULT hr = S_OK;;
  LONGLONG prestime = 0;
  MFTIME   systime = 0;
  LONGLONG currenttime = 0;
  DWORD waittime = 10;
  if (!m_pCallback || endofstream)
    return;
  LPDIRECT3DDEVICE9 pDevice = m_pCallback->GetD3DDev();
  
  int64_t  llPerf = 0;
  int                         nSamplesLeft = 0;
  CAutoSingleLock lock(m_section);
    

  if ((index < 0 )&& (index > m_pSurfaces.size()))
    return;
  if (m_pSurfaces.at(index))
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
    m_pTextures[index]->GetLevelDesc(0,&desc);
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
  hr = pDevice->SetTexture(0,m_pTextures[index]);
  hr = TextureBlt(pDevice, vv, D3DTEXF_POINT);
    
    //vheight
    //vwidth
    
  if (FAILED(hr))
  {
    hr = pDevice->StretchRect(m_pSurfaces[index], NULL, target, NULL, D3DTEXF_NONE);
    //OutputDebugStringA("DSAllocator Failed to Render Target with EVR");
  }
  //Lock();
  /*if (fullevrsamples.size()==0)
  {
    waittime=0;
    Unlock();
    return;
  }*/
  }
  
}
DWORD WINAPI DsAllocator::GetMixerThreadStatic(LPVOID lpParam)
{
  DsAllocator*    pThis = (DsAllocator*) lpParam;
  pThis->GetMixerThread();
  return 0;
}

void DsAllocator::GetMixerThread()
{
#if 0
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
          //CAutoLock AutoLock(&m_ImageProcessingLock);
          if (m_ImageProcessingLock.TryLock())
          {
            bDoneSomething = GetImageFromMixer();
            m_ImageProcessingLock.Unlock();
          }
        }
        if (m_rtTimePerFrame == 0 && bDoneSomething)
        {
          // If framerate not set by Video Decoder choose 23.97...
          if (m_rtTimePerFrame == 0) 
            m_rtTimePerFrame = 417166;

          m_fps = (float)(10000000.0 / m_rtTimePerFrame);
        }

      }
      break;
    }
  }

  timeEndPeriod (dwResolution);
#endif
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
      //llClockBefore = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
      hr = m_pMixer->ProcessOutput (0 , 1, &Buffer, &dwStatus);
      //llClockAfter = CurrentHostCounter();//CTimeUtils::GetPerfCounter();
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

    int64_t TimePerFrame = m_rtTimePerFrame;
    //CLog::DebugLog("EVR: Get from Mixer : %d  (%I64d) (%I64d)", dwSurface, nsSampleTime, TimePerFrame!=0?nsSampleTime/TimePerFrame:0);

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

    //m_hThread    = ::CreateThread(NULL, 0, PresentThread, (LPVOID)this, 0, &dwThreadId);
    //SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
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

bool DsAllocator::GetPicture(DVDVideoPicture *pDvdVideoPicture)
{
  IMFSample* pMFSample = m_pMixerThread->ReadyListPop();
  if (!pMFSample)
    return false;
  int nSamplesLeft = 0;
  int surf_index = 0;
  LONGLONG sample_time;
  CAutoSingleLock lock(m_section);
  HRESULT hr = pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&surf_index);
  /*if (SUCCEEDED (GetScheduledSample(&pMFSample, nSamplesLeft)))
  {
    HRESULT hr = pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&surf_index);*/
    pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
    pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
    if (SUCCEEDED(pMFSample->GetSampleTime(&sample_time)))
    {
      pDvdVideoPicture->pts = sample_time / 10;
    }
    UINT32 width, height;
    if (SUCCEEDED(MFGetAttributeSize(pMFSample, MF_MT_FRAME_SIZE, &width, &height)))
    {
      pDvdVideoPicture->iWidth = width;
      pDvdVideoPicture->iHeight = height;
      pDvdVideoPicture->iDisplayWidth = width;
      pDvdVideoPicture->iDisplayHeight = height;
    }
    pDvdVideoPicture->pSurfaceIndex = surf_index;
    //m_BusySamples.InsertBack(pMFSample);
    while( m_BusyList.Count())
      m_pMixerThread->FreeListPush( m_BusyList.Pop() );
    m_BusyList.Push(pMFSample);
    
    return true;
  /*}
  else
    return false;*/
}
void DsAllocator::RenderThread()
{
  
  HANDLE    hAvrt;
  DWORD     dwTaskIndex, dwUser   = 0;
  HANDLE    hEvts[]       = { m_hEvtQuit, m_hEvtFlush};
  bool      bQuit         = false;
  TIMECAPS  tc;
  DWORD     dwResolution, dwObject;
  MFTIME    nsSampleTime;
  int64_t   llClockTime;

  // Tell Vista Multimedia Class Scheduler we are a playback thretad (increase priority)
  if (pfAvSetMmThreadCharacteristicsW)
    hAvrt = pfAvSetMmThreadCharacteristicsW (L"Playback", &dwTaskIndex);
  if (pfAvSetMmThreadPriority)
    pfAvSetMmThreadPriority (hAvrt, AVRT_PRIORITY_HIGH /*AVRT_PRIORITY_CRITICAL*/);

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
            
            //PaintInternal();

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

                //PaintInternal();
                
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
bool DsAllocator::AcceptMoreData()
{
  return (m_pMixerThread->GetFreeCount()>0);
}

void DsAllocator::RemoveAllSamples()
{
  FlushSamples();
  m_ScheduledSamples.Clear();
  m_FreeSamples.Clear();
  if (m_pMixerThread)
  {
    while(m_BusyList.Count())
      m_pMixerThread->FreeListPush( m_BusyList.Pop() );

    m_pMixerThread->StopThread();
    delete m_pMixerThread;
    m_pMixerThread = NULL;
  }
  
  //while(m_ScheduledSamples.GetCount())
  //  delete m_ScheduledSamples.Pop();
  //while(m_FreeSamples.GetCount())
  //  delete m_FreeSamples.Pop();

  m_LastScheduledSampleTime = -1;
  m_LastScheduledUncorrectedSampleTime = -1;
  //ASSERT(m_nUsedBuffer == 0);
  m_nUsedBuffer = 0;
}

HRESULT DsAllocator::GetFreeSample(IMFSample** ppSample)
{
  HRESULT    hr = S_OK;

  if (m_FreeSamples.GetCount() > 1)  // <= Cannot use first free buffer (can be currently displayed)
  {
    InterlockedIncrement (&m_nUsedBuffer);
    hr = m_FreeSamples.RemoveFront(ppSample) ;
  }
  else
    hr = MF_E_SAMPLEALLOCATOR_EMPTY;
  
  return hr;
}


HRESULT DsAllocator::GetScheduledSample(IMFSample** ppSample, int &_Count)
{
  HRESULT    hr = S_OK;

  _Count = m_ScheduledSamples.GetCount();
  if (_Count > 0)
  {
    hr = m_ScheduledSamples.RemoveFront(ppSample);
    --_Count;
  }
  else
    hr = MF_E_SAMPLEALLOCATOR_EMPTY;
  return hr;
}


void DsAllocator::MoveToFreeList(IMFSample* pSample, bool bTail)
{
  InterlockedDecrement (&m_nUsedBuffer);
  if (m_bPendingMediaFinished && m_nUsedBuffer == 0)
  {
    m_bPendingMediaFinished = false;
    m_pSink->Notify (EC_COMPLETE, 0, 0);
  }
  if (bTail)
    m_FreeSamples.InsertBack(pSample);
  else
    m_FreeSamples.InsertFront(pSample);//is it really needed to push to a specific position in the free list?
}


void DsAllocator::MoveToScheduledList(IMFSample* pSample, bool _bSorted)
{
  CAutoSingleLock lock(m_section);
  m_ScheduledSamples.InsertFront(pSample);
  m_ready_event.Set();
  m_pCallback->FrameReady(m_ScheduledSamples.GetCount());
#if 0 
  if (_bSorted)
  {
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

    //TRACE_EVR("EVR: Time: %f %f %f\n", Time / 10000000.0, SetDuration / 10000000.0, m_DetectedFrameRate);
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
#endif
}



void DsAllocator::FlushSamples()
{
  CAutoLock        lock(this);
  
  FlushSamplesInternal();
  m_LastScheduledSampleTime = -1;
}

void DsAllocator::FlushSamplesInternal()
{
  while (m_ScheduledSamples.GetCount() > 0)
  {
    Com::SmartPtr<IMFSample>    pMFSample;
    m_ScheduledSamples.RemoveFront(&pMFSample);
    MoveToFreeList (pMFSample, true);
  }

  m_LastSampleOffset      = 0;
  m_bLastSampleOffsetValid  = false;
  m_bSignaledStarvation = false;
}
