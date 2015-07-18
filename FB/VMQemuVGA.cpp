
#include <stdarg.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include "VMQemuVGA.h"
#include <IOKit/IOLib.h>


//for log
#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)

//#define VGA_DEBUG

#ifdef  VGA_DEBUG
#define DLOG(fmt, args...)  IOLog(fmt, ## args)
#else
#define DLOG(fmt, args...)
#endif


//for getPixelFormat
static char const pixelFormatStrings[] = IO32BitDirectPixels "\0";

/*************#define CLASS VMsvga2********************/
#define CLASS VMQemuVGA
#define super IOFramebuffer

OSDefineMetaClassAndStructors(VMQemuVGA, IOFramebuffer);

#pragma mark -
#pragma mark IOService Methods
#pragma mark -

/*************START********************/
bool CLASS::start(IOService* provider)
{
	uint32_t max_w, max_h;
	
	DLOG("%s::%s \n", getName(), __FUNCTION__);
	
	//get a PCIDevice provider
	if (!OSDynamicCast(IOPCIDevice, provider))
	{
		return false;
	}
	
	//call super::start
	if (!super::start(provider))
	{
		DLOG("%s: super::start failed.\n", __FUNCTION__);
		return false;
	}
	
	//Initiate private variables
	m_restore_call = 0;
	m_iolock = 0;
	
	m_intr_enabled = false;
	m_accel_updates = false;
	
	
	//Init svga
	svga.Init();
	//Start svga, init the FIFO too
	if (!svga.Start(static_cast<IOPCIDevice*>(provider)))
	{
		goto fail;
	}
	
	//BAR0 is vram
	//m_vram = provider->getDeviceMemoryWithIndex(0U);//Guest Framebuffer (BAR0)
	m_vram = svga.get_m_vram();	
	
	//populate customMode with modeList define in modes.cpp
	memcpy(&customMode, &modeList[0], sizeof(DisplayModeEntry));
	
	/* End Added */
	//select the valid modes
	max_w = svga.getMaxWidth();
	max_h = svga.getMaxHeight();
	m_num_active_modes = 0U;
	for (uint32_t i = 0U; i != NUM_DISPLAY_MODES; ++i)//26 in common_fb.h
	{
		if (modeList[i].width <= max_w &&
			modeList[i].height <= max_h)
		{
			m_modes[m_num_active_modes++] = i + 1U;
		}
	}
	if (m_num_active_modes <= 2U) {
		goto fail;
	}
	
	//Allocate thread for restoring modes
	m_restore_call = thread_call_allocate(&_RestoreAllModes, this);
	if (!m_restore_call)
	{
		DLOG("%s: Failed to allocate thread for restoring modes.\n", __FUNCTION__);
	}
	
	//initiate variable for custom mode and switch
	m_custom_switch = 0U;
	m_custom_mode_switched = false;
	
	//Alloc the FIFO mutex
	m_iolock = IOLockAlloc();
	if (!m_iolock) 
	{
		DLOG("%s: Failed to allocate the FIFO mutex.\n", __FUNCTION__);
		goto fail;
	}
	
	//Detect and set current display mode
	m_display_mode = TryDetectCurrentDisplayMode(3);
	m_depth_mode = 0;
		
	return true;
	
fail:
	Cleanup();
	super::stop(provider);
	return false;
}

