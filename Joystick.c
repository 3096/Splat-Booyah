/*
Nintendo Switch Fightstick - Proof-of-Concept

Based on the LUFA library's Low-Level Joystick Demo
	(C) Dean Camera
Based on the HORI's Pokken Tournament Pro Pad design
	(C) HORI

This project implements a modified version of HORI's Pokken Tournament Pro Pad
USB descriptors to allow for the creation of custom controllers for the
Nintendo Switch. This also works to a limited degree on the PS3.

Since System Update v3.0.0, the Nintendo Switch recognizes the Pokken
Tournament Pro Pad as a Pro Controller. Physical design limitations prevent
the Pokken Controller from functioning at the same level as the Pro
Controller. However, by default most of the descriptors are there, with the
exception of Home and Capture. Descriptor modification allows us to unlock
these buttons for our use.
*/

/** \file
 *
 *  Main source file for the posts printer demo. This file contains the main tasks of
 *  the demo and is responsible for the initial application hardware configuration.
 */

#include "Joystick.h"

// Main entry point.
int main(void)
{
	// We'll start by performing hardware and peripheral setup.
	SetupHardware();
	// We'll then enable global interrupts for our use.
	GlobalInterruptEnable();
	// Once that's done, we'll enter an infinite loop.
	for (;;)
	{
		// We need to run our task to process and deliver data for our IN and OUT endpoints.
		HID_Task();
		// We also need to run the main USB management task.
		USB_USBTask();
	}
}

// Configures hardware and peripherals, such as the USB peripherals.
void SetupHardware(void)
{
	// We need to disable watchdog if enabled by bootloader/fuses.
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// We need to disable clock division before initializing the USB hardware.
	clock_prescale_set(clock_div_1);

	// We can then initialize our hardware and peripherals, including the USB stack.

#ifdef ALERT_WHEN_DONE
	// Both PORTD and PORTB will be used for the optional LED flashing and buzzer.
#warning LED and Buzzer functionality enabled. All pins on both PORTB and PORTD will toggle when printing is done.
	DDRD = 0xFF; //Teensy uses PORTD
	PORTD = 0x0;
	//We'll just flash all pins on both ports since the UNO R3
	DDRB = 0xFF; //uses PORTB. Micro can use either or, but both give us 2 LEDs
	PORTB = 0x0; //The ATmega328P on the UNO will be resetting, so unplug it?
#endif

	// The USB stack should be initialized last.
	USB_Init();
}

// Fired to indicate that the device is enumerating.
void EVENT_USB_Device_Connect(void)
{
	// We can indicate that we're enumerating here (via status LEDs, sound, etc.).
}

// Fired to indicate that the device is no longer connected to a host.
void EVENT_USB_Device_Disconnect(void)
{
	// We can indicate that our device is not ready (via status LEDs, sound, etc.).
}

// Fired when the host set the current configuration of the USB device after enumeration.
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	// We setup the HID report endpoints.
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_IN_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);

	// We can read ConfigSuccess to indicate a success or failure at this point.
}

// Process control requests sent to the device from the USB host.
void EVENT_USB_Device_ControlRequest(void)
{
	// We can handle two control requests: a GetReport and a SetReport.

	// Not used here, it looks like we don't receive control request from the Switch.
}

// Process and deliver data from IN and OUT endpoints.
void HID_Task(void)
{
	// If the device isn't connected and properly configured, we can't do anything here.
	if (USB_DeviceState != DEVICE_STATE_Configured)
		return;

	// We'll start with the OUT endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);
	// We'll check to see if we received something on the OUT endpoint.
	if (Endpoint_IsOUTReceived())
	{
		// If we did, and the packet has data, we'll react to it.
		if (Endpoint_IsReadWriteAllowed())
		{
			// We'll create a place to store our data received from the host.
			USB_JoystickReport_Output_t JoystickOutputData;
			// We'll then take in that data, setting it up in our storage.
			Endpoint_Read_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData), NULL);
			// At this point, we can react to this data.

			// However, since we're not doing anything with this data, we abandon it.
		}
		// Regardless of whether we reacted to the data, we acknowledge an OUT packet on this endpoint.
		Endpoint_ClearOUT();
	}

	// We'll then move on to the IN endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_IN_EPADDR);
	// We first check to see if the host is ready to accept data.
	if (Endpoint_IsINReady())
	{
		// We'll create an empty report.
		USB_JoystickReport_Input_t JoystickInputData;
		// We'll then populate this report with what we want to send to the host.
		GetNextReport(&JoystickInputData);
		// Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
		Endpoint_Write_Stream_LE(&JoystickInputData, sizeof(JoystickInputData), NULL);
		// We then send an IN packet on this endpoint.
		Endpoint_ClearIN();
	}
}

