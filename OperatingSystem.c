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
void OperatingSystem_MoveToTheBLOCKEDState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateExecutingProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int);
void OperatingSystem_ReleaseMainMemory(int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_HandleClockInterrupt();
void OperatingSystem_BlockRunningProcess();
int OperatingSystem_ExtractReadyFromSleepingQueue();
int OperatingSystem_PeekReadyToRunQueue();

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

// Number of partitions in the system
int numberOfPartitions;

// Initial PID for assignation (Not assigned)
int initialPID = -1;

// Begin indes for daemons in programList
// int baseDaemonsInProgramList;

// Array that contains the identifiers of the READY processes
heapItem *readyToRunQueue[NUMBEROFQUEUES];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0, 0};
// heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
// int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0};

// Exercise 5-b of V2
// Heap with blocked processes sorted by when to wakeup
heapItem *sleepingProcessesQueue;
int numberOfSleepingProcesses = 0;

char *queueNames[NUMBEROFQUEUES] = {"USER", "DAEMONS"};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses = 0;

// char DAEMONS_PROGRAMS_FILE[MAXFILENAMELENGTH]="teachersDaemons";

int MAINMEMORYSECTIONSIZE = 60;

extern int MAINMEMORYSIZE;

int PROCESSTABLEMAXSIZE = 4;

// Keep track of the number of clock interrupts that occurr
int numberOfClockInterrupts = 0;

// For debug messages
char *stateNames[5] = {"NEW", "READY", "EXECUTING", "BLOCKED", "EXIT"};
char *exceptionNames[4] = {"division by zero", "invalid processor mode", "invalid address", "invalid instruction"};

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

	// Space for the sleeping processes queue
	sleepingProcessesQueue = Heap_create(PROCESSTABLEMAXSIZE);

	// Initialize partition table
	numberOfPartitions = OperatingSystem_InitializePartitionTable();

	// Start loading Operating System code
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

	// Fill the ArrivalTimeQueue before LTS V3-1c
	ComputerSystem_FillInArrivalTimeQueue();

	// Print system status V3-1d
	OperatingSystem_PrintStatus();

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

	// If we only managed to create SIP & there are no new processes coming next, terminate execution
	if (numCreatedProcesses == 1 && numberOfProgramsInArrivalTimeQueue == 0)
		// Tell SIP we are ready to shutdown
		OperatingSystem_ReadyToShutdown();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Prints general status
	OperatingSystem_PrintStatus();
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler()
{
	int PID, i,
		numberOfSuccessfullyCreatedProcesses = 0;

	// While there are new programs that need to be created create them
	while (OperatingSystem_IsThereANewProgram() == YES)
	{
		// Extract the new program
		i = Heap_poll(arrivalTimeQueue, QUEUE_ARRIVAL, &numberOfProgramsInArrivalTimeQueue);

		// Create the program
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
		else if (PID == MEMORYFULL)
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 144, ERROR, programList[i]->executableName);
		else
		{ // Process successfully created
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type == USERPROGRAM)
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}

	// If any process was created print the general status
	if (numberOfSuccessfullyCreatedProcesses)
		OperatingSystem_PrintStatus();

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram)
{
	int PID;
	int processSize;
	int partitionIndex;
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

	// * Obtain enough memory space if available & load program if possible

	// Log the memory request and show the initial state of the partitions table
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 142, SYSMEM, PID, executableProgram->executableName, processSize);
	OperatingSystem_ShowPartitionTable("before allocating memory");

	// Obtain best partition
	partitionIndex = OperatingSystem_ObtainMainMemory(processSize);
	if (partitionIndex < 0)
		return partitionIndex; // TOOBIGPROCESS, MEMORYFULL

	// Load program in the allocated memory if possible
	int loadStatus = OperatingSystem_LoadProgram(programFile, partitionsTable[partitionIndex].initAddress, processSize);
	if (loadStatus < 0)
		return loadStatus; // TOOBIGPROCESS

	// Allocate partition & log the allocation
	partitionsTable[partitionIndex].PID = PID;
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 143, SYSMEM, partitionIndex, partitionsTable[partitionIndex].initAddress, partitionsTable[partitionIndex].size, PID, executableProgram->executableName);

	// * PCB initialization
	OperatingSystem_PCBInitialization(PID, partitionsTable[partitionIndex].initAddress, processSize, priority, indexOfExecutableProgram);

	// Show the updated state of the partitions table (here because this function relies on the process table entry)
	OperatingSystem_ShowPartitionTable("after allocating memory");

	// Removed for V3
	// Show message "Process [PID] created from program [executableName]\n"
	// ComputerSystem_DebugMessage(TIMED_MESSAGE, 70, SYSPROC, PID, executableProgram->executableName);

	return PID;
}