/*************STOP********************/
void CLASS::stop(IOService* provider)
{
	DLOG("%s: \n", __FUNCTION__);
	Cleanup();
	super::stop(provider);
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

/*********CLEANUP*********/
void CLASS::Cleanup()
{
	
	svga.Cleanup();
	
	if (m_restore_call) {
		thread_call_free(m_restore_call);
		m_restore_call = 0;
	}

	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

#pragma mark -
#pragma mark Custom Mode Methods
#pragma mark 

/*************RESTOREALLMODES********************/
void CLASS::RestoreAllModes()
{
	uint32_t i;
	IODisplayModeID t;
	DisplayModeEntry const* dme1;
	DisplayModeEntry const* dme2 = 0;
	
	if (m_custom_switch != 2U)
		return;
	
	dme1 = GetDisplayMode(CUSTOM_MODE_ID);
	if (!dme1)
		return;
	for (i = 0U; i != m_num_active_modes; ++i) {
		dme2 = GetDisplayMode(m_modes[i]);
		if (!dme2)
			continue;
		if (dme2->width != dme1->width || dme2->height != dme1->height)
			goto found_slot;
	}
	return;
	
found_slot:
	t = m_modes[0];
	m_modes[0] = m_modes[i];
	m_modes[i] = t;
	DLOG("%s: Swapped mode IDs in slots 0 and %u.\n", __FUNCTION__, i);
	m_custom_mode_switched = true;
	CustomSwitchStepSet(0U);
	EmitConnectChangedEvent();
}

/************RESTOREALLMODSE***************************/
void CLASS::_RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->RestoreAllModes();
}
/*************EMITCONNECTCHANGEEVENT********************/
void CLASS::EmitConnectChangedEvent()
{
	if (!m_intr.proc || !m_intr_enabled)
		return;
	
	DLOG("%s: Before call.\n", __FUNCTION__);
	m_intr.proc(m_intr.target, m_intr.ref);
	DLOG("%s: After call.\n", __FUNCTION__);
}

/*************CUSTOMSWITCHSTEPWAIT********************/
void CLASS::CustomSwitchStepWait(uint32_t value)
{
	DLOG("%s: value=%u.\n", __FUNCTION__, value);
	while (m_custom_switch != value) {
		if (assert_wait(&m_custom_switch, THREAD_UNINT) != THREAD_WAITING)
			continue;
		if (m_custom_switch == value)
			thread_wakeup(&m_custom_switch);
		thread_block(0);
	}
	DLOG("%s: done waiting.\n", __FUNCTION__);
}
/*************CUSTOMSWITCHSTEPSET********************/
void CLASS::CustomSwitchStepSet(uint32_t value)
{
	DLOG("%s: value=%u.\n", __FUNCTION__, value);
	m_custom_switch = value;
	thread_wakeup(&m_custom_switch);
}


/**************GetDispayMode****************/
DisplayModeEntry const* CLASS::GetDisplayMode(IODisplayModeID displayMode)
{
	if (displayMode == CUSTOM_MODE_ID)
		return &customMode;
	if (displayMode >= 1 && displayMode <= NUM_DISPLAY_MODES)
		return &modeList[displayMode - 1];
	DLOG( "%s: Bad mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	return 0;
}

/******IOSELECTTOSTRING********************/
void CLASS::IOSelectToString(IOSelect io_select, char* output)
{
	*output = static_cast<char>(io_select >> 24);
	output[1] = static_cast<char>(io_select >> 16);
	output[2] = static_cast<char>(io_select >> 8);
	output[3] = static_cast<char>(io_select);
	output[4] = '\0';
}
/*************TRYDETECTCURRENTDISPLAYMODE*********************/
IODisplayModeID CLASS::TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const
{
	IODisplayModeID tableDefault = 0;
	uint32_t w = svga.getCurrentWidth();
	uint32_t h = svga.getCurrentHeight();
	
	for (IODisplayModeID i = 1; i < NUM_DISPLAY_MODES; ++i) 
	{
		if (w == modeList[i].width && h == modeList[i].height)
		{
			return i + 1;
		}
		if (modeList[i].flags & kDisplayModeDefaultFlag)
		{
			tableDefault = i + 1;
		}
	}
	return (tableDefault ? : defaultMode);
}

/*************CUSTOMMODE********************/
IOReturn CLASS::CustomMode(CustomModeData const* inData, CustomModeData* outData, size_t inSize, size_t* outSize)
{
	DisplayModeEntry const* dme1;
	unsigned w, h;
	AbsoluteTime deadline;
	
	if (!m_restore_call)
	{
		return kIOReturnUnsupported;
	}
	
	DLOG("%s: inData=%p outData=%p inSize=%lu outSize=%lu.\n", __FUNCTION__,
		 inData, outData, inSize, outSize ? *outSize : 0UL);
	if (!inData) 
	{
		DLOG("%s: inData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (inSize < sizeof(CustomModeData)) 
	{
		DLOG("%s: inSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outData) 
	{
		DLOG("%s: outData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outSize || *outSize < sizeof(CustomModeData)) 
	{
		DLOG("%s: *outSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
	{
		return kIOReturnUnsupported;
	}
	if (inData->flags & 1U) 
	{
		DLOG("%s: Set resolution to %ux%u.\n", __FUNCTION__, inData->width, inData->height);
		w = inData->width;
		if (w < 800U)
		{
			w = 800U;
		}
		else if (w > svga.getMaxWidth())
		{
			w = svga.getMaxWidth();
		}
		h = inData->height;
		if (h < 600U)
		{
			h = 600U;
		}
		else if (h > svga.getMaxHeight())
		{
			h = svga.getMaxHeight();
		}
		if (w == dme1->width && h == dme1->height)
		{
			goto finish_up;
		}
		customMode.width = w;
		customMode.height = h;
		CustomSwitchStepSet(1U);
		EmitConnectChangedEvent();
		CustomSwitchStepWait(2U);	// TBD: this wait for the WindowServer should be time-bounded
		DLOG("%s: Scheduling RestoreAllModes().\n", __FUNCTION__);
		clock_interval_to_deadline(2000U, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_restore_call, deadline);
	}
finish_up:
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	outData->flags = inData->flags;
	outData->width = dme1->width;
	outData->height = dme1->height;
	return kIOReturnSuccess;
}

/***************************************************************/

/****************IOFramebuffer Method*************/
//These are untouched from zenith source
#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

/*************GETPIXELFORMATFORDISPLAYMODE********************/
UInt64 CLASS::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	return 0ULL;
}

/*************SETINTERRUPTSTATE********************/
IOReturn CLASS::setInterruptState(void* interruptRef, UInt32 state)
{
	DLOG("%s: \n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	m_intr_enabled = (state != 0);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

/*************UNREGISTERINTERRUPT********************/
IOReturn CLASS::unregisterInterrupt(void* interruptRef)
{
	DLOG("%s: \n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	bzero(interruptRef, sizeof m_intr);
	m_intr_enabled = false;
	return kIOReturnSuccess;
}

/*************GETCONNECTIONCOUNT********************/
IOItemCount CLASS::getConnectionCount()
{
	DLOG("%s: \n", __FUNCTION__);
	return 1U;
}

/*************GETCURRENTDISPLAYMODE********************/
IOReturn CLASS::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth)
{
	if (displayMode)
		*displayMode = m_display_mode;
	if (depth)
		*depth = m_depth_mode;
	DLOG("%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__,
		 FMT_D(m_display_mode), FMT_D(m_depth_mode));
	return kIOReturnSuccess;
}

/*************GETDISPLAYMODES********************/
IOReturn CLASS::getDisplayModes(IODisplayModeID* allDisplayModes)
{
	DLOG("%s: \n", __FUNCTION__);
	if (!allDisplayModes)
	{
		return kIOReturnBadArgument;
	}
	if (m_custom_switch) 
	{
		*allDisplayModes = CUSTOM_MODE_ID;
		return kIOReturnSuccess;
	}
	memcpy(allDisplayModes, &m_modes[0], m_num_active_modes * sizeof(IODisplayModeID));
	return kIOReturnSuccess;
}

/*************GETDISPLAYMODECOUNT********************/
IOItemCount CLASS::getDisplayModeCount()
{
	IOItemCount r;
	r = m_custom_switch ? 1 : m_num_active_modes;
	DLOG ("%s: mode count=%u\n", __FUNCTION__, FMT_U(r));
	return r;
}

/*************GETPIXELFORMATS********************/
const char* CLASS::getPixelFormats()
{
	DLOG( "%s: pixel formats=%s\n", __FUNCTION__, &pixelFormatStrings[0]);
	return &pixelFormatStrings[0];
}

/*************GETVRAMRANGE********************/
IODeviceMemory* CLASS::getVRAMRange()
{
	DLOG( "%s: \n", __FUNCTION__);
	if (!m_vram)
		return 0;
	
	if (svga.getVRAMSize() >= m_vram->getLength()) {
		m_vram->retain();
		return m_vram;
	}
	return IODeviceMemory::withSubRange(m_vram, 0U, svga.getVRAMSize());
}

/*************GETAPERTURERANGE********************/
IODeviceMemory* CLASS::getApertureRange(IOPixelAperture aperture)
{
	
	uint32_t fb_offset, fb_size;
	IODeviceMemory* mem;
	
	if (aperture != kIOFBSystemAperture) 
	{
		DLOG("%s: Failed request for aperture=%d (%d)\n", __FUNCTION__,
			 FMT_D(aperture), kIOFBSystemAperture);
		return 0;
	}
	
	if (!m_vram)
	{
		return 0;
	}
	
	IOLockLock(m_iolock);
	fb_offset = svga.getCurrentFBOffset();
	fb_size   = svga.getCurrentFBSize();
	IOLockUnlock(m_iolock);
	
	DLOG("%s: aperture=%d, fb offset=%u, fb size=%u\n", __FUNCTION__,
		 FMT_D(aperture), fb_offset, fb_size);
	
	mem = IODeviceMemory::withSubRange(m_vram, fb_offset, fb_size);
	if (!mem)
	{
		DLOG("%s: Failed to create IODeviceMemory, aperture=%d\n", __FUNCTION__, kIOFBSystemAperture);
	}
	
	return mem;
	
}

/*************ISCONSOLEDEVICE********************/
bool CLASS::isConsoleDevice()
{
	DLOG("%s: \n", __FUNCTION__);
	return 0 != getProvider()->getProperty("AAPL,boot-display");
}

/*************GETATTRIBUTE********************/
IOReturn CLASS::getAttribute(IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];
	
	/*
	 * Also called from base class:
	 *   kIOMirrorDefaultAttribute
	 *   kIOVRAMSaveAttribute
	 */
	
	//no hw cursor for cirrus	
	if (attribute == kIOHardwareCursorAttribute) {
		if (value)
			*value = 0;//1;
		r = kIOReturnSuccess;
	} else
		r = super::getAttribute(attribute, value);
	
	//debug	
	if (true) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			DLOG("%s: attr=%s *value=%#08lx ret=%#08x\n", __FUNCTION__, &attr[0], *value, r);
		else
			DLOG("%s: attr=%s ret=%#08x\n", __FUNCTION__, &attr[0], r);
	}
	return r;
}

/*************GETATTRIBUTEFORCONNECTION********************/
IOReturn CLASS::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];
	
	/*
	 * Also called from base class:
	 *   kConnectionCheckEnable
	 */
	switch (attribute) {
		case kConnectionSupportsAppleSense:
		case kConnectionDisplayParameterCount:
		case kConnectionSupportsLLDDCSense:
		case kConnectionDisplayParameters:
		case kConnectionPower:
		case kConnectionPostWake:
			r = kIOReturnUnsupported;
			break;
		case kConnectionChanged:
			DLOG("%s: kConnectionChanged value=%s\n", __FUNCTION__,
				 value ? "non-NULL" : "NULL");
			if (value)
				removeProperty("IOFBConfig");
			r = kIOReturnSuccess;
			break;
		case kConnectionEnable:
			DLOG("%s: kConnectionEnable\n", __FUNCTION__);
			if (value)
				*value = 1U;
			r = kIOReturnSuccess;
			break;
		case kConnectionFlags:
			DLOG("%s: kConnectionFlags\n", __FUNCTION__);
			if (value)
				*value = 0U;
			r = kIOReturnSuccess;
			break;
		case kConnectionSupportsHLDDCSense:
			r = /*m_edid ? kIOReturnSuccess :*/ kIOReturnUnsupported;
			break;
		default:
			r = super::getAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	
		IOSelectToString(attribute, &attr[0]);
		if (value)
			DLOG("%s: index=%d, attr=%s *value=%#08lx ret=%#08x\n", __FUNCTION__,
				 FMT_D(connectIndex), &attr[0], *value, r);
		else
			DLOG("%s: index=%d, attr=%s ret=%#08x\n", __FUNCTION__,
				 FMT_D(connectIndex), &attr[0], r);

	return r;
}

/*************SETATTRIBUTE********************/
IOReturn CLASS::setAttribute(IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	char attr[5];
	
	r = super::setAttribute(attribute, value);
	if (true /*logLevelFB >= 2*/) {
		IOSelectToString(attribute, &attr[0]);
		DLOG("%s: attr=%s value=%#08lx ret=%#08x\n",
			 __FUNCTION__, &attr[0], value, r);
	}
	if (attribute == kIOCapturedAttribute &&
		!value &&
		m_custom_switch == 1U &&
		m_display_mode == CUSTOM_MODE_ID) {
		CustomSwitchStepSet(2U);
	}
	return r;
}

/*************SETATTRIBUTEFORCONNECTION********************/
IOReturn CLASS::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	
	switch (attribute) {
		case kConnectionFlags:
			DLOG("%s: kConnectionFlags %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		case kConnectionProbe:
			DLOG("%s: kConnectionProbe %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		default:
			r = super::setAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	
#ifdef  VGA_DEBUG
	char attr[5];

		IOSelectToString(attribute, &attr[0]);
		DLOG("%s: index=%d, attr=%s value=%#08lx ret=%#08x\n", __FUNCTION__,
			 FMT_D(connectIndex), &attr[0], value, r);
#endif
	
	return r;
}

/*************REGISTERFORINTERRUPTTYPE********************/
IOReturn CLASS::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef)
{
	
#ifdef  VGA_DEBUG
	char int_type[5];
		IOSelectToString(interruptType, &int_type[0]);
		DLOG("%s: interruptType=%s\n", __FUNCTION__, &int_type[0]);
#endif
	
	/*
	 * Also called from base class:
	 *   kIOFBVBLInterruptType
	 *   kIOFBDisplayPortInterruptType
	 */
	//if (interruptType == kIOFBMCCSInterruptType)
	//	return super::registerForInterruptType(interruptType, proc, target, ref, interruptRef);
	if (interruptType != kIOFBConnectInterruptType)
		return kIOReturnUnsupported;
	bzero(&m_intr, sizeof m_intr);
	m_intr.target = target;
	m_intr.ref = ref;
	m_intr.proc = proc;
	m_intr_enabled = true;
	if (interruptRef)
		*interruptRef = &m_intr;
	return kIOReturnSuccess;
}

/*************GETINFORMATIONFORDISPLAYMODE********************/
IOReturn CLASS::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info)
{
	DisplayModeEntry const* dme;
	
	DLOG("%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	
	if (!info)
	{
		return kIOReturnBadArgument;
	}
	
	dme = GetDisplayMode(displayMode);
	if (!dme) 
	{
		DLOG("%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	
	bzero(info, sizeof(IODisplayModeInformation));
	info->maxDepthIndex = 0;
	info->nominalWidth = dme->width;
	info->nominalHeight = dme->height;
	info->refreshRate = 60U << 16;
	info->flags = dme->flags;
	
	DLOG("%s: mode ID=%d, max depth=%d, wxh=%ux%u, flags=%#x\n", __FUNCTION__,
		 FMT_D(displayMode), 0, FMT_U(info->nominalWidth), FMT_U(info->nominalHeight), FMT_U(info->flags));
	
	return kIOReturnSuccess;
	
}

/*************GETPIXELINFORMATION********************/
IOReturn CLASS::getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo)
{
	DisplayModeEntry const* dme;
	
	//DLOG("%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	
	if (!pixelInfo)
	{
		return kIOReturnBadArgument;
	}
	
	if (aperture != kIOFBSystemAperture) 
	{
		DLOG("%s: aperture=%d not supported\n", __FUNCTION__, FMT_D(aperture));
		return kIOReturnUnsupportedMode;
	}
	
	if (depth) 
	{
		DLOG("%s: Depth mode %d not found.\n", __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	
	dme = GetDisplayMode(displayMode);
	if (!dme) 
	{
		DLOG("%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	
	//DLOG("%s: mode ID=%d, wxh=%ux%u\n", __FUNCTION__,
	//		  FMT_D(displayMode), dme->width, dme->height);
	
	bzero(pixelInfo, sizeof(IOPixelInformation));
	pixelInfo->activeWidth = dme->width;
	pixelInfo->activeHeight = dme->height;
	pixelInfo->flags = dme->flags;
	strlcpy(&pixelInfo->pixelFormat[0], &pixelFormatStrings[0], sizeof(IOPixelEncoding));
	pixelInfo->pixelType = kIORGBDirectPixels;
	pixelInfo->componentMasks[0] = 0xFF0000U;
	pixelInfo->componentMasks[1] = 0x00FF00U;
	pixelInfo->componentMasks[2] = 0x0000FFU;
	pixelInfo->bitsPerPixel = 32U;
	pixelInfo->componentCount = 3U;
	pixelInfo->bitsPerComponent = 8U;
	pixelInfo->bytesPerRow = ((pixelInfo->activeWidth + 7U) & (~7U)) << 2;
	
	//DLOG("%s: bitsPerPixel=%u, bytesPerRow=%u\n", __FUNCTION__, 32U, FMT_U(pixelInfo->bytesPerRow));
	
	return kIOReturnSuccess;
}

/*************SETDISPLAYMODE********************/
IOReturn CLASS::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	DisplayModeEntry const* dme;
	
	DLOG("%s::%s display ID=%d, depth ID=%d\n", getName(), __FUNCTION__,
		 FMT_D(displayMode), FMT_D(depth));
	
	if (depth) 
	{
		DLOG("%s::%s: Depth mode %d not found.\n", getName(), __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	
	dme = GetDisplayMode(displayMode);
	if (!dme) 
	{
		DLOG("%s::%s: Display mode %d not found.\n", getName(), __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	
	if (m_custom_mode_switched) 
	{
		if (customMode.width == dme->width && customMode.height == dme->height)
			m_custom_mode_switched = false;
		else
			DLOG("%s::%s: Not setting mode in virtual hardware\n", getName(), __FUNCTION__);
		m_display_mode = displayMode;
		m_depth_mode = 0;
		return kIOReturnSuccess;
	}
	
	IOLockLock(m_iolock);
	svga.SetMode(dme->width, dme->height, 32U);
	IOLockUnlock(m_iolock);
	
	m_display_mode = displayMode;
	m_depth_mode = 0;
	
	DLOG("%s::%s: display mode ID=%d, depth mode ID=%d\n", getName(), __FUNCTION__,
		 FMT_D(m_display_mode), FMT_D(m_depth_mode));
	
	return kIOReturnSuccess;
}

/*******REMAIN from Accel***************/

#pragma mark -
#pragma mark Accelerator Support Methods
#pragma mark -

void CLASS::lockDevice()
{
	IOLockLock(m_iolock);
}

void CLASS::unlockDevice()
{
	IOLockUnlock(m_iolock);
}


void CLASS::useAccelUpdates(bool state)
{
	if (state == m_accel_updates)
		return;
	m_accel_updates = state;
	
	setProperty("VMwareSVGAAccelSynchronize", state);
	DLOG("Accelerator Assisted Updates: %s\n", state ? "On" : "Off");
}
