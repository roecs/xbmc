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

#ifndef DSNERROR_H
#define DSNERROR_H

typedef enum
{
    DSN_OK = 0,
    DSN_LOADLIBRARY,
    DSN_INPUT_NOTACCEPTED,
    DSN_INPUT_CONNFAILED,
    DSN_OUTPUT_NOTACCEPTED,
    DSN_OUTPUT_NOTSUPPORTED,
    DSN_OUTPUT_CONNFAILED,
    DSN_FAIL_ALLOCATOR,
    DSN_FAIL_GRAPH,
    DNS_FAIL_FILTER,
    DSN_FAIL_DECODESAMPLE,
    DSN_FAIL_RECEIVE,
    DSN_FAIL_ENUM,
    DSN_SUCCEEDED_BUT_NOSURFACE,
    DSN_DATA_QUEUE_FULL,
    DSN_MAX
} dsnerror_t;

#define DSN_CHECK(expr, err) do { if ((m_res = (expr)) != S_OK) return err; } while (0)
#endif