/*
 *      Copyright (C) 2010 Team XBMC
 *      http://www.xbmc.org
 *      Modified version of DShow Native wrapper from Gianluigi Tiesi <sherpya@netfarm.it>
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "stdafx.h"
#include "dsnerror.h"
extern "C" const char * WINAPI DSStrError(dsnerror_t error)
{
    switch (error)
    {
        case DSN_OK                  : return "Success";
        case DSN_LOADLIBRARY         : return "LoadLibrary Failed";
        case DSN_INPUT_NOTACCEPTED   : return "Input not accepted";
        case DSN_INPUT_CONNFAILED    : return "Connection to input pin failed";
        case DSN_OUTPUT_NOTACCEPTED  : return "Output not accepted";
        case DSN_OUTPUT_NOTSUPPORTED : return "Output not supported";
        case DSN_OUTPUT_CONNFAILED   : return "Connection to output pin failed";
        case DSN_FAIL_ALLOCATOR      : return "Error with Allocator";
        case DSN_FAIL_GRAPH          : return "Error building Graph";
        case DNS_FAIL_FILTER         : return "Error building Filter";
        case DSN_FAIL_DECODESAMPLE   : return "Error decoding sample";
        case DSN_FAIL_RECEIVE        : return "Error receiving sample from codec";
        case DSN_FAIL_ENUM           : return "Codec Enum Pins failed";
    }
    return "Unknown Error";
}
