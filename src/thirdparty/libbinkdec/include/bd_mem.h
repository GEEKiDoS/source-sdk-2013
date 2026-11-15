
#ifndef BD_MEM
#define BD_MEM

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

	extern void* ( *bd_malloc )( size_t );
	extern void ( *bd_free )( void* );

#ifdef __cplusplus
}
#endif

#endif
