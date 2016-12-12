////////////////////////////////////////////////////////////////
//nakedsoftware.org, spi@oifii.org or stephane.poirier@oifii.org
//
//
//2013dec01, creation from spimonitor (portmidi mm.c) for 
//2013dec03, creation from spimidiarpeggiator.cpp (not stabilized)
//2013dec04, stabilized poly version success
//2013dec04, tested at various tempo (60, ... 960) ok 
//2013dec04, good when playing legato and sustenutos (carl test)
//2013dec08, GOZILLA version uses a map with note off 
//2013dec09, ULTRAMAN version uses a map without note off
//2013dec10, AVATAR version uses a list without note off
//
//nakedsoftware.org, spi@oifii.org or stephane.poirier@oifii.org
////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include <ctime>
#include <string>

#include <map>
#include <list>

#include <iostream>
using namespace std;
#include <assert.h>
#include <windows.h>



#include "stdlib.h"
#include "ctype.h"
#include "stdio.h"
#include "porttime.h"
#include "portmidi.h"

#define STRING_MAX 80

#define MIDI_CODE_MASK  0xf0
#define MIDI_CHN_MASK   0x0f
//#define MIDI_REALTIME   0xf8
//  #define MIDI_CHAN_MODE  0xfa 
#define MIDI_OFF_NOTE   0x80
#define MIDI_ON_NOTE    0x90
#define MIDI_POLY_TOUCH 0xa0
#define MIDI_CTRL       0xb0
#define MIDI_CH_PROGRAM 0xc0
#define MIDI_TOUCH      0xd0
#define MIDI_BEND       0xe0

#define MIDI_SYSEX      0xf0
#define MIDI_Q_FRAME	0xf1
#define MIDI_SONG_POINTER 0xf2
#define MIDI_SONG_SELECT 0xf3
#define MIDI_TUNE_REQ	0xf6
#define MIDI_EOX        0xf7
#define MIDI_TIME_CLOCK 0xf8
#define MIDI_START      0xfa
#define MIDI_CONTINUE	0xfb
#define MIDI_STOP       0xfc
#define MIDI_ACTIVE_SENSING 0xfe
#define MIDI_SYS_RESET  0xff

#define MIDI_ALL_SOUND_OFF 0x78
#define MIDI_RESET_CONTROLLERS 0x79
#define MIDI_LOCAL	0x7a
#define MIDI_ALL_OFF	0x7b
#define MIDI_OMNI_OFF	0x7c
#define MIDI_OMNI_ON	0x7d
#define MIDI_MONO_ON	0x7e
#define MIDI_POLY_ON	0x7f


#define private static

#ifndef false
#define false 0
#define true 1
#endif


int Terminate();
UINT global_TimerId=0;
PmStream* global_pPmStreamMIDIOUT = NULL; // midi output
PmStream* global_pPmStreamMIDIIN;      // midi input 
boolean global_active = false;     // set when global_pPmStreamMIDIIN is ready for reading 
//int global_notenumber = -1;
bool global_playflag = true;

int inputmidideviceid =  11; //alesis q49 midi port id (when midi yoke installed)
//int inputmididevice =  1; //midi yoke 1 (when midi yoke installed)
int outputmidideviceid = 13; //device id 13, for "Out To MIDI Yoke:  1", when 8 yoke installed for spi
int outputmidichannel = 1; 
float global_tempo_bpm = 60.0f;

#define PROGRAM_GOZILLA		0
#define PROGRAM_ULTRAMAN	1
#define PROGRAM_AVATAR		2
#define PROGRAM_THOR		3
string global_programstring = "AVATAR";
int global_programid;
//GOZILLA
map<int,int> global_notenumbermap; 
map<int,int>::iterator global_mapit=global_notenumbermap.begin();
//AVATAR
list<int> global_notenumberlist;
list<int>::iterator global_listit=global_notenumberlist.begin();
//THOR
int global_prevnotenumber=-1;

map<string,int> global_inputmididevicemap;
map<string,int> global_outputmididevicemap;
string inputmididevicename = "Q49";
string outputmididevicename = "Out To MIDI Yoke:  1";

//typedef int boolean;

int debug = false;	// never set, but referenced by userio.c 
boolean in_sysex = false;   // we are reading a sysex message 
boolean inited = false;     // suppress printing during command line parsing 
boolean done = false;       // when true, exit 
boolean notes = true;       // show notes? 
boolean controls = true;    // show continuous controllers 
boolean bender = true;      // record pitch bend etc.? 
boolean excldata = true;    // record system exclusive data? 
boolean verbose = true;     // show text representation? 
boolean realdata = true;    // record real time messages? 
boolean clksencnt = true;   // clock and active sense count on 
boolean chmode = true;      // show channel mode messages 
boolean pgchanges = true;   // show program changes 
boolean flush = false;	    // flush all pending MIDI data 

