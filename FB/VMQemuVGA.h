
#ifndef __VMSVGA2_H__
#define __VMSVGA2_H__

#include <IOKit/IOService.h>
#include <IOKit/graphics/IOFramebuffer.h>

#include "QemuVGADevice.h"
#include "common_fb.h"


class VMQemuVGA : public IOFramebuffer
{
	OSDeclareDefaultStructors(VMQemuVGA);

private:
	//variables
	QemuVGADevice svga;					//the svga device
	IODeviceMemory* m_vram;				//VRAM Framebuffer (BAR0)

	uint32_t m_num_active_modes;		//number of custom mode
	IODisplayModeID m_display_mode;
	IOIndex m_depth_mode;
	IODisplayModeID m_modes[NUM_DISPLAY_MODES];

	struct {
		OSObject* target;
		void* ref;
		IOFBInterruptProc proc;
	} m_intr;

	IOLock* m_iolock;					//mutex for the FIFO
	
	thread_call_t m_restore_call;		//???
	uint32_t m_custom_switch;			//???
	bool m_custom_mode_switched;		//???

	bool m_intr_enabled;				//if interrupt enbaled ?
	bool m_accel_updates;				//if update support accel procedure
	DisplayModeEntry customMode;		//define in common_fb.h

	//functions
	void Cleanup();
	DisplayModeEntry const* GetDisplayMode(IODisplayModeID displayMode);
	static void IOSelectToString(IOSelect io_select, char* output);

	IODisplayModeID TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const;

	void CustomSwitchStepSet( uint32_t value);
	void CustomSwitchStepWait(uint32_t value);
	void EmitConnectChangedEvent();
	void RestoreAllModes();
	static void _RestoreAllModes(thread_call_param_t param0,
								thread_call_param_t param1);


public:
	bool 		start(IOService* provider);
	void 		stop(IOService* provider);

	//IOFrame buffer stuff
	UInt64   	getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth);	
	IOReturn 	setInterruptState(void* interruptRef, UInt32 state);
	IOReturn 	unregisterInterrupt(void* interruptRef);		
	IOItemCount getConnectionCount();
	IOReturn 	getCurrentDisplayMode(IODisplayModeID* displayMode, 
										IOIndex* depth);	
	IOReturn 	getDisplayModes(IODisplayModeID* allDisplayModes);	
	IOItemCount getDisplayModeCount();
	const char* getPixelFormats();
	IODeviceMemory* getVRAMRange();
	IODeviceMemory* getApertureRange(IOPixelAperture aperture);
	bool 		isConsoleDevice();
	IOReturn 	getAttribute(IOSelect attribute, uintptr_t* value);		
	IOReturn 	getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value);
	IOReturn 	setAttribute(IOSelect attribute, uintptr_t value);
	IOReturn 	setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value);
	IOReturn 	registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef);
	IOReturn 	getInformationForDisplayMode(IODisplayModeID displayMode,
										IODisplayModeInformation* info);
	IOReturn 	getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
						 IOPixelAperture aperture, IOPixelInformation* pixelInfo);										
										


	IOReturn	CustomMode(CustomModeData const* inData, CustomModeData* outData, 
										size_t inSize, size_t* outSize);
	IOReturn 	setDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	
	/*
	 * Accelerator Support
	 */
	QemuVGADevice* getDevice() { return &svga; }
	void lockDevice();
	void unlockDevice();
	void useAccelUpdates(bool state);
	
	
};















#endif /* __VMSVGA2_H__ */
