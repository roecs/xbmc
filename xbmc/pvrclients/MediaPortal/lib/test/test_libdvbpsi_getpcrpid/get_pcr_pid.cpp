/*****************************************************************************
 * get_pcr_pid.c: stdout the PID of the PCR of a given program
 *****************************************************************************
 * Copyright (C) 2009-2010 VideoLAN
 * $Id: pcread.c 15 2006-06-15 22:17:58Z cmassiot $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *****************************************************************************/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
//#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>

#if defined(HAVE_INTTYPES_H)
#include "inttypes.h"
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

/* The libdvbpsi distribution defines DVBPSI_DIST */
#include "dvbpsi.h"
#include "descriptor.h"
#include "demux.h"
#include "tables/pat.h"
#include "tables/pmt.h"
#include "psi.h"

/*****************************************************************************
 * Local declarations
 *****************************************************************************/
#define READ_ONCE 100
#define MAX_PROGRAMS 25
#define TS_SIZE 188

#ifdef _MSC_VER
#include <Windows.h>
HANDLE i_fd = 0;
#else
static int i_fd = -1;
#endif

static int i_ts_read = 0;
static dvbpsi_handle p_pat_dvbpsi_fd;
static int i_nb_programs = 0;
static uint16_t i_program = 0;
static uint16_t pi_pmt_pids[MAX_PROGRAMS];
static dvbpsi_handle p_pmt_dvbpsi_fds[MAX_PROGRAMS];

/*****************************************************************************
 * PMTCallback
 *****************************************************************************/
static void PMTCallback( void *_unused, dvbpsi_pmt_t *p_pmt )
{
    if ( p_pmt->i_program_number != i_program )
    {
        dvbpsi_DeletePMT( p_pmt );
        return;
    }

    printf( "%u\n", p_pmt->i_pcr_pid );
#ifdef _MSC_VER
    CloseHandle( i_fd );
#else
    close( i_fd );
#endif
    exit(EXIT_SUCCESS);
}

/*****************************************************************************
 * PATCallback
 *****************************************************************************/
static void PATCallback( void *_unused, dvbpsi_pat_t *p_pat )
{
    dvbpsi_pat_program_t *p_program;

    for( p_program = p_pat->p_first_program; p_program != NULL;
         p_program = p_program->p_next )
    {
        if( p_program->i_number != 0
             && (!i_program || i_program == p_program->i_number) )
        {
            pi_pmt_pids[i_nb_programs] = p_program->i_pid;
            p_pmt_dvbpsi_fds[i_nb_programs] =
                        dvbpsi_AttachPMT( p_program->i_number, PMTCallback,
                                          NULL );
            i_nb_programs++;
        }
    }

    dvbpsi_DeletePAT( p_pat );
}

/*****************************************************************************
 * TSHandle: find and decode PSI
 *****************************************************************************/
static inline uint16_t ts_GetPID( const uint8_t *p_ts )
{
    return (((uint16_t)p_ts[1] & 0x1f) << 8) | p_ts[2];
}

static inline int ts_CheckSync( const uint8_t *p_ts )
{
    return p_ts[0] == 0x47;
}

static void TSHandle( uint8_t *p_ts )
{
    uint16_t i_pid = ts_GetPID( p_ts );
    int i;

    if ( !ts_CheckSync( p_ts ) )
    {
        fprintf( stderr, "lost TS synchro, go and fix your file (pos=%ju)\n",
                 (uintmax_t)i_ts_read * TS_SIZE );
        exit(EXIT_FAILURE);
    }

    if ( i_pid == 0 && !i_nb_programs )
    {
        dvbpsi_PushPacket( p_pat_dvbpsi_fd, p_ts );
    }

    for ( i = 0; i < i_nb_programs; i++ )
    {
        if ( pi_pmt_pids[i] == i_pid )
            dvbpsi_PushPacket( p_pmt_dvbpsi_fds[i], p_ts );
    }
}

/*****************************************************************************
 * Entry point
 *****************************************************************************/
int main( int i_argc, char **pp_argv )
{
    uint8_t *p_buffer;

    if ( i_argc < 2 || i_argc > 3 || !strcmp( pp_argv[1], "-" ) )
    {
        fprintf( stderr, "Usage: get_pcr_pid <input ts> [<program number>]\n" );
        exit(EXIT_FAILURE);
    }

#ifdef _MSC_VER
    if (( i_fd = ::CreateFile(pp_argv[1],      // The filename
             (DWORD) GENERIC_READ,             // File access
             (DWORD) FILE_SHARE_READ,          // Share access
             NULL,                             // Security
             (DWORD) OPEN_EXISTING,            // Open flags
             (DWORD) 0,                        // More flags
             NULL)) == INVALID_HANDLE_VALUE)

#else
    if ( (i_fd = fopen( pp_argv[1], "r" )) == NULL )
#endif
    {
        fprintf( stderr, "open(%s) failed (%s)\n", pp_argv[1],
                 strerror(errno) );
        exit(EXIT_FAILURE);
    }

    if ( i_argc == 3 )
        i_program = strtol( pp_argv[2], NULL, 0 );

    p_pat_dvbpsi_fd = dvbpsi_AttachPAT( PATCallback, NULL );

    p_buffer = (uint8_t*) malloc( TS_SIZE * READ_ONCE );

    for ( ; ; )
    {
        int i;

#ifdef _MSC_VER
        DWORD i_ret;
        if(ReadFile(i_fd, p_buffer, (TS_SIZE * READ_ONCE), &i_ret, NULL) == FALSE)
#else
        ssize_t i_ret;
        if ( (i_ret = read(p_buffer, sizeof(uint8_t), (TS_SIZE * READ_ONCE),  i_fd )) < 0 )
#endif
        {
            fprintf( stderr, "read error (%s)\n", strerror(errno) );
            break;
        }
        if ( i_ret == 0 )
        {
            fprintf( stderr, "end of file reached\n" );
            break;
        }
        for ( i = 0; i < i_ret / TS_SIZE; i++ )
        {
            TSHandle( p_buffer + TS_SIZE * i );
            i_ts_read++;
        }
    }

#ifdef _MSC_VER
    CloseHandle (i_fd);
#else
    close( i_fd );
#endif
    fprintf( stderr, "no PAT/PMT found\n" );

    return EXIT_FAILURE;
}

