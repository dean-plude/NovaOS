/**
 * @file
 *
 * DHCPv6 client: IPv6 address autoconfiguration as per
 * RFC 3315 (stateful DHCPv6) and
 * RFC 3736 (stateless DHCPv6).
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

#ifndef LWIP_HDR_IP6_DHCP6_H
#define LWIP_HDR_IP6_DHCP6_H

#include "lwip/opt.h"

#if LWIP_IPV6_DHCP6  /* don't build if not configured for use in lwipopts.h */

#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/prot/dhcp6.h"

#ifdef __cplusplus
extern "C" {
#endif

/** period (in milliseconds) of the application calling dhcp6_tmr() */
#define DHCP6_TIMER_MSECS   500

#if LWIP_IPV6_DHCP6_STATEFUL
#define DHCP6_DUID_MAX      20      /* (ours is a 10-byte DUID-LL) */
#define DHCP6_SERVER_ID_MAX 130     /* a DUID is at most 130 bytes (RFC 8415 section 11.1) */
#define DHCP6_DNS_KEPT      2
#define DHCP6_DOMAINS_MAX   128
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

struct dhcp6
{
  /** transaction identifier of last sent request */
  u32_t xid;
  /** track PCB allocation state */
  u8_t pcb_allocated;
  /** current DHCPv6 state machine state */
  u8_t state;
  /** retries of current request */
  u8_t tries;
  /** if request config is triggered while another action is active, this keeps track of it */
  u8_t request_config_pending;
  /** #ticks with period DHCP6_TIMER_MSECS for request timeout */
  u16_t request_timeout;
#if LWIP_IPV6_DHCP6_STATEFUL
  /* NovaOS: the stateful client's lease (one IA_NA with one address) */
  /** stateful DHCPv6 enabled (dhcp6_enable_stateful); stateless still answers RAs with only the O flag */
  u8_t stateful;
  /** released by hand (dhcp6_release): no Solicit on RAs until dhcp6_renew */
  u8_t hold;
  /** the last router advertisement had the M flag (a lost lease is solicited again at once) */
  u8_t managed;
  /** a Solicit's best Advertise so far is in server_id/addr */
  u8_t have_adv;
  /** its Preference option */
  u8_t adv_pref;
  /** this client's DUID (DUID-LL from the MAC) and the IA_NA's IAID */
  u8_t duid[DHCP6_DUID_MAX];
  u8_t duid_len;
  u32_t iaid;
  /** the server's DUID (Server Identifier option) and where its last message came from */
  u8_t server_id[DHCP6_SERVER_ID_MAX];
  u8_t server_id_len;
  ip6_addr_t server_addr;
  /** the leased (or advertised) address; addr_idx: its slot on the netif (-1: not on it) */
  ip6_addr_t addr;
  s8_t addr_idx;
  /** as the server granted them, in seconds (0xffffffff: infinite) */
  u32_t t1, t2, pref_life, valid_life;
  /** DHCP6_TIMER_MSECS ticks since the lease was granted or extended */
  u32_t lease_ticks;
  /** sys_now() at the first message of this exchange (Elapsed Time) */
  u32_t exchange_start;
  /** the retransmission timeout before randomization, ms (RFC 8415 section 15) */
  u32_t rt;
  /** what the server's last Reply named: DNS servers and the domain search list ("a.example,b.example") */
  ip6_addr_t dns[DHCP6_DNS_KEPT];
  u8_t dns_count;
  char domains[DHCP6_DOMAINS_MAX];
  /** events so far, for whoever logs them */
  u16_t binds, renews, rebinds, releases, declines, expiries;
#endif /* LWIP_IPV6_DHCP6_STATEFUL */
};

void dhcp6_set_struct(struct netif *netif, struct dhcp6 *dhcp6);
/** Remove a struct dhcp6 previously set to the netif using dhcp6_set_struct() */
#define dhcp6_remove_struct(netif) netif_set_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6, NULL)
void dhcp6_cleanup(struct netif *netif);

err_t dhcp6_enable_stateful(struct netif *netif);
err_t dhcp6_enable_stateless(struct netif *netif);
void dhcp6_disable(struct netif *netif);

void dhcp6_tmr(void);

#if LWIP_IPV6_DHCP6_STATEFUL
/* NovaOS: release the lease (a Release to the server; the address goes at
 * once) and hold off until dhcp6_renew; renew it now (a Renew when bound,
 * else a Solicit); the link went down (the lease goes, unsent) or up; and
 * whether netif address slot @idx is the leased one (SLAAC leaves it be) */
void dhcp6_release(struct netif *netif);
void dhcp6_renew(struct netif *netif);
void dhcp6_link_changed(struct netif *netif, u8_t up);
u8_t dhcp6_owns_address(struct netif *netif, s8_t idx);
/* the lease's state: whether an address is leased now */
#define dhcp6_bound(dhcp6) ((dhcp6) != NULL && (dhcp6)->addr_idx >= 0 && \
  ((dhcp6)->state == DHCP6_STATE_BOUND || (dhcp6)->state == DHCP6_STATE_RENEW || (dhcp6)->state == DHCP6_STATE_REBIND))
#endif /* LWIP_IPV6_DHCP6_STATEFUL */

void dhcp6_nd6_ra_trigger(struct netif *netif, u8_t managed_addr_config, u8_t other_config);

#if LWIP_DHCP6_GET_NTP_SRV
/** This function must exist, in other to add offered NTP servers to
 * the NTP (or SNTP) engine.
 * See LWIP_DHCP6_MAX_NTP_SERVERS */
extern void dhcp6_set_ntp_servers(u8_t num_ntp_servers, const ip_addr_t* ntp_server_addrs);
#endif /* LWIP_DHCP6_GET_NTP_SRV */

#define netif_dhcp6_data(netif) ((struct dhcp6*)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP6))

#ifdef __cplusplus
}
#endif

#endif /* LWIP_IPV6_DHCP6 */

#endif /* LWIP_HDR_IP6_DHCP6_H */