uint32_t filter = 0;            // remember state of midi filter 

uint32_t clockcount = 0;        // count of clocks 
uint32_t actsensecount = 0;     // cout of active sensing bytes 
uint32_t notescount = 0;        // #notes since last request 
uint32_t notestotal = 0;        // total #notes 

char val_format[] = "    Val %d\n";


//The event signaled when the app should be terminated.
HANDLE g_hTerminateEvent = NULL;
//Handles events that would normally terminate a console application. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);



///////////////////////////////////////////////////////////////////////////////
//    Imported variables
///////////////////////////////////////////////////////////////////////////////

extern  int     abort_flag;

///////////////////////////////////////////////////////////////////////////////
//    Routines local to this module
///////////////////////////////////////////////////////////////////////////////

//private    void    mmexit(int code);
private void output(PmMessage data);
private int  put_pitch(int p);
private void showhelp();
private void showbytes(PmMessage data, int len, boolean newline);
private void showstatus(boolean flag);
private void doascii(char c);
private int  get_number(char *prompt);

// read a number from console
//
int get_number(char *prompt)
{
    char line[STRING_MAX];
    int n = 0, i;
    printf(prompt);
    while (n != 1) {
        n = scanf("%d", &i);
        fgets(line, STRING_MAX, stdin);

    }
    return i;
}


void receive_poll(PtTimestamp timestamp, void *userData)
{
    PmEvent event;
    int count; 
    if (!global_active) return;
    while ((count = Pm_Read(global_pPmStreamMIDIIN, &event, 1))) 
	{
        if (count == 1) 
		{
			//1) output message
			output(event.message);
			
			//2) detect note
			int msgstatus = Pm_MessageStatus(event.message);
			int notenumber = Pm_MessageData1(event.message);
			//if(msgstatus==MIDI_ON_NOTE)
			if(msgstatus>=MIDI_ON_NOTE && msgstatus<(MIDI_ON_NOTE+16) && Pm_MessageData2(event.message)!=0)
			{
				if(global_programid==PROGRAM_ULTRAMAN || global_programid==PROGRAM_AVATAR || global_programid==PROGRAM_THOR)
				{
					//send note on
					PmEvent tempPmEvent;
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 100); //note on, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
				}

				if(global_programid==PROGRAM_GOZILLA || global_programid==PROGRAM_ULTRAMAN)
				{
					//insert note to map
					global_notenumbermap.insert(pair<int,int>(notenumber,0));
					global_mapit = global_notenumbermap.begin();
				}
				else if(global_programid==PROGRAM_AVATAR || global_programid==PROGRAM_THOR)
				{
					//insert note to list
					global_notenumberlist.push_back(notenumber);
					global_listit = global_notenumberlist.begin();
				}
			}
			else if((msgstatus>=MIDI_ON_NOTE && msgstatus<(MIDI_ON_NOTE+16) && Pm_MessageData2(event.message)==0)
				   || (msgstatus>=MIDI_OFF_NOTE && msgstatus<(MIDI_OFF_NOTE+16)))
			{
				if(global_programid==PROGRAM_GOZILLA)
				{
					//send note off
					PmEvent tempPmEvent;
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 0); //note off, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);

					//erase note from map
					global_mapit = global_notenumbermap.find(notenumber);
					global_mapit = global_notenumbermap.erase(global_mapit);
					global_mapit = global_notenumbermap.begin();
				}
				else if(global_programid==PROGRAM_ULTRAMAN || global_programid==PROGRAM_AVATAR || global_programid==PROGRAM_THOR)
				{
				}
			}
			
		}
        else            
		{
			printf(Pm_GetErrorText((PmError)count)); //spi a cast as (PmError)
		}
    }
}

