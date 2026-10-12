/*
 * se.c — security descriptors, ACLs and SIDs for drivers
 *
 * The security run-time library a driver uses to build the descriptor it
 * puts on a device object (ftdibus.sys builds one for its bus device):
 * RtlCreateSecurityDescriptor / RtlCreateAcl / RtlAddAccessAllowedAce /
 * RtlSetDaclSecurityDescriptor, the getters that read either format,
 * RtlAbsoluteToSelfRelativeSD and SeCaptureSecurityDescriptor, which hand
 * back the self-relative form, and SeExports, the table of well-known
 * privilege LUIDs and SIDs a driver takes its SIDs from.
 *
 * The structures are Windows' x64 ones byte for byte: an absolute
 * SECURITY_DESCRIPTOR is 40 bytes with pointers, a self-relative one 20
 * bytes with ULONG offsets from its start in the same four slots (Owner,
 * Group, Sacl, Dacl), the parts it points at following it, each padded to
 * a ULONG.  NovaOS does not check access against these descriptors yet;
 * it builds and copies them so a driver's own bookkeeping holds.
 */
#include "nt.h"
#include "ntapi.h"
#include "../lib/string.h"

/* Status codes this file returns beyond the ones wdm.h has */
#ifndef NT_STATUS_UNKNOWN_REVISION
#define NT_STATUS_UNKNOWN_REVISION         ((NT_STATUS)0xC0000058)
#endif
#ifndef NT_STATUS_REVISION_MISMATCH
#define NT_STATUS_REVISION_MISMATCH        ((NT_STATUS)0xC0000059)
#endif
#ifndef NT_STATUS_INVALID_ACL
#define NT_STATUS_INVALID_ACL              ((NT_STATUS)0xC0000077)
#endif
#ifndef NT_STATUS_INVALID_SID
#define NT_STATUS_INVALID_SID              ((NT_STATUS)0xC0000078)
#endif
#ifndef NT_STATUS_INVALID_SECURITY_DESCR
#define NT_STATUS_INVALID_SECURITY_DESCR   ((NT_STATUS)0xC0000079)
#endif
#ifndef NT_STATUS_ALLOTTED_SPACE_EXCEEDED
#define NT_STATUS_ALLOTTED_SPACE_EXCEEDED  ((NT_STATUS)0xC0000099)
#endif
#ifndef NT_STATUS_BAD_DESCRIPTOR_FORMAT
#define NT_STATUS_BAD_DESCRIPTOR_FORMAT    ((NT_STATUS)0xC00000E7)
#endif

/* -----------------------------------------------------------------------
 * The structures, in Windows' x64 layout
 * ----------------------------------------------------------------------- */

/* SID: S-1-<IdentifierAuthority>-<SubAuthority>... */
typedef struct _NT_SID {
    UINT8  Revision;               /* SID_REVISION (1) */
    UINT8  SubAuthorityCount;      /* at most SID_MAX_SUB_AUTHORITIES (15) */
    UINT8  IdentifierAuthority[6]; /* big-endian 48-bit value */
    UINT32 SubAuthority[1];        /* SubAuthorityCount of them */
} NT_SID;
#define NT_SID_REVISION            1
#define NT_SID_MAX_SUB_AUTHORITIES 15

/* ACL: the header; the ACEs follow it, AceCount of them, each AceSize long */
typedef struct _NT_ACL {
    UINT8  AclRevision;            /* ACL_REVISION (2) or ACL_REVISION_DS (4) */
    UINT8  Sbz1;
    UINT16 AclSize;                /* the whole buffer, header included */
    UINT16 AceCount;
    UINT16 Sbz2;
} NT_ACL;
#define NT_MIN_ACL_REVISION 2
#define NT_MAX_ACL_REVISION 4

typedef struct _NT_ACE_HEADER {
    UINT8  AceType;
    UINT8  AceFlags;
    UINT16 AceSize;
} NT_ACE_HEADER;

typedef struct _NT_ACCESS_ALLOWED_ACE {
    NT_ACE_HEADER Header;
    UINT32        Mask;            /* ACCESS_MASK */
    UINT32        SidStart;        /* the SID starts here */
} NT_ACCESS_ALLOWED_ACE;
#define NT_ACCESS_ALLOWED_ACE_TYPE 0

