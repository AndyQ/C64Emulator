/*
 * rawnetarch.c - raw ethernet interface,
 *                 architecture-dependant stuff
 *
 * Written by
 *  Spiro Trikaliotis <Spiro.Trikaliotis@gmx.de>
 * 
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#ifdef HAVE_TFE 

/* if we have a pcap version with either pcap_sendpacket or pcap_inject, do not use libnet anymore! */
#if defined(HAVE_PCAP_SENDPACKET) || defined(HAVE_PCAP_INJECT)
 #undef HAVE_LIBNET
#endif

#include "pcap.h"

#ifdef HAVE_LIBNET
#include "libnet.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "log.h"
#include "rawnetarch.h"

/*
    FIXME: rename all remaining tfe_ stuff to rawnet_
*/

#define RAWNET_DEBUG_WARN 1 /* this should not be deactivated */

/** #define RAWNET_DEBUG_ARCH 1 **/
/** #define RAWNET_DEBUG_PKTDUMP 1 **/

/* ------------------------------------------------------------------------- */
/*    variables needed                                                       */

static log_t rawnet_arch_log = LOG_ERR;

static pcap_if_t *TfePcapNextDev = NULL;
static pcap_if_t *TfePcapAlldevs = NULL;

static pcap_t *TfePcapFP = NULL;

#ifdef HAVE_LIBNET
#ifdef VICE_USE_LIBNET_1_1
static libnet_t *TfeLibnetFP = NULL;
#else /* VICE_USE_LIBNET_1_1 */
static struct libnet_link_int *TfeLibnetFP = NULL;
#endif /* VICE_USE_LIBNET_1_1 */

static char TfeLibnetErrBuf[LIBNET_ERRBUF_SIZE];

#endif /* HAVE_LIBNET */

static char TfePcapErrbuf[PCAP_ERRBUF_SIZE];

#ifdef RAWNET_DEBUG_PKTDUMP

static void debug_output( const char *text, BYTE *what, int count )
{
    char buffer[256];
    char *p = buffer;
    char *pbuffer1 = what;
    int len1 = count;
    int i;

    sprintf(buffer, "\n%s: length = %u\n", text, len1);
    fprintf(stderr, "%s", buffer);
    do {
        p = buffer;
        for (i=0; (i<8) && len1>0; len1--, i++) {
            sprintf(p, "%02x ", (unsigned int)(unsigned char)*pbuffer1++);
            p += 3;
        }
        *(p-1) = '\n'; *p = 0;
        fprintf(stderr, "%s", buffer);
    } while (len1>0);
}
#endif /* #ifdef RAWNET_DEBUG_PKTDUMP */

/*
 These functions let the UI enumerate the available interfaces.

 First, TfeEnumAdapterOpen() is used to start enumeration.

 TfeEnumAdapter is then used to gather information for each adapter present
 on the system, where:

   ppname points to a pointer which will hold the name of the interface
   ppdescription points to a pointer which will hold the description of the interface

   For each of these parameters, new memory is allocated, so it has to be
   freed with lib_free().

 TfeEnumAdapterClose() must be used to stop processing.

 Each function returns 1 on success, and 0 on failure.
 TfeEnumAdapter() only fails if there is no more adpater; in this case, 
   *ppname and *ppdescription are not altered.
*/
int rawnet_arch_enumadapter_open(void)
{
    if (pcap_findalldevs(&TfePcapAlldevs, TfePcapErrbuf) == -1) {
        log_message(rawnet_arch_log, "ERROR in TfeEnumAdapterOpen: pcap_findalldevs: '%s'", TfePcapErrbuf);
        return 0;
    }

    if (!TfePcapAlldevs) {
        log_message(rawnet_arch_log, "ERROR in TfeEnumAdapterOpen, finding all pcap devices - Do we have the necessary privilege rights?");
        return 0;
    }

    TfePcapNextDev = TfePcapAlldevs;
    return 1;
}

int rawnet_arch_enumadapter(char **ppname, char **ppdescription)
{
    if (!TfePcapNextDev) {
        return 0;
    }

    *ppname = lib_stralloc(TfePcapNextDev->name);
    *ppdescription = lib_stralloc(TfePcapNextDev->description);

    TfePcapNextDev = TfePcapNextDev->next;

    return 1;
}

int rawnet_arch_enumadapter_close(void)
{
    if (TfePcapAlldevs) {
        pcap_freealldevs(TfePcapAlldevs);
        TfePcapAlldevs = NULL;
    }
    return 1;
}

