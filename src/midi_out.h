//  Description:
//
//		midi_out.h -- minimal cross-platform MIDI output.
//
//		One output port open at a time, short messages only. Each platform
//		uses the API that ships with the OS so the app needs no MIDI library:
//
//		  platform  backend        what a "port" is
//		  Windows   WinMM          a midiOut device (synth, USB interface,
//		                           loopMIDI port, ...)
//		  Linux     ALSA sequencer a writable sequencer port; our own port
//		                           is also visible for others to connect to
//		  macOS     CoreMIDI       a MIDI destination endpoint
//
//		Port indices are only valid until the next mo_refresh().
//

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <stdint.h>

#define MO_MAX_PORTS     64
#define MO_MAX_NAME      128

/**
  * @brief  Start the MIDI backend and scan the available output ports.
  *
  * @retval 0 on success, -1 the backend could not be started.
  */
int mo_init( void );

/**
  * @brief  Close any open port and release the backend.
  *
  * @retval None.
  */
void mo_shutdown( void );

/**
  * @brief  Rescan the output ports.
  *
  * Closes the open port, because its index may no longer be valid.
  *
  * @retval Number of ports found.
  */
int mo_refresh( void );

/**
  * @brief  Get the number of ports found by the last scan.
  *
  * @retval Number of ports.
  */
int mo_port_count( void );

/**
  * @brief  Get the display name of a port.
  *
  * @param  index Port index from 0 to mo_port_count() - 1.
  *
  * @retval The name, or "" for a bad index.
  */
const char *mo_port_name( int index );

/**
  * @brief  Open an output port, closing the previous one.
  *
  * @param  index Port index from 0 to mo_port_count() - 1.
  *
  * @retval 0 on success, -1 bad index, -2 the OS refused to open it.
  */
int mo_open( int index );

/**
  * @brief  Close the open port, if any.
  *
  * @retval None.
  */
void mo_close( void );

/**
  * @brief  Get the index of the open port.
  *
  * @retval The index, or -1 if no port is open.
  */
int mo_current( void );

/**
  * @brief  Send one short MIDI message (1 to 3 bytes, with status byte).
  *
  * @param  msg The message bytes.
  * @param  len Number of bytes, 1 to 3.
  *
  * @retval 0 on success, -1 no port open, -2 bad arguments, -3 send failed.
  */
int mo_send( const uint8_t *msg, int len );

#endif