HHOOK   g_kb_hook = 0;
LRESULT CALLBACK kb_proc(int code, WPARAM w, LPARAM l)
{
	HWND myHWNDactive=GetForegroundWindow(); //GetActiveWindow();
	HWND myHWNDconsole=GetConsoleWindow();
	//printf("active %d, console %d \n", myHWNDactive, myHWNDconsole);
	if(myHWNDactive==myHWNDconsole)
	{
		PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)l;
		const char *info = NULL;
		if (w == WM_KEYDOWN)
			info = "key dn";
		else if (w == WM_KEYUP)
			info = "key up";
		else if (w == WM_SYSKEYDOWN)
			info = "sys key dn";
		else if (w == WM_SYSKEYUP)
			info = "sys key up";
		//printf ("%s - vkCode [%04x], scanCode [%04x]\n", info, p->vkCode, p->scanCode);
		if(w==WM_KEYDOWN && p->vkCode==0x0020) //space bar
		{
			//toggle play/pause
			if(global_playflag) 
			{
				printf("pause\n");
				global_playflag=false;
			}
			else 
			{
				printf("play\n");
				global_playflag=true;
			}
		}
		if(w==WM_KEYDOWN && p->vkCode==0x0043  && global_active==false) //C key
		{
			PmError err = Pm_OpenInput(&global_pPmStreamMIDIIN, inputmidideviceid, NULL, 512, NULL, NULL);
			if (err) 
			{
				/*
				printf(Pm_GetErrorText(err));
				Pt_Stop();
				Terminate();
				*/
				printf("***********************\n");
				printf("midi input port is BUSY\n");
				printf("***********************\n");
			}
			else
			{
				Pm_SetFilter(global_pPmStreamMIDIIN, filter);
				inited = true; // now can document changes, set filter 
				global_active = true;
				printf("*************************\n");
				printf("midi input port CONNECTED\n");
				printf("*************************\n");
			}
		}
		if(w==WM_KEYDOWN && p->vkCode==0x0044 && global_active==true) //D key
		{
			//disconnect portmidi input
			global_active = false;
			Pm_Close(global_pPmStreamMIDIIN);
			printf("****************************\n");
			printf("midi input port DISCONNECTED\n");
			printf("****************************\n");
		}
		if(w==WM_KEYDOWN && p->vkCode==0x0045) //E key
		{
			//erase last note
			if(global_notenumberlist.size()>0)
			{
				printf("erase one note\n");
				global_notenumberlist.erase(global_notenumberlist.begin());
				global_listit=global_notenumberlist.begin();
			}
		}
		if(w==WM_KEYDOWN && p->vkCode==0x0058) //X key
		{
			//exit application
			Terminate();
			exit(0);
		}
	}

    // always call next hook
    return CallNextHookEx(g_kb_hook, code, w, l);
};

///////////////////////////////////////////////////////////////////////////////
//               main
// Effect: prompts for parameters, starts monitor
///////////////////////////////////////////////////////////////////////////////
/*
Instrument* global_pInstrument=NULL;
*/

int main(int argc, char **argv)
{
	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "begin.bat", "", NULL, nShowCmd);

	if(argc>1)
	{
		//input midi device
		//inputmididevice=atoi(argv[1]);
		inputmididevicename=argv[1];
	}
	if(argc>2)
	{
		//output midi device
		//outputmididevice = atoi(argv[2]);
		outputmididevicename = argv[2];
	}
	if(argc>3)
	{
		//output midi channel
		outputmidichannel = atoi(argv[3]);
	}
	if(argc>4)
	{
		//tempo
		global_tempo_bpm = atof(argv[4]);
	}
	if(argc>5)
	{
		//program
		global_programstring = argv[5];
	}
	if(outputmidichannel<0 || outputmidichannel>15)
	{
		cout << "invalid outputmidichannel, midi channel must range from 0 to 15 inclusively." << endl;
		Terminate();
	}
	if(global_programstring!="GOZILLA" && 
		global_programstring!="ULTRAMAN" &&
		global_programstring!="AVATAR" &&
		global_programstring!="THOR")
	{
		cout << "invalid global_program, global program must either be GOZILLA, ULTRAMAN, AVATAR or THOR." << endl;
		Terminate();
	}
	if(global_programstring=="GOZILLA")
	{
		global_programid=PROGRAM_GOZILLA;
	}
	else if(global_programstring=="ULTRAMAN")
	{
		global_programid=PROGRAM_ULTRAMAN;
	}
	else if(global_programstring=="AVATAR")
	{
		global_programid=PROGRAM_AVATAR;
	}
	else if(global_programstring=="THOR")
	{
		global_programid=PROGRAM_THOR;
	}
	else
	{
		assert(false);
		cout << "invalid global_program, global program must either be GOZILLA, ULTRAMAN, AVATAR or THOR." << endl;
		Terminate();
	}

	//2012june18, spi, begin
    //Auto-reset, initially non-signaled event 
    g_hTerminateEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //Add the break handler
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    g_kb_hook = SetWindowsHookEx(WH_KEYBOARD_LL, &kb_proc,
                                GetModuleHandle (NULL), // cannot be NULL, otherwise it will fail
                                0);
    if (g_kb_hook == NULL)
    {
        fprintf (stderr, "SetWindowsHookEx failed with error %d\n", ::GetLastError ());
        return 0;
    };


	//////////////////////////
	//initialize random number
	//////////////////////////
	srand((unsigned)time(0));


    PmError err;
	Pm_Initialize();

	/////////////////////////////
	//input midi device selection
	/////////////////////////////
	const PmDeviceInfo* deviceInfo;
    int numDevices = Pm_CountDevices();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pm_GetDeviceInfo( i );
		if (deviceInfo->input)
		{
			string devicenamestring = deviceInfo->name;
			global_inputmididevicemap.insert(pair<string,int>(devicenamestring,i));
		}
	}
	map<string,int>::iterator it;
	it = global_inputmididevicemap.find(inputmididevicename);
	if(it!=global_inputmididevicemap.end())
	{
		inputmidideviceid = (*it).second;
		printf("%s maps to %d\n", inputmididevicename.c_str(), inputmidideviceid);
		deviceInfo = Pm_GetDeviceInfo(inputmidideviceid);
	}
	else
	{
		assert(false);
		for(it=global_inputmididevicemap.begin(); it!=global_inputmididevicemap.end(); it++)
		{
			printf("%s maps to %d\n", (*it).first.c_str(), (*it).second);
		}
		printf("input midi device not found\n");
	}
	//////////////////////////////
	//output midi device selection
	//////////////////////////////
	//const PmDeviceInfo* deviceInfo;
    //int numDevices = Pm_CountDevices();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pm_GetDeviceInfo( i );
		if (deviceInfo->output)
		{
			string devicenamestring = deviceInfo->name;
			global_outputmididevicemap.insert(pair<string,int>(devicenamestring,i));
		}
	}
	//map<string,int>::iterator it;
	it = global_outputmididevicemap.find(outputmididevicename);
	if(it!=global_outputmididevicemap.end())
	{
		outputmidideviceid = (*it).second;
		printf("%s maps to %d\n", outputmididevicename.c_str(), outputmidideviceid);
		deviceInfo = Pm_GetDeviceInfo(outputmidideviceid);
	}
	else
	{
		assert(false);
		for(it=global_outputmididevicemap.begin(); it!=global_outputmididevicemap.end(); it++)
		{
			printf("%s maps to %d\n", (*it).first.c_str(), (*it).second);
		}
		printf("output midi device not found\n");
	}

    // list device information 
    cout << "MIDI output devices:" << endl;
    for (int i = 0; i < Pm_CountDevices(); i++) 
	{
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->output) printf("%d: %s, %s\n", i, info->interf, info->name);
    }
	cout << "device " << outputmidideviceid << " selected" << endl;
    //err = Pm_OpenInput(&midi_in, inp, NULL, 512, NULL, NULL);
    err = Pm_OpenOutput(&global_pPmStreamMIDIOUT, outputmidideviceid, NULL, 512, NULL, NULL, 0); //0 latency
    if (err) 
	{
        printf(Pm_GetErrorText(err));
        //Pt_Stop();
		Terminate();
        //mmexit(1);
    }


    // use porttime callback to empty midi queue and print 
    //Pt_Start(1, receive_poll, global_pInstrument); 
	Pt_Start(1, receive_poll, 0);
    // list device information 
    printf("MIDI input devices:\n");
    for (int i = 0; i < Pm_CountDevices(); i++) 
	{
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->input) printf("%d: %s, %s\n", i, info->interf, info->name);
    }
    //inputmididevice = get_number("Type input device number: ");
	printf("device %d selected\n", inputmidideviceid);
	showhelp();

    err = Pm_OpenInput(&global_pPmStreamMIDIIN, inputmidideviceid, NULL, 512, NULL, NULL);
    if (err) 
	{
		/*
        printf(Pm_GetErrorText(err));
        Pt_Stop();
		Terminate();
        //mmexit(1);
		*/
		//disconnect portmidi input
		global_active = false;
		printf("****************************\n");
		printf("midi input port DISCONNECTED\n");
		printf("****************************\n");

    }
	else
	{
		Pm_SetFilter(global_pPmStreamMIDIIN, filter);
		inited = true; // now can document changes, set filter 
		//printf("Midi Monitor ready.\n");
		global_active = true;
		printf("*************************\n");
		printf("midi input port CONNECTED\n");
		printf("*************************\n");
	}
    /*
	while (!done) 
	{
        char s[100];
        if (fgets(s, 100, stdin)) 
		{
            doascii(s[0]);
        }
    }
	*/
	/////////////////////////
	//midi message definition
	/////////////////////////
	PmEvent myPmEvent[2];
	//note on
	myPmEvent[0].timestamp = 0;
	myPmEvent[0].message = Pm_Message(0x90+outputmidichannel, 60, 100); //channel 0
	//myPmEvent[0].message = Pm_Message(0x91, 60, 100); //channel 1
	//note off
	myPmEvent[1].timestamp = 0;
	myPmEvent[1].message = Pm_Message(0x90+outputmidichannel, 60, 0); //channel 0
	//myPmEvent[1].message = Pm_Message(0x91, 60, 0); //channel 1

	PmEvent tempPmEvent;

	float timerelapse_s = 60.0f/global_tempo_bpm; //sec per beat
	UINT timerelapse_ms = (UINT)(timerelapse_s*1000);
	global_TimerId = SetTimer(NULL, 1, timerelapse_ms, NULL);
	if (!global_TimerId)
		return 16;
	MSG Msg;
	while (GetMessage(&Msg, NULL, 0, 0)) 
	{
		//++Counter;
		if (Msg.message == WM_TIMER && global_playflag)
		{
			if(global_programid==PROGRAM_GOZILLA || global_programid==PROGRAM_ULTRAMAN)
			{
				if(global_notenumbermap.size()>0)
				{
					int notenumber = global_mapit->first;
					assert(notenumber>-1 && notenumber<128);
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 0); //note off, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 100); //note on, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					global_mapit++;
					if(global_mapit==global_notenumbermap.end())
					{
						global_mapit=global_notenumbermap.begin();
					}
				}
			}
			else if(global_programid==PROGRAM_AVATAR)
			{
				if(global_notenumberlist.size()>0)
				{
					int notenumber = *global_listit;
					assert(notenumber>-1 && notenumber<128);
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 0); //note off, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 100); //note on, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					global_listit++;
					if(global_listit==global_notenumberlist.end())
					{
						global_listit=global_notenumberlist.begin();
					}
				}
			}
			else if(global_programid==PROGRAM_THOR)
			{
				if(global_notenumberlist.size()>0)
				{
					int notenumber = *global_listit;
					assert(notenumber>-1 && notenumber<128);
					if(global_prevnotenumber==-1) global_prevnotenumber = notenumber; 
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, global_prevnotenumber, 0); //note off, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 100); //note on, channel 0
					Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
					global_prevnotenumber = notenumber;
					global_listit++;
					if(global_listit==global_notenumberlist.end())
					{
						global_listit=global_notenumberlist.begin();
					}
				}
			}
		}
		//TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}


//exit:
	Terminate();
	return 0;
}

int Terminate()
{
	//kill timer
	if(global_TimerId) 
	{
		KillTimer(NULL, global_TimerId);
	}

	//send note off
	PmEvent tempPmEvent;
	if(global_programid==PROGRAM_THOR)
	{
		/*
		if(global_prevnotenumber!=-1)
		{
			assert(global_prevnotenumber>-1 && global_prevnotenumber<128);
			tempPmEvent.timestamp = 0;
			tempPmEvent.message = Pm_Message(0x90+outputmidichannel, global_prevnotenumber, 0); //note off, channel 0
			Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
		}
		*/
		if(global_notenumberlist.size()>0)
		{
			for(global_listit=global_notenumberlist.begin(); global_listit!=global_notenumberlist.end(); global_listit++)
			{
				int notenumber = *global_listit;
				assert(notenumber>-1&&notenumber<128);
				tempPmEvent.timestamp = 0;
				tempPmEvent.message = Pm_Message(0x90+outputmidichannel, notenumber, 0); //note off, channel 0
				Pm_Write(global_pPmStreamMIDIOUT, &tempPmEvent, 1);
			}
		}
	}

	//unhook keyboard
    UnhookWindowsHookEx(g_kb_hook);

	////////////////////
	//terminate portmidi
	////////////////////
    global_active = false;
    Pm_Close(global_pPmStreamMIDIIN);
    Pm_Close(global_pPmStreamMIDIOUT);
    Pt_Stop();
    Pm_Terminate();
    //mmexit(0);

	printf("Exiting!\n"); fflush(stdout);
	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "end.bat", "", NULL, nShowCmd);
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
//               doascii
// Inputs:
//    char c: input character
// Effect: interpret to revise flags
///////////////////////////////////////////////////////////////////////////////