static int TfePcapOpenAdapter(const char *interface_name) 
{
    TfePcapFP = pcap_open_live((char*)interface_name, 1700, 1, 20, TfePcapErrbuf);
    if ( TfePcapFP == NULL) {
        log_message(rawnet_arch_log, "ERROR opening adapter: '%s'", TfePcapErrbuf);
        return 0;
    }

    if (pcap_setnonblock(TfePcapFP, 1, TfePcapErrbuf) < 0) {
        log_message(rawnet_arch_log, "WARNING: Setting PCAP to non-blocking failed: '%s'", TfePcapErrbuf);
    }

    /* Check the link layer. We support only Ethernet for simplicity. */
    if (pcap_datalink(TfePcapFP) != DLT_EN10MB) {
        log_message(rawnet_arch_log, "ERROR: TFE works only on Ethernet networks.");
        return 0;
    }

#ifdef HAVE_LIBNET
    /* now, open the libnet device to be able to send afterwards */
#ifdef VICE_USE_LIBNET_1_1
    TfeLibnetFP = libnet_init(LIBNET_LINK, (char *)interface_name, TfeLibnetErrBuf);
#else /* VICE_USE_LIBNET_1_1 */
    TfeLibnetFP = libnet_open_link_interface(interface_name, TfeLibnetErrBuf);
#endif /* VICE_USE_LIBNET_1_1 */

    if (TfeLibnetFP == NULL) {
        log_message(rawnet_arch_log, "Libnet interface could not be opened: '%s'", TfeLibnetErrBuf);

        if (TfePcapFP) {
            pcap_close(TfePcapFP);
            TfePcapFP = NULL;
        }
        return 0;
    }
#endif /* HAVE_LIBNET */

    return 1;
}

/* ------------------------------------------------------------------------- */
/*    the architecture-dependend functions                                   */

int rawnet_arch_init(void)
{
    rawnet_arch_log = log_open("TFEARCH");

    return 1;
}

void rawnet_arch_pre_reset(void)
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "rawnet_arch_pre_reset()." );
#endif
}

void rawnet_arch_post_reset(void)
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "rawnet_arch_post_reset()." );
#endif
}

int rawnet_arch_activate(const char *interface_name)
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "rawnet_arch_activate()." );
#endif
    if (!TfePcapOpenAdapter(interface_name)) {
        return 0;
    }
    return 1;
}

void rawnet_arch_deactivate( void )
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "rawnet_arch_deactivate()." );
#endif
}

void rawnet_arch_set_mac( const BYTE mac[6] )
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "New MAC address set: %02X:%02X:%02X:%02X:%02X:%02X.", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
#endif
}

void rawnet_arch_set_hashfilter(const DWORD hash_mask[2])
{
#ifdef RAWNET_DEBUG_ARCH
    log_message( rawnet_arch_log, "New hash filter set: %08X:%08X.", hash_mask[1], hash_mask[0]);
#endif
}

/* int bBroadcast   - broadcast */
/* int bIA          - individual address (IA) */
/* int bMulticast   - multicast if address passes the hash filter */
/* int bCorrect     - accept correct frames */
/* int bPromiscuous - promiscuous mode */
/* int bIAHash      - accept if IA passes the hash filter */

void rawnet_arch_recv_ctl(int bBroadcast, int bIA, int bMulticast, int bCorrect, int bPromiscuous, int bIAHash)
{
#ifdef RAWNET_DEBUG_ARCH
    log_message(rawnet_arch_log, "rawnet_arch_recv_ctl() called with the following parameters:" );
    log_message(rawnet_arch_log, "\tbBroadcast   = %s", bBroadcast ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbIA          = %s", bIA ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbMulticast   = %s", bMulticast ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbCorrect     = %s", bCorrect ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbPromiscuous = %s", bPromiscuous ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbIAHash      = %s", bIAHash ? "TRUE" : "FALSE");
#endif
}

void rawnet_arch_line_ctl(int bEnableTransmitter, int bEnableReceiver )
{
#ifdef RAWNET_DEBUG_ARCH
    log_message(rawnet_arch_log, "rawnet_arch_line_ctl() called with the following parameters:");
    log_message(rawnet_arch_log, "\tbEnableTransmitter = %s", bEnableTransmitter ? "TRUE" : "FALSE");
    log_message(rawnet_arch_log, "\tbEnableReceiver    = %s", bEnableReceiver ? "TRUE" : "FALSE");
#endif
}

typedef struct TFE_PCAP_INTERNAL_tag {
    unsigned int len;
    BYTE *buffer;
} TFE_PCAP_INTERNAL;

/* Callback function invoked by libpcap for every incoming packet */
static void TfePcapPacketHandler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data)
{
    TFE_PCAP_INTERNAL *pinternal = (void*)param;

    /* determine the count of bytes which has been returned, 
     * but make sure not to overrun the buffer 
     */
    if (header->caplen < pinternal->len) {
        pinternal->len = header->caplen;
    }

    memcpy(pinternal->buffer, pkt_data, pinternal->len);
}

/* the following function receives a frame.

   If there's none, it returns a -1.
   If there is one, it returns the length of the frame in bytes.

   It copies the frame to *buffer and returns the number of copied 
   bytes as return value.

   At most 'len' bytes are copied.
*/
static int rawnet_arch_receive_frame(TFE_PCAP_INTERNAL *pinternal)
{
    int ret = -1;

    /* check if there is something to receive */
    if (pcap_dispatch(TfePcapFP, 1, TfePcapPacketHandler, (void*)pinternal)!=0) {
        /* Something has been received */
        ret = pinternal->len;
    }

#ifdef RAWNET_DEBUG_ARCH
    log_message(rawnet_arch_log, "rawnet_arch_receive_frame() called, returns %d.", ret);
#endif

    return ret;
}

#ifdef HAVE_LIBNET

# ifdef VICE_USE_LIBNET_1_1

#  define RAWNET_ARCH_TRANSMIT rawnet_arch_transmit_libnet_1_1

static void rawnet_arch_transmit_libnet_1_1(int force, int onecoll, int inhibit_crc, int tx_pad_dis, int txlength, BYTE *txframe)
{
    /* we want to send via libnet */

    do {
        libnet_pblock_t *p;

        p = libnet_pblock_new(TfeLibnetFP, txlength);

        if (p == NULL) {
            log_message(rawnet_arch_log, "WARNING! Could not send packet, libnet_pblock_probe() failed!");
            break;
        }

        if ( libnet_pblock_append(TfeLibnetFP, p, txframe, txlength) == -1 ) {
            log_message(rawnet_arch_log, "WARNING! Could not send packet, libnet_pblock_append() failed!");
            break;
        }

        libnet_pblock_update(TfeLibnetFP, p, 0, LIBNET_PBLOCK_ETH_H);

        if ( libnet_write(TfeLibnetFP) == -1 ) {
            log_message(rawnet_arch_log, "WARNING! Could not send packet, libnet_write() failed!");
            break;
        }

        libnet_pblock_delete(TfeLibnetFP, p);

    } while (0);
}

# else /* VICE_USE_LIBNET_1_1 */

#  define RAWNET_ARCH_TRANSMIT rawnet_arch_transmit_libnet_1_0

static void rawnet_arch_transmit_libnet_1_0(int force, int onecoll, int inhibit_crc, int tx_pad_dis, int txlength, BYTE *txframe)
{
    u_char *plibnet_buffer = NULL;

    /* we want to send via libnet 1.0 */

    if (libnet_init_packet(txlength, &plibnet_buffer)==-1) {
        log_message(rawnet_arch_log, "WARNING! Could not send packet!");
    } else {
        if (plibnet_buffer) {
            memcpy(plibnet_buffer, txframe, txlength);
            libnet_write_link_layer(TfeLibnetFP, "eth0", plibnet_buffer, txlength);
            libnet_destroy_packet(&plibnet_buffer);
        } else {
            log_message(rawnet_arch_log, "WARNING! Could not send packet: plibnet_buffer==NULL, but libnet_init_packet() did NOT fail!!");
        }
    }

}

# endif 

#else /* HAVE_LIBNET */

#  define RAWNET_ARCH_TRANSMIT rawnet_arch_transmit_pcap

 #if defined(HAVE_PCAP_INJECT)
  #define PCAP_INJECT pcap_inject
 #elif defined(HAVE_PCAP_SENDPACKET)
  #define PCAP_INJECT pcap_sendpacket
 #else
  #error SHOULD NOT HAPPEN: No libnet, but neither HAVE_PCAP_SENDPACKET nor HAVE_PCAP_INJECT are defined!
 #endif

static void rawnet_arch_transmit_pcap(int force, int onecoll, int inhibit_crc, int tx_pad_dis, int txlength, BYTE *txframe)
{
    /* we want to send via pcap */

    if (PCAP_INJECT(TfePcapFP, txframe, txlength) < 0) {
        log_message(rawnet_arch_log, "WARNING! Could not send packet!");
    }
}