typedef enum {
	SYNC_CONTROLLER,
	BOOYAH,
	DONE
} State_t;
State_t state = SYNC_CONTROLLER;

// Repeat ECHOES times the last sent report.
//
// This value is affected by several factors:
// - The descriptors *.PollingIntervalMS value.
// - The Switch readiness to accept reports (driven by the Endpoint_IsINReady() function,
//   it looks to be 8 ms).
// - The Switch screen refresh rate (it looks that anything that would update the screen
//   at more than 30 fps triggers pixel skipping).
#if defined(ZIG_ZAG_PRINTING)
	#if defined(SYNC_TO_30_FPS)
		// In this case we will send 641 moves and 1 stop every 2 lines, using 4 reports for
		// each send (done in 32 ms). We will inject an additional report every 6 commands, to
		// align them to 6 video frames (lasting 200 ms).
		#define ECHOES 3
	#else
		// In this case we will send 641 moves and 1 stop every 2 lines, using 5 reports for
		// each send, in around 25 s (thus 8 ms per report), updating the screen every 40 ms.
		#define ECHOES 4
	#endif
#else
	// In this case we will send 320 moves and 320 stops per line, using 3 reports for each
	// send, in around 15 s (thus 8 ms per report), updating the screen every 48 ms.
	#define ECHOES 2
#endif
int echoes = 0;
USB_JoystickReport_Input_t last_report;

int command_count = 0;
int report_count = 0;
int xpos = 0;
int ypos = 0;
int portsval = 0;

#define max(a, b) (a > b ? a : b)
#define ms_2_count(ms) (ms / ECHOES / (max(POLLING_MS, 8) / 8 * 8))

bool moving_left = true;
bool turning_left = true;

void booyah(USB_JoystickReport_Input_t *const ReportData)
{
	bool a_down = false;
	if(command_count % 2 != 0)
	{
		ReportData->Button |= SWITCH_A | SWITCH_RCLICK;
		a_down = true;
	}

	// shoot and swim
	bool shooting = true;
	if(command_count/60 % 3 != 0)
		ReportData->Button |= SWITCH_ZR;
	else
	{
		ReportData->Button |= SWITCH_ZL;
		shooting = false;
	}

	int rnd;

	// Booyah!
	if((rnd = rand() % 512) == 0 && a_down)
		ReportData->HAT = HAT_BOTTOM;

	// Attempt throw bomb
	if((rnd = rand() % 16) == 0 && command_count % 180 < 15)
		ReportData->Button |= SWITCH_R;

	// Strafe left and right
	if((rnd = rand() % 16) == 0)
		moving_left = !moving_left;
	if(moving_left)
		ReportData->LX = STICK_MIN;
	else
		ReportData->LX = STICK_MAX;

	// Move forward and backward
	if(shooting == false)
		ReportData->LY = STICK_MIN;
	else
		ReportData->LY = STICK_MAX;

	// Turn left and right
	if((rnd = rand() % 16) == 0)
	{
		if((rnd = rand() % 4) == 0)
			turning_left = !turning_left;
		if(turning_left)
			ReportData->RX = STICK_MIN;
		else
			ReportData->RX = STICK_MAX;
	}
	
}


// Prepare the next report for the host
void GetNextReport(USB_JoystickReport_Input_t *const ReportData)
{

	// Prepare an empty report
	memset(ReportData, 0, sizeof(USB_JoystickReport_Input_t));
	ReportData->LX = STICK_CENTER;
	ReportData->LY = STICK_CENTER;
	ReportData->RX = STICK_CENTER;
	ReportData->RY = STICK_CENTER;
	ReportData->HAT = HAT_CENTER;

	// Repeat ECHOES times the last report
	if (echoes > 0)
	{
		memcpy(ReportData, &last_report, sizeof(USB_JoystickReport_Input_t));
		echoes--;
		return;
	}

	// States and moves management
	switch (state)
	{
	case SYNC_CONTROLLER:
		if (command_count > ms_2_count(2000))
		{
			command_count = 0;
			state = BOOYAH;
		}
		else
		{
			if (command_count == ms_2_count(500) || command_count == ms_2_count(1000))
				ReportData->Button |= SWITCH_L | SWITCH_R;
			else if (command_count == ms_2_count(1500) || command_count == ms_2_count(2000))
				ReportData->Button |= SWITCH_A;
			command_count++;
		}
		break;
	case BOOYAH:
		booyah(ReportData);
		command_count++;
		break;
	case DONE:
#ifdef ALERT_WHEN_DONE
		portsval = ~portsval;
		PORTD = portsval; // flash LED(s) and sound buzzer if attached
		PORTB = portsval;
		_delay_ms(250);
#endif
		return;
	}

	// Prepare to echo this report
	memcpy(&last_report, ReportData, sizeof(USB_JoystickReport_Input_t));
	echoes = ECHOES;
}
