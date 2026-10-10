//  Description:
//
//		midi_out.c -- minimal cross-platform MIDI output.
//
//		The file is split into one backend per platform, each providing the
//		same five static hooks (be_init, be_shutdown, be_scan, be_open,
//		be_close, be_send). The public API at the bottom is shared, so the
//		port list and the open/closed state are handled in one place.
//
//		Only the Windows backend is built and tested on the author's machine;
//		the ALSA and CoreMIDI backends follow the documented APIs.
//

//------------------------------------------------------------------------------
//------------------------------- Includes -------------------------------------
//------------------------------------------------------------------------------

#include "midi_out.h"

#include <stdio.h>
#include <string.h>

#if defined( _WIN32 )
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <mmsystem.h>
#elif defined( __APPLE__ )
	#include <CoreMIDI/CoreMIDI.h>
#else
	#include <alsa/asoundlib.h>
#endif

//------------------------------------------------------------------------------
//---------------------- Local types and structures ----------------------------
//------------------------------------------------------------------------------

typedef struct
{
	char name[MO_MAX_NAME];
	int  id_a;  // backend specific: device / client / destination index
	int  id_b;  // backend specific: ALSA port number, unused elsewhere
} mo_port;

//------------------------------------------------------------------------------
//---------------------------- Local variables ---------------------------------
//------------------------------------------------------------------------------

static mo_port mo_ports[MO_MAX_PORTS];
static int     mo_nports;
static int     mo_open_index = -1;
static int     mo_started;

#if defined( _WIN32 )
static HMIDIOUT mo_handle;
#elif defined( __APPLE__ )
static MIDIClientRef   mo_client;
static MIDIPortRef     mo_outport;
static MIDIEndpointRef mo_dest;
#else
static snd_seq_t         *mo_seq;
static snd_midi_event_t  *mo_enc;
static int                mo_myport = -1;
#endif

//------------------------------------------------------------------------------
//----------------------- Local function prototypes ----------------------------
//------------------------------------------------------------------------------

static int  be_init( void );
static void be_shutdown( void );
static void be_scan( void );
static int  be_open( const mo_port *p );
static void be_close( const mo_port *p );
static int  be_send( const uint8_t *msg, int len );
static void add_port( const char *name, int id_a, int id_b );

//------------------------------------------------------------------------------
//---------------------------- Local functions ---------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Append a port to the scan result, silently dropping overflow.
  *
  * @param  name Display name.
  * @param  id_a Backend specific first id.
  * @param  id_b Backend specific second id.
  *
  * @retval None.
  */
static void add_port( const char *name, int id_a, int id_b )
{
	mo_port *p;

	if ( mo_nports >= MO_MAX_PORTS )
	{
		return;
	}
	p = &mo_ports[mo_nports++];
	snprintf( p->name, sizeof p->name, "%s", name );
	p->id_a = id_a;
	p->id_b = id_b;
}

#if defined( _WIN32 )

//------------------------------------------------------------------------------
//--------------------------- Windows (WinMM) ----------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  WinMM needs no global setup.
  *
  * @retval 0 always.
  */
static int be_init( void ) { return 0; }

/**
  * @brief  WinMM needs no global teardown.
  *
  * @retval None.
  */
static void be_shutdown( void ) { }

/**
  * @brief  List the midiOut devices.
  *
  * @retval None.
  */
static void be_scan( void )
{
	MIDIOUTCAPSA caps;
	UINT i, n;

	n = midiOutGetNumDevs();
	for ( i = 0; i < n; i++ )
	{
		if ( midiOutGetDevCapsA( i, &caps, sizeof caps ) == MMSYSERR_NOERROR )
		{
			add_port( caps.szPname, (int)i, 0 );
		}
	}
}

/**
  * @brief  Open a midiOut device.
  *
  * Fails if another program holds a driver that only allows one client,
  * which is common for older USB interfaces.
  *
  * @param  p The port to open.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_open( const mo_port *p )
{
	if ( midiOutOpen( &mo_handle, (UINT)p->id_a, 0, 0, CALLBACK_NULL )
	     != MMSYSERR_NOERROR )
	{
		mo_handle = NULL;
		return -1;
	}
	return 0;
}

/**
  * @brief  Close the midiOut device.
  *
  * @param  p The open port.
  *
  * @retval None.
  */
static void be_close( const mo_port *p )
{
	(void)p;
	if ( mo_handle )
	{
		midiOutReset( mo_handle );  // turns off anything still sounding
		midiOutClose( mo_handle );
		mo_handle = NULL;
	}
}