// Obtains a partition index from Main Memory if one is available
int OperatingSystem_ObtainMainMemory(int processSize)
{
	// * Find the best fit partition

	int bestPartitionIndex = -1;
	int existsBigEnoughPartition = 0;

	// Go through all partitions
	for (int i = 0; i < numberOfPartitions; i++)
	{
		// Skip partitions that are too small
		if (partitionsTable[i].size < processSize)
			continue;

		// Register a big enough partition exists
		existsBigEnoughPartition = 1;

		// Skip partitions that were already allocated to other processes
		if (partitionsTable[i].PID != NOPROCESS)
			continue;

		// If we have no partition yet, assign the first one that fits
		if (bestPartitionIndex == -1)
			bestPartitionIndex = i;
		// If the size of the partition fits and is lower than the best partition we found until now, change the partition
		else if (partitionsTable[i].size < partitionsTable[bestPartitionIndex].size)
			bestPartitionIndex = i;
		// If the size matches, choose the one with the lowest address
		else if (partitionsTable[i].size == partitionsTable[bestPartitionIndex].size && partitionsTable[i].initAddress < partitionsTable[bestPartitionIndex].initAddress)
			bestPartitionIndex = i;
	}

	// If we were not able to find a fit
	if (bestPartitionIndex == -1)
	{
		// If there were free partitions and the process was too big, return TOOBIGPROCESS
		if (existsBigEnoughPartition == 0)
			return TOOBIGPROCESS;
		// If there were no free partitions, return MEMORYFULL
		else
			return MEMORYFULL;
	}

	// Return the best partition index
	return bestPartitionIndex;
}

void OperatingSystem_ReleaseMainMemory(int PID)
{
	// Log the partition table before releasing memory
	OperatingSystem_ShowPartitionTable("before releasing memory");

	// Store the PID of the program which had the partition assigned
	int partitionIndex = 0;

	// Go through all partitions until finding the one assigned to the process
	while (partitionsTable[partitionIndex].PID != PID)
		partitionIndex++;

	// Release the memory
	partitionsTable[partitionIndex].PID = NOPROCESS;

	// Log the memory release
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 145, SYSMEM, partitionIndex, partitionsTable[partitionIndex].initAddress, partitionsTable[partitionIndex].size, PID, programList[processTable[PID].programListIndex]->executableName);

	// Log the partition table after releasing memory
	OperatingSystem_ShowPartitionTable("after releasing memory");
}

// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex)
{
	// Track state changes
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 111, SYSPROC, PID, stateNames[NEW], programList[processPLIndex]->executableName);

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
	processTable[PID].whenToWakeUp = 0;
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
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, stateNames[processTable[PID].state], stateNames[READY]);

	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID], QUEUE_PRIORITY, &(numberOfReadyToRunProcesses[processTable[PID].queueID])) >= 0)
		processTable[PID].state = READY;

	// Print ready to run queue
	// Exercise V2-4 was to comment the following line
	// OperatingSystem_PrintReadyToRunQueue();
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
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 110, SYSPROC, PID, programList[processTable[PID].programListIndex]->executableName, stateNames[processTable[PID].state], stateNames[EXECUTING]);

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
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 140, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, exceptionNames[Processor_GetRegisterD()]);

	OperatingSystem_TerminateExecutingProcess();

	// Print general status
	OperatingSystem_PrintStatus();
}

