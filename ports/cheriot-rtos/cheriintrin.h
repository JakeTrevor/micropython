#include <cheri-builtins.h>

#define cheri_length_get(foo) cgetlen(foo)
#define cheri_perms_get(foo) cgetperms(foo)
#define cheri_type_get(foo) cgettype(foo)
#define cheri_tag_get(foo) cgettag(foo)
#define cheri_offset_get(foo) cgetoffset(foo)
#define cheri_offset_set(a, b) csetoffset((a), (b))
#define cheri_address_get(foo) cgetaddr(foo)
#define cheri_address_set(a, b) csetaddr((a), (b))
#define cheri_base_get(foo) cgetbase(foo)
#define cheri_perms_and(a, b) candperms((a), (b))
#define cheri_seal(a, b) cseal((a), (b))
#define cheri_unseal(a, b) cunseal((a), (b))
#define cheri_bounds_set(a, b) csetbounds((a), (b))
#define cheri_bounds_set_exact(a, b) csetboundsext((a), (b))