/* SECURITY_DESCRIPTOR (absolute) and SECURITY_DESCRIPTOR_RELATIVE */
typedef struct _NT_SECURITY_DESCRIPTOR {
    UINT8   Revision;
    UINT8   Sbz1;
    UINT16  Control;
    NT_SID *Owner;
    NT_SID *Group;
    NT_ACL *Sacl;
    NT_ACL *Dacl;
} NT_SECURITY_DESCRIPTOR;

typedef struct _NT_SECURITY_DESCRIPTOR_RELATIVE {
    UINT8   Revision;
    UINT8   Sbz1;
    UINT16  Control;
    UINT32  Owner;
    UINT32  Group;
    UINT32  Sacl;
    UINT32  Dacl;
} NT_SECURITY_DESCRIPTOR_RELATIVE;

#define NT_SECURITY_DESCRIPTOR_REVISION 1
#define NT_SE_OWNER_DEFAULTED  0x0001
#define NT_SE_GROUP_DEFAULTED  0x0002
#define NT_SE_DACL_PRESENT     0x0004
#define NT_SE_DACL_DEFAULTED   0x0008
#define NT_SE_SACL_PRESENT     0x0010
#define NT_SE_SACL_DEFAULTED   0x0020
#define NT_SE_SELF_RELATIVE    0x8000

/* LUID: a privilege value in SeExports */
typedef struct _NT_LUID {
    UINT32 LowPart;
    INT32  HighPart;
} NT_LUID;

_Static_assert(sizeof(NT_SECURITY_DESCRIPTOR) == 40 && NT_OFFSET(NT_SECURITY_DESCRIPTOR, Owner) == 8 &&
               NT_OFFSET(NT_SECURITY_DESCRIPTOR, Dacl) == 32, "SECURITY_DESCRIPTOR");
_Static_assert(sizeof(NT_SECURITY_DESCRIPTOR_RELATIVE) == 20 && NT_OFFSET(NT_SECURITY_DESCRIPTOR_RELATIVE, Dacl) == 16,
               "SECURITY_DESCRIPTOR_RELATIVE");
_Static_assert(sizeof(NT_ACL) == 8 && sizeof(NT_ACE_HEADER) == 4 && NT_OFFSET(NT_ACCESS_ALLOWED_ACE, SidStart) == 8,
               "ACL, ACE");
_Static_assert(sizeof(NT_SID) == 12 && sizeof(NT_LUID) == 8, "SID, LUID");

#define LONG_ALIGN(n) (((n) + 3u) & ~3u)

/* -----------------------------------------------------------------------
 * SIDs
 * ----------------------------------------------------------------------- */

NT_API UINT32 nt_RtlLengthSid(void *Sid)
{
    return 8u + 4u * ((NT_SID *)Sid)->SubAuthorityCount;
}

static bool sid_valid(const NT_SID *s)
{
    return s && s->Revision == NT_SID_REVISION && s->SubAuthorityCount <= NT_SID_MAX_SUB_AUTHORITIES;
}

/* -----------------------------------------------------------------------
 * ACLs
 * ----------------------------------------------------------------------- */

NT_API NT_STATUS nt_RtlCreateAcl(NT_ACL *Acl, UINT32 AclLength, UINT32 AclRevision)
{
    if (AclLength < sizeof(NT_ACL)) return NT_STATUS_BUFFER_TOO_SMALL;
    if (AclRevision < NT_MIN_ACL_REVISION || AclRevision > NT_MAX_ACL_REVISION || AclLength > 0xFFFF)
        return NT_STATUS_INVALID_PARAMETER;
    Acl->AclRevision = (UINT8)AclRevision;
    Acl->Sbz1 = 0;
    Acl->AclSize = (UINT16)(AclLength & ~3u);   /* ACEs are ULONG-aligned; never claim past the buffer */
    Acl->AceCount = 0;
    Acl->Sbz2 = 0;
    return NT_STATUS_SUCCESS;
}

static bool acl_valid(const NT_ACL *a)
{
    return a && a->AclRevision >= NT_MIN_ACL_REVISION && a->AclRevision <= NT_MAX_ACL_REVISION &&
           a->AclSize >= sizeof(NT_ACL);
}

/* The first byte past the ACL's ACEs, or NULL if they overrun AclSize */
static UINT8 *acl_first_free(NT_ACL *a)
{
    UINT8 *p = (UINT8 *)(a + 1), *end = (UINT8 *)a + a->AclSize;
    for (UINT32 i = 0; i < a->AceCount; i++) {
        if (p + sizeof(NT_ACE_HEADER) > end) return NULL;
        UINT16 size = ((NT_ACE_HEADER *)p)->AceSize;
        if (size < sizeof(NT_ACE_HEADER) || p + size > end) return NULL;
        p += size;
    }
    return p;
}

NT_API NT_STATUS nt_RtlAddAccessAllowedAce(NT_ACL *Acl, UINT32 AceRevision, UINT32 AccessMask, void *Sid)
{
    NT_SID *sid = Sid;
    if (!sid_valid(sid)) return NT_STATUS_INVALID_SID;
    if (!acl_valid(Acl)) return NT_STATUS_INVALID_ACL;
    if (AceRevision < NT_MIN_ACL_REVISION || AceRevision > NT_MAX_ACL_REVISION)
        return NT_STATUS_REVISION_MISMATCH;
    UINT8 *at = acl_first_free(Acl);
    if (!at) return NT_STATUS_INVALID_ACL;

    UINT32 sid_len = nt_RtlLengthSid(sid);
    UINT32 ace_len = NT_OFFSET(NT_ACCESS_ALLOWED_ACE, SidStart) + sid_len;
    if ((UINT64)(at - (UINT8 *)Acl) + ace_len > Acl->AclSize) return NT_STATUS_ALLOTTED_SPACE_EXCEEDED;

    NT_ACCESS_ALLOWED_ACE *ace = (NT_ACCESS_ALLOWED_ACE *)at;
    ace->Header.AceType = NT_ACCESS_ALLOWED_ACE_TYPE;
    ace->Header.AceFlags = 0;
    ace->Header.AceSize = (UINT16)ace_len;
    ace->Mask = AccessMask;
    memcpy(&ace->SidStart, sid, sid_len);
    Acl->AceCount++;
    if (AceRevision > Acl->AclRevision) Acl->AclRevision = (UINT8)AceRevision;
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Security descriptors
 * ----------------------------------------------------------------------- */

/* The four parts of a descriptor in either format (NULL where absent) */
static void *sd_part(const void *sd, int slot)          /* 0 Owner, 1 Group, 2 Sacl, 3 Dacl */
{
    const NT_SECURITY_DESCRIPTOR *a = sd;
    if (a->Control & NT_SE_SELF_RELATIVE) {
        const NT_SECURITY_DESCRIPTOR_RELATIVE *r = sd;
        UINT32 off = slot == 0 ? r->Owner : slot == 1 ? r->Group : slot == 2 ? r->Sacl : r->Dacl;
        return off ? (UINT8 *)sd + off : NULL;
    }
    return slot == 0 ? (void *)a->Owner : slot == 1 ? (void *)a->Group : slot == 2 ? (void *)a->Sacl : (void *)a->Dacl;
}
static NT_SID *sd_owner(const void *sd) { return sd_part(sd, 0); }
static NT_SID *sd_group(const void *sd) { return sd_part(sd, 1); }
static NT_ACL *sd_sacl(const void *sd)
{
    return (((const NT_SECURITY_DESCRIPTOR *)sd)->Control & NT_SE_SACL_PRESENT) ? sd_part(sd, 2) : NULL;
}
static NT_ACL *sd_dacl(const void *sd)
{
    return (((const NT_SECURITY_DESCRIPTOR *)sd)->Control & NT_SE_DACL_PRESENT) ? sd_part(sd, 3) : NULL;
}

/* The length of @sd's parts, each padded to a ULONG */
static UINT32 sd_parts_length(const void *sd)
{
    UINT32 n = 0;
    NT_SID *o = sd_owner(sd), *g = sd_group(sd);
    NT_ACL *s = sd_sacl(sd), *d = sd_dacl(sd);
    if (o) n += LONG_ALIGN(nt_RtlLengthSid(o));
    if (g) n += LONG_ALIGN(nt_RtlLengthSid(g));
    if (s) n += LONG_ALIGN((UINT32)s->AclSize);
    if (d) n += LONG_ALIGN((UINT32)d->AclSize);
    return n;
}

/* The self-relative copy of @sd (either format) is this long */
static UINT32 sd_relative_length(const void *sd)
{
    return (UINT32)sizeof(NT_SECURITY_DESCRIPTOR_RELATIVE) + sd_parts_length(sd);
}

/* Write @sd (either format) to @out as a self-relative descriptor of
 * sd_relative_length(sd) bytes: the header, then the SACL, the DACL, the
 * owner and the group, as Windows lays them out */
static void sd_make_relative(const void *sd, void *out)
{
    const NT_SECURITY_DESCRIPTOR *src = sd;
    NT_SECURITY_DESCRIPTOR_RELATIVE *r = out;
    NT_SID *o = sd_owner(sd), *g = sd_group(sd);
    NT_ACL *s = sd_sacl(sd), *d = sd_dacl(sd);
    UINT32 at = sizeof(*r);

    r->Revision = src->Revision;
    r->Sbz1 = src->Sbz1;
    r->Control = (UINT16)(src->Control | NT_SE_SELF_RELATIVE);
    r->Owner = r->Group = r->Sacl = r->Dacl = 0;
#define PUT(field, ptr, len) do { \
        UINT32 l_ = (len);                                   \
        memcpy((UINT8 *)out + at, (ptr), l_);                \
        if (LONG_ALIGN(l_) != l_)                            \
            memset((UINT8 *)out + at + l_, 0, LONG_ALIGN(l_) - l_); \
        r->field = at; at += LONG_ALIGN(l_);                 \
    } while (0)
    if (s) PUT(Sacl, s, s->AclSize);
    if (d) PUT(Dacl, d, d->AclSize);
    if (o) PUT(Owner, o, nt_RtlLengthSid(o));
    if (g) PUT(Group, g, nt_RtlLengthSid(g));
#undef PUT
}

