
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include "QemuVGADevice.h"
#include "common_fb.h"


//#define SVGA_DEBUG

#ifdef  SVGA_DEBUG
#define DLOG(fmt, args...)  IOLog(fmt, ## args)
#else
#define DLOG(fmt, args...)
#endif


#define CLASS QemuVGADevice


/*************WRITEREGVBE********************/

void CLASS::WriteRegVBE(uint16_t index, uint16_t value)
{
	m_provider->ioWrite16( VBE_DISPI_IOPORT_INDEX, index );
	m_provider->ioWrite16( VBE_DISPI_IOPORT_DATA, value);
}

/*************READREGVBE********************/
uint16_t CLASS::ReadRegVBE(uint16_t index)
{
	uint16_t value;
	m_provider->ioWrite16( VBE_DISPI_IOPORT_INDEX, index );
	value = m_provider->ioRead16(VBE_DISPI_IOPORT_DATA);
	return value;
}

/*************INIT********************/
bool CLASS::Init()
{
	return true;
}

/*************CLEANUP********************/
void CLASS::Cleanup()
{	
	if (m_provider)
	{
		m_provider = 0;
	}
	
	return;
}

/*************START********************/
bool CLASS::Start(IOPCIDevice* provider)
{
    
	DLOG("%s\n",  __FUNCTION__);
	DLOG("%s: PCI bus %u device %u function %u\n", __FUNCTION__,
		 provider->getBusNumber(),
		 provider->getDeviceNumber(),
		 provider->getFunctionNumber());
	DLOG("%s: PCI device %#04x vendor %#04x revision %#02x\n", __FUNCTION__,
		 provider->configRead16(kIOPCIConfigDeviceID),
		 provider->configRead16(kIOPCIConfigVendorID),
		 provider->configRead8(kIOPCIConfigRevisionID));
	DLOG("%s: PCI subsystem %#04x vendor %#04x\n", __FUNCTION__,
		 provider->configRead16(kIOPCIConfigSubSystemID),
		 provider->configRead16(kIOPCIConfigSubSystemVendorID));

	//I/O space, at PCI Base Address Register 0 (BAR0)
	m_provider = provider;	
	provider->setMemoryEnable(true);
	provider->setIOEnable(true);
	
	//get the MVRAM, bar0 for cirrus
	m_vram = provider->getDeviceMemoryWithIndex(0U);
	if (!m_vram) {
		DLOG("%s Failed to map the mvram.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_vram_base = m_vram->getPhysicalAddress();
	m_vram_size = m_vram->getLength();
	
	//FB info	
	m_max_width  = VBE_DISPI_MAX_XRES;
	m_max_height = VBE_DISPI_MAX_YRES ;
	m_fb_offset = 0;
	m_fb_size   = static_cast<uint32_t>(m_vram_size);

	//get initial value
	m_width  = ReadRegVBE(VBE_DISPI_INDEX_XRES);
	m_height = ReadRegVBE(VBE_DISPI_INDEX_YRES);
	m_bpp	 = ReadRegVBE(VBE_DISPI_INDEX_BPP);

	DLOG("%s Starting with mode : w:%d h:%d bpp:%d\n", __FUNCTION__, m_width, m_height, m_bpp );

	//Cosmetic
	provider->setProperty("model", "Qemu VBE/VGA Std");
	
	return true;
}

/************SETMODE*****************/
void CLASS::SetMode(uint32_t width, uint32_t height, uint32_t bpp)
{
	//save current mode value
	m_width = width;
	m_height = height;
	m_bpp = bpp;
	
	//use vbe to set mode
	WriteRegVBE(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
	WriteRegVBE(VBE_DISPI_INDEX_XRES, width);
	WriteRegVBE(VBE_DISPI_INDEX_YRES, height);
	WriteRegVBE(VBE_DISPI_INDEX_BPP, bpp);
	WriteRegVBE(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
	
}



