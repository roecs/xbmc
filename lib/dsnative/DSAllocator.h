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

#ifndef DSALLOCATOR_H
#define DSALLOCATOR_H

#include <queue>
#include <vector>


using namespace std;

#include <winsock2.h>

#include <d3d9.h>
#include <dshow.h>
#include <vmr9.h>
#include <evr.h>
#include <Dxva2api.h>
#include <evcode.h>
#include <mferror.h>
#include "IPaintCallback.h"
#include "DSNative.h"
#include "CoordGeom.h"
#include "SmartList.h"
#include "..\..\xbmc\cores\dllloader\Win32DllLoader.h"
#include "..\..\xbmc\threads\mutex.h"
#include "threads/Event.h"

#define NB_JITTER          126
#ifndef EXECUTE_ASSERT
#define EXECUTE_ASSERT(_x_) ASSERT(_x_)
#endif
#include "wxutil.h"
#ifndef EC_PROCESSING_LATENCY
#define EC_PROCESSING_LATENCY               0x21
#endif
#ifndef DVD_NOPTS_VALUE
#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64
#endif
//Time base from directshow is a 100 nanosec unit
#define DS_TIME_BASE 1E7

#define DS_TIME_TO_SEC(x)     ((double)(x / DS_TIME_BASE))
#define DS_TIME_TO_MSEC(x)    ((double)(x * 1000 / DS_TIME_BASE))
#define SEC_TO_DS_TIME(x)     ((__int64)(x * DS_TIME_BASE))
#define MSEC_TO_DS_TIME(x)    ((__int64)(x * DS_TIME_BASE / 1000))
#define SEC_TO_MSEC(x)        ((double)(x * 1E3))

#define MAXULONG64  ((ULONG64)~((ULONG64)0))
#define MAXLONG64   ((LONG64)(MAXULONG64 >> 1))
#define MINLONG64   ((LONG64)~MAXLONG64)

static int64_t GetPerfCounter()
{
  LARGE_INTEGER i64Ticks100ns;
  LARGE_INTEGER llPerfFrequency;

  QueryPerformanceFrequency (&llPerfFrequency);
  if (llPerfFrequency.QuadPart != 0)
  {
    QueryPerformanceCounter (&i64Ticks100ns);
    return llMulDiv(i64Ticks100ns.QuadPart, 10000000, llPerfFrequency.QuadPart, 0);
  }
  else
  {
    // ms to 100ns units
    return timeGetTime() * 10000; 
  }
}

typedef Com::ComPtrList<IMFSample> VideoSampleList;
class CEvrMixerThread;
class CVMR9RenderThread;
//The Allocator and Presenter for VMR9 is also a Presenter for EVR
class DsAllocator 
  : public IVMRSurfaceAllocator9, IVMRImagePresenter9, 
    public IMFVideoDeviceID, IMFTopologyServiceLookupClient,
    public IMFVideoPresenter,IMFGetService, IQualProp, IMFRateSupport,
    public IPaintCallback, CCritSec//callback from xbmc to render on the specified target
{
public:
  DsAllocator(IDSInfoCallback *pCallback);
  virtual ~DsAllocator();

  virtual bool GetPicture(DVDVideoPicture *pDvdVideoPicture);

  virtual HRESULT STDMETHODCALLTYPE StartPresenting(DWORD_PTR userid);
  virtual HRESULT STDMETHODCALLTYPE StopPresenting(DWORD_PTR userid);
  virtual HRESULT STDMETHODCALLTYPE PresentImage(DWORD_PTR userid,VMR9PresentationInfo* presinf);

  virtual HRESULT STDMETHODCALLTYPE InitializeDevice(DWORD_PTR userid,
    VMR9AllocationInfo* allocinf,DWORD* numbuf);
  virtual HRESULT STDMETHODCALLTYPE TerminateDevice(DWORD_PTR userid); 
  virtual HRESULT STDMETHODCALLTYPE GetSurface(DWORD_PTR userid,DWORD surfindex,DWORD surfflags, IDirect3DSurface9** surf);
  virtual HRESULT STDMETHODCALLTYPE AdviseNotify(IVMRSurfaceAllocatorNotify9* allnoty);
  

  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID refiid,void ** obj);
  virtual ULONG STDMETHODCALLTYPE AddRef();
  virtual ULONG STDMETHODCALLTYPE Release();

  IPaintCallback* AcquireCallback();
  

  void LostDevice(IDirect3DDevice9 *d3ddev, IDirect3D9* d3d);

  /* EVR members */
  virtual HRESULT STDMETHODCALLTYPE GetDeviceID(IID *pDid);

  virtual HRESULT STDMETHODCALLTYPE InitServicePointers(IMFTopologyServiceLookup *plooky);
  virtual HRESULT STDMETHODCALLTYPE ReleaseServicePointers();

  virtual HRESULT STDMETHODCALLTYPE ProcessMessage(MFVP_MESSAGE_TYPE mess,ULONG_PTR mess_para);

  virtual HRESULT STDMETHODCALLTYPE OnClockStart(MFTIME systime,LONGLONG startoffset);
  virtual HRESULT STDMETHODCALLTYPE OnClockStop(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockPause(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockRestart(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockSetRate(MFTIME systime,float rate);
  virtual HRESULT STDMETHODCALLTYPE GetCurrentMediaType(IMFVideoMediaType **mtype);

  virtual HRESULT STDMETHODCALLTYPE GetService(const GUID &guid,const IID &iid,LPVOID *obj);
  /*IBaseFilter*/
  bool   GetState( DWORD dwMilliSecsTimeout, FILTER_STATE *State, HRESULT &_ReturnValue);

  /*IQualProp*/
  virtual HRESULT STDMETHODCALLTYPE get_FramesDrawn(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgFrameRate(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_Jitter(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_DevSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_FramesDroppedInRenderer(int *val);
  
  void CalculateJitter(int64_t PerfCounter);
  // IMFRateSupport
  STDMETHODIMP GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate);
  STDMETHODIMP GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate);
  STDMETHODIMP IsRateSupported(BOOL fThin, float flRate, float *pflNearestSupportedRate);
  
  float GetMaxRate(BOOL bThin);

  HRESULT CheckShutdown() const 
{
    if (m_nRenderState == Shutdown)
        return MF_E_SHUTDOWN;
    else
        return S_OK;
}

  void CompleteFrameStep(bool bCancel);
  
  bool AcceptMoreData();
  void FreeFirstBuffer();

  //IPaintCallback
  virtual void Render(const RECT& dst, IDirect3DSurface9* target, int index);
  virtual bool WaitOutput(unsigned int msec);
  
  virtual int GetReadySample();
  
  void SetDSMediaType(CMediaType mt);
protected:
  IMFSample *m_gPtr;
  void RenegotiateEVRMediaType();
  void AllocateEVRSurfaces();

  void ResetSyncOffsets();
  void CalcSyncOffsets(int sync);
  void CalcJitter(int jitter);

  uint32_t                              m_RefreshRate;//not implemented yet TODO
  
  int64_t                               m_PaintTime;//TODO
  int64_t                               m_PaintTimeMin;//TODO
  int64_t                               m_PaintTimeMax;//TODO

  vector<IDirect3DSurface9*> m_pSurfaces;
  vector<IDirect3DTexture9*> m_pTextures;
  int               m_bInterlaced;
  float             m_fps;
  
  void CleanupSurfaces();
  LONG refcount;
  DWORD vheight;
  DWORD vwidth;
  
  bool inevrmode;
  bool endofstream;
  //Evr
  CEvrMixerThread*               m_pMixerThread;
  Com::SmartPtr<IMFTransform>    m_pMixer;
  Com::SmartPtr<IMediaEventSink> m_pSink;
  Com::SmartPtr<IMFClock>        m_pClock;
  Com::SmartPtr<IMFMediaType>    m_pMediaType;
  Com::CSyncPtrQueue<IMFSample>  m_BusyList;
  CEvent                         m_ready_event;
  //Vmr9
  IVMRSurfaceAllocatorNotify9*   surfallocnotify;
  CVMR9RenderThread*             m_pVmr9Thread;
  Com::CSyncPtrQueue<IDirect3DSurface9> m_BusySurfaceList;
  int                            current_index;
  //common to both
  IDSInfoCallback*               m_pCallback;
  IDirect3DDeviceManager9*       m_pD3DDevManager;
  CCritSec                       m_section;
  
  static const int n_stats=126;
  int sync_offset[n_stats];
  int jitter_offset[n_stats];
  unsigned int sync_pos;
  unsigned int jitter_pos;
  int framesdrawn;
  int framesdropped;
  int avg_sync_offset;
  int dev_sync_offset;
  int jitter;
  int avgfps;
  LONGLONG lastdelframe;
  void                     RemoveAllSamples();
  void                     ResetStats();
private:
  typedef enum
  {
    Started = State_Running,
    Stopped = State_Stopped,
    Paused = State_Paused,
    Shutdown = State_Running + 1
  } RENDER_STATE;
  RENDER_STATE             m_nRenderState;
  

  int64_t                  m_rtTimePerFrame;
  int64_t                  m_llLastPerf;
  int64_t                  m_JitterStdDev;
  int64_t                  m_MaxJitter;
  int64_t                  m_MinJitter;
  int                      m_nNextJitter;
  int64_t                  m_pllJitter [NB_JITTER];        // Jitter buffer for stats

  int                      m_nCurSurface;
  int                      m_nStepCount;
  UINT                     m_pcFrames;
  UINT                     m_nDroppedUpdate;
  UINT                     m_pcFramesDrawn;  // Retrieves the number of frames drawn since streaming started
  UINT                     m_piAvg;
  UINT                     m_piDev;
  
  double                   m_fAvrFps;               // Estimate the real FPS
  double                   m_fJitterStdDev;         // Estimate the Jitter std dev
  double                   m_fJitterMean;
  double                   m_fSyncOffsetStdDev;
  double                   m_fSyncOffsetAvr;
};

#endif