/**
  * @brief  Send a short message.
  *
  * @param  msg Message bytes.
  * @param  len 1 to 3.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_send( const uint8_t *msg, int len )
{
	DWORD w;

	w = msg[0];
	if ( len > 1 )
	{
		w |= (DWORD)msg[1] << 8;
	}
	if ( len > 2 )
	{
		w |= (DWORD)msg[2] << 16;
	}
	return midiOutShortMsg( mo_handle, w ) == MMSYSERR_NOERROR ? 0 : -1;
}

#elif defined( __APPLE__ )

//------------------------------------------------------------------------------
//--------------------------- macOS (CoreMIDI) ---------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Create the CoreMIDI client and output port.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_init( void )
{
	if ( MIDIClientCreate( CFSTR( "PC MIDI Keyboard" ), NULL, NULL,
	                       &mo_client ) != noErr )
	{
		return -1;
	}
	if ( MIDIOutputPortCreate( mo_client, CFSTR( "out" ), &mo_outport )
	     != noErr )
	{
		return -1;
	}
	return 0;
}

/**
  * @brief  Dispose of the CoreMIDI client, which also disposes its ports.
  *
  * @retval None.
  */
static void be_shutdown( void )
{
	if ( mo_client )
	{
		MIDIClientDispose( mo_client );
		mo_client = 0;
	}
}

/**
  * @brief  List the MIDI destinations.
  *
  * @retval None.
  */
static void be_scan( void )
{
	char buf[MO_MAX_NAME];
	CFStringRef s;
	ItemCount i, n;

	n = MIDIGetNumberOfDestinations();
	for ( i = 0; i < n; i++ )
	{
		s = NULL;
		strcpy( buf, "?" );
		if ( MIDIObjectGetStringProperty( MIDIGetDestination( i ),
		                                  kMIDIPropertyDisplayName,
		                                  &s ) == noErr && s )
		{
			CFStringGetCString( s, buf, sizeof buf, kCFStringEncodingUTF8 );
			CFRelease( s );
		}
		add_port( buf, (int)i, 0 );
	}
}

/**
  * @brief  Select a destination; CoreMIDI has nothing to open.
  *
  * @param  p The port to open.
  *
  * @retval 0 on success, -1 the destination has gone.
  */
static int be_open( const mo_port *p )
{
	mo_dest = MIDIGetDestination( (ItemCount)p->id_a );
	return mo_dest ? 0 : -1;
}

/**
  * @brief  Forget the destination.
  *
  * @param  p The open port.
  *
  * @retval None.
  */
static void be_close( const mo_port *p )
{
	(void)p;
	mo_dest = 0;
}

/**
  * @brief  Send a short message as a one-packet list, timestamped "now".
  *
  * @param  msg Message bytes.
  * @param  len 1 to 3.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_send( const uint8_t *msg, int len )
{
	// The union gives the list its natural alignment; a plain Byte array
	// cast to MIDIPacketList * may be misaligned.
	union
	{
		MIDIPacketList pl;
		Byte           raw[128];
	} buf;
	MIDIPacketList *pl = &buf.pl;
	MIDIPacket *pk;

	pk = MIDIPacketListInit( pl );
	pk = MIDIPacketListAdd( pl, sizeof buf, pk, 0, (ByteCount)len, msg );
	if ( !pk )
	{
		return -1;
	}
	return MIDISend( mo_outport, mo_dest, pl ) == noErr ? 0 : -1;
}

#else

//------------------------------------------------------------------------------
//------------------------- Linux (ALSA sequencer) -----------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Open the sequencer and create our own readable port.
  *
  * The port is subscribable, so other programs (or aconnect) can also
  * connect to us without choosing a port here.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_init( void )
{
	if ( snd_seq_open( &mo_seq, "default", SND_SEQ_OPEN_OUTPUT, 0 ) < 0 )
	{
		mo_seq = NULL;
		return -1;
	}
	snd_seq_set_client_name( mo_seq, "PC MIDI Keyboard" );
	mo_myport = snd_seq_create_simple_port( mo_seq, "out",
	                SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
	                SND_SEQ_PORT_TYPE_MIDI_GENERIC |
	                SND_SEQ_PORT_TYPE_APPLICATION );
	if ( mo_myport < 0 || snd_midi_event_new( 16, &mo_enc ) < 0 )
	{
		return -1;
	}
	return 0;
}

/**
  * @brief  Release the encoder and close the sequencer.
  *
  * @retval None.
  */
static void be_shutdown( void )
{
	if ( mo_enc )
	{
		snd_midi_event_free( mo_enc );
		mo_enc = NULL;
	}
	if ( mo_seq )
	{
		snd_seq_close( mo_seq );
		mo_seq = NULL;
	}
}

/**
  * @brief  List every writable, subscribable port except our own.
  *
  * @retval None.
  */
