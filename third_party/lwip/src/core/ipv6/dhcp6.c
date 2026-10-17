/**
 * @file
 *
 * @defgroup dhcp6 DHCPv6
 * @ingroup ip6
 * DHCPv6 client: IPv6 address autoconfiguration as per
 * RFC 3315 (stateful DHCPv6) and
 * RFC 3736 (stateless DHCPv6).
 *
 * NovaOS: stateful DHCPv6 (RFC 8415: one IA_NA with one address, its DNS
 * servers and domain search list; Renew at T1, Rebind at T2, Release and
 * Decline) is implemented as well, beside stateless DHCPv6.
 *
 * TODO:
 * - enable/disable API to not always start when RA is received
 * - stateful DHCPv6 (for now, only stateless DHCPv6 for DNS and NTP servers works)
 * - create Client Identifier?
 * - only start requests if a valid local address is available on the netif
 * - only start information requests if required (not for every RA)
 *
 * dhcp6_enable_stateful() enables stateful DHCPv6 for a netif (stateless disabled)<br>
 * dhcp6_enable_stateless() enables stateless DHCPv6 for a netif (stateful disabled)<br>
 * dhcp6_disable() disable DHCPv6 for a netif
 *
 * When enabled, requests are only issued after receipt of RA with the
 * corresponding bits set.
 */

/*
 * Copyright (c) 2018 Simon Goldschmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 */

#include "lwip/opt.h"

#if LWIP_IPV6 && LWIP_IPV6_DHCP6 /* don't build if not configured for use in lwipopts.h */

#include "lwip/dhcp6.h"
#include "lwip/prot/dhcp6.h"
#include "lwip/def.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/sys.h"

#include <string.h>

#ifdef LWIP_HOOK_FILENAME
#include LWIP_HOOK_FILENAME
#endif
#ifndef LWIP_HOOK_DHCP6_APPEND_OPTIONS
#define LWIP_HOOK_DHCP6_APPEND_OPTIONS(netif, dhcp6, state, msg, msg_type, options_len_ptr, max_len)
#endif
#ifndef LWIP_HOOK_DHCP6_PARSE_OPTION
#define LWIP_HOOK_DHCP6_PARSE_OPTION(netif, dhcp6, state, msg, msg_type, option, len, pbuf, offset) do { LWIP_UNUSED_ARG(msg); } while(0)
#endif

#if LWIP_DNS && LWIP_DHCP6_MAX_DNS_SERVERS
#if DNS_MAX_SERVERS > LWIP_DHCP6_MAX_DNS_SERVERS
#define LWIP_DHCP6_PROVIDE_DNS_SERVERS LWIP_DHCP6_MAX_DNS_SERVERS
#else
#define LWIP_DHCP6_PROVIDE_DNS_SERVERS DNS_MAX_SERVERS
#endif
#else
#define LWIP_DHCP6_PROVIDE_DNS_SERVERS 0
#endif


/** Option handling: options are parsed in dhcp6_parse_reply
 * and saved in an array where other functions can load them from.
 * This might be moved into the struct dhcp6 (not necessarily since
 * lwIP is single-threaded and the array is only used while in recv
 * callback). */
enum dhcp6_option_idx {
  DHCP6_OPTION_IDX_CLI_ID = 0,
  DHCP6_OPTION_IDX_SERVER_ID,
#if LWIP_DHCP6_PROVIDE_DNS_SERVERS
  DHCP6_OPTION_IDX_DNS_SERVER,
  DHCP6_OPTION_IDX_DOMAIN_LIST,
#endif /* LWIP_DHCP_PROVIDE_DNS_SERVERS */
#if LWIP_DHCP6_GET_NTP_SRV
  DHCP6_OPTION_IDX_NTP_SERVER,
#endif /* LWIP_DHCP_GET_NTP_SRV */
#if LWIP_IPV6_DHCP6_STATEFUL
  DHCP6_OPTION_IDX_IA_NA,
  DHCP6_OPTION_IDX_STATUS_CODE,
  DHCP6_OPTION_IDX_PREFERENCE,
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  DHCP6_OPTION_IDX_MAX
};

struct dhcp6_option_info {
  u8_t option_given;
  u16_t val_start;
  u16_t val_length;
};

/** Holds the decoded option info, only valid while in dhcp6_recv. */
struct dhcp6_option_info dhcp6_rx_options[DHCP6_OPTION_IDX_MAX];

#define dhcp6_option_given(dhcp6, idx)           (dhcp6_rx_options[idx].option_given != 0)
#define dhcp6_got_option(dhcp6, idx)             (dhcp6_rx_options[idx].option_given = 1)
#define dhcp6_clear_option(dhcp6, idx)           (dhcp6_rx_options[idx].option_given = 0)
#define dhcp6_clear_all_options(dhcp6)           (memset(dhcp6_rx_options, 0, sizeof(dhcp6_rx_options)))
#define dhcp6_get_option_start(dhcp6, idx)       (dhcp6_rx_options[idx].val_start)
#define dhcp6_get_option_length(dhcp6, idx)      (dhcp6_rx_options[idx].val_length)
#define dhcp6_set_option(dhcp6, idx, start, len) do { dhcp6_rx_options[idx].val_start = (start); dhcp6_rx_options[idx].val_length = (len); }while(0)


const ip_addr_t dhcp6_All_DHCP6_Relay_Agents_and_Servers = IPADDR6_INIT_HOST(0xFF020000, 0, 0, 0x00010002);
const ip_addr_t dhcp6_All_DHCP6_Servers = IPADDR6_INIT_HOST(0xFF020000, 0, 0, 0x00010003);

static struct udp_pcb *dhcp6_pcb;
static u8_t dhcp6_pcb_refcount;


/* receive, unfold, parse and free incoming messages */
static void dhcp6_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

/** Ensure DHCP PCB is allocated and bound */
static err_t
dhcp6_inc_pcb_refcount(void)
{
  if (dhcp6_pcb_refcount == 0) {
    LWIP_ASSERT("dhcp6_inc_pcb_refcount(): memory leak", dhcp6_pcb == NULL);

    /* allocate UDP PCB */
    dhcp6_pcb = udp_new_ip6();

    if (dhcp6_pcb == NULL) {
      return ERR_MEM;
    }

    ip_set_option(dhcp6_pcb, SOF_BROADCAST);

    /* set up local and remote port for the pcb -> listen on all interfaces on all src/dest IPs */
    udp_bind(dhcp6_pcb, IP6_ADDR_ANY, DHCP6_CLIENT_PORT);
    udp_recv(dhcp6_pcb, dhcp6_recv, NULL);
  }

  dhcp6_pcb_refcount++;

  return ERR_OK;
}

/** Free DHCP PCB if the last netif stops using it */
static void
dhcp6_dec_pcb_refcount(void)
{
  LWIP_ASSERT("dhcp6_pcb_refcount(): refcount error", (dhcp6_pcb_refcount > 0));
  dhcp6_pcb_refcount--;

  if (dhcp6_pcb_refcount == 0) {
    udp_remove(dhcp6_pcb);
    dhcp6_pcb = NULL;
  }
}

/**
 * @ingroup dhcp6
 * Set a statically allocated struct dhcp6 to work with.
 * Using this prevents dhcp6_start to allocate it using mem_malloc.
 *
 * @param netif the netif for which to set the struct dhcp
 * @param dhcp6 (uninitialised) dhcp6 struct allocated by the application
 */
void
dhcp6_set_struct(struct netif *netif, struct dhcp6 *dhcp6)
{
  LWIP_ASSERT("netif != NULL", netif != NULL);
  LWIP_ASSERT("dhcp6 != NULL", dhcp6 != NULL);
  LWIP_ASSERT("netif already has a struct dhcp6 set", netif_dhcp6_data(netif) == NULL);

  /* clear data structure */
  memset(dhcp6, 0, sizeof(struct dhcp6));
  /* dhcp6_set_state(&dhcp, DHCP6_STATE_OFF); */
  netif_set_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6, dhcp6);
}

/**
 * @ingroup dhcp6
 * Removes a struct dhcp6 from a netif.
 *
 * ATTENTION: Only use this when not using dhcp6_set_struct() to allocate the
 *            struct dhcp6 since the memory is passed back to the heap.
 *
 * @param netif the netif from which to remove the struct dhcp
 */
void dhcp6_cleanup(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", netif != NULL);

  if (netif_dhcp6_data(netif) != NULL) {
    mem_free(netif_dhcp6_data(netif));
    netif_set_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6, NULL);
  }
}

static struct dhcp6*
dhcp6_get_struct(struct netif *netif, const char *dbg_requester)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  if (dhcp6 == NULL) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("%s: mallocing new DHCPv6 client\n", dbg_requester));
    dhcp6 = (struct dhcp6 *)mem_malloc(sizeof(struct dhcp6));
    if (dhcp6 == NULL) {
      LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("%s: could not allocate dhcp6\n", dbg_requester));
      return NULL;
    }

    /* clear data structure, this implies DHCP6_STATE_OFF */
    memset(dhcp6, 0, sizeof(struct dhcp6));
    /* store this dhcp6 client in the netif */
    netif_set_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6, dhcp6);
  } else {
    /* already has DHCP6 client attached */
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("%s: using existing DHCPv6 client\n", dbg_requester));
  }

  if (!dhcp6->pcb_allocated) {
    if (dhcp6_inc_pcb_refcount() != ERR_OK) { /* ensure DHCP6 PCB is allocated */
      mem_free(dhcp6);
      netif_set_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6, NULL);
      return NULL;
    }
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("%s: allocated dhcp6\n", dbg_requester));
    dhcp6->pcb_allocated = 1;
  }
  return dhcp6;
}

/*
 * Set the DHCPv6 state
 * If the state changed, reset the number of tries.
 */
static void
dhcp6_set_state(struct dhcp6 *dhcp6, u8_t new_state, const char *dbg_caller)
{
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("DHCPv6 state: %d -> %d (%s)\n",
    dhcp6->state, new_state, dbg_caller));
  if (new_state != dhcp6->state) {
    dhcp6->state = new_state;
    dhcp6->tries = 0;
    dhcp6->request_timeout = 0;
  }
}

#if LWIP_IPV6_DHCP6_STATEFUL
#if !LWIP_IPV6_DHCP6_STATELESS || !LWIP_DHCP6_PROVIDE_DNS_SERVERS
#error "NovaOS: stateful DHCPv6 is built with stateless DHCPv6 and DNS servers"
#endif
/* the state to go back to after an Information-request */
#define DHCP6_IDLE_STATE(dhcp6) ((dhcp6)->stateful ? DHCP6_STATE_STATEFUL_IDLE : DHCP6_STATE_STATELESS_IDLE)
static void dhcp6_drop_lease(struct netif *netif, struct dhcp6 *dhcp6);
#else
#define DHCP6_IDLE_STATE(dhcp6) DHCP6_STATE_STATELESS_IDLE
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

static int
dhcp6_stateless_enabled(struct dhcp6 *dhcp6)
{
#if LWIP_IPV6_DHCP6_STATEFUL
  if (dhcp6->stateful) {
    return 0;
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  if ((dhcp6->state == DHCP6_STATE_STATELESS_IDLE) ||
      (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG)) {
    return 1;
  }
  return 0;
}

/*static int
dhcp6_stateful_enabled(struct dhcp6 *dhcp6)
{
  if (dhcp6->state == DHCP6_STATE_OFF) {
    return 0;
  }
  if (dhcp6_stateless_enabled(dhcp6)) {
    return 0;
  }
  return 1;
}*/

/**
 * @ingroup dhcp6
 * Enable stateful DHCPv6 on this netif
 * Requests are sent on receipt of an RA message with the
 * ND6_RA_FLAG_MANAGED_ADDR_CONFIG flag set.
 *
 * A struct dhcp6 will be allocated for this netif if not
 * set via @ref dhcp6_set_struct before.
 *
 * @todo: stateful DHCPv6 not supported, yet
 */
err_t
dhcp6_enable_stateful(struct netif *netif)
{
#if LWIP_IPV6_DHCP6_STATEFUL
  /* NovaOS: a Solicit on an RA with the M flag; an Information-request
   * (stateless) on one with only the O flag */
  struct dhcp6 *dhcp6 = dhcp6_get_struct(netif, "dhcp6_enable_stateful()");
  if (dhcp6 == NULL) {
    return ERR_MEM;
  }
  if (dhcp6->stateful) {
    return ERR_OK;
  }
  dhcp6->stateful = 1;
  dhcp6->hold = 0;
  dhcp6->addr_idx = -1;
  ip6_addr_set_zero(&dhcp6->addr);
  dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_enable_stateful");
  return ERR_OK;
#else
  LWIP_UNUSED_ARG(netif);
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("stateful dhcp6 not implemented yet\n"));
  return ERR_VAL;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
}

/**
 * @ingroup dhcp6
 * Enable stateless DHCPv6 on this netif
 * Requests are sent on receipt of an RA message with the
 * ND6_RA_FLAG_OTHER_CONFIG flag set.
 *
 * A struct dhcp6 will be allocated for this netif if not
 * set via @ref dhcp6_set_struct before.
 */
err_t
dhcp6_enable_stateless(struct netif *netif)
{
  struct dhcp6 *dhcp6;

  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("dhcp6_enable_stateless(netif=%p) %c%c%"U16_F"\n", (void *)netif, netif->name[0], netif->name[1], (u16_t)netif->num));

  dhcp6 = dhcp6_get_struct(netif, "dhcp6_enable_stateless()");
  if (dhcp6 == NULL) {
    return ERR_MEM;
  }
  if (dhcp6_stateless_enabled(dhcp6)) {
    LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp6_enable_stateless(): stateless DHCPv6 already enabled\n"));
    return ERR_OK;
  } else if (dhcp6->state != DHCP6_STATE_OFF) {
    /* stateful running */
    LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp6_enable_stateless(): switching from stateful to stateless DHCPv6\n"));
#if LWIP_IPV6_DHCP6_STATEFUL
    dhcp6_drop_lease(netif, dhcp6);           /* NovaOS: (the address goes) */
    dhcp6->stateful = 0;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  }
  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp6_enable_stateless(): stateless DHCPv6 enabled\n"));
  dhcp6_set_state(dhcp6, DHCP6_STATE_STATELESS_IDLE, "dhcp6_enable_stateless");
  return ERR_OK;
}

/**
 * @ingroup dhcp6
 * Disable stateful or stateless DHCPv6 on this netif
 * Requests are sent on receipt of an RA message with the
 * ND6_RA_FLAG_OTHER_CONFIG flag set.
 */
void
dhcp6_disable(struct netif *netif)
{
  struct dhcp6 *dhcp6;

  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("dhcp6_disable(netif=%p) %c%c%"U16_F"\n", (void *)netif, netif->name[0], netif->name[1], (u16_t)netif->num));

  dhcp6 = netif_dhcp6_data(netif);
  if (dhcp6 != NULL) {
    if (dhcp6->state != DHCP6_STATE_OFF) {
      LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_disable(): DHCPv6 disabled (old state: %s)\n",
        (dhcp6_stateless_enabled(dhcp6) ? "stateless" : "stateful")));
#if LWIP_IPV6_DHCP6_STATEFUL
      if (dhcp6->stateful) {
        dhcp6_drop_lease(netif, dhcp6);       /* NovaOS */
        dhcp6->stateful = 0;
      }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
      dhcp6_set_state(dhcp6, DHCP6_STATE_OFF, "dhcp6_disable");
      if (dhcp6->pcb_allocated != 0) {
        dhcp6_dec_pcb_refcount(); /* free DHCPv6 PCB if not needed any more */
        dhcp6->pcb_allocated = 0;
      }
    }
  }
}

/**
 * Create a DHCPv6 request, fill in common headers
 *
 * @param netif the netif under DHCPv6 control
 * @param dhcp6 dhcp6 control struct
 * @param message_type message type of the request
 * @param opt_len_alloc option length to allocate
 * @param options_out_len option length on exit
 * @return a pbuf for the message
 */
static struct pbuf *
dhcp6_create_msg(struct netif *netif, struct dhcp6 *dhcp6, u8_t message_type,
                 u16_t opt_len_alloc, u16_t *options_out_len)
{
  struct pbuf *p_out;
  struct dhcp6_msg *msg_out;

  LWIP_ERROR("dhcp6_create_msg: netif != NULL", (netif != NULL), return NULL;);
  LWIP_ERROR("dhcp6_create_msg: dhcp6 != NULL", (dhcp6 != NULL), return NULL;);
  p_out = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcp6_msg) + opt_len_alloc, PBUF_RAM);
  if (p_out == NULL) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_LEVEL_SERIOUS,
                ("dhcp6_create_msg(): could not allocate pbuf\n"));
    return NULL;
  }
  LWIP_ASSERT("dhcp6_create_msg: check that first pbuf can hold struct dhcp6_msg",
              (p_out->len >= sizeof(struct dhcp6_msg) + opt_len_alloc));

  /* @todo: limit new xid for certain message types? */
  /* reuse transaction identifier in retransmissions */
  if (dhcp6->tries == 0) {
    dhcp6->xid = LWIP_RAND() & 0xFFFFFF;
  }

  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE,
              ("transaction id xid(%"X32_F")\n", dhcp6->xid));

  msg_out = (struct dhcp6_msg *)p_out->payload;
  memset(msg_out, 0, sizeof(struct dhcp6_msg) + opt_len_alloc);

  msg_out->msgtype = message_type;
  msg_out->transaction_id[0] = (u8_t)(dhcp6->xid >> 16);
  msg_out->transaction_id[1] = (u8_t)(dhcp6->xid >> 8);
  msg_out->transaction_id[2] = (u8_t)dhcp6->xid;
  *options_out_len = 0;
  return p_out;
}

static u16_t
dhcp6_option_short(u16_t options_out_len, u8_t *options, u16_t value)
{
  options[options_out_len++] = (u8_t)((value & 0xff00U) >> 8);
  options[options_out_len++] = (u8_t) (value & 0x00ffU);
  return options_out_len;
}

static u16_t
dhcp6_option_optionrequest(u16_t options_out_len, u8_t *options, const u16_t *req_options,
                           u16_t num_req_options, u16_t max_len)
{
  size_t i;
  u16_t ret;

  LWIP_ASSERT("dhcp6_option_optionrequest: options_out_len + sizeof(struct dhcp6_msg) + addlen <= max_len",
    sizeof(struct dhcp6_msg) + options_out_len + 4U + (2U * num_req_options) <= max_len);
  LWIP_UNUSED_ARG(max_len);

  ret = dhcp6_option_short(options_out_len, options, DHCP6_OPTION_ORO);
  ret = dhcp6_option_short(ret, options, 2 * num_req_options);
  for (i = 0; i < num_req_options; i++) {
    ret = dhcp6_option_short(ret, options, req_options[i]);
  }
  return ret;
}

/* All options are added, shrink the pbuf to the required size */
static void
dhcp6_msg_finalize(u16_t options_out_len, struct pbuf *p_out)
{
  /* shrink the pbuf to the actual content length */
  pbuf_realloc(p_out, (u16_t)(sizeof(struct dhcp6_msg) + options_out_len));
}


#if LWIP_IPV6_DHCP6_STATELESS
static void
dhcp6_information_request(struct netif *netif, struct dhcp6 *dhcp6)
{
  const u16_t requested_options[] = {
#if LWIP_DHCP6_PROVIDE_DNS_SERVERS
    DHCP6_OPTION_DNS_SERVERS,
    DHCP6_OPTION_DOMAIN_LIST
#endif
#if LWIP_DHCP6_GET_NTP_SRV
    , DHCP6_OPTION_SNTP_SERVERS
#endif
  };

  u16_t msecs;
  struct pbuf *p_out;
  u16_t options_out_len;
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_information_request()\n"));
  /* create and initialize the DHCP message header */
  p_out = dhcp6_create_msg(netif, dhcp6, DHCP6_INFOREQUEST, 4 + sizeof(requested_options), &options_out_len);
  if (p_out != NULL) {
    err_t err;
    struct dhcp6_msg *msg_out = (struct dhcp6_msg *)p_out->payload;
    u8_t *options = (u8_t *)(msg_out + 1);
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_information_request: making request\n"));

    options_out_len = dhcp6_option_optionrequest(options_out_len, options, requested_options,
      LWIP_ARRAYSIZE(requested_options), p_out->len);
    LWIP_HOOK_DHCP6_APPEND_OPTIONS(netif, dhcp6, DHCP6_STATE_REQUESTING_CONFIG, msg_out,
      DHCP6_INFOREQUEST, options_out_len, p_out->len);
    dhcp6_msg_finalize(options_out_len, p_out);

    err = udp_sendto_if(dhcp6_pcb, p_out, &dhcp6_All_DHCP6_Relay_Agents_and_Servers, DHCP6_SERVER_PORT, netif);
    pbuf_free(p_out);
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("dhcp6_information_request: INFOREQUESTING -> %d\n", (int)err));
    if (err == ERR_RTE) {
      /* NovaOS: no usable source address yet (the link-local address is
       * still tentative while DAD runs): nothing was sent, so try again on
       * the next timer tick instead of backing off */
      dhcp6_set_state(dhcp6, DHCP6_STATE_REQUESTING_CONFIG, "dhcp6_information_request");
      dhcp6->request_timeout = 1;
      return;
    }
  } else {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_LEVEL_SERIOUS, ("dhcp6_information_request: could not allocate DHCP6 request\n"));
  }
  dhcp6_set_state(dhcp6, DHCP6_STATE_REQUESTING_CONFIG, "dhcp6_information_request");
  if (dhcp6->tries < 255) {
    dhcp6->tries++;
  }
  msecs = (u16_t)((dhcp6->tries < 6 ? 1 << dhcp6->tries : 60) * 1000);
  dhcp6->request_timeout = (u16_t)((msecs + DHCP6_TIMER_MSECS - 1) / DHCP6_TIMER_MSECS);
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("dhcp6_information_request(): set request timeout %"U16_F" msecs\n", msecs));
}

static err_t
dhcp6_request_config(struct netif *netif, struct dhcp6 *dhcp6)
{
  /* stateless mode enabled and no request running? */
  if (dhcp6->state == DHCP6_IDLE_STATE(dhcp6)) {
    /* send Information-request and wait for answer; setup receive timeout */
    dhcp6_information_request(netif, dhcp6);
  }

  return ERR_OK;
}

static void
dhcp6_abort_config_request(struct dhcp6 *dhcp6)
{
  if (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG) {
    /* abort running request */
    dhcp6_set_state(dhcp6, DHCP6_IDLE_STATE(dhcp6), "dhcp6_abort_config_request");
  }
}

/* Handle a REPLY to INFOREQUEST
 * This parses DNS and NTP server addresses from the reply.
 */
static void
dhcp6_handle_config_reply(struct netif *netif, struct pbuf *p_msg_in)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);

  LWIP_UNUSED_ARG(dhcp6);
  LWIP_UNUSED_ARG(p_msg_in);

#if LWIP_DHCP6_PROVIDE_DNS_SERVERS
  if (dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_DNS_SERVER)) {
    ip_addr_t dns_addr;
    ip6_addr_t *dns_addr6;
    u16_t op_start = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_DNS_SERVER);
    u16_t op_len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_DNS_SERVER);
    u16_t idx;
    u8_t n;

    ip_addr_set_zero_ip6(&dns_addr);
    dns_addr6 = ip_2_ip6(&dns_addr);
    for (n = 0, idx = op_start; (idx < op_start + op_len) && (n < LWIP_DHCP6_PROVIDE_DNS_SERVERS);
         n++, idx += sizeof(struct ip6_addr_packed)) {
      u16_t copied = pbuf_copy_partial(p_msg_in, dns_addr6, sizeof(struct ip6_addr_packed), idx);
      if (copied != sizeof(struct ip6_addr_packed)) {
        /* pbuf length mismatch */
        return;
      }
      ip6_addr_assign_zone(dns_addr6, IP6_UNKNOWN, netif);
      /* NovaOS: as for RDNSS (nd6.c), don't replace servers DHCPv4 set;
       * add this one at the first free slot, unless it is there already */
      {
        u8_t s, free_s = DNS_MAX_SERVERS;
        for (s = 0; s < DNS_MAX_SERVERS; s++) {
          if (ip_addr_eq(dns_getserver(s), &dns_addr)) {
            break;
          }
          if ((free_s == DNS_MAX_SERVERS) && ip_addr_isany(dns_getserver(s))) {
            free_s = s;
          }
        }
        if ((s == DNS_MAX_SERVERS) && (free_s < DNS_MAX_SERVERS)) {
          dns_setserver(free_s, &dns_addr);
        }
      }
#if LWIP_IPV6_DHCP6_STATEFUL
      if (dhcp6->dns_count < DHCP6_DNS_KEPT) {
        ip6_addr_copy(dhcp6->dns[dhcp6->dns_count], *dns_addr6);
        dhcp6->dns_count++;
      }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
    }
  }
#if LWIP_IPV6_DHCP6_STATEFUL
  /* NovaOS: the Domain Search List (RFC 3646: names as DNS labels,
   * uncompressed), kept as "a.example,b.example" */
  if (dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_DOMAIN_LIST)) {
    u16_t i = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_DOMAIN_LIST);
    u16_t end = (u16_t)(i + dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_DOMAIN_LIST));
    u16_t o = 0, name_start = 0, k;
    u8_t labels = 0, cut = 0;
    while (i < end) {
      u8_t lab;
      if (pbuf_copy_partial(p_msg_in, &lab, 1, i) != 1) {
        cut = 1;
        break;
      }
      i++;
      if (lab == 0) {                         /* a name ends */
        if (labels) {
          dhcp6->domains[o++] = ',';
          labels = 0;
        }
        name_start = o;
        continue;
      }
      if ((lab > 63) || (i + lab > end) || (o + lab + 2 >= DHCP6_DOMAINS_MAX)) {
        cut = 1;
        break;
      }
      if (labels) {
        dhcp6->domains[o++] = '.';
      }
      if (pbuf_copy_partial(p_msg_in, &dhcp6->domains[o], lab, i) != lab) {
        cut = 1;
        break;
      }
      for (k = o; k < o + lab; k++) {
        if ((dhcp6->domains[k] <= ' ') || (dhcp6->domains[k] > '~') || (dhcp6->domains[k] == ',')) {
          dhcp6->domains[k] = '-';
        }
      }
      o = (u16_t)(o + lab);
      i = (u16_t)(i + lab);
      labels++;
    }
    if (cut || labels) {                      /* (a name cut off part-way goes) */
      o = name_start;
    }
    if ((o > 0) && (dhcp6->domains[o - 1] == ',')) {
      o--;
    }
    dhcp6->domains[o] = '\0';
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
#endif /* LWIP_DHCP6_PROVIDE_DNS_SERVERS */

#if LWIP_DHCP6_GET_NTP_SRV
  if (dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_NTP_SERVER)) {
    ip_addr_t ntp_server_addrs[LWIP_DHCP6_MAX_NTP_SERVERS];
    u16_t op_start = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_NTP_SERVER);
    u16_t op_len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_NTP_SERVER);
    u16_t idx;
    u8_t n;

    for (n = 0, idx = op_start; (idx < op_start + op_len) && (n < LWIP_DHCP6_MAX_NTP_SERVERS);
         n++, idx += sizeof(struct ip6_addr_packed)) {
      u16_t copied;
      ip6_addr_t *ntp_addr6 = ip_2_ip6(&ntp_server_addrs[n]);
      ip_addr_set_zero_ip6(&ntp_server_addrs[n]);
      copied = pbuf_copy_partial(p_msg_in, ntp_addr6, sizeof(struct ip6_addr_packed), idx);
      if (copied != sizeof(struct ip6_addr_packed)) {
        /* pbuf length mismatch */
        return;
      }
      ip6_addr_assign_zone(ntp_addr6, IP6_UNKNOWN, netif);
    }
    dhcp6_set_ntp_servers(n, ntp_server_addrs);
  }
#endif /* LWIP_DHCP6_GET_NTP_SRV */
}
#endif /* LWIP_IPV6_DHCP6_STATELESS */

#if LWIP_IPV6_DHCP6_STATEFUL
/* NovaOS: stateful DHCPv6 (RFC 8415), one IA_NA holding one address.
 * An RA with the M flag starts a Solicit; the client takes the best
 * Advertise of those that came in the first retransmission timeout (or
 * the first with Preference 255, or the first at all after that), sends
 * a Request, and the Reply binds the address with its DNS servers and
 * domain search list.  At T1 it sends Renews to that server, at T2
 * Rebinds to any, and when the valid lifetime runs out the address goes
 * and a Solicit starts again.  dhcp6_release sends a Release; an address
 * DAD finds in use gets a Decline.  Messages are retransmitted as section
 * 15 says, with the constants of section 7.6. */
#define SOL_MAX_DELAY   1000
#define SOL_TIMEOUT     1000
#define SOL_MAX_RT      3600000
#define REQ_TIMEOUT     1000
#define REQ_MAX_RT      30000
#define REQ_MAX_RC      10
#define REN_TIMEOUT     10000
#define REN_MAX_RT      600000
#define REB_TIMEOUT     10000
#define REB_MAX_RT      600000
#define REL_TIMEOUT     1000
#define REL_MAX_RC      4
#define DEC_TIMEOUT     1000
#define DEC_MAX_RC      4

#define DHCP6_LEASE_SECS(dhcp6) ((dhcp6)->lease_ticks / (1000 / DHCP6_TIMER_MSECS))

static void
dhcp6_set_timeout_ms(struct dhcp6 *dhcp6, u32_t ms)
{
  u32_t ticks = (ms + DHCP6_TIMER_MSECS - 1) / DHCP6_TIMER_MSECS;
  dhcp6->request_timeout = (u16_t)(ticks == 0 ? 1 : (ticks > 0xFFFF ? 0xFFFF : ticks));
}

/* The next retransmission timeout: IRT the first time, then twice the
 * last, at most MRT (0: no limit); each randomized by +-10 % (the first
 * Solicit's only upwards) */
static u32_t
dhcp6_next_rt(struct dhcp6 *dhcp6, u32_t irt, u32_t mrt, u8_t first_up)
{
  u32_t rt = (dhcp6->tries == 0) ? irt : 2 * dhcp6->rt;
  u32_t j;
  if ((mrt != 0) && (rt > mrt)) {
    rt = mrt;
  }
  dhcp6->rt = rt;
  j = rt / 10;
  if (j == 0) {
    return rt;
  }
  if (first_up && (dhcp6->tries == 0)) {
    return rt + (LWIP_RAND() % (j + 1));
  }
  return rt - j + (LWIP_RAND() % (2 * j + 1));
}

/* This client's DUID (DUID-LL: hardware type 1 and the MAC, stable for
 * the adapter) and its IA_NA's IAID (the MAC's last four bytes) */
static void
dhcp6_make_ids(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6->duid[0] = 0;
  dhcp6->duid[1] = DHCP6_DUID_LL;
  dhcp6->duid[2] = 0;
  dhcp6->duid[3] = 1;
  MEMCPY(&dhcp6->duid[4], netif->hwaddr, 6);
  dhcp6->duid_len = 10;
  dhcp6->iaid = ((u32_t)netif->hwaddr[2] << 24) | ((u32_t)netif->hwaddr[3] << 16) |
                ((u32_t)netif->hwaddr[4] << 8) | netif->hwaddr[5];
}

static u16_t
dhcp6_option_long(u16_t options_out_len, u8_t *options, u32_t value)
{
  options_out_len = dhcp6_option_short(options_out_len, options, (u16_t)(value >> 16));
  return dhcp6_option_short(options_out_len, options, (u16_t)value);
}

static u16_t
dhcp6_option_bytes(u16_t options_out_len, u8_t *options, u16_t code, const void *data, u16_t len)
{
  options_out_len = dhcp6_option_short(options_out_len, options, code);
  options_out_len = dhcp6_option_short(options_out_len, options, len);
  MEMCPY(&options[options_out_len], data, len);
  return (u16_t)(options_out_len + len);
}

/* Send a Solicit, Request, Renew, Rebind, Release or Decline */
static err_t
dhcp6_stateful_send(struct netif *netif, struct dhcp6 *dhcp6, u8_t msg_type)
{
  static const u16_t requested_options[] = { DHCP6_OPTION_DNS_SERVERS, DHCP6_OPTION_DOMAIN_LIST };
  u8_t with_server = (u8_t)((msg_type == DHCP6_REQUEST) || (msg_type == DHCP6_RENEW) ||
                            (msg_type == DHCP6_RELEASE) || (msg_type == DHCP6_DECLINE));
  u8_t with_addr = (u8_t)((msg_type != DHCP6_SOLICIT) && !ip6_addr_isany(&dhcp6->addr));
  u8_t with_oro = (u8_t)((msg_type != DHCP6_RELEASE) && (msg_type != DHCP6_DECLINE));
  u16_t alloc = (u16_t)(4 + DHCP6_DUID_MAX + 4 + DHCP6_SERVER_ID_MAX + 6 + 16 + 28 + 4 + sizeof(requested_options));
  u16_t n;
  u32_t elapsed;
  err_t err;
  struct dhcp6_msg *msg_out;
  u8_t *options;
  struct pbuf *p_out = dhcp6_create_msg(netif, dhcp6, msg_type, alloc, &n);
  if (p_out == NULL) {
    return ERR_MEM;
  }
  msg_out = (struct dhcp6_msg *)p_out->payload;
  options = (u8_t *)(msg_out + 1);
  if (dhcp6->tries == 0) {
    dhcp6->exchange_start = sys_now();
  }
  elapsed = (sys_now() - dhcp6->exchange_start) / 10;     /* (hundredths of a second) */
  n = dhcp6_option_bytes(n, options, DHCP6_OPTION_CLIENTID, dhcp6->duid, dhcp6->duid_len);
  if (with_server) {
    n = dhcp6_option_bytes(n, options, DHCP6_OPTION_SERVERID, dhcp6->server_id, dhcp6->server_id_len);
  }
  n = dhcp6_option_short(n, options, DHCP6_OPTION_ELAPSED_TIME);
  n = dhcp6_option_short(n, options, 2);
  n = dhcp6_option_short(n, options, (u16_t)(elapsed > 0xFFFF ? 0xFFFF : elapsed));
  /* the IA_NA: T1 and T2 0 (the server's choice), with the address held or offered */
  n = dhcp6_option_short(n, options, DHCP6_OPTION_IA_NA);
  n = dhcp6_option_short(n, options, (u16_t)(12 + (with_addr ? 28 : 0)));
  n = dhcp6_option_long(n, options, dhcp6->iaid);
  n = dhcp6_option_long(n, options, 0);
  n = dhcp6_option_long(n, options, 0);
  if (with_addr) {
    n = dhcp6_option_short(n, options, DHCP6_OPTION_IAADDR);
    n = dhcp6_option_short(n, options, 24);
    MEMCPY(&options[n], dhcp6->addr.addr, 16);
    n = (u16_t)(n + 16);
    n = dhcp6_option_long(n, options, 0);
    n = dhcp6_option_long(n, options, 0);
  }
  if (with_oro) {
    n = dhcp6_option_optionrequest(n, options, requested_options, LWIP_ARRAYSIZE(requested_options), p_out->len);
  }
  LWIP_HOOK_DHCP6_APPEND_OPTIONS(netif, dhcp6, dhcp6->state, msg_out, msg_type, n, p_out->len);
  dhcp6_msg_finalize(n, p_out);
  err = udp_sendto_if(dhcp6_pcb, p_out, &dhcp6_All_DHCP6_Relay_Agents_and_Servers, DHCP6_SERVER_PORT, netif);
  pbuf_free(p_out);
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_stateful_send(%d): %d\n", (int)msg_type, (int)err));
  return err;
}

/* Send (or send again) and set the retransmission timeout */
static void
dhcp6_send_and_wait(struct netif *netif, struct dhcp6 *dhcp6, u8_t msg_type, u32_t irt, u32_t mrt)
{
  u32_t ms;
  if (dhcp6_stateful_send(netif, dhcp6, msg_type) == ERR_RTE) {
    /* no usable source address yet (the link-local address is tentative
     * while DAD runs): nothing went out, so again on the next tick */
    dhcp6->request_timeout = 1;
    return;
  }
  ms = dhcp6_next_rt(dhcp6, irt, mrt, (u8_t)(msg_type == DHCP6_SOLICIT));
  if (dhcp6->tries < 255) {
    dhcp6->tries++;
  }
  dhcp6_set_timeout_ms(dhcp6, ms);
}

/* The leased address leaves the netif (dhcp6->addr stays for a Release or Decline) */
static void
dhcp6_drop_address(struct netif *netif, struct dhcp6 *dhcp6)
{
  s8_t i = dhcp6->addr_idx;
  if ((i >= 0) && (i < LWIP_IPV6_NUM_ADDRESSES) &&
      ip6_addr_zoneless_eq(netif_ip6_addr(netif, i), &dhcp6->addr)) {
    netif_ip6_addr_set_state(netif, i, IP6_ADDR_INVALID);
    netif_ip6_addr_set_valid_life(netif, i, 0);
    netif_ip6_addr_set_pref_life(netif, i, 0);
  }
  dhcp6->addr_idx = -1;
}

/* ... and the DNS servers the server named, and the search list */
static void
dhcp6_forget_config(struct dhcp6 *dhcp6)
{
  u8_t i, s;
  for (i = 0; i < dhcp6->dns_count; i++) {
    ip_addr_t a;
    ip_addr_copy_from_ip6(a, dhcp6->dns[i]);
    for (s = 0; s < DNS_MAX_SERVERS; s++) {
      if (ip_addr_zoneless_eq(dns_getserver(s), &a)) {
        dns_setserver(s, NULL);
      }
    }
  }
  dhcp6->dns_count = 0;
  dhcp6->domains[0] = '\0';
}

static void
dhcp6_drop_lease(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_drop_address(netif, dhcp6);
  dhcp6_forget_config(dhcp6);
  ip6_addr_set_zero(&dhcp6->addr);
  dhcp6->t1 = dhcp6->t2 = dhcp6->pref_life = dhcp6->valid_life = 0;
  dhcp6->lease_ticks = 0;
}

static void
dhcp6_start_solicit(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_make_ids(netif, dhcp6);
  dhcp6_set_state(dhcp6, DHCP6_STATE_SOLICIT, "dhcp6_start_solicit");
  dhcp6->have_adv = 0;
  dhcp6->adv_pref = 0;
  dhcp6->server_id_len = 0;
  if (dhcp6->addr_idx < 0) {
    ip6_addr_set_zero(&dhcp6->addr);
  }
  /* the first Solicit waits a random time up to SOL_MAX_DELAY (section 18.2.1) */
  dhcp6_set_timeout_ms(dhcp6, LWIP_RAND() % SOL_MAX_DELAY);
}

static void
dhcp6_start_request(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_set_state(dhcp6, DHCP6_STATE_REQUEST, "dhcp6_start_request");
  dhcp6_send_and_wait(netif, dhcp6, DHCP6_REQUEST, REQ_TIMEOUT, REQ_MAX_RT);
}

static void
dhcp6_start_renew(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_set_state(dhcp6, DHCP6_STATE_RENEW, "dhcp6_start_renew");
  dhcp6_send_and_wait(netif, dhcp6, DHCP6_RENEW, REN_TIMEOUT, REN_MAX_RT);
}

static void
dhcp6_start_rebind(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_set_state(dhcp6, DHCP6_STATE_REBIND, "dhcp6_start_rebind");
  dhcp6_send_and_wait(netif, dhcp6, DHCP6_REBIND, REB_TIMEOUT, REB_MAX_RT);
}

static void
dhcp6_start_decline(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6->declines++;
  dhcp6_drop_address(netif, dhcp6);
  dhcp6_forget_config(dhcp6);
  dhcp6_set_state(dhcp6, DHCP6_STATE_DECLINE, "dhcp6_start_decline");
  dhcp6_send_and_wait(netif, dhcp6, DHCP6_DECLINE, DEC_TIMEOUT, 0);
}

/* The lease is gone: Solicit again if the router still says to */
static void
dhcp6_lease_lost(struct netif *netif, struct dhcp6 *dhcp6)
{
  dhcp6_drop_lease(netif, dhcp6);
  if (dhcp6->managed && !dhcp6->hold) {
    dhcp6_start_solicit(netif, dhcp6);
  } else {
    dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_lease_lost");
  }
}

/* A server's message names it and is for this client (section 16) */
static int
dhcp6_for_us(struct dhcp6 *dhcp6, struct pbuf *p)
{
  u8_t buf[DHCP6_DUID_MAX];
  u16_t len;
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_SERVER_ID) || !dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_CLI_ID)) {
    return 0;
  }
  len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_SERVER_ID);
  if ((len == 0) || (len > DHCP6_SERVER_ID_MAX)) {
    return 0;
  }
  len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_CLI_ID);
  if ((len != dhcp6->duid_len) ||
      (pbuf_copy_partial(p, buf, len, dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_CLI_ID)) != len)) {
    return 0;
  }
  return memcmp(buf, dhcp6->duid, len) == 0;
}

static void
dhcp6_take_server(struct dhcp6 *dhcp6, struct pbuf *p, const ip_addr_t *from)
{
  u16_t len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_SERVER_ID);
  dhcp6->server_id_len = (u8_t)pbuf_copy_partial(p, dhcp6->server_id, len,
                                                 dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_SERVER_ID));
  ip6_addr_copy(dhcp6->server_addr, *ip_2_ip6(from));
}

/* A Status Code option's code (at @start, @len long); success when absent */
static u16_t
dhcp6_status_at(struct pbuf *p, u16_t start, u16_t len)
{
  u8_t c[2];
  if ((len < 2) || (pbuf_copy_partial(p, c, 2, start) != 2)) {
    return DHCP6_STATUS_SUCCESS;
  }
  return (u16_t)((c[0] << 8) | c[1]);
}

static u32_t
dhcp6_get_u32(const u8_t *b)
{
  return ((u32_t)b[0] << 24) | ((u32_t)b[1] << 16) | ((u32_t)b[2] << 8) | b[3];
}

/* The message's IA_NA for this client: its status code, T1, T2 and its
 * first address with a valid lifetime (DHCP6_STATUS_NOADDRSAVAIL when it
 * holds none); -1 when there is no IA_NA for us, or a bad one */
struct dhcp6_ia {
  u32_t t1, t2, pref, valid;
  ip6_addr_t addr;
};

static int
dhcp6_parse_ia_na(struct dhcp6 *dhcp6, struct pbuf *p, struct dhcp6_ia *ia)
{
  u8_t b[24];
  u16_t start, end, off;
  u16_t status = DHCP6_STATUS_SUCCESS;
  u8_t found = 0;
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_IA_NA)) {
    return -1;
  }
  start = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_IA_NA);
  end = (u16_t)(start + dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_IA_NA));
  if ((end < start + 12) || (pbuf_copy_partial(p, b, 12, start) != 12) || (dhcp6_get_u32(b) != dhcp6->iaid)) {
    return -1;
  }
  ia->t1 = dhcp6_get_u32(&b[4]);
  ia->t2 = dhcp6_get_u32(&b[8]);
  if ((ia->t1 != 0) && (ia->t2 != 0) && (ia->t1 > ia->t2)) {
    return -1;                                /* (section 21.4: discarded) */
  }
  for (off = (u16_t)(start + 12); off + 4 <= end; ) {
    u16_t code, len;
    if (pbuf_copy_partial(p, b, 4, off) != 4) {
      return -1;
    }
    code = (u16_t)((b[0] << 8) | b[1]);
    len = (u16_t)((b[2] << 8) | b[3]);
    if (off + 4 + len > end) {
      return -1;
    }
    if ((code == DHCP6_OPTION_IAADDR) && (len >= 24) && !found &&
        (pbuf_copy_partial(p, b, 24, (u16_t)(off + 4)) == 24)) {
      u32_t pref = dhcp6_get_u32(&b[16]), valid = dhcp6_get_u32(&b[20]);
      if ((valid != 0) && (pref <= valid)) {
        MEMCPY(ia->addr.addr, b, 16);           /* (network byte order, as lwIP keeps it) */
        ip6_addr_clear_zone(&ia->addr);
        ia->pref = pref;
        ia->valid = valid;
        found = 1;
      }
    } else if (code == DHCP6_OPTION_STATUS_CODE) {
      status = dhcp6_status_at(p, (u16_t)(off + 4), len);
    }
    off = (u16_t)(off + 4 + len);
  }
  if ((status == DHCP6_STATUS_SUCCESS) && !found) {
    return DHCP6_STATUS_NOADDRSAVAIL;
  }
  return status;
}

static u16_t
dhcp6_top_status(struct dhcp6 *dhcp6, struct pbuf *p)
{
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_STATUS_CODE)) {
    return DHCP6_STATUS_SUCCESS;
  }
  return dhcp6_status_at(p, dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_STATUS_CODE),
                         dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_STATUS_CODE));
}

/* An Advertise, while soliciting */
static void
dhcp6_handle_advertise(struct netif *netif, struct dhcp6 *dhcp6, struct pbuf *p, const ip_addr_t *from)
{
  struct dhcp6_ia ia;
  u8_t pref = 0;
  if ((dhcp6->state != DHCP6_STATE_SOLICIT) || !dhcp6_for_us(dhcp6, p) ||
      (dhcp6_top_status(dhcp6, p) != DHCP6_STATUS_SUCCESS) ||
      (dhcp6_parse_ia_na(dhcp6, p, &ia) != DHCP6_STATUS_SUCCESS)) {
    return;
  }
  if (dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_PREFERENCE) &&
      (dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_PREFERENCE) >= 1)) {
    pbuf_copy_partial(p, &pref, 1, dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_PREFERENCE));
  }
  if (!dhcp6->have_adv || (pref > dhcp6->adv_pref)) {
    dhcp6_take_server(dhcp6, p, from);
    ip6_addr_copy(dhcp6->addr, ia.addr);
    dhcp6->adv_pref = pref;
    dhcp6->have_adv = 1;
  }
  /* the best possible, or the first one after the first timeout: at once */
  if ((pref == 255) || (dhcp6->tries > 1)) {
    dhcp6_start_request(netif, dhcp6);
  }
}

/* The Reply's address goes on the netif (or has its lifetimes extended) */
static void
dhcp6_bind(struct netif *netif, struct dhcp6 *dhcp6, const struct dhcp6_ia *ia)
{
  s8_t idx = dhcp6->addr_idx;
  u8_t was = dhcp6->state;
  if ((idx >= 0) && !ip6_addr_zoneless_eq(&dhcp6->addr, &ia->addr)) {
    dhcp6_drop_address(netif, dhcp6);         /* (another address than before) */
    idx = -1;
  }
  if (idx < 0) {
    s8_t had = netif_get_ip6_addr_match(netif, &ia->addr);
    if (((had >= 0) && netif_ip6_addr_isstatic(netif, had)) ||
        (netif_add_ip6_address(netif, &ia->addr, &idx) != ERR_OK) || (idx < 0)) {
      /* set by hand already, or no free address slot: not ours to lease */
      dhcp6->addr_idx = -1;
      dhcp6_drop_lease(netif, dhcp6);
      dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_bind");
      return;
    }
  }
  dhcp6->addr_idx = idx;
  ip6_addr_copy(dhcp6->addr, ia->addr);
  netif_ip6_addr_set_valid_life(netif, idx, ia->valid);
  netif_ip6_addr_set_pref_life(netif, idx, ia->pref);
  if ((ia->pref > 0) && ip6_addr_isdeprecated(netif_ip6_addr_state(netif, idx))) {
    netif_ip6_addr_set_state(netif, idx, IP6_ADDR_PREFERRED);
  }
  dhcp6->pref_life = ia->pref;
  dhcp6->valid_life = ia->valid;
  dhcp6->t1 = ia->t1;
  dhcp6->t2 = ia->t2;
  if ((dhcp6->t1 == 0) || (dhcp6->t2 == 0)) {
    /* left to the client: half and four fifths of the preferred lifetime (section 21.4) */
    if (ip6_addr_life_isinfinite(ia->pref)) {
      dhcp6->t1 = dhcp6->t2 = IP6_ADDR_LIFE_INFINITE;
    } else {
      dhcp6->t1 = ia->pref / 2;
      dhcp6->t2 = ia->pref / 5 * 4;
    }
  }
  dhcp6->lease_ticks = 0;
  dhcp6_set_state(dhcp6, DHCP6_STATE_BOUND, "dhcp6_bind");
  if (was == DHCP6_STATE_RENEW) {
    dhcp6->renews++;
  } else if (was == DHCP6_STATE_REBIND) {
    dhcp6->rebinds++;
  } else {
    dhcp6->binds++;
  }
}

/* A Reply to a Request, Renew, Rebind, Release or Decline (section 18.2.10) */
static void
dhcp6_handle_stateful_reply(struct netif *netif, struct dhcp6 *dhcp6, struct pbuf *p, const ip_addr_t *from)
{
  struct dhcp6_ia ia;
  int status;
  u8_t state = dhcp6->state;
  if (!dhcp6_for_us(dhcp6, p)) {
    return;
  }
  if (state == DHCP6_STATE_RELEASE) {
    dhcp6->releases++;
    ip6_addr_set_zero(&dhcp6->addr);
    dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_handle_stateful_reply");
    return;
  }
  if (state == DHCP6_STATE_DECLINE) {
    ip6_addr_set_zero(&dhcp6->addr);
    dhcp6_start_solicit(netif, dhcp6);
    return;
  }
  if ((state != DHCP6_STATE_REQUEST) && (state != DHCP6_STATE_RENEW) && (state != DHCP6_STATE_REBIND)) {
    return;
  }
  if (dhcp6_top_status(dhcp6, p) != DHCP6_STATUS_SUCCESS) {
    return;                                   /* (sent again when the timeout comes) */
  }
  status = dhcp6_parse_ia_na(dhcp6, p, &ia);
  if (status < 0) {
    return;
  }
  if (status == DHCP6_STATUS_NOBINDING && state != DHCP6_STATE_REQUEST) {
    /* the server lost the binding: Request it again (section 18.2.10.1) */
    dhcp6_take_server(dhcp6, p, from);
    dhcp6_start_request(netif, dhcp6);
    return;
  }
  if (status != DHCP6_STATUS_SUCCESS) {
    /* no address, not on this link, ...: start over */
    dhcp6->expiries++;
    dhcp6_drop_lease(netif, dhcp6);
    dhcp6_start_solicit(netif, dhcp6);
    return;
  }
  dhcp6_take_server(dhcp6, p, from);
  dhcp6_bind(netif, dhcp6, &ia);
  if (dhcp6->state == DHCP6_STATE_BOUND) {
    dhcp6_forget_config(dhcp6);               /* (what this Reply names replaces it) */
    dhcp6_handle_config_reply(netif, p);
  }
}

/* A retransmission timeout */
static void
dhcp6_stateful_timeout(struct netif *netif, struct dhcp6 *dhcp6)
{
  switch (dhcp6->state) {
    case DHCP6_STATE_SOLICIT:
      if (dhcp6->have_adv) {
        dhcp6_start_request(netif, dhcp6);
      } else {
        dhcp6_send_and_wait(netif, dhcp6, DHCP6_SOLICIT, SOL_TIMEOUT, SOL_MAX_RT);
      }
      break;
    case DHCP6_STATE_REQUEST:
      if (dhcp6->tries >= REQ_MAX_RC) {
        dhcp6_start_solicit(netif, dhcp6);
      } else {
        dhcp6_send_and_wait(netif, dhcp6, DHCP6_REQUEST, REQ_TIMEOUT, REQ_MAX_RT);
      }
      break;
    case DHCP6_STATE_RENEW:
      dhcp6_send_and_wait(netif, dhcp6, DHCP6_RENEW, REN_TIMEOUT, REN_MAX_RT);
      break;
    case DHCP6_STATE_REBIND:
      dhcp6_send_and_wait(netif, dhcp6, DHCP6_REBIND, REB_TIMEOUT, REB_MAX_RT);
      break;
    case DHCP6_STATE_RELEASE:
      if (dhcp6->tries >= REL_MAX_RC) {
        ip6_addr_set_zero(&dhcp6->addr);
        dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_stateful_timeout");
      } else {
        dhcp6_send_and_wait(netif, dhcp6, DHCP6_RELEASE, REL_TIMEOUT, 0);
      }
      break;
    case DHCP6_STATE_DECLINE:
      if (dhcp6->tries >= DEC_MAX_RC) {
        ip6_addr_set_zero(&dhcp6->addr);
        dhcp6_start_solicit(netif, dhcp6);
      } else {
        dhcp6_send_and_wait(netif, dhcp6, DHCP6_DECLINE, DEC_TIMEOUT, 0);
      }
      break;
    default:
      break;
  }
}

/* Every DHCP6_TIMER_MSECS while a lease is held: T1, T2, the valid
 * lifetime, and the address's DAD */
static void
dhcp6_lease_tick(struct netif *netif, struct dhcp6 *dhcp6)
{
  u32_t secs;
  u8_t st;
  if ((dhcp6->state != DHCP6_STATE_BOUND) && (dhcp6->state != DHCP6_STATE_RENEW) &&
      (dhcp6->state != DHCP6_STATE_REBIND)) {
    return;
  }
  dhcp6->lease_ticks++;
  secs = DHCP6_LEASE_SECS(dhcp6);
  st = dhcp6->addr_idx >= 0 ? netif_ip6_addr_state(netif, dhcp6->addr_idx) : IP6_ADDR_INVALID;
  if (ip6_addr_isduplicated(st)) {
    dhcp6_start_decline(netif, dhcp6);        /* another machine has it */
  } else if (ip6_addr_isinvalid(st) || !ip6_addr_zoneless_eq(netif_ip6_addr(netif, dhcp6->addr_idx), &dhcp6->addr) ||
             (!ip6_addr_life_isinfinite(dhcp6->valid_life) && (secs >= dhcp6->valid_life))) {
    dhcp6->expiries++;                        /* the valid lifetime ran out */
    dhcp6_lease_lost(netif, dhcp6);
  } else if ((dhcp6->state == DHCP6_STATE_BOUND) && !ip6_addr_life_isinfinite(dhcp6->t1) && (secs >= dhcp6->t1)) {
    dhcp6_start_renew(netif, dhcp6);
  } else if ((dhcp6->state != DHCP6_STATE_REBIND) && !ip6_addr_life_isinfinite(dhcp6->t2) && (secs >= dhcp6->t2)) {
    dhcp6_start_rebind(netif, dhcp6);
  }
}

/**
 * NovaOS: release the lease: the address goes now and a Release tells
 * the server; no Solicit until dhcp6_renew (ipconfig /release6, and
 * before the machine shuts down or restarts)
 */
void
dhcp6_release(struct netif *netif)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  if ((dhcp6 == NULL) || !dhcp6->stateful) {
    return;
  }
  dhcp6->hold = 1;
  if (dhcp6_bound(dhcp6)) {
    dhcp6_drop_address(netif, dhcp6);
    dhcp6_forget_config(dhcp6);
    dhcp6_set_state(dhcp6, DHCP6_STATE_RELEASE, "dhcp6_release");
    dhcp6_send_and_wait(netif, dhcp6, DHCP6_RELEASE, REL_TIMEOUT, 0);
  } else if ((dhcp6->state == DHCP6_STATE_SOLICIT) || (dhcp6->state == DHCP6_STATE_REQUEST)) {
    dhcp6_drop_lease(netif, dhcp6);
    dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_release");
  }
}

/**
 * NovaOS: renew the lease now (ipconfig /renew6): a Renew while bound,
 * else a Solicit
 */
void
dhcp6_renew(struct netif *netif)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  if ((dhcp6 == NULL) || !dhcp6->stateful) {
    return;
  }
  dhcp6->hold = 0;
  if (dhcp6->state == DHCP6_STATE_BOUND) {
    dhcp6_start_renew(netif, dhcp6);
  } else if ((dhcp6->state == DHCP6_STATE_STATEFUL_IDLE) || (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG) ||
             (dhcp6->state == DHCP6_STATE_RELEASE)) {
    dhcp6_start_solicit(netif, dhcp6);
  }
}

/**
 * NovaOS: the link went down (the lease goes: another network may be
 * there when it comes back, whose RA starts a Solicit) or up
 */
void
dhcp6_link_changed(struct netif *netif, u8_t up)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  if ((dhcp6 == NULL) || !dhcp6->stateful || up) {
    return;
  }
  dhcp6->managed = 0;
  dhcp6_drop_lease(netif, dhcp6);
  dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_link_changed");
}

/** NovaOS: whether address slot @idx holds the leased address (nd6.c leaves its lifetimes to DHCPv6) */
u8_t
dhcp6_owns_address(struct netif *netif, s8_t idx)
{
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  return (u8_t)((dhcp6 != NULL) && dhcp6->stateful && (idx >= 0) && (dhcp6->addr_idx == idx));
}
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

/** This function is called from nd6 module when an RA message is received
 * It triggers DHCPv6 requests (if enabled).
 */
void
dhcp6_nd6_ra_trigger(struct netif *netif, u8_t managed_addr_config, u8_t other_config)
{
  struct dhcp6 *dhcp6;

  LWIP_ASSERT("netif != NULL", netif != NULL);
  dhcp6 = netif_dhcp6_data(netif);

  LWIP_UNUSED_ARG(managed_addr_config);
  LWIP_UNUSED_ARG(other_config);
  LWIP_UNUSED_ARG(dhcp6);

#if LWIP_IPV6_DHCP6_STATEFUL
  /* NovaOS: M flag: a Solicit (unless a lease is held or being sought, or
   * it was released by hand); only the O flag: an Information-request */
  if ((dhcp6 != NULL) && dhcp6->stateful) {
    dhcp6->managed = (u8_t)(managed_addr_config != 0);
    if (managed_addr_config) {
      if (!dhcp6->hold && ((dhcp6->state == DHCP6_STATE_STATEFUL_IDLE) ||
                           (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG))) {
        dhcp6_start_solicit(netif, dhcp6);
      }
    } else if (other_config) {
      dhcp6_request_config(netif, dhcp6);
    }
    return;
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
#if LWIP_IPV6_DHCP6_STATELESS
  if (dhcp6 != NULL) {
    if (dhcp6_stateless_enabled(dhcp6)) {
      if (other_config) {
        dhcp6_request_config(netif, dhcp6);
      } else {
        dhcp6_abort_config_request(dhcp6);
      }
    }
  }
#endif /* LWIP_IPV6_DHCP6_STATELESS */
}

/**
 * Parse the DHCPv6 message and extract the DHCPv6 options.
 *
 * Extract the DHCPv6 options (offset + length) so that we can later easily
 * check for them or extract the contents.
 */
static err_t
dhcp6_parse_reply(struct pbuf *p, struct dhcp6 *dhcp6)
{
  u16_t offset;
  u16_t offset_max;
  u16_t options_idx;
  struct dhcp6_msg *msg_in;

  LWIP_UNUSED_ARG(dhcp6);

  /* clear received options */
  dhcp6_clear_all_options(dhcp6);
  msg_in = (struct dhcp6_msg *)p->payload;

  /* parse options */

  options_idx = sizeof(struct dhcp6_msg);
  /* parse options to the end of the received packet */
  offset_max = p->tot_len;

  offset = options_idx;
  /* at least 4 byte to read? */
  while ((offset + 4 <= offset_max)) {
    u8_t op_len_buf[4];
    u8_t *op_len;
    u16_t op;
    u16_t len;
    u16_t val_offset = (u16_t)(offset + 4);
    if (val_offset < offset) {
      /* overflow */
      return ERR_BUF;
    }
    /* copy option + length, might be split across pbufs */
    op_len = (u8_t *)pbuf_get_contiguous(p, op_len_buf, 4, 4, offset);
    if (op_len == NULL) {
      /* failed to get option and length */
      return ERR_VAL;
    }
    op = (op_len[0] << 8) | op_len[1];
    len = (op_len[2] << 8) | op_len[3];
    offset = val_offset + len;
    if (offset < val_offset) {
      /* overflow */
      return ERR_BUF;
    }

    switch (op) {
      case (DHCP6_OPTION_CLIENTID):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_CLI_ID);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_CLI_ID, val_offset, len);
        break;
      case (DHCP6_OPTION_SERVERID):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_SERVER_ID);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_SERVER_ID, val_offset, len);
        break;
#if LWIP_DHCP6_PROVIDE_DNS_SERVERS
      case (DHCP6_OPTION_DNS_SERVERS):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_DNS_SERVER);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_DNS_SERVER, val_offset, len);
        break;
      case (DHCP6_OPTION_DOMAIN_LIST):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_DOMAIN_LIST);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_DOMAIN_LIST, val_offset, len);
        break;
#endif /* LWIP_DHCP6_PROVIDE_DNS_SERVERS */
#if LWIP_DHCP6_GET_NTP_SRV
      case (DHCP6_OPTION_SNTP_SERVERS):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_NTP_SERVER);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_NTP_SERVER, val_offset, len);
        break;
#endif /* LWIP_DHCP6_GET_NTP_SRV*/
#if LWIP_IPV6_DHCP6_STATEFUL
      case (DHCP6_OPTION_IA_NA):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_IA_NA);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_IA_NA, val_offset, len);
        break;
      case (DHCP6_OPTION_STATUS_CODE):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_STATUS_CODE);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_STATUS_CODE, val_offset, len);
        break;
      case (DHCP6_OPTION_PREFERENCE):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_PREFERENCE);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_PREFERENCE, val_offset, len);
        break;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
      default:
        LWIP_DEBUGF(DHCP6_DEBUG, ("skipping option %"U16_F" in options\n", op));
        LWIP_HOOK_DHCP6_PARSE_OPTION(ip_current_netif(), dhcp6, dhcp6->state, msg_in,
          msg_in->msgtype, op, len, q, val_offset);
        break;
    }
  }
  return ERR_OK;
}

static void
dhcp6_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  struct netif *netif = ip_current_input_netif();
  struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
  struct dhcp6_msg *reply_msg = (struct dhcp6_msg *)p->payload;
  u8_t msg_type;
  u32_t xid;

  LWIP_UNUSED_ARG(arg);

  /* Caught DHCPv6 message from netif that does not have DHCPv6 enabled? -> not interested */
  if ((dhcp6 == NULL) || (dhcp6->pcb_allocated == 0)) {
    goto free_pbuf_and_return;
  }

  LWIP_ERROR("invalid server address type", IP_IS_V6(addr), goto free_pbuf_and_return;);

  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_recv(pbuf = %p) from DHCPv6 server %s port %"U16_F"\n", (void *)p,
    ipaddr_ntoa(addr), port));
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("pbuf->len = %"U16_F"\n", p->len));
  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("pbuf->tot_len = %"U16_F"\n", p->tot_len));
  /* prevent warnings about unused arguments */
  LWIP_UNUSED_ARG(pcb);
  LWIP_UNUSED_ARG(addr);
  LWIP_UNUSED_ARG(port);

  if (p->len < sizeof(struct dhcp6_msg)) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_LEVEL_WARNING, ("DHCPv6 reply message or pbuf too short\n"));
    goto free_pbuf_and_return;
  }

  /* match transaction ID against what we expected */
  xid = reply_msg->transaction_id[0] << 16;
  xid |= reply_msg->transaction_id[1] << 8;
  xid |= reply_msg->transaction_id[2];
  if (xid != dhcp6->xid) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_LEVEL_WARNING,
                ("transaction id mismatch reply_msg->xid(%"X32_F")!= dhcp6->xid(%"X32_F")\n", xid, dhcp6->xid));
    goto free_pbuf_and_return;
  }
  /* option fields could be unfold? */
  if (dhcp6_parse_reply(p, dhcp6) != ERR_OK) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_LEVEL_SERIOUS,
                ("problem unfolding DHCPv6 message - too short on memory?\n"));
    goto free_pbuf_and_return;
  }

  /* read DHCP message type */
  msg_type = reply_msg->msgtype;
  /* message type is DHCP6 REPLY? */
  if (msg_type == DHCP6_REPLY) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("DHCP6_REPLY received\n"));
#if LWIP_IPV6_DHCP6_STATELESS
    /* in info-requesting state? */
    if (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG) {
      dhcp6_set_state(dhcp6, DHCP6_IDLE_STATE(dhcp6), "dhcp6_recv");
      dhcp6_handle_config_reply(netif, p);
    } else
#endif /* LWIP_IPV6_DHCP6_STATELESS */
#if LWIP_IPV6_DHCP6_STATEFUL
    if (dhcp6->stateful) {
      dhcp6_handle_stateful_reply(netif, dhcp6, p, addr);   /* NovaOS */
    } else
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
    {
      /* @todo: handle reply in other states? */
    }
#if LWIP_IPV6_DHCP6_STATEFUL
  } else if ((msg_type == DHCP6_ADVERTISE) && dhcp6->stateful) {
    dhcp6_handle_advertise(netif, dhcp6, p, addr);         /* NovaOS */
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  } else {
    /* @todo: handle other message types */
  }

free_pbuf_and_return:
  pbuf_free(p);
}

/**
 * A DHCPv6 request has timed out.
 *
 * The timer that was started with the DHCPv6 request has
 * timed out, indicating no response was received in time.
 */
static void
dhcp6_timeout(struct netif *netif, struct dhcp6 *dhcp6)
{
  LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("dhcp6_timeout()\n"));

  LWIP_UNUSED_ARG(netif);
  LWIP_UNUSED_ARG(dhcp6);

#if LWIP_IPV6_DHCP6_STATELESS
  /* back-off period has passed, or server selection timed out */
  if (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG) {
    LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE, ("dhcp6_timeout(): retrying information request\n"));
    dhcp6_information_request(netif, dhcp6);
  }
#endif /* LWIP_IPV6_DHCP6_STATELESS */
#if LWIP_IPV6_DHCP6_STATEFUL
  if (dhcp6->stateful) {
    dhcp6_stateful_timeout(netif, dhcp6);
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
}

/**
 * DHCPv6 timeout handling (this function must be called every 500ms,
 * see @ref DHCP6_TIMER_MSECS).
 *
 * A DHCPv6 server is expected to respond within a short period of time.
 * This timer checks whether an outstanding DHCPv6 request is timed out.
 */
void
dhcp6_tmr(void)
{
  struct netif *netif;
  /* loop through netif's */
  NETIF_FOREACH(netif) {
    struct dhcp6 *dhcp6 = netif_dhcp6_data(netif);
    /* only act on DHCPv6 configured interfaces */
    if (dhcp6 != NULL) {
#if LWIP_IPV6_DHCP6_STATEFUL
      if (dhcp6->stateful) {
        dhcp6_lease_tick(netif, dhcp6);       /* NovaOS: T1, T2, the lease's end */
      }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
      /* timer is active (non zero), and is about to trigger now */
      if (dhcp6->request_timeout > 1) {
        dhcp6->request_timeout--;
      } else if (dhcp6->request_timeout == 1) {
        dhcp6->request_timeout--;
        /* { dhcp6->request_timeout == 0 } */
        LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE, ("dhcp6_tmr(): request timeout\n"));
        /* this client's request timeout triggered */
        dhcp6_timeout(netif, dhcp6);
      }
    }
  }
}

#endif /* LWIP_IPV6 && LWIP_IPV6_DHCP6 */
