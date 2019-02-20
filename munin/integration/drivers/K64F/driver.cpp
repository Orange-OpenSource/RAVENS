/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * @author Emile-Hugo Spir
 */

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

	RAVENS_CRITICAL void eraseSector(size_t address)
	{
		erase_sector(static_cast<int>(address));
	}

	RAVENS_CRITICAL void programFlash(size_t address, const uint8_t *data, size_t length)
	{
		program_flash(static_cast<int>(address), (char*) data, static_cast<unsigned int>(length));
	}

	bool irqAlreadyDisabled = false;

	RAVENS_CRITICAL void enableIRQ()
	{
		if(irqAlreadyDisabled)
		{
			irqAlreadyDisabled = false;
			__enable_irq();
		}
	}

	RAVENS_CRITICAL void disableIRQ()
	{
		if(!irqAlreadyDisabled)
		{
			__disable_irq();
			irqAlreadyDisabled = true;
		}
	}
}
#endif