private void doascii(char c)
{
    if (isupper(c)) c = tolower(c);
    if (c == 'q') done = true;
    else if (c == 'b') {
        bender = !bender;
        filter ^= PM_FILT_PITCHBEND;
        if (inited)
            printf("Pitch Bend, etc. %s\n", (bender ? "ON" : "OFF"));
    } else if (c == 'c') {
        controls = !controls;
        filter ^= PM_FILT_CONTROL;
        if (inited)
            printf("Control Change %s\n", (controls ? "ON" : "OFF"));
    } else if (c == 'h') {
        pgchanges = !pgchanges;
        filter ^= PM_FILT_PROGRAM;
        if (inited)
            printf("Program Changes %s\n", (pgchanges ? "ON" : "OFF"));
    } else if (c == 'n') {
        notes = !notes;
        filter ^= PM_FILT_NOTE;
        if (inited)
            printf("Notes %s\n", (notes ? "ON" : "OFF"));
    } else if (c == 'x') {
        excldata = !excldata;
        filter ^= PM_FILT_SYSEX;
        if (inited)
            printf("System Exclusive data %s\n", (excldata ? "ON" : "OFF"));
    } else if (c == 'r') {
        realdata = !realdata;
        filter ^= (PM_FILT_PLAY | PM_FILT_RESET | PM_FILT_TICK | PM_FILT_UNDEFINED);
        if (inited)
            printf("Real Time messages %s\n", (realdata ? "ON" : "OFF"));
    } else if (c == 'k') {
        clksencnt = !clksencnt;
        filter ^= PM_FILT_CLOCK;
        if (inited)
            printf("Clock and Active Sense Counting %s\n", (clksencnt ? "ON" : "OFF"));
        if (!clksencnt) clockcount = actsensecount = 0;
    } else if (c == 's') {
        if (clksencnt) {
            if (inited)
                printf("Clock Count %ld\nActive Sense Count %ld\n", 
                        (long) clockcount, (long) actsensecount);
        } else if (inited) {
            printf("Clock Counting not on\n");
        }
    } else if (c == 't') {
        notestotal+=notescount;
        if (inited)
            printf("This Note Count %ld\nTotal Note Count %ld\n",
                    (long) notescount, (long) notestotal);
        notescount=0;
    } else if (c == 'v') {
        verbose = !verbose;
        if (inited)
            printf("Verbose %s\n", (verbose ? "ON" : "OFF"));
    } else if (c == 'm') {
        chmode = !chmode;
        if (inited)
            printf("Channel Mode Messages %s", (chmode ? "ON" : "OFF"));
    } else {
        if (inited) {
            if (c == ' ') {
                PmEvent event;
                while (Pm_Read(global_pPmStreamMIDIIN, &event, 1)) ;	// flush midi input 
                printf("...FLUSHED MIDI INPUT\n\n");
            } else showhelp();
        }
    }
    if (inited) Pm_SetFilter(global_pPmStreamMIDIIN, filter);
}


/*
private void mmexit(int code)
{
    // if this is not being run from a console, maybe we should wait for
    // the user to read error messages before exiting
    //
    exit(code);
}
*/

//Called by the operating system in a separate thread to handle an app-terminating event. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT ||
        dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT)
    {
        // CTRL_C_EVENT - Ctrl+C was pressed 
        // CTRL_BREAK_EVENT - Ctrl+Break was pressed 
        // CTRL_CLOSE_EVENT - Console window was closed 
		Terminate();
        // Tell the main thread to exit the app 
        ::SetEvent(g_hTerminateEvent);
        return TRUE;
    }

    //Not an event handled by this function.
    //The only events that should be able to
	//reach this line of code are events that
    //should only be sent to services. 
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
//               output
// Inputs:
//    data: midi message buffer holding one command or 4 bytes of sysex msg
// Effect: format and print  midi data
///////////////////////////////////////////////////////////////////////////////

char vel_format[] = "    Vel %d\n";

private void output(PmMessage data)
{
    int command;    // the current command 
    int chan;   // the midi channel of the current event 
    int len;    // used to get constant field width 

    // printf("output data %8x; ", data); 

    command = Pm_MessageStatus(data) & MIDI_CODE_MASK;
    chan = Pm_MessageStatus(data) & MIDI_CHN_MASK;

    if (in_sysex || Pm_MessageStatus(data) == MIDI_SYSEX) {
#define sysex_max 16
        int i;
        PmMessage data_copy = data;
        in_sysex = true;
        // look for MIDI_EOX in first 3 bytes 
        // if realtime messages are embedded in sysex message, they will
        // be printed as if they are part of the sysex message
        //
        for (i = 0; (i < 4) && ((data_copy & 0xFF) != MIDI_EOX); i++) 
            data_copy >>= 8;
        if (i < 4) {
            in_sysex = false;
            i++; // include the EOX byte in output 
        }
        showbytes(data, i, verbose);
        if (verbose) printf("System Exclusive\n");
    } else if (command == MIDI_ON_NOTE && Pm_MessageData2(data) != 0) {
        notescount++;
        if (notes) {
            showbytes(data, 3, verbose);
            if (verbose) {
                printf("NoteOn  Chan %2d Key %3d ", chan, Pm_MessageData1(data));
                len = put_pitch(Pm_MessageData1(data));
                printf(vel_format + len, Pm_MessageData2(data));
            }
        }
    } else if ((command == MIDI_ON_NOTE // && Pm_MessageData2(data) == 0
                || command == MIDI_OFF_NOTE) && notes) {
        showbytes(data, 3, verbose);
        if (verbose) {
            printf("NoteOff Chan %2d Key %3d ", chan, Pm_MessageData1(data));
            len = put_pitch(Pm_MessageData1(data));
            printf(vel_format + len, Pm_MessageData2(data));
        }
    } else if (command == MIDI_CH_PROGRAM && pgchanges) {
        showbytes(data, 2, verbose);
        if (verbose) {
            printf("  ProgChg Chan %2d Prog %2d\n", chan, Pm_MessageData1(data) + 1);
        }
    } else if (command == MIDI_CTRL) {
               // controls 121 (MIDI_RESET_CONTROLLER) to 127 are channel
               // mode messages. 
        if (Pm_MessageData1(data) < MIDI_ALL_SOUND_OFF) {
            showbytes(data, 3, verbose);
            if (verbose) {
                printf("CtrlChg Chan %2d Ctrl %2d Val %2d\n",
                       chan, Pm_MessageData1(data), Pm_MessageData2(data));
            }
        } else if (chmode) { // channel mode 
            showbytes(data, 3, verbose);
            if (verbose) {
                switch (Pm_MessageData1(data)) {
                  case MIDI_ALL_SOUND_OFF:
                      printf("All Sound Off, Chan %2d\n", chan);
                    break;
                  case MIDI_RESET_CONTROLLERS:
                    printf("Reset All Controllers, Chan %2d\n", chan);
                    break;
                  case MIDI_LOCAL:
                    printf("LocCtrl Chan %2d %s\n",
                            chan, Pm_MessageData2(data) ? "On" : "Off");
                    break;
                  case MIDI_ALL_OFF:
                    printf("All Off Chan %2d\n", chan);
                    break;
                  case MIDI_OMNI_OFF:
                    printf("OmniOff Chan %2d\n", chan);
                    break;
                  case MIDI_OMNI_ON:
                    printf("Omni On Chan %2d\n", chan);
                    break;
                  case MIDI_MONO_ON:
                    printf("Mono On Chan %2d\n", chan);
                    if (Pm_MessageData2(data))
                        printf(" to %d received channels\n", Pm_MessageData2(data));
                    else
                        printf(" to all received channels\n");
                    break;
                  case MIDI_POLY_ON:
                    printf("Poly On Chan %2d\n", chan);
                    break;
                }
            }
        }
    } else if (command == MIDI_POLY_TOUCH && bender) {
        showbytes(data, 3, verbose);
        if (verbose) {
            printf("P.Touch Chan %2d Key %2d ", chan, Pm_MessageData1(data));
            len = put_pitch(Pm_MessageData1(data));
            printf(val_format + len, Pm_MessageData2(data));
        }
    } else if (command == MIDI_TOUCH && bender) {
        showbytes(data, 2, verbose);
        if (verbose) {
            printf("  A.Touch Chan %2d Val %2d\n", chan, Pm_MessageData1(data));
        }
    } else if (command == MIDI_BEND && bender) {
        showbytes(data, 3, verbose);
        if (verbose) {
            printf("P.Bend  Chan %2d Val %2d\n", chan,
                    (Pm_MessageData1(data) + (Pm_MessageData2(data)<<7)));
        }
    } else if (Pm_MessageStatus(data) == MIDI_SONG_POINTER) {
        showbytes(data, 3, verbose);
        if (verbose) {
            printf("    Song Position %d\n",
                    (Pm_MessageData1(data) + (Pm_MessageData2(data)<<7)));
        }
    } else if (Pm_MessageStatus(data) == MIDI_SONG_SELECT) {
        showbytes(data, 2, verbose);
        if (verbose) {
            printf("    Song Select %d\n", Pm_MessageData1(data));
        }
    } else if (Pm_MessageStatus(data) == MIDI_TUNE_REQ) {
        showbytes(data, 1, verbose);
        if (verbose) {
            printf("    Tune Request\n");
        }
    } else if (Pm_MessageStatus(data) == MIDI_Q_FRAME && realdata) {
        showbytes(data, 2, verbose);
        if (verbose) {
            printf("    Time Code Quarter Frame Type %d Values %d\n",
                    (Pm_MessageData1(data) & 0x70) >> 4, Pm_MessageData1(data) & 0xf);
        }
    } else if (Pm_MessageStatus(data) == MIDI_START && realdata) {
        showbytes(data, 1, verbose);
        if (verbose) {
            printf("    Start\n");
        }
    } else if (Pm_MessageStatus(data) == MIDI_CONTINUE && realdata) {
        showbytes(data, 1, verbose);
        if (verbose) {
            printf("    Continue\n");
        }
    } else if (Pm_MessageStatus(data) == MIDI_STOP && realdata) {
        showbytes(data, 1, verbose);
        if (verbose) {
            printf("    Stop\n");
        }
    } else if (Pm_MessageStatus(data) == MIDI_SYS_RESET && realdata) {
        showbytes(data, 1, verbose);
        if (verbose) {
            printf("    System Reset\n");
        }
    } else if (Pm_MessageStatus(data) == MIDI_TIME_CLOCK) {
        if (clksencnt) clockcount++;
        else if (realdata) {
            showbytes(data, 1, verbose);
            if (verbose) {
                printf("    Clock\n");
            }
        }
    } else if (Pm_MessageStatus(data) == MIDI_ACTIVE_SENSING) {
        if (clksencnt) actsensecount++;
        else if (realdata) {
            showbytes(data, 1, verbose);
            if (verbose) {
                printf("    Active Sensing\n");
            }
        }
    } else showbytes(data, 3, verbose);
    fflush(stdout);
}