NT_API NT_STATUS nt_RtlCreateSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, UINT32 Revision)
{
    if (Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    memset(SecurityDescriptor, 0, sizeof(*SecurityDescriptor));
    SecurityDescriptor->Revision = (UINT8)Revision;
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_RtlSetDaclSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, UINT8 DaclPresent,
                                                 NT_ACL *Dacl, UINT8 DaclDefaulted)
{
    NT_SECURITY_DESCRIPTOR *sd = SecurityDescriptor;
    if (sd->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    if (sd->Control & NT_SE_SELF_RELATIVE) return NT_STATUS_INVALID_SECURITY_DESCR;
    if (!DaclPresent) {
        sd->Control &= (UINT16)~NT_SE_DACL_PRESENT;
        return NT_STATUS_SUCCESS;
    }
    sd->Control |= NT_SE_DACL_PRESENT;
    sd->Dacl = Dacl;
    if (DaclDefaulted) sd->Control |= NT_SE_DACL_DEFAULTED;
    else               sd->Control &= (UINT16)~NT_SE_DACL_DEFAULTED;
    return NT_STATUS_SUCCESS;
}

/* Rtl{Get}{Dacl,Sacl}SecurityDescriptor: *Present always; *Acl and
 * *Defaulted only when the ACL is present, as Windows leaves them */
static NT_STATUS get_acl(const NT_SECURITY_DESCRIPTOR *sd, UINT16 present, UINT16 defaulted, int slot,
                         UINT8 *Present, NT_ACL **Acl, UINT8 *Defaulted)
{
    if (sd->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    *Present = (sd->Control & present) ? 1 : 0;
    if (*Present) {
        *Acl = sd_part(sd, slot);
        *Defaulted = (sd->Control & defaulted) ? 1 : 0;
    }
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_RtlGetDaclSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, UINT8 *DaclPresent,
                                                 NT_ACL **Dacl, UINT8 *DaclDefaulted)
{
    return get_acl(SecurityDescriptor, NT_SE_DACL_PRESENT, NT_SE_DACL_DEFAULTED, 3, DaclPresent, Dacl, DaclDefaulted);
}

NT_API NT_STATUS nt_RtlGetSaclSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, UINT8 *SaclPresent,
                                                 NT_ACL **Sacl, UINT8 *SaclDefaulted)
{
    return get_acl(SecurityDescriptor, NT_SE_SACL_PRESENT, NT_SE_SACL_DEFAULTED, 2, SaclPresent, Sacl, SaclDefaulted);
}

NT_API NT_STATUS nt_RtlGetOwnerSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, void **Owner,
                                                  UINT8 *OwnerDefaulted)
{
    if (SecurityDescriptor->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    *Owner = sd_owner(SecurityDescriptor);
    *OwnerDefaulted = (SecurityDescriptor->Control & NT_SE_OWNER_DEFAULTED) ? 1 : 0;
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_RtlGetGroupSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor, void **Group,
                                                  UINT8 *GroupDefaulted)
{
    if (SecurityDescriptor->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    *Group = sd_group(SecurityDescriptor);
    *GroupDefaulted = (SecurityDescriptor->Control & NT_SE_GROUP_DEFAULTED) ? 1 : 0;
    return NT_STATUS_SUCCESS;
}

/* RtlLengthSecurityDescriptor: the header of the descriptor's own format
 * (20 bytes self-relative, 40 absolute, as Windows counts it) plus each
 * present part padded to a ULONG.  For a self-relative descriptor this is
 * its exact length. */
NT_API UINT32 nt_RtlLengthSecurityDescriptor(NT_SECURITY_DESCRIPTOR *SecurityDescriptor)
{
    UINT32 hdr = (SecurityDescriptor->Control & NT_SE_SELF_RELATIVE) ? sizeof(NT_SECURITY_DESCRIPTOR_RELATIVE)
                                                                       : sizeof(NT_SECURITY_DESCRIPTOR);
    return hdr + sd_parts_length(SecurityDescriptor);
}

NT_API NT_STATUS nt_RtlAbsoluteToSelfRelativeSD(NT_SECURITY_DESCRIPTOR *AbsoluteSecurityDescriptor,
                                                void *SelfRelativeSecurityDescriptor, UINT32 *BufferLength)
{
    NT_SECURITY_DESCRIPTOR *sd = AbsoluteSecurityDescriptor;
    if (sd->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;
    if (sd->Control & NT_SE_SELF_RELATIVE) return NT_STATUS_BAD_DESCRIPTOR_FORMAT;
    UINT32 need = sd_relative_length(sd);
    if (*BufferLength < need) {
        *BufferLength = need;
        return NT_STATUS_BUFFER_TOO_SMALL;
    }
    sd_make_relative(sd, SelfRelativeSecurityDescriptor);
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Capturing a descriptor
 * ----------------------------------------------------------------------- */

#define NT_KERNEL_MODE  0
#define SE_CAPTURE_TAG  0x63536553u   /* 'SeSc' */

/* SeCaptureSecurityDescriptor: a self-relative copy in pool the caller
 * releases (SeReleaseSecurityDescriptor or ExFreePool); a kernel-mode
 * caller that does not force the capture gets its own pointer back. */
NT_API NT_STATUS nt_SeCaptureSecurityDescriptor(NT_SECURITY_DESCRIPTOR *InputSecurityDescriptor, char RequestorMode,
                                                int PoolType, UINT8 ForceCapture,
                                                NT_SECURITY_DESCRIPTOR **OutputSecurityDescriptor)
{
    NT_SECURITY_DESCRIPTOR *in = InputSecurityDescriptor;
    if (!in) {
        *OutputSecurityDescriptor = NULL;
        return NT_STATUS_SUCCESS;
    }
    if (RequestorMode == NT_KERNEL_MODE && !ForceCapture) {
        *OutputSecurityDescriptor = in;
        return NT_STATUS_SUCCESS;
    }
    if (in->Revision != NT_SECURITY_DESCRIPTOR_REVISION) return NT_STATUS_UNKNOWN_REVISION;

    NT_SID *o = sd_owner(in), *g = sd_group(in);
    NT_ACL *s = sd_sacl(in), *d = sd_dacl(in);
    if ((o && !sid_valid(o)) || (g && !sid_valid(g))) return NT_STATUS_INVALID_SID;
    if ((s && !acl_valid(s)) || (d && !acl_valid(d))) return NT_STATUS_INVALID_ACL;

    UINT32 len = sd_relative_length(in);
    void *out = nt_ExAllocatePoolWithTag(PoolType, len, SE_CAPTURE_TAG);
    if (!out) return NT_STATUS_INSUFFICIENT_RESOURCES;
    sd_make_relative(in, out);
    *OutputSecurityDescriptor = out;
    return NT_STATUS_SUCCESS;
}

/* SeReleaseSecurityDescriptor: frees what SeCaptureSecurityDescriptor
 * allocated (nothing when it returned the caller's own pointer) */
NT_API void nt_SeReleaseSecurityDescriptor(NT_SECURITY_DESCRIPTOR *CapturedSecurityDescriptor, char RequestorMode,
                                           UINT8 ForceCapture)
{
    if (CapturedSecurityDescriptor && (RequestorMode != NT_KERNEL_MODE || ForceCapture))
        nt_ExFreePool(CapturedSecurityDescriptor);
}

/* -----------------------------------------------------------------------
 * SeExports: the well-known privileges and SIDs, a data export holding a
 * pointer to an SE_EXPORTS laid out as the WDK's ntddk.h has it
 * ----------------------------------------------------------------------- */

typedef struct _NT_SE_EXPORTS {
    /* Privilege values */
    NT_LUID SeCreateTokenPrivilege;
    NT_LUID SeAssignPrimaryTokenPrivilege;
    NT_LUID SeLockMemoryPrivilege;
    NT_LUID SeIncreaseQuotaPrivilege;
    NT_LUID SeUnsolicitedInputPrivilege;
    NT_LUID SeTcbPrivilege;
    NT_LUID SeSecurityPrivilege;
    NT_LUID SeTakeOwnershipPrivilege;
    NT_LUID SeLoadDriverPrivilege;
    NT_LUID SeCreatePagefilePrivilege;
    NT_LUID SeIncreaseBasePriorityPrivilege;
    NT_LUID SeSystemProfilePrivilege;
    NT_LUID SeSystemtimePrivilege;
    NT_LUID SeProfileSingleProcessPrivilege;
    NT_LUID SeCreatePermanentPrivilege;
    NT_LUID SeBackupPrivilege;
    NT_LUID SeRestorePrivilege;
    NT_LUID SeShutdownPrivilege;
    NT_LUID SeDebugPrivilege;
    NT_LUID SeAuditPrivilege;
    NT_LUID SeSystemEnvironmentPrivilege;
    NT_LUID SeChangeNotifyPrivilege;
    NT_LUID SeRemoteShutdownPrivilege;
    /* Universally defined SIDs */
    void *SeNullSid, *SeWorldSid, *SeLocalSid, *SeCreatorOwnerSid, *SeCreatorGroupSid;
    /* NT defined SIDs */
    void *SeNtAuthoritySid, *SeDialupSid, *SeNetworkSid, *SeBatchSid, *SeInteractiveSid, *SeLocalSystemSid;
    void *SeAliasAdminsSid, *SeAliasUsersSid, *SeAliasGuestsSid, *SeAliasPowerUsersSid, *SeAliasAccountOpsSid;
    void *SeAliasSystemOpsSid, *SeAliasPrintOpsSid, *SeAliasBackupOpsSid;
    /* NT5 SIDs */
    void *SeAuthenticatedUsersSid, *SeRestrictedSid, *SeAnonymousLogonSid;
    /* NT5 privileges */
    NT_LUID SeUndockPrivilege;
    NT_LUID SeSyncAgentPrivilege;
    NT_LUID SeEnableDelegationPrivilege;
    /* post-Windows 2000 SIDs */
    void *SeLocalServiceSid, *SeNetworkServiceSid;
    /* post-Windows 2000 privileges */
    NT_LUID SeManageVolumePrivilege;
    NT_LUID SeImpersonatePrivilege;
    NT_LUID SeCreateGlobalPrivilege;
    /* post-Windows Server 2003 privileges */
    NT_LUID SeTrustedCredManAccessPrivilege;
    NT_LUID SeRelabelPrivilege;
    NT_LUID SeIncreaseWorkingSetPrivilege;
    NT_LUID SeTimeZonePrivilege;
    NT_LUID SeCreateSymbolicLinkPrivilege;
    /* post-Windows Server 2003 SIDs */
    void *SeIUserSid;
    void *SeUntrustedMandatorySid, *SeLowMandatorySid, *SeMediumMandatorySid, *SeHighMandatorySid,
         *SeSystemMandatorySid;
    void *SeOwnerRightsSid;
    void *SeAllAppPackagesSid, *SeUserModeDriversSid;
    void *SeProcTrustWinTcbSid;
    void *SeTrustedInstallerSid;
    /* Windows 10 privilege */
    NT_LUID SeDelegateSessionUserImpersonatePrivilege;
    /* App silo SIDs */
    void *SeAppSiloSid, *SeAppSiloVolumeRootMinimalCapabilitySid, *SeAppSiloProfilesRootMinimalCapabilitySid;
} NT_SE_EXPORTS;

_Static_assert(NT_OFFSET(NT_SE_EXPORTS, SeNullSid) == 23 * 8 &&
               NT_OFFSET(NT_SE_EXPORTS, SeUndockPrivilege) == 23 * 8 + 22 * 8 &&
               NT_OFFSET(NT_SE_EXPORTS, SeNetworkServiceSid) == 23 * 8 + 22 * 8 + 3 * 8 + 8,
               "SE_EXPORTS");

/* The well-known SIDs, each a SID with exactly its own sub-authorities */
#define SE_SID(name, auth, n, ...)                                                   \
    static struct { UINT8 Revision, Count, Authority[6]; UINT32 Sub[n]; } name = { \
        NT_SID_REVISION, n, { 0, 0, 0, 0, 0, auth }, { __VA_ARGS__ } }

static struct { UINT8 Revision, Count, Authority[6]; } g_sid_nt_authority = { NT_SID_REVISION, 0, { 0, 0, 0, 0, 0, 5 } };

SE_SID(g_sid_null,           0, 1, 0);
SE_SID(g_sid_world,          1, 1, 0);
SE_SID(g_sid_local,          2, 1, 0);
SE_SID(g_sid_creator_owner,  3, 1, 0);
SE_SID(g_sid_creator_group,  3, 1, 1);
SE_SID(g_sid_owner_rights,   3, 1, 4);
SE_SID(g_sid_dialup,         5, 1, 1);
SE_SID(g_sid_network,        5, 1, 2);
SE_SID(g_sid_batch,          5, 1, 3);
SE_SID(g_sid_interactive,    5, 1, 4);
SE_SID(g_sid_anonymous,      5, 1, 7);
SE_SID(g_sid_auth_users,     5, 1, 11);
SE_SID(g_sid_restricted,     5, 1, 12);
SE_SID(g_sid_iuser,          5, 1, 17);
SE_SID(g_sid_local_system,   5, 1, 18);
SE_SID(g_sid_local_service,  5, 1, 19);
SE_SID(g_sid_net_service,    5, 1, 20);
SE_SID(g_sid_admins,         5, 2, 32, 544);
SE_SID(g_sid_users,          5, 2, 32, 545);
SE_SID(g_sid_guests,         5, 2, 32, 546);
SE_SID(g_sid_power_users,    5, 2, 32, 547);
SE_SID(g_sid_account_ops,    5, 2, 32, 548);
SE_SID(g_sid_system_ops,     5, 2, 32, 549);
SE_SID(g_sid_print_ops,      5, 2, 32, 550);
SE_SID(g_sid_backup_ops,     5, 2, 32, 551);
SE_SID(g_sid_umdf,           5, 6, 84, 0, 0, 0, 0, 0);
SE_SID(g_sid_trusted_inst,   5, 6, 80, 956008885u, 3418522649u, 1831038044u, 1853292631u, 2271478464u);
SE_SID(g_sid_all_app_pkgs,  15, 2, 2, 1);
SE_SID(g_sid_untrusted_il,  16, 1, 0x0000);
SE_SID(g_sid_low_il,        16, 1, 0x1000);
SE_SID(g_sid_medium_il,     16, 1, 0x2000);
SE_SID(g_sid_high_il,       16, 1, 0x3000);
SE_SID(g_sid_system_il,     16, 1, 0x4000);
SE_SID(g_sid_proc_trust_tcb,19, 2, 1024, 8192);

#define LUID(v) { (v), 0 }

static NT_SE_EXPORTS g_se_exports = {
    .SeCreateTokenPrivilege          = LUID(2),
    .SeAssignPrimaryTokenPrivilege   = LUID(3),
    .SeLockMemoryPrivilege           = LUID(4),
    .SeIncreaseQuotaPrivilege        = LUID(5),
    .SeUnsolicitedInputPrivilege     = LUID(6),
    .SeTcbPrivilege                  = LUID(7),
    .SeSecurityPrivilege             = LUID(8),
    .SeTakeOwnershipPrivilege        = LUID(9),
    .SeLoadDriverPrivilege           = LUID(10),
    .SeCreatePagefilePrivilege       = LUID(15),
    .SeIncreaseBasePriorityPrivilege = LUID(14),
    .SeSystemProfilePrivilege        = LUID(11),
    .SeSystemtimePrivilege           = LUID(12),
    .SeProfileSingleProcessPrivilege = LUID(13),
    .SeCreatePermanentPrivilege      = LUID(16),
    .SeBackupPrivilege               = LUID(17),
    .SeRestorePrivilege              = LUID(18),
    .SeShutdownPrivilege             = LUID(19),
    .SeDebugPrivilege                = LUID(20),
    .SeAuditPrivilege                = LUID(21),
    .SeSystemEnvironmentPrivilege    = LUID(22),
    .SeChangeNotifyPrivilege         = LUID(23),
    .SeRemoteShutdownPrivilege       = LUID(24),

    .SeNullSid            = &g_sid_null,
    .SeWorldSid           = &g_sid_world,
    .SeLocalSid           = &g_sid_local,
    .SeCreatorOwnerSid    = &g_sid_creator_owner,
    .SeCreatorGroupSid    = &g_sid_creator_group,
    .SeNtAuthoritySid     = &g_sid_nt_authority,
    .SeDialupSid          = &g_sid_dialup,
    .SeNetworkSid         = &g_sid_network,
    .SeBatchSid           = &g_sid_batch,
    .SeInteractiveSid     = &g_sid_interactive,
    .SeLocalSystemSid     = &g_sid_local_system,
    .SeAliasAdminsSid     = &g_sid_admins,
    .SeAliasUsersSid      = &g_sid_users,
    .SeAliasGuestsSid     = &g_sid_guests,
    .SeAliasPowerUsersSid = &g_sid_power_users,
    .SeAliasAccountOpsSid = &g_sid_account_ops,
    .SeAliasSystemOpsSid  = &g_sid_system_ops,
    .SeAliasPrintOpsSid   = &g_sid_print_ops,
    .SeAliasBackupOpsSid  = &g_sid_backup_ops,

    .SeAuthenticatedUsersSid = &g_sid_auth_users,
    .SeRestrictedSid         = &g_sid_restricted,
    .SeAnonymousLogonSid     = &g_sid_anonymous,

    .SeUndockPrivilege           = LUID(25),
    .SeSyncAgentPrivilege        = LUID(26),
    .SeEnableDelegationPrivilege = LUID(27),

    .SeLocalServiceSid   = &g_sid_local_service,
    .SeNetworkServiceSid = &g_sid_net_service,

    .SeManageVolumePrivilege         = LUID(28),
    .SeImpersonatePrivilege          = LUID(29),
    .SeCreateGlobalPrivilege         = LUID(30),
    .SeTrustedCredManAccessPrivilege = LUID(31),
    .SeRelabelPrivilege              = LUID(32),
    .SeIncreaseWorkingSetPrivilege   = LUID(33),
    .SeTimeZonePrivilege             = LUID(34),
    .SeCreateSymbolicLinkPrivilege   = LUID(35),

    .SeIUserSid              = &g_sid_iuser,
    .SeUntrustedMandatorySid = &g_sid_untrusted_il,
    .SeLowMandatorySid       = &g_sid_low_il,
    .SeMediumMandatorySid    = &g_sid_medium_il,
    .SeHighMandatorySid      = &g_sid_high_il,
    .SeSystemMandatorySid    = &g_sid_system_il,
    .SeOwnerRightsSid        = &g_sid_owner_rights,
    .SeAllAppPackagesSid     = &g_sid_all_app_pkgs,
    .SeUserModeDriversSid    = &g_sid_umdf,
    .SeProcTrustWinTcbSid    = &g_sid_proc_trust_tcb,
    .SeTrustedInstallerSid   = &g_sid_trusted_inst,

    .SeDelegateSessionUserImpersonatePrivilege = LUID(36),
    /* the app-silo SIDs are left NULL: NovaOS has no app silos */
};

NT_SE_EXPORTS *nt_SeExports = &g_se_exports;