// All tasks regarding the removal of the executing process
void OperatingSystem_TerminateExecutingProcess()
{
	// Track state changes
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 110, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, stateNames[processTable[executingProcessID].state], stateNames[EXIT]);

	// Exit the process
	processTable[executingProcessID].state = EXIT;

	// Release the memory partition used by the process
	OperatingSystem_ReleaseMainMemory(executingProcessID);

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

	// Check if there are unfinished processes and process to be newly created in the future before shutting down
	if (numberOfNotTerminatedUserProcesses == 0 && numberOfProgramsInArrivalTimeQueue == 0)
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();

	// Select the next process to execute (sipID if no more user processes)
	int selectedProcess = OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall()
{
	int systemCallID, delay;
	int PID = NOPROCESS;

	// Register A contains the identifier of the issued system call
	systemCallID = Processor_GetRegisterC();

	switch (systemCallID)
	{
	// Handle print exception syscall
	case SYSCALL_PRINTEXECPID:
		// Show message: "Process [executingProcessID] has the processor assigned\n"
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 72, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, Processor_GetRegisterA(), Processor_GetRegisterB());
		break;

	// Handle yield syscall
	case SYSCALL_YIELD:
		// Get process from the ready processes queue if exists
		PID = Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID], numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]);

		// Check priorities match
		if (PID != NOPROCESS && processTable[executingProcessID].priority != processTable[PID].priority)
			PID = NOPROCESS;

		// Show error message if no equivalent process was found
		if (PID == NOPROCESS)
		{
			ComputerSystem_DebugMessage(TIMED_MESSAGE, 117, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
			break;
		}

		// Perform the yield
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 116, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, PID, programList[processTable[PID].programListIndex]->executableName);

		// Remove the process from the queue
		Heap_poll(readyToRunQueue[processTable[executingProcessID].queueID], QUEUE_PRIORITY, &(numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]));

		// Preempt the running process
		OperatingSystem_PreemptRunningProcess();

		// Assign the processor to the process
		OperatingSystem_Dispatch(PID);

		// Print general status
		OperatingSystem_PrintStatus();

		break;

	// Handle sleep syscall
	case SYSCALL_SLEEP:
		// Compute the sleep time to the process PCB
		delay = Processor_GetRegisterD() > 0 ? Processor_GetRegisterD() : abs(Processor_GetAccumulator());
		processTable[executingProcessID].whenToWakeUp = delay + numberOfClockInterrupts + 1;

		// Blocks the current process
		OperatingSystem_BlockRunningProcess();

		// Dispatches the next process in the queue which is decided by the STS
		PID = OperatingSystem_ShortTermScheduler();
		OperatingSystem_Dispatch(PID);

		// Print general status
		OperatingSystem_PrintStatus();
		break;
	// Handle end syscalls
	case SYSCALL_END:
		// Show message: "Process [executingProcessID] has requested to terminate\n"
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 73, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
		OperatingSystem_TerminateExecutingProcess();

		// Print general status
		OperatingSystem_PrintStatus();
		break;
	// Handle invalid syscalls
	default:
		// Show message
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 141, INTERRUPT, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, systemCallID);

		// Terminate process
		OperatingSystem_TerminateExecutingProcess();

		// Print general status
		OperatingSystem_PrintStatus();
		break;
	}
}