/////////////////////////////////////////////////////////////////////////////
//               put_pitch
// Inputs:
//    int p: pitch number
// Effect: write out the pitch name for a given number
/////////////////////////////////////////////////////////////////////////////

private int put_pitch(int p)
{
    char result[8];
    static char *ptos[] = {
        "c", "cs", "d", "ef", "e", "f", "fs", "g",
        "gs", "a", "bf", "b"    };
    // note octave correction below 
    sprintf(result, "%s%d", ptos[p % 12], (p / 12) - 1);
    printf(result);
    return strlen(result);
}


/////////////////////////////////////////////////////////////////////////////
//               showbytes
// Effect: print hex data, precede with newline if asked
/////////////////////////////////////////////////////////////////////////////

char nib_to_hex[] = "0123456789ABCDEF";

private void showbytes(PmMessage data, int len, boolean newline)
{
    int count = 0;
    int i;

//    if (newline) {
//        putchar('\n');
//        count++;
//    } 
    for (i = 0; i < len; i++) {
        putchar(nib_to_hex[(data >> 4) & 0xF]);
        putchar(nib_to_hex[data & 0xF]);
        count += 2;
        if (count > 72) {
            putchar('.');
            putchar('.');
            putchar('.');
            break;
        }
        data >>= 8;
    }
    putchar(' ');
}



/////////////////////////////////////////////////////////////////////////////
//               showhelp
// Effect: print help text
/////////////////////////////////////////////////////////////////////////////

private void showhelp()
{
    printf("\n");
    printf("   Item Reported  Range     Item Reported  Range\n");
    printf("   -------------  -----     -------------  -----\n");
    printf("   Channels       1 - 16    Programs       1 - 128\n");
    printf("   Controllers    0 - 127   After Touch    0 - 127\n");
    printf("   Loudness       0 - 127   Pitch Bend     0 - 16383, center = 8192\n");
    printf("   Pitches        0 - 127, 60 = c4 = middle C\n");
    printf(" \n");
    printf("n toggles notes");
    showstatus(notes);
    printf("t displays noteon count since last t\n");
    printf("b toggles pitch bend, aftertouch");
    showstatus(bender);
    printf("c toggles continuous control");
    showstatus(controls);
    printf("h toggles program changes");
    showstatus(pgchanges);
    printf("x toggles system exclusive");
    showstatus(excldata);
    printf("k toggles clock and sense counting only");
    showstatus(clksencnt);
    printf("r toggles other real time messages & SMPTE");
    showstatus(realdata);
    printf("s displays clock and sense count since last k\n");
    printf("m toggles channel mode messages");
    showstatus(chmode);
    printf("v toggles verbose text");
    showstatus(verbose);
    printf("q quits\n");
    printf("\n");
}

/////////////////////////////////////////////////////////////////////////////
//               showstatus
// Effect: print status of flag
/////////////////////////////////////////////////////////////////////////////

private void showstatus(boolean flag)
{
    printf(", now %s\n", flag ? "ON" : "OFF" );
}
