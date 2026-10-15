/**
 * @file
 *
 * @defgroup dhcp6 DHCPv6
 * @ingroup ip6
 * DHCPv6 client: IPv6 address autoconfiguration as per
 * RFC 3315 (stateful DHCPv6) and
 * RFC 3736 (stateless DHCPv6).
 *
 * Stateful DHCPv6 supports IA_NA address assignment and lease renewal.
 *
 * TODO:
 * - enable/disable API to not always start when RA is received
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
#if LWIP_IPV6_DHCP6_STATEFUL && LWIP_IPV6_DHCP6_STATELESS
static void dhcp6_handle_config_reply(struct netif *netif, struct pbuf *p_msg_in);
#endif

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
#if LWIP_IPV6_DHCP6_STATEFUL
  dhcp6->address_idx = -1;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
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
#if LWIP_IPV6_DHCP6_STATEFUL
    dhcp6->address_idx = -1;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
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

static int
dhcp6_stateless_enabled(struct dhcp6 *dhcp6)
{
#if LWIP_IPV6_DHCP6_STATEFUL
  if (dhcp6->stateful_enabled) {
    return (offset == start + len);
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  if ((dhcp6->state == DHCP6_STATE_STATELESS_IDLE) ||
      (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG)) {
    return 1;
  }
  return 0;
}

#if LWIP_IPV6_DHCP6_STATEFUL
static int
dhcp6_stateful_enabled(struct dhcp6 *dhcp6)
{
  return dhcp6->stateful_enabled != 0;
}

static void
dhcp6_stateful_clear_lease(struct netif *netif, struct dhcp6 *dhcp6)
{
  if ((dhcp6->address_idx >= 0) &&
      (dhcp6->address_idx < LWIP_IPV6_NUM_ADDRESSES)) {
    s8_t idx = dhcp6->address_idx;
    netif_ip6_addr_set_state(netif, idx, IP6_ADDR_INVALID);
    netif_ip6_addr_set(netif, idx, IP6_ADDR_ANY6);
#if LWIP_IPV6_ADDRESS_LIFETIMES
    netif_ip6_addr_set_valid_life(netif, idx, IP6_ADDR_LIFE_STATIC);
    netif_ip6_addr_set_pref_life(netif, idx, IP6_ADDR_LIFE_STATIC);
#endif /* LWIP_IPV6_ADDRESS_LIFETIMES */
  }
  dhcp6->address_idx = -1;
  dhcp6->renew_time = 0;
  dhcp6->rebind_time = 0;
  dhcp6->valid_time = 0;
}
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

/**
 * @ingroup dhcp6
 * Enable stateful DHCPv6 on this netif
 * Requests are sent on receipt of an RA message with the
 * ND6_RA_FLAG_MANAGED_ADDR_CONFIG flag set.
 *
 * A struct dhcp6 will be allocated for this netif if not
 * set via @ref dhcp6_set_struct before.
 *
 */
err_t
dhcp6_enable_stateful(struct netif *netif)
{
#if LWIP_IPV6_DHCP6_STATEFUL
  struct dhcp6 *dhcp6;

  LWIP_DEBUGF(DHCP6_DEBUG | LWIP_DBG_TRACE | LWIP_DBG_STATE,
              ("dhcp6_enable_stateful(netif=%p)\n", (void *)netif));
  dhcp6 = dhcp6_get_struct(netif, "dhcp6_enable_stateful()");
  if (dhcp6 == NULL) {
    return ERR_MEM;
  }
  if (dhcp6_stateful_enabled(dhcp6)) {
    return ERR_OK;
  }
  dhcp6_stateful_clear_lease(netif, dhcp6);
  memset(dhcp6->server_id, 0, sizeof(dhcp6->server_id));
  dhcp6->server_id_len = 0;
  dhcp6->stateful_enabled = 1;
  dhcp6->address_idx = -1;
  dhcp6->renew_time = 0;
  dhcp6->rebind_time = 0;
  dhcp6->valid_time = 0;
  dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_enable_stateful");
  return ERR_OK;
#else
  LWIP_UNUSED_ARG(netif);
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
    dhcp6_stateful_clear_lease(netif, dhcp6);
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  }
#if LWIP_IPV6_DHCP6_STATEFUL
  dhcp6->stateful_enabled = 0;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
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
      if (dhcp6_stateful_enabled(dhcp6)) {
        dhcp6_stateful_clear_lease(netif, dhcp6);
        dhcp6->stateful_enabled = 0;
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

#if LWIP_IPV6_DHCP6_STATEFUL
static u16_t
dhcp6_option_long(u16_t options_out_len, u8_t *options, u32_t value)
{
  options[options_out_len++] = (u8_t)(value >> 24);
  options[options_out_len++] = (u8_t)(value >> 16);
  options[options_out_len++] = (u8_t)(value >> 8);
  options[options_out_len++] = (u8_t)value;
  return options_out_len;
}

static u16_t
dhcp6_option_add(u16_t options_out_len, u8_t *options, u16_t code,
                 const u8_t *value, u16_t len)
{
  options_out_len = dhcp6_option_short(options_out_len, options, code);
  options_out_len = dhcp6_option_short(options_out_len, options, len);
  if (len != 0) {
    MEMCPY(options + options_out_len, value, len);
    options_out_len = (u16_t)(options_out_len + len);
  }
  return options_out_len;
}

static void
dhcp6_stateful_schedule(struct dhcp6 *dhcp6)
{
  u16_t msecs;
  if (dhcp6->tries < 255) {
    dhcp6->tries++;
  }
  msecs = (u16_t)((dhcp6->tries < 6 ? 1U << dhcp6->tries : 60U) * 1000U);
  dhcp6->request_timeout = (u16_t)((msecs + DHCP6_TIMER_MSECS - 1) / DHCP6_TIMER_MSECS);
}

static void
dhcp6_stateful_send(struct netif *netif, struct dhcp6 *dhcp6)
{
  struct pbuf *p_out;
  struct dhcp6_msg *msg_out;
  u8_t *options;
  u8_t client_id[4 + NETIF_MAX_HWADDR_LEN];
  u8_t ia_na[40];
  u16_t client_id_len, ia_na_len, options_out_len;
  u16_t message_type;
  u32_t iaid;
  err_t err;

  switch (dhcp6->state) {
    case DHCP6_STATE_SOLICITING: message_type = DHCP6_SOLICIT; break;
    case DHCP6_STATE_REQUESTING: message_type = DHCP6_REQUEST; break;
    case DHCP6_STATE_RENEWING: message_type = DHCP6_RENEW; break;
    case DHCP6_STATE_REBINDING: message_type = DHCP6_REBIND; break;
    default: return;
  }

  client_id[0] = 0;
  client_id[1] = DHCP6_DUID_LL;
  client_id[2] = 0;
  client_id[3] = 1; /* Ethernet */
  client_id_len = (u16_t)(4 + netif->hwaddr_len);
  MEMCPY(client_id + 4, netif->hwaddr, netif->hwaddr_len);
  iaid = netif_get_index(netif);

  memset(ia_na, 0, sizeof(ia_na));
  ia_na[0] = (u8_t)(iaid >> 24);
  ia_na[1] = (u8_t)(iaid >> 16);
  ia_na[2] = (u8_t)(iaid >> 8);
  ia_na[3] = (u8_t)iaid;
  ia_na_len = 12;
  if ((message_type == DHCP6_REQUEST) ||
      (message_type == DHCP6_RENEW) ||
      (message_type == DHCP6_REBIND)) {
    const ip6_addr_t *address = (message_type == DHCP6_REQUEST) ?
      &dhcp6->offered_address :
      ((dhcp6->address_idx >= 0) ? netif_ip6_addr(netif, dhcp6->address_idx) : NULL);
    if (address != NULL) {
      ia_na[12] = (u8_t)(DHCP6_OPTION_IAADDR >> 8);
      ia_na[13] = (u8_t)DHCP6_OPTION_IAADDR;
      ia_na[14] = 0;
      ia_na[15] = 24;
      MEMCPY(ia_na + 16, address->addr, sizeof(address->addr));
      ia_na_len = 32;
      if (message_type == DHCP6_REQUEST) {
        ia_na_len = dhcp6_option_long(ia_na_len, ia_na, dhcp6->offered_preferred_time);
        ia_na_len = dhcp6_option_long(ia_na_len, ia_na, dhcp6->offered_valid_time);
      } else {
        ia_na_len = dhcp6_option_long(ia_na_len, ia_na,
                                      netif_ip6_addr_pref_life(netif, dhcp6->address_idx));
        ia_na_len = dhcp6_option_long(ia_na, ia_na, netif_ip6_addr_valid_life(netif, dhcp6->address_idx));
      }
    }
  }
  p_out = dhcp6_create_msg(netif, dhcp6, (u8_t)message_type, 256, &options_out_len);
  if (p_out == NULL) {
    dhcp6_stateful_schedule(dhcp6);
    return;
  }
  msg_out = (struct dhcp6_msg *)p_out->payload;
  options = (u8_t *)(msg_out + 1);
  options_out_len = dhcp6_option_add(options_out_len, options,
                                     DHCP6_OPTION_CLIENTID, client_id, client_id_len);
  if ((message_type == DHCP6_REQUEST) || (message_type == DHCP6_RENEW)) {
    options_out_len = dhcp6_option_add(options_out_len, options, DHCP6_OPTION_SERVERID,
                                       dhcp6->server_id, dhcp6->server_id_len);
  }
  options_out_len = dhcp6_option_add(options_out_len, options, DHCP6_OPTION_IA_NA,
                                     ia_na, ia_na_len);
#if LWIP_DHCP6_PROVIDE_DNS_SERVERS
  {
    const u16_t requested_options[] = { DHCP6_OPTION_DNS_SERVERS, DHCP6_OPTION_DOMAIN_LIST };
    options_out_len = dhcp6_option_optionrequest(options_out_len, options, requested_options,
      LWIP_ARRAYSIZE(requested_options), p_out->len);
  }
#endif /* LWIP_DHCP6_PROVIDE_DNS_SERVERS */
  LWIP_HOOK_DHCP6_APPEND_OPTIONS(netif, dhcp6, dhcp6->state, msg_out,
    (u8_t)message_type, options_out_len, p_out->len);
  dhcp6_msg_finalize(options_out_len, p_out);
  err = udp_sendto_if(dhcp6_pcb, p_out, &dhcp6_All_DHCP6_Relay_Agents_and_Servers,
                      DHCP6_SERVER_PORT, netif);
  pbuf_free(p_out);
  if (err == ERR_RTE) {
    dhcp6->request_timeout = 1;
    return;
  }
  dhcp6_stateful_schedule(dhcp6);
}

static u32_t
dhcp6_read_u32(const u8_t *p)
{
  return ((u32_t)p[0] << 24) | ((u32_t)p[1] << 16) | ((u32_t)p[2] << 8) | p[3];
}

static int
dhcp6_stateful_client_id_matches(struct netif *netif, struct dhcp6 *dhcp6, struct pbuf *p)
{
  u8_t client_id[4 + NETIF_MAX_HWADDR_LEN];
  u16_t len;
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_CLI_ID)) {
    return 0;
  }
  len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_CLI_ID);
  if (len != 4 + netif->hwaddr_len) {
    return 0;
  }
  client_id[0] = 0;
  client_id[1] = DHCP6_DUID_LL;
  client_id[2] = 0;
  client_id[3] = 1;
  MEMCPY(client_id + 4, netif->hwaddr, netif->hwaddr_len);
  {
    u8_t received[4 + NETIF_MAX_HWADDR_LEN];
    return (pbuf_copy_partial(p, received, len,
      dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_CLI_ID)) == len) &&
      (memcmp(client_id, received, len) == 0);
  }
}

static int
dhcp6_stateful_read_ia(struct netif *netif, struct pbuf *p, struct dhcp6 *dhcp6, int use_offer)
{
  u16_t start = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_IA_NA);
  u16_t len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_IA_NA);
  u8_t ia_header[12];
  u16_t offset;

  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_IA_NA) || (len < sizeof(ia_header)) ||
      (pbuf_copy_partial(p, ia_header, sizeof(ia_header), start) != sizeof(ia_header))) {
    return 0;
  }
  if (dhcp6_read_u32(ia_header) != netif_get_index(netif)) {
    return 0;
  }
  offset = (u16_t)(start + sizeof(ia_header));
  while (offset + 4U <= start + len) {
    u8_t header[4];
    u16_t code, option_len;
    if (pbuf_copy_partial(p, header, sizeof(header), offset) != sizeof(header)) {
      return 0;
    }
    code = (u16_t)(((u16_t)header[0] << 8) | header[1]);
    option_len = (u16_t)(((u16_t)header[2] << 8) | header[3]);
    offset = (u16_t)(offset + sizeof(header));
    if (option_len > start + len - offset) {
      return 0;
    }
    if ((code == DHCP6_OPTION_IAADDR) && (option_len >= 24)) {
      u8_t data[24];
      if (pbuf_copy_partial(p, data, sizeof(data), offset) != sizeof(data)) {
        return 0;
      }
      MEMCPY(dhcp6->offered_address.addr, data, sizeof(dhcp6->offered_address.addr));
      ip6_addr_clear_zone(&dhcp6->offered_address);
      dhcp6->offered_preferred_time = dhcp6_read_u32(data + 16);
      dhcp6->offered_valid_time = dhcp6_read_u32(data + 20);
      if ((dhcp6->offered_preferred_time > dhcp6->offered_valid_time) ||
          (ip6_addr_isany(&dhcp6->offered_address)) ||
          ip6_addr_ismulticast(&dhcp6->offered_address)) {
        return 0;
      }
      if (!use_offer && (dhcp6->offered_valid_time == 0)) {
        return 0;
      }
      return 1;
    }
    offset = (u16_t)(offset + option_len);
  }
  return 0;
}

static void
dhcp6_stateful_begin(struct netif *netif, struct dhcp6 *dhcp6)
{
  if (dhcp6->state == DHCP6_STATE_STATEFUL_IDLE) {
    dhcp6_set_state(dhcp6, DHCP6_STATE_SOLICITING, "dhcp6_stateful_begin");
    dhcp6_stateful_send(netif, dhcp6);
  }
}

static void
dhcp6_stateful_handle_advertise(struct netif *netif, struct dhcp6 *dhcp6, struct pbuf *p)
{
  u16_t id_len;
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_SERVER_ID)) {
    return;
  }
  id_len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_SERVER_ID);
  if ((id_len == 0) || (id_len > DHCP6_SERVER_ID_MAX) ||
      !dhcp6_stateful_client_id_matches(netif, dhcp6, p) ||
      !dhcp6_stateful_read_ia(netif, p, dhcp6, 1)) {
    return;
  }
  if (pbuf_copy_partial(p, dhcp6->server_id, id_len,
      dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_SERVER_ID)) != id_len) {
    return;
  }
  dhcp6->server_id_len = id_len;
  dhcp6_set_state(dhcp6, DHCP6_STATE_REQUESTING, "dhcp6_stateful_handle_advertise");
  dhcp6_stateful_send(netif, dhcp6);
}

static void
dhcp6_stateful_handle_reply(struct netif *netif, struct dhcp6 *dhcp6, struct pbuf *p)
{
  ip6_addr_t address;
  s8_t idx;
  u32_t valid, preferred, t1, t2;
  u8_t id[DHCP6_SERVER_ID_MAX];
  u16_t id_len;

  if ((dhcp6->state != DHCP6_STATE_REQUESTING) &&
      (dhcp6->state != DHCP6_STATE_RENEWING) &&
      (dhcp6->state != DHCP6_STATE_REBINDING)) {
    return;
  }
  if (!dhcp6_option_given(dhcp6, DHCP6_OPTION_IDX_SERVER_ID) ||
      !dhcp6_stateful_client_id_matches(netif, dhcp6, p) ||
      !dhcp6_stateful_read_ia(netif, p, dhcp6, 0)) {
    return;
  }
  id_len = dhcp6_get_option_length(dhcp6, DHCP6_OPTION_IDX_SERVER_ID);
  if ((id_len == 0) || (id_len > sizeof(id)) ||
      (pbuf_copy_partial(p, id, id_len,
       dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_SERVER_ID)) != id_len)) {
    return;
  }
  if (dhcp6->state != DHCP6_STATE_REBINDING &&
      ((id_len != dhcp6->server_id_len) || memcmp(id, dhcp6->server_id, id_len))) {
    return;
  }
  address = dhcp6->offered_address;
  valid = dhcp6->offered_valid_time;
  preferred = dhcp6->offered_preferred_time;
  idx = netif_get_ip6_addr_match(netif, &address);
  if ((dhcp6->address_idx >= 0) && (idx < 0)) {
    dhcp6_stateful_clear_lease(netif, dhcp6);
  }
  if (netif_add_ip6_address(netif, &address, &idx) != ERR_OK) {
    return;
  }
  dhcp6->address_idx = idx;
#if LWIP_IPV6_ADDRESS_LIFETIMES
  netif_ip6_addr_set_valid_life(netif, idx, valid);
  netif_ip6_addr_set_pref_life(netif, idx, preferred);
#endif /* LWIP_IPV6_ADDRESS_LIFETIMES */
  if (!ip6_addr_istentative(netif_ip6_addr_state(netif, idx))) {
    if (preferred == 0 && ip6_addr_ispreferred(netif_ip6_addr_state(netif, idx))) {
      netif_ip6_addr_set_state(netif, idx, IP6_ADDR_DEPRECATED);
    } else if (preferred > 0 && ip6_addr_isdeprecated(netif_ip6_addr_state(netif, idx))) {
      netif_ip6_addr_set_state(netif, idx, IP6_ADDR_PREFERRED);
    }
  }
  t1 = 0;
  t2 = 0;
  {
    u8_t ia[12];
    u16_t start = dhcp6_get_option_start(dhcp6, DHCP6_OPTION_IDX_IA_NA);
    if (pbuf_copy_partial(p, ia, sizeof(ia), start) == sizeof(ia)) {
      t1 = dhcp6_read_u32(ia + 4);
      t2 = dhcp6_read_u32(ia + 8);
    }
  }
  if (t1 == 0 || t1 > valid) t1 = (valid > 1) ? valid / 2 : valid;
  if (t2 == 0 || t2 > valid) t2 = (valid / 5) * 4;
  if (t2 < t1) t2 = t1;
  dhcp6->renew_time = t1;
  dhcp6->rebind_time = t2;
  dhcp6->valid_time = valid;
  dhcp6->second_tick = 0;
  if (id_len <= DHCP6_SERVER_ID_MAX) {
    MEMCPY(dhcp6->server_id, id, id_len);
    dhcp6->server_id_len = id_len;
  }
  dhcp6_set_state(dhcp6, DHCP6_STATE_BOUND, "dhcp6_stateful_handle_reply");
  dhcp6->request_timeout = 0;
#if LWIP_IPV6_DHCP6_STATELESS
  dhcp6_handle_config_reply(netif, p);
#endif /* LWIP_IPV6_DHCP6_STATELESS */
}
#endif /* LWIP_IPV6_DHCP6_STATEFUL */


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
  if ((dhcp6->state == DHCP6_STATE_STATELESS_IDLE)
#if LWIP_IPV6_DHCP6_STATEFUL
      || (dhcp6->stateful_enabled && (dhcp6->state == DHCP6_STATE_STATEFUL_IDLE))
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
     ) {
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
#if LWIP_IPV6_DHCP6_STATEFUL
    if (dhcp6->stateful_enabled) {
      dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_abort_config_request");
      return;
    }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
    dhcp6_set_state(dhcp6, DHCP6_STATE_STATELESS_IDLE, "dhcp6_abort_config_request");
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
    }
  }
  /* @ todo: parse and set Domain Search List */
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
#if LWIP_IPV6_DHCP6_STATEFUL
  if ((dhcp6 != NULL) && dhcp6_stateful_enabled(dhcp6) &&
      (dhcp6->state == DHCP6_STATE_STATEFUL_IDLE)) {
    if (managed_addr_config) {
      dhcp6_stateful_begin(netif, dhcp6);
    }
#if LWIP_IPV6_DHCP6_STATELESS
    else if (other_config) {
      dhcp6_request_config(netif, dhcp6);
    }
    else if (dhcp6->state == DHCP6_STATE_REQUESTING_CONFIG) {
      dhcp6_abort_config_request(dhcp6);
    }
#endif /* LWIP_IPV6_DHCP6_STATELESS */
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
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
    if ((val_offset > offset_max) || (len > offset_max - val_offset)) {
    return ERR_BUF;
    }
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
#if LWIP_IPV6_DHCP6_STATEFUL
      case (DHCP6_OPTION_IA_NA):
        dhcp6_got_option(dhcp6, DHCP6_OPTION_IDX_IA_NA);
        dhcp6_set_option(dhcp6, DHCP6_OPTION_IDX_IA_NA, val_offset, len);
        break;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
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
#if LWIP_IPV6_DHCP6_STATEFUL
      dhcp6_set_state(dhcp6, dhcp6->stateful_enabled ? DHCP6_STATE_STATEFUL_IDLE : DHCP6_STATE_STATELESS_IDLE,
                      "dhcp6_recv");
#else
      dhcp6_set_state(dhcp6, DHCP6_STATE_STATELESS_IDLE, "dhcp6_recv");
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
      dhcp6_handle_config_reply(netif, p);
    } else
#endif /* LWIP_IPV6_DHCP6_STATELESS */
    {
#if LWIP_IPV6_DHCP6_STATEFUL
      dhcp6_stateful_handle_reply(netif, dhcp6, p);
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
    }
#if LWIP_IPV6_DHCP6_STATEFUL
  } else if ((msg_type == DHCP6_ADVERTISE) &&
             (dhcp6->state == DHCP6_STATE_SOLICITING)) {
    dhcp6_stateful_handle_advertise(netif, dhcp6, p);
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
  } else {
    /* Ignore messages not applicable to the active exchange. */
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
  if ((dhcp6->state == DHCP6_STATE_SOLICITING) ||
      (dhcp6->state == DHCP6_STATE_REQUESTING) ||
      (dhcp6->state == DHCP6_STATE_RENEWING) ||
      (dhcp6->state == DHCP6_STATE_REBINDING)) {
    dhcp6_stateful_send(netif, dhcp6);
  }
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
}

#if LWIP_IPV6_DHCP6_STATEFUL
static void
dhcp6_stateful_tmr(struct netif *netif, struct dhcp6 *dhcp6)
{
  if (++dhcp6->second_tick < 2) {
    return;
  }
  dhcp6->second_tick = 0;
  if ((dhcp6->state != DHCP6_STATE_BOUND) &&
      (dhcp6->state != DHCP6_STATE_RENEWING) &&
      (dhcp6->state != DHCP6_STATE_REBINDING)) {
    return;
  }
  if (dhcp6->valid_time > 0) {
    dhcp6->valid_time--;
  }
  if (dhcp6->renew_time > 0) {
    dhcp6->renew_time--;
  }
  if (dhcp6->rebind_time > 0) {
    dhcp6->rebind_time--;
  }
  if (dhcp6->valid_time == 0) {
    dhcp6_stateful_clear_lease(netif, dhcp6);
    dhcp6_set_state(dhcp6, DHCP6_STATE_STATEFUL_IDLE, "dhcp6_stateful_tmr");
    dhcp6_stateful_begin(netif, dhcp6);
  } else if ((dhcp6->state == DHCP6_STATE_BOUND) && (dhcp6->renew_time == 0)) {
    dhcp6_set_state(dhcp6, DHCP6_STATE_RENEWING, "dhcp6_stateful_tmr");
    dhcp6_stateful_send(netif, dhcp6);
  } else if ((dhcp6->state == DHCP6_STATE_RENEWING) && (dhcp6->rebind_time == 0)) {
    dhcp6_set_state(dhcp6, DHCP6_STATE_REBINDING, "dhcp6_stateful_tmr");
    dhcp6_stateful_send(netif, dhcp6);
  }
}
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

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
      dhcp6_stateful_tmr(netif, dhcp6);
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