//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint)
{
	switch (entryPoint)
	{
	// Handle syscalls
	case SYSCALL_BIT: // SYSCALL_BIT=2
		OperatingSystem_HandleSystemCall();
		break;
	// Handle exceptions
	case EXCEPTION_BIT: // EXCEPTION_BIT=6
		OperatingSystem_HandleException();
		break;
	// Handle clock interrupts
	case CLOCKINT_BIT: // CLOCKINT_BIT = 9
		OperatingSystem_HandleClockInterrupt();
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

void OperatingSystem_HandleClockInterrupt()
{
	// Log clock interrupt
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 120, INTERRUPT, ++numberOfClockInterrupts);

	// * Waking up processes

	// Store the PID of the process to wake up, the number of woken processes & whether the executing process must be preempted
	int PID = NOPROCESS;
	int wokenProcesses = 0;

	// Check sleeping processes for any process to wake up
	while (PID = OperatingSystem_ExtractReadyFromSleepingQueue(), PID != NOPROCESS)
	{
		// Move the process to the ready state
		OperatingSystem_MoveToTheREADYState(PID);

		// Increase the woken processes counter
		wokenProcesses++;
	}

	// * Creating newly arrived process
	OperatingSystem_LongTermScheduler();

	// Log the general status if any process was woken up
	if (wokenProcesses > 0)
		OperatingSystem_PrintStatus();

	// If there are no unfinished tasks finish the process SIP to shutdown
	if (numberOfNotTerminatedUserProcesses == 0 && numberOfProgramsInArrivalTimeQueue == 0)
		// Terminate SIP
		OperatingSystem_TerminateExecutingProcess();

	// * Preempting executing process

	// Check whether the executing process has to be preempted
	PID = OperatingSystem_PeekReadyToRunQueue();

	// If the executing process exists, check if it must be preempted (higher queue/priority process)
	if (PID != NOPROCESS &&
		(processTable[PID].queueID < processTable[executingProcessID].queueID ||   // Queue is higher priority
		 (processTable[PID].queueID == processTable[executingProcessID].queueID && // OR queue is the same with higher priority
		  processTable[PID].priority < processTable[executingProcessID].priority)))
	{
		// Log what is going to happen
		ComputerSystem_DebugMessage(TIMED_MESSAGE, 121, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, PID, programList[processTable[PID].programListIndex]->executableName);

		// Preempt the running process
		OperatingSystem_PreemptRunningProcess();

		// Extract the process from the queue
		OperatingSystem_ExtractFromReadyToRun(); // PID is the same so there is no need to update the variable

		// Dispatch the next process in the queue
		OperatingSystem_Dispatch(PID);

		// Log the general status
		OperatingSystem_PrintStatus();
	}
}

// Blocks the running process
void OperatingSystem_BlockRunningProcess()
{
	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);

	// * Move executing process to the blocked state
	// Track state changes
	ComputerSystem_DebugMessage(TIMED_MESSAGE, 110, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, stateNames[processTable[executingProcessID].state], stateNames[BLOCKED]);

	// Add the process to the sleeping processes queue
	if (Heap_add(executingProcessID, sleepingProcessesQueue, QUEUE_WAKEUP, &(numberOfSleepingProcesses)) >= 0)
		processTable[executingProcessID].state = BLOCKED;

	// The processor is not assigned until the OS selects another process
	executingProcessID = NOPROCESS;
}

// Extracts a ready process from the sleeping processes queue
int OperatingSystem_ExtractReadyFromSleepingQueue()
{
	// Variables to extract the ready process from the sleeping queue
	int selPID = Heap_getFirst(sleepingProcessesQueue, numberOfSleepingProcesses);

	// If it is a process and it is time to wake up
	if (selPID != NOPROCESS && processTable[selPID].whenToWakeUp == numberOfClockInterrupts)
		// Remove it from the heap
		Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
	else
		// Return NOPROCESS if no suitable process was found
		selPID = NOPROCESS;

	// Return the process
	return selPID;
}

// Return PID of the most priority process in the READY queues
int OperatingSystem_PeekReadyToRunQueue()
{
	// Variables to extract the ready process
	int selectedProcess = NOPROCESS;
	int currentQID = 0;

	// Extract a process if we have more queues and we dont have a process yet
	while (selectedProcess == NOPROCESS && currentQID < NUMBEROFQUEUES)
	{
		// Get the process from the queue if exists
		selectedProcess = Heap_getFirst(readyToRunQueue[currentQID], numberOfReadyToRunProcesses[currentQID]);
		// Move to the next queue
		currentQID++;
	}

	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess;
}