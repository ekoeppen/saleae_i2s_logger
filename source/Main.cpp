#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <SaleaeDeviceApi.h>

#include <memory>
#include <iostream>
#include <string>

void __stdcall OnConnect( U64 device_id, GenericInterface* device_interface, void* user_data );
void __stdcall OnDisconnect( U64 device_id, void* user_data );
void __stdcall OnReadData( U64 device_id, U8* data, U32 data_length, void* user_data );
void __stdcall OnWriteData( U64 device_id, U8* data, U32 data_length, void* user_data );
void __stdcall OnError( U64 device_id, void* user_data );

FILE *f = NULL;

#define BITS 24
#define CHANNELS 4

#define NUM_ELEMENTS(array) (sizeof(array)/sizeof(array[0]))

LogicInterface* gDeviceInterface = NULL;

U64 gLogicId = 0;
U32 gSampleRateHz = 24000000;

enum analyzer_mode_type {
	SAVE,
	LIVE,
	REPLAY
};

enum protocol_state {
	IDLE,
	FRAME_START,
	FRAME_FIRST_BIT,
	FRAME_ACTIVE,
	FRAME_END,
	DATA_BIT_START,
	DATA_BIT_ACTIVE
};

struct protocol_transition {
	protocol_state current_state;
	U8 mask;
	U8 match;
	protocol_state new_state;
	void (*transition)(int state_index, U8 data);
};

protocol_state current_state = IDLE;
int channel[CHANNELS];
int current_channel;
int current_bit;
analyzer_mode_type analyzer_mode = LIVE;
int ascii = 0;

void handle_frame_start(int state_index, U8 data)
{
	if (ascii) {
		printf("[ ");
	}
}

void handle_frame_end(int state_index, U8 data)
{
	int channel_count = 1; // CHANNELS

	for (int i = 0; i < channel_count; i++) {
		if (ascii) {
			if (channel[i] & 0x00800000) {
				channel[i] = -(0x01000000 - channel[i]);
			}
			printf("%15d ", channel[i]);
		} else {
			putchar((channel[i] & 0x0000ff)      );
			putchar((channel[i] & 0x00ff00) >>  8);
			putchar((channel[i] & 0xff0000) >> 16);
		}
	}
	if (ascii) {
		printf("]\n[ ");
	}

	current_channel = 0;
	current_bit = 0;
	channel[current_channel] = 0;
}

void handle_data_bit(int state_index, U8 data)
{
	channel[current_channel] <<= 1;
	if (data & 0x4) channel[current_channel] |= 1;
	current_bit++;
	if (current_bit == BITS) {
		current_bit = 0;
		current_channel++;
		channel[current_channel] = 0;
	}
}

struct protocol_transition state_machine[] = {
	{IDLE, 			0b00000001, 0b00000001, FRAME_START,		handle_frame_start},
	{FRAME_START,		0b00000001, 0b00000000, FRAME_ACTIVE,		NULL},
	{FRAME_ACTIVE,		0b00000010, 0b00000010, DATA_BIT_ACTIVE,	NULL},
	{DATA_BIT_ACTIVE, 	0b00000010, 0b00000000, FRAME_ACTIVE,		handle_data_bit},
	{FRAME_ACTIVE, 		0b00000001, 0b00000001, FRAME_START,		handle_frame_end},
};

void transition(U8 data)
{
	int i;

	for (i = 0; i < NUM_ELEMENTS(state_machine); i++) {
		if (state_machine[i].current_state == current_state &&
		    (data & state_machine[i].mask) == state_machine[i].match) {
			if (state_machine[i].transition) {
				state_machine[i].transition(i, data);
			}
			current_state = state_machine[i].new_state;
			break;
		}
	}
}

void replay(const char *file_name)
{
	U8 data;
	int n = 0;

	f = fopen(file_name, "r");
	while (!feof(f)) {
		fread(&data, 1, 1, f);
		transition(data);
	}
}

void save(const char *file_name)
{
	f = fopen(file_name, "w");
}

int main( int argc, char *argv[])
{
	if (argc > 1) {
		switch (argv[1][0]) {
		case 'r': analyzer_mode = REPLAY; replay(argv[2]); break;
		case 's': analyzer_mode = SAVE; save(argv[2]); break;
		}
	}
	if (analyzer_mode != REPLAY) {
		DevicesManagerInterface::RegisterOnConnect( &OnConnect );
		DevicesManagerInterface::RegisterOnDisconnect( &OnDisconnect );
		DevicesManagerInterface::BeginConnect();

		std::cout << std::uppercase << "Devices are currently set up to read and write at " << gSampleRateHz << " Hz.  You can change this in the code." << std::endl;

		while( true )
		{
			std::cout << std::endl << std::endl << "You can type read, write, readbyte, writebyte, stop, or exit." << std::endl << "(r, w, rb, wb, s, and e for short)" << std::endl << std::endl;
			std::string command;
			std::getline( std::cin, command );

			if( command == "exit" || command == "e" )
				break;

			if( command == "" )
				continue;

			if( gDeviceInterface == NULL )
			{
				std::cout << "Sorry, no devices are connected." << std::endl;
				continue;
			} 

			if( command == "stop" || command == "s" )
			{
				if( gDeviceInterface->IsStreaming() == false )
					std::cout << "Sorry, the device is not currently streaming." << std::endl;
				else
					gDeviceInterface->Stop();

				continue;
			}

			if( gDeviceInterface->IsStreaming() == true )
			{
				std::cout << "Sorry, the device is already streaming." << std::endl;
				continue;
			}

			if( command == "read" || command == "r" )
			{
				gDeviceInterface->ReadStart();
			}
			else
				if( command == "write" || command == "w" )
				{	
					gDeviceInterface->WriteStart();
				}
				else
					if( command == "readbyte" || command == "rb" )
					{

						std::cout << "Got value 0x" << std::hex << U32( gDeviceInterface->GetInput() ) << std::dec << std::endl;
					}
					else
						if( command == "writebyte" || command == "wb" )
						{
							static U8 write_val = 0;

							gDeviceInterface->SetOutput( write_val );
							std::cout << "Logic is now outputting 0x" << std::hex << U32( write_val ) << std::dec << std::endl;
							write_val++;
						}
		}
	}

	if (f) fclose(f);
	return 0;
}

void __stdcall OnConnect( U64 device_id, GenericInterface* device_interface, void* user_data )
{
	if( dynamic_cast<LogicInterface*>( device_interface ) != NULL )
	{
		std::cout << "A Logic device was connected (id=0x" << std::hex << device_id << std::dec << ")." << std::endl;

		gDeviceInterface = (LogicInterface*)device_interface;
		gLogicId = device_id;

		gDeviceInterface->RegisterOnReadData( &OnReadData );
		gDeviceInterface->RegisterOnWriteData( &OnWriteData );
		gDeviceInterface->RegisterOnError( &OnError );

		gDeviceInterface->SetSampleRateHz( gSampleRateHz );
	}
}

void __stdcall OnDisconnect( U64 device_id, void* user_data )
{
	if( device_id == gLogicId )
	{
		std::cout << "A device was disconnected (id=0x" << std::hex << device_id << std::dec << ")." << std::endl;
		gDeviceInterface = NULL;
	}
}

void __stdcall OnReadData( U64 device_id, U8* data, U32 data_length, void* user_data )
{
	switch (analyzer_mode) {
	case SAVE:
		fwrite(data, data_length, 1, f);
		break;
	case LIVE:
		for (int i = 0; i < data_length; i++) {
			transition(data[i]);
		}
		break;
	}
	//you own this data.  You don't have to delete it immediately, you could keep it and process it later, for example, or pass it to another thread for processing.
	DevicesManagerInterface::DeleteU8ArrayPtr( data );
}

void __stdcall OnWriteData( U64 device_id, U8* data, U32 data_length, void* user_data )
{
	static U8 dat = 0;

	//it's our job to feed data to Logic whenever this function gets called.  Here we're just counting.
	//Note that you probably won't be able to get Logic to write data at faster than 4MHz (on Windows) do to some driver limitations.

	//here we're just filling the data with a 0-255 pattern.
	for( U32 i=0; i<data_length; i++ )
	{
		*data = dat;
		dat++;
		data++;
	}

	std::cout << "Wrote " << data_length << " bytes of data." << std::endl;
}

void __stdcall OnError( U64 device_id, void* user_data )
{
	std::cout << "A device reported an Error.  This probably means that it could not keep up at the given data rate, or was disconnected. You can re-start the capture automatically, if your application can tolerate gaps in the data." << std::endl;
	//note that you should not attempt to restart data collection from this function -- you'll need to do it from your main thread (or at least not the one that just called this function).
}
