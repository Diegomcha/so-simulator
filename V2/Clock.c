// V1
#include "Clock.h"
#include "Processor.h"
#include "ComputerSystemBase.h"

int tics = 0;

void Clock_Update()
{
	// Increment tics
	tics++;

	// Raise an interrupt every intervalBetweenInterrupts tics
	if (tics % intervalBetweenInterrupts == 0)
		Processor_RaiseInterrupt(CLOCKINT_BIT);

	// ComputerSystem_DebugMessage(NO_TIMED_MESSAGE,97,CLOCK,tics);
}

int Clock_GetTime()
{

	return tics;
}
