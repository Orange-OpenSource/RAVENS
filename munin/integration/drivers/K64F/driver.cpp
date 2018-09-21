//
// Created by Emile-Hugo Spir on 6/29/18.
//

#include "../../mbed-os/mbed.h"

#ifdef TARGET_Freescale

#include "../../../core.h"
#include "FreescaleIAP.h"

extern "C"
{
	void reboot()
	{
		NVIC_SystemReset();
	}

	HERMES_CRITICAL void eraseSector(size_t address)
	{
		erase_sector(static_cast<int>(address));
	}

	HERMES_CRITICAL void programFlash(size_t address, const uint8_t *data, size_t length)
	{
		program_flash(static_cast<int>(address), (char*) data, static_cast<unsigned int>(length));
	}

	bool irqAlreadyDisabled = false;

	HERMES_CRITICAL void enableIRQ()
	{
		if(irqAlreadyDisabled)
		{
			irqAlreadyDisabled = false;
			__enable_irq();
		}
	}

	HERMES_CRITICAL void disableIRQ()
	{
		if(!irqAlreadyDisabled)
		{
			__disable_irq();
			irqAlreadyDisabled = true;
		}
	}
}
#endif