#endif /* HAVE_LIBNET */

/* int force       - FORCE: Delete waiting frames in transmit buffer */
/* int onecoll     - ONECOLL: Terminate after just one collision */
/* int inhibit_crc - INHIBITCRC: Do not append CRC to the transmission */
/* int tx_pad_dis  - TXPADDIS: Disable padding to 60 Bytes */
/* int txlength    - Frame length */
/* BYTE *txframe   - Pointer to the frame to be transmitted */

void rawnet_arch_transmit(int force, int onecoll, int inhibit_crc, int tx_pad_dis, int txlength, BYTE *txframe)
{
#ifdef RAWNET_DEBUG_ARCH
    log_message(rawnet_arch_log, "rawnet_arch_transmit() called, with: force = %s, onecoll = %s, inhibit_crc=%s, tx_pad_dis=%s, txlength=%u",
                force ? "TRUE" : "FALSE", 
                onecoll ? "TRUE" : "FALSE",
                inhibit_crc ? "TRUE" : "FALSE",
                tx_pad_dis ? "TRUE" : "FALSE",
                txlength);
#endif

#ifdef RAWNET_DEBUG_PKTDUMP
    debug_output("Transmit frame: ", txframe, txlength);
#endif /* #ifdef RAWNET_DEBUG_PKTDUMP */

    RAWNET_ARCH_TRANSMIT(force, onecoll, inhibit_crc, tx_pad_dis, txlength, txframe);
}

/*
  rawnet_arch_receive()

  This function checks if there was a frame received.
  If so, it returns 1, else 0.

  If there was no frame, none of the parameters is changed!

  If there was a frame, the following actions are done:

  - at maximum *plen byte are transferred into the buffer given by pbuffer
  - *plen gets the length of the received frame, EVEN if this is more
    than has been copied to pbuffer!
  - if the dest. address was accepted by the hash filter, *phashed is set, else
    cleared.
  - if the dest. address was accepted by the hash filter, *phash_index is
    set to the number of the rule leading to the acceptance
  - if the receive was ok (good CRC and valid length), *prx_ok is set, 
    else cleared.
  - if the dest. address was accepted because it's exactly our MAC address
    (set by rawnet_arch_set_mac()), *pcorrect_mac is set, else cleared.
  - if the dest. address was accepted since it was a broadcast address,
    *pbroadcast is set, else cleared.
  - if the received frame had a crc error, *pcrc_error is set, else cleared
*/

/* BYTE *pbuffer     - where to store a frame */
/* int *plen         - IN: maximum length of frame to copy; */
/*                     OUT: length of received frame OUT */
/*                          can be bigger than IN if received */
/*                          frame was longer than supplied buffer */
/* int *phashed      - set if the dest. address is accepted by the hash filter */
/* int *phash_index  - hash table index if hashed == TRUE */   
/* int *prx_ok       - set if good CRC and valid length */
/* int *pcorrect_mac - set if dest. address is exactly our IA */
/* int *pbroadcast   - set if dest. address is a broadcast address */
/* int *pcrc_error   - set if received frame had a CRC error */

int rawnet_arch_receive(BYTE *pbuffer, int *plen, int  *phashed, int *phash_index, int *prx_ok, int *pcorrect_mac, int *pbroadcast, int *pcrc_error)
{
    int len;

    TFE_PCAP_INTERNAL internal = { *plen, pbuffer };

#ifdef RAWNET_DEBUG_ARCH
    log_message(rawnet_arch_log, "rawnet_arch_receive() called, with *plen=%u.", *plen);
#endif

    assert((*plen & 1) == 0);

    len = rawnet_arch_receive_frame(&internal);

    if (len != -1) {

#ifdef RAWNET_DEBUG_PKTDUMP
        debug_output("Received frame: ", internal.buffer, internal.len);
#endif /* #ifdef RAWNET_DEBUG_PKTDUMP */

        if (len & 1) {
            ++len;
        }

        *plen = len;

        /* we don't decide if this frame fits the needs;
         * by setting all zero, we let tfe.c do the work
         * for us
         */
        *phashed =
        *phash_index =
        *pbroadcast = 
        *pcorrect_mac =
        *pcrc_error = 0;

        /* this frame has been received correctly */
        *prx_ok = 1;

        return 1;
    }

    return 0;
}

#endif /* #ifdef HAVE_TFE */
