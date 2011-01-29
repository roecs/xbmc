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
#include "IPaintCallback.h"
#include "DSNative.h"
#include "CoordGeom.h"
#include "SmartList.h"
#include "..\..\xbmc\cores\dllloader\Win32DllLoader.h"
#include "..\..\xbmc\threads\mutex.h"

#ifndef EXECUTE_ASSERT
#define EXECUTE_ASSERT(_x_) ASSERT(_x_)
#endif
#include "wxutil.h"
#ifndef EC_PROCESSING_LATENCY
#define EC_PROCESSING_LATENCY               0x21
#endif

typedef Com::ComPtrList<IMFSample> VideoSampleList;

//The Allocator and Presenter for VMR9 is also a Presenter for EVR
class DsAllocator 
  : public IVMRSurfaceAllocator9, IVMRImagePresenter9, 
    public IMFVideoDeviceID, IMFTopologyServiceLookupClient,
    public IMFVideoPresenter,IMFGetService, IQualProp,
    public IPaintCallback//callback from xbmc to render on the specified target
{
public:
	DsAllocator(IDSInfoCallback *pCallback);
	virtual ~DsAllocator();

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

  virtual HRESULT STDMETHODCALLTYPE get_FramesDrawn(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgFrameRate(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_Jitter(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_DevSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_FramesDroppedInRenderer(int *val);

  bool GetFreeSample(IMFSample** ppSample);
  void GetNextSurface(IMFMediaBuffer **pBuf,DWORD *waittime);
  bool AcceptMoreData();
  bool SurfaceReady();
	void DiscardSurfaceandgetWait(DWORD *waittime);
  void Lock() { m_mutex->Wait(); }
  void Unlock() { m_mutex->Release();}
  //IPaintCallback
  virtual void Render(const RECT& dst, IDirect3DSurface9* target);
  void SetPointer(IMFSample *ptr){ m_gPtr = ptr; }
protected:
   IMFSample *m_gPtr;
	void RenegotiateEVRMediaType();
	void AllocateEVRSurfaces();
	void FlushEVRSamples();
	void GetEVRSamples();

	void ResetSyncOffsets();
	void CalcSyncOffsets(int sync);
	void CalcJitter(int jitter);
	
	vector<IDirect3DSurface9*> m_pSurfaces;
  vector<IDirect3DTexture9*> m_pTextures;
  IDirect3DSurface9* m_pSurfaceRenderTarget;
  IDirect3DTexture9* m_pTextureRenderTarget;
  int               currentsurfaceindex;
	queue<IMFSample*> emptyevrsamples;
	queue<IMFSample*> fullevrsamples;
  VideoSampleList          m_FreeSamples;
  VideoSampleList          m_ScheduledSamples;
  LONGLONG          m_iLastSampleDuration;
  LONGLONG          m_iLastSampleTime;
	//CCritSec objCritSec;
	IVMRSurfaceAllocatorNotify9* surfallocnotify;
	void CleanupSurfaces();
	LONG refcount;
	DWORD vheight;
	DWORD vwidth;
	bool inevrmode;
	bool endofstream;

	IMFTransform* m_pMixer;
	IMediaEventSink* mediasink;
	IMFClock* m_pClock;
	IMFMediaType *m_pMediaType;

  //IDirect3DDevice9* m_pD3DDevice;
  IDSInfoCallback* m_pCallback;
  IDirect3DDeviceManager9* m_pD3DDevManager;
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
  CMutex* m_mutex;
private:
  CCritSec								m_SampleQueueLock;
	CCritSec								m_ImageProcessingLock;

};








#endif
