#include "Simulator.h"
#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateExecutingProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();

// The process table
// PCB processTable[PROCESSTABLEMAXSIZE];
PCB *processTable;

// Size of the memory occupied for the OS
int OS_MEMORY_SIZE = 32;

// Address base for OS code in this version
int OS_address_base;

// Identifier of the current executing process
int executingProcessID = NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation (Not assigned)
int initialPID = -1;

// Begin indes for daemons in programList
// int baseDaemonsInProgramList;

// Array that contains the identifiers of the READY processes
// heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
heapItem *readyToRunQueue[NUMBEROFQUEUES];
// int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0};
int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0, 0};

char *queueNames[NUMBEROFQUEUES] = {"USER", "DAEMONS"};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses = 0;

// char DAEMONS_PROGRAMS_FILE[MAXFILENAMELENGTH]="teachersDaemons";

int MAINMEMORYSECTIONSIZE = 60;

extern int MAINMEMORYSIZE;

int PROCESSTABLEMAXSIZE = 4;

// For debug messages
char *statesNames[5] = {"NEW", "READY", "EXECUTING", "BLOCKED", "EXIT"};

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int programsFromFileIndex)
{

	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// In this version, with original configuration of memory size (300) and number of processes (4)
	// every process occupies a 60 positions main memory chunk
	// and OS code and the system stack occupies 60 positions

	OS_address_base = MAINMEMORYSIZE - OS_MEMORY_SIZE;

	MAINMEMORYSECTIONSIZE = OS_address_base / PROCESSTABLEMAXSIZE;

	if (initialPID < 0) // if not assigned in options...
		initialPID = PROCESSTABLEMAXSIZE - 1;

	// Space for the processTable
	processTable = (PCB *)malloc(PROCESSTABLEMAXSIZE * sizeof(PCB));

	// Space for the ready to run queues (one queue initially...)
	for (int i = 0; i < NUMBEROFQUEUES; i++)
		readyToRunQueue[i] = Heap_create(PROCESSTABLEMAXSIZE);

	programFile = fopen("OperatingSystemCode", "r");
	if (programFile == NULL)
	{
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 99, SHUTDOWN, "FATAL ERROR: Missing Operating System!\n");
		exit(1);
	}

	// Obtain the memory requirements of the program
	int processSize = OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);

	// Process table initialization (all entries are free)
	for (i = 0; i < PROCESSTABLEMAXSIZE; i++)
	{
		processTable[i].busy = 0;
		processTable[i].programListIndex = -1;
		processTable[i].initialPhysicalAddress = -1;
		processTable[i].processSize = -1;
		processTable[i].copyOfSPRegister = -1;
		processTable[i].state = -1;
		processTable[i].priority = -1;
		processTable[i].copyOfPCRegister = -1;
		processTable[i].copyOfPSWRegister = 0;
		processTable[i].programListIndex = -1;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base + 2);

	// Include in program list all user or system daemon processes
	OperatingSystem_PrepareDaemons(programsFromFileIndex);

	// Create all user processes from the information given in the command line
	int numCreatedProcesses = OperatingSystem_LongTermScheduler();

	if (strcmp(programList[processTable[sipID].programListIndex]->executableName, "SystemIdleProcess") && processTable[sipID].state == READY)
	{
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 99, SHUTDOWN, "FATAL ERROR: Missing SIP program!\n");
		exit(1);
	}

	// At least, one process has been created
	// Select the first process that is going to use the processor
	selectedProcess = OperatingSystem_ShortTermScheduler();

	Processor_SetSSP(MAINMEMORYSIZE - 1);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);

	// If we only managed to create SIP terminate execution
	if (numCreatedProcesses == 1)
	{
		// Set the executing to SIP that way TerminateExecutingProcess halts the processor
		executingProcessID = sipID;
		OperatingSystem_TerminateExecutingProcess();
	}
	else
		// Assign the processor to the selected process
		OperatingSystem_Dispatch(selectedProcess);
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler()
{

	int PID, i,
		numberOfSuccessfullyCreatedProcesses = 0;

	for (i = 0; programList[i] != NULL && i < PROGRAMSMAXNUMBER; i++)
	{
		PID = OperatingSystem_CreateProcess(i);

		// Handle process creation errors and display appropriate messages
		if (PID == NOFREEENTRY)
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 103, ERROR, programList[i]->executableName);
		else if (PID == TOOBIGPROCESS)
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 105, ERROR, programList[i]->executableName);
		else if (PID == PROGRAMDOESNOTEXIST)
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 104, ERROR, programList[i]->executableName, "it does not exist");
		else if (PID == PROGRAMNOTVALID)
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 104, ERROR, programList[i]->executableName, "invalid priority or size");
		else
		{ // Process successfully created
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type == USERPROGRAM)
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram)
{
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram = programList[indexOfExecutableProgram];

	// Obtain a process ID & reject new programs if an error ocurred
	PID = OperatingSystem_ObtainAnEntryInTheProcessTable();
	if (PID < 0)
		return PID; // NOFREEENTRY

	// Get program file & check if programFile exists
	programFile = fopen(executableProgram->executableName, "r");
	if (programFile == NULL)
		return PROGRAMDOESNOTEXIST;

	// Obtain the memory requirements of the program & check for errors
	processSize = OperatingSystem_ObtainProgramSize(programFile);
	if (processSize < 0)
		return processSize; // PROGRAMNOTVALID

	// Obtain the priority for the process & check for errors
	priority = OperatingSystem_ObtainPriority(programFile);
	if (priority < 0)
		return priority; // PROGRAMNOTVALID

	// Obtain enough memory space if available
	loadingPhysicalAddress = OperatingSystem_ObtainMainMemory(processSize, PID);
	if (loadingPhysicalAddress < 0)
		return loadingPhysicalAddress; // TOOBIGPROCESS

	// Load program in the allocated memory if possible
	int loadStatus = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if (loadStatus < 0)
		return loadStatus; // TOOBIGPROCESS

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);

	// Show message "Process [PID] created from program [executableName]\n"
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 70, SYSPROC, PID, executableProgram->executableName);

	return PID;
}

// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID)
{
	if (processSize > MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;

	return PID * MAINMEMORYSECTIONSIZE;
}

// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex)
{
	// Track state changes
	ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 111, SYSPROC, PID, statesNames[NEW], programList[processPLIndex]->executableName);

	processTable[PID].busy = 1;
	processTable[PID].initialPhysicalAddress = initialPhysicalAddress;
	processTable[PID].processSize = processSize;
	processTable[PID].copyOfSPRegister = initialPhysicalAddress + processSize;
	processTable[PID].state = NEW;
	processTable[PID].priority = priority;
	processTable[PID].programListIndex = processPLIndex;
	processTable[PID].copyOfAccumulatorRegister = 0;
	processTable[PID].copyofRegisterA = 0;
	processTable[PID].copyofRegisterB = 0;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM)
	{
		processTable[PID].queueID = DAEMONSQUEUE;
		processTable[PID].copyOfPCRegister = initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister = ((unsigned int)1) << EXECUTION_MODE_BIT;
	}
	else
	{
		processTable[PID].queueID = USERPROCESSQUEUE;
		processTable[PID].copyOfPCRegister = 0;
		processTable[PID].copyOfPSWRegister = 0;
	}
}

// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID)
{
	// Track state changes
	ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, statesNames[processTable[PID].state], statesNames[READY]);

	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID], QUEUE_PRIORITY, &(numberOfReadyToRunProcesses[processTable[PID].queueID])) >= 0)
	{
		processTable[PID].state = READY;
	}

	// Print ready to run queue
	OperatingSystem_PrintReadyToRunQueue();
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler()
{
	int selectedProcess;

	selectedProcess = OperatingSystem_ExtractFromReadyToRun();

	return selectedProcess;
}

// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun()
{
	// Variables to extract the ready process
	int selectedProcess = NOPROCESS;
	int currentQID = 0;

	// Extract a process if we have more queues and we dont have a process yet
	while (selectedProcess == NOPROCESS && currentQID < NUMBEROFQUEUES)
	{
		// Get the process from the queue if exists
		selectedProcess = Heap_poll(readyToRunQueue[currentQID], QUEUE_PRIORITY, &(numberOfReadyToRunProcesses[currentQID]));
		// Move to the next queue
		currentQID++;
	}

	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess;
}

// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID)
{
	// Track state changes
	ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, statesNames[processTable[PID].state], statesNames[EXECUTING]);

	//  The process identified by PID becomes the current executing process
	executingProcessID = PID;
	// Change the process' state
	processTable[PID].state = EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}

// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID)
{
	// New values for the CPU registers are obtained from the PCB
	Processor_PushInSystemStack(processTable[PID].copyOfPCRegister);
	Processor_PushInSystemStack(processTable[PID].copyOfPSWRegister);
	Processor_SetRegisterSP(processTable[PID].copyOfSPRegister);
	Processor_SetAccumulator(processTable[PID].copyOfAccumulatorRegister);
	Processor_SetRegisterA(processTable[PID].copyofRegisterA);
	Processor_SetRegisterB(processTable[PID].copyofRegisterB);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}

