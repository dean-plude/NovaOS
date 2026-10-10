/* pnptest.exe — the Plug and Play device tree (kernel/io/pnp.h, novapnp.h)
 *
 *   pnptest list                  every device node: the tree must hang
 *                                 together (one root, each present node's
 *                                 parent present, instance IDs unique)
 *   pnptest arrival PREFIX [ms]   wait (from the first event since boot)
 *   pnptest removal PREFIX [ms]   for a device whose instance ID starts
 *                                 with PREFIX to arrive, or to leave; it
 *                                 must then be present, or a phantom
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <novapnp.h>

static int g_failed;

static void fail(const char *what, const char *inst)
{
    printf("FAIL: %s: %s\n", what, inst);
    g_failed++;
}

static void show(const char *what, const NovaPnpNode *n)
{
    printf("%s%s [%s] %s (%s)%s%s\n", what, n->instance, n->cls[0] ? n->cls : "no class", n->desc,
           n->service[0] ? n->service : "no driver", n->friendly[0] ? " \"" : "", n->friendly[0] ? n->friendly : "");
}

static int list(void)
{
    DWORD count = nova_pnp_count();
    static NovaPnpNode node[1024];
    int present = 0, phantoms = 0;
    if (count > 1024) count = 1024;
    for (DWORD id = 1; id <= count; id++) {
        NovaPnpNode *n = &node[id - 1];
        if (!nova_pnp_node(id, n)) { n->id = 0; continue; }
        if (n->id != id) fail("wrong ID", n->instance);
    }
    if (!count || !(node[0].flags & NOVA_PNP_PRESENT) || strcmp(node[0].instance, "HTREE\\ROOT\\0"))
        fail("no root", count ? node[0].instance : "(none)");
    for (DWORD id = 1; id <= count; id++) {
        NovaPnpNode *n = &node[id - 1];
        if (!n->id) continue;
        if (!(n->flags & NOVA_PNP_PRESENT)) { phantoms++; continue; }
        present++;
        int depth = 0;
        for (DWORD p = n->parent; p && depth < 16; p = node[p - 1].parent) depth++;
        printf("%*s", depth * 2, "");
        show("", n);
        if (id != NOVA_PNP_ROOT && (!n->parent || n->parent > count || !(node[n->parent - 1].flags & NOVA_PNP_PRESENT)))
            fail("parent not present", n->instance);
        if (!n->hwids[0] && id != NOVA_PNP_ROOT && strncmp(n->instance, "USB\\ROOT_HUB", 12)) fail("no hardware ID", n->instance);
        for (DWORD k = id + 1; k <= count; k++)
            if (node[k - 1].id && !_stricmp(node[k - 1].instance, n->instance)) fail("instance ID twice", n->instance);
    }
    printf("pnptest: %d devices present, %d not present, %d failed\n", present, phantoms, g_failed);
    return g_failed ? 1 : 0;
}

static int wait_for(DWORD kind, const char *prefix, DWORD ms)
{
    NovaPnpRead r;
    memset(&r, 0, sizeof(r));
    DWORD start = GetTickCount();
    size_t pl = strlen(prefix);
    for (;;) {
        r.max = 32;
        r.wait_ms = 500;
        int n = nova_pnp_events(&r);
        for (int i = 0; i < n; i++) {
            NovaPnpNode node;
            if (!nova_pnp_node(r.e[i].id, &node)) continue;
            if (r.e[i].kind != kind) continue;
            show(kind == NOVA_PNP_ARRIVAL ? "arrival: " : "removal: ", &node);
            if (_strnicmp(node.instance, prefix, pl)) continue;
            BOOL present = (node.flags & NOVA_PNP_PRESENT) != 0;
            if (present != (kind == NOVA_PNP_ARRIVAL)) {
                /* (it came back or left again since: look for that event) */
                continue;
            }
            printf("pnptest: %s seen, 0 failed\n", kind == NOVA_PNP_ARRIVAL ? "arrival" : "removal");
            return 0;
        }
        if (n) r.after = r.e[n - 1].seq;
        if (GetTickCount() - start > ms) break;
    }
    printf("FAIL: no %s of %s in %lu ms\n", kind == NOVA_PNP_ARRIVAL ? "arrival" : "removal", prefix, (unsigned long)ms);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "list")) return list();
    if (argc >= 3 && (!strcmp(argv[1], "arrival") || !strcmp(argv[1], "removal")))
        return wait_for(!strcmp(argv[1], "arrival") ? NOVA_PNP_ARRIVAL : NOVA_PNP_REMOVAL, argv[2],
                        argc >= 4 ? (DWORD)atoi(argv[3]) : 15000);
    printf("usage: pnptest list | arrival PREFIX [ms] | removal PREFIX [ms]\n");
    return 2;
}
