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


#ifdef TARGET_LIKE_MBED

#include "mbed-os/mbed.h"
DigitalOut led1(LED1);
InterruptIn button(SW2);

extern "C" int usleep(useconds_t usec)
{
	wait_us(usec);
	return 0;
}

#else

#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include "../common/layout.h"

int wait(int i)
{
	return usleep(static_cast<__useconds_t>(i) * 1000 * 1000);
}


bool led1 = false;

#endif

extern "C"
{
#include "Userland/userland.h"
}

int main()
{

	printf("Booting version %lu...\n", getVersion());

#ifdef TARGET_LIKE_MBED
	Thread newThread;
	newThread.start(&checkUpdate);

//	button.rise(&NVIC_SystemReset);
#endif

	while (true)
	{
		printf("Happily blinking version %lu\n", getVersion());
		led1 = !led1;
		wait(1);
	}
}