// Function invoked when the executing process leaves the CPU
void OperatingSystem_PreemptRunningProcess()
{
	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID = NOPROCESS;
}

// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID)
{
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister = Processor_PopFromSystemStack();

	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister = Processor_PopFromSystemStack();

	// Save RegisterSP
	processTable[PID].copyOfSPRegister = Processor_GetRegisterSP();

	// Save the accumulator
	processTable[PID].copyOfAccumulatorRegister = Processor_GetAccumulator();

	// Save the general purpose register A
	processTable[PID].copyofRegisterA = Processor_GetRegisterA();

	// Save the general purpose register B
	processTable[PID].copyofRegisterB = Processor_GetRegisterB();
}

// Exception management routine
void OperatingSystem_HandleException()
{
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 71, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);

	OperatingSystem_TerminateExecutingProcess();
}

// All tasks regarding the removal of the executing process
void OperatingSystem_TerminateExecutingProcess()
{
	// Track state changes
	ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 110, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, statesNames[processTable[executingProcessID].state], statesNames[EXIT]);

	processTable[executingProcessID].state = EXIT;

	if (executingProcessID == sipID)
	{
		// finishing sipID, change PC to address of OS HALT instruction
		Processor_SetSSP(MAINMEMORYSIZE - 1);
		Processor_PushInSystemStack(OS_address_base + 1);
		Processor_PushInSystemStack(Processor_GetPSW());
		executingProcessID = NOPROCESS;
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 99, SHUTDOWN, "The system will shut down now...\n");
		return; // Don't dispatch any process
	}

	Processor_SetSSP(Processor_GetSSP() + 2); // unstack PC and PSW stacked

	if (programList[processTable[executingProcessID].programListIndex]->type == USERPROGRAM)
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;

	if (numberOfNotTerminatedUserProcesses == 0)
	{
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	int selectedProcess = OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall()
{
	int systemCallID;
	int PID = NOPROCESS;

	// Register A contains the identifier of the issued system call
	systemCallID = Processor_GetRegisterC();

	switch (systemCallID)
	{
	case SYSCALL_PRINTEXECPID:
		// Show message: "Process [executingProcessID] has the processor assigned\n"
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 72, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, Processor_GetRegisterA(), Processor_GetRegisterB());
		break;

	case SYSCALL_YIELD:
		// Get process from the executing proccess queue if exists
		PID = Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID], numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]);

		// Check priorities match
		if (PID != NOPROCESS && processTable[executingProcessID].priority < processTable[PID].priority)
			PID = NOPROCESS;

		// Show error message if no equivalent process was found
		if (PID == NOPROCESS)
		{
			ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 117, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
			break;
		}

		// Perform the yield
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 116, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, PID, programList[processTable[PID].programListIndex]->executableName);

		// Preempt the running process
		OperatingSystem_PreemptRunningProcess();

		// Remove the process from the queue
		Heap_poll(readyToRunQueue[processTable[executingProcessID].queueID], QUEUE_PRIORITY, &(numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]));

		// Assign the processor to the process
		OperatingSystem_Dispatch(PID);

		break;

	case SYSCALL_END:
		// Show message: "Process [executingProcessID] has requested to terminate\n"
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 73, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
		OperatingSystem_TerminateExecutingProcess();
		break;
	}
}

//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint)
{
	switch (entryPoint)
	{
	case SYSCALL_BIT: // SYSCALL_BIT=2
		OperatingSystem_HandleSystemCall();
		break;
	case EXCEPTION_BIT: // EXCEPTION_BIT=6
		OperatingSystem_HandleException();
		break;
	}
}

void OperatingSystem_PrintReadyToRunQueue()
{
	// Print title
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 106, SHORTTERMSCHEDULE);

	// Prepare last variable
	int last;

	// Print queues
	for (int id = 0; id < NUMBEROFQUEUES; id++)
	{
		// Print title
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 107, SHORTTERMSCHEDULE, queueNames[id]);

		// Get last element
		last = numberOfReadyToRunProcesses[id] - 1;

		// Print queue elements
		for (int i = 0; i <= last; i++)
		{
			int pid = readyToRunQueue[id][i].info;
			ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, last != i ? 108 : 109, SHORTTERMSCHEDULE, pid, processTable[pid].priority);
		}

		// Print new line
		ComputerSystem_DebugMessage(NO_TIMED_MESSAGE, 112, SHORTTERMSCHEDULE);
	}
}