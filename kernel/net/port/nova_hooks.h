/*
 * nova_hooks.h — lwIP's hooks into NovaOS (LWIP_HOOK_FILENAME): every
 * IPv4 and IPv6 packet that arrives passes the filtering engine's inbound
 * layers first (kernel/net/sock.c; non-zero: the packet was dropped)
 */
#pragma once
struct pbuf;
struct netif;
int nova_ip4_input_hook(struct pbuf *p, struct netif *inp);
int nova_ip6_input_hook(struct pbuf *p, struct netif *inp);