static void be_scan( void )
{
	snd_seq_client_info_t *ci;
	snd_seq_port_info_t *pi;
	char buf[MO_MAX_NAME];
	unsigned int need = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
	unsigned int caps;
	int client;

	if ( !mo_seq )
	{
		return;
	}
	snd_seq_client_info_alloca( &ci );
	snd_seq_port_info_alloca( &pi );
	snd_seq_client_info_set_client( ci, -1 );
	while ( snd_seq_query_next_client( mo_seq, ci ) >= 0 )
	{
		client = snd_seq_client_info_get_client( ci );
		if ( client == snd_seq_client_id( mo_seq ) || client == 0 )
		{
			continue;  // 0 is the System client (timer, announce)
		}
		snd_seq_port_info_set_client( pi, client );
		snd_seq_port_info_set_port( pi, -1 );
		while ( snd_seq_query_next_port( mo_seq, pi ) >= 0 )
		{
			caps = snd_seq_port_info_get_capability( pi );
			if ( ( caps & need ) != need ||
			     ( caps & SND_SEQ_PORT_CAP_NO_EXPORT ) )
			{
				continue;
			}
			snprintf( buf, sizeof buf, "%d:%d %s", client,
			          snd_seq_port_info_get_port( pi ),
			          snd_seq_port_info_get_name( pi ) );
			add_port( buf, client, snd_seq_port_info_get_port( pi ) );
		}
	}
}

/**
  * @brief  Connect our port to the chosen destination.
  *
  * @param  p The port to open.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_open( const mo_port *p )
{
	if ( !mo_seq )
	{
		return -1;
	}
	return snd_seq_connect_to( mo_seq, mo_myport, p->id_a, p->id_b ) < 0
	       ? -1 : 0;
}

/**
  * @brief  Disconnect from the destination.
  *
  * @param  p The open port.
  *
  * @retval None.
  */
static void be_close( const mo_port *p )
{
	if ( mo_seq )
	{
		snd_seq_disconnect_to( mo_seq, mo_myport, p->id_a, p->id_b );
	}
}

/**
  * @brief  Encode the raw bytes as a sequencer event and send it directly.
  *
  * @param  msg Message bytes.
  * @param  len 1 to 3.
  *
  * @retval 0 on success, -1 on failure.
  */
static int be_send( const uint8_t *msg, int len )
{
	snd_seq_event_t ev;

	snd_seq_ev_clear( &ev );
	snd_midi_event_reset_encode( mo_enc );
	if ( snd_midi_event_encode( mo_enc, msg, len, &ev ) < len ||
	     ev.type == SND_SEQ_EVENT_NONE )
	{
		return -1;
	}
	snd_seq_ev_set_source( &ev, mo_myport );
	snd_seq_ev_set_subs( &ev );  // to everyone connected, not only our pick
	snd_seq_ev_set_direct( &ev );
	return snd_seq_event_output_direct( mo_seq, &ev ) < 0 ? -1 : 0;
}

#endif

//------------------------------------------------------------------------------
//------------------------------ Public API ------------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Start the MIDI backend and scan the available output ports.
  *
  * @retval 0 on success, -1 the backend could not be started.
  */
int mo_init( void )
{
	if ( be_init() != 0 )
	{
		return -1;
	}
	mo_started = 1;
	mo_refresh();
	return 0;
}

/**
  * @brief  Close any open port and release the backend.
  *
  * @retval None.
  */
void mo_shutdown( void )
{
	mo_close();
	be_shutdown();
	mo_started = 0;
	mo_nports = 0;
}

/**
  * @brief  Rescan the output ports.
  *
  * @retval Number of ports found.
  */
int mo_refresh( void )
{
	mo_close();
	mo_nports = 0;
	if ( mo_started )
	{
		be_scan();
	}
	return mo_nports;
}

/**
  * @brief  Get the number of ports found by the last scan.
  *
  * @retval Number of ports.
  */
int mo_port_count( void ) { return mo_nports; }

/**
  * @brief  Get the display name of a port.
  *
  * @param  index Port index.
  *
  * @retval The name, or "" for a bad index.
  */
const char *mo_port_name( int index )
{
	if ( index < 0 || index >= mo_nports )
	{
		return "";
	}
	return mo_ports[index].name;
}

/**
  * @brief  Open an output port, closing the previous one.
  *
  * @param  index Port index.
  *
  * @retval 0 on success, -1 bad index, -2 the OS refused to open it.
  */
int mo_open( int index )
{
	if ( index < 0 || index >= mo_nports )
	{
		return -1;
	}
	mo_close();
	if ( be_open( &mo_ports[index] ) != 0 )
	{
		return -2;
	}
	mo_open_index = index;
	return 0;
}

/**
  * @brief  Close the open port, if any.
  *
  * @retval None.
  */
void mo_close( void )
{
	if ( mo_open_index >= 0 )
	{
		be_close( &mo_ports[mo_open_index] );
		mo_open_index = -1;
	}
}

/**
  * @brief  Get the index of the open port.
  *
  * @retval The index, or -1 if no port is open.
  */
int mo_current( void ) { return mo_open_index; }

/**
  * @brief  Send one short MIDI message.
  *
  * @param  msg The message bytes.
  * @param  len Number of bytes, 1 to 3.
  *
  * @retval 0 on success, -1 no port open, -2 bad arguments, -3 send failed.
  */
int mo_send( const uint8_t *msg, int len )
{
	if ( mo_open_index < 0 )
	{
		return -1;
	}
	if ( !msg || len < 1 || len > 3 || !( msg[0] & 0x80 ) )
	{
		return -2;
	}
	return be_send( msg, len ) == 0 ? 0 : -3;
}
