
#ifndef __QemuVGADevice_H__
#define __QemuVGADevice_H__

#include <stdint.h>
#include <sys/types.h>

#include "vga_int-stripped.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

class IOPCIDevice;
class IODeviceMemory;
class IOMemoryMap;


class QemuVGADevice
{
private:
	
	IOPCIDevice* m_provider;
	
	//bar 0
	IODeviceMemory*		m_vram;				//VRAM Framebuffer (BAR0)
	IOPhysicalAddress   m_vram_base;
	IOByteCount			m_vram_size;

	uint32_t m_fb_offset;//0
	uint32_t m_fb_size;//m_vram_size
	
	uint32_t m_max_width;
	uint32_t m_max_height;
	
	uint32_t m_width;
	uint32_t m_height;
	uint32_t m_bpp;
	
	
	//Read Write VBE Reg	
	void	 WriteRegVBE(uint16_t index, uint16_t value);
	uint16_t ReadRegVBE(uint16_t index);

public:
	bool Init();
	bool Start(IOPCIDevice* provider);
	void Cleanup();	
		
	void SetMode(uint32_t width, uint32_t height, uint32_t bpp);
	

	uint32_t getMaxWidth() const { return m_max_width; }
	uint32_t getMaxHeight() const { return m_max_height; }

	uint32_t getCurrentWidth() const { return m_width; }
	uint32_t getCurrentHeight() const { return m_height; }
	
	uint32_t getCurrentFBOffset() const { return m_fb_offset; }
	uint32_t getCurrentFBSize() const { return m_fb_size; }
	
	IODeviceMemory*	get_m_vram() const { return m_vram; }
	
	uint32_t getVRAMSize() const { return static_cast<uint32_t>(m_vram_size); }
	

};
#endif /* __QemuVGADevice_H__ */

