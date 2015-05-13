
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER ft_tracepoint

#if !defined(FT_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define FT_TRACEPOINT_H

#include <lttng/tracepoint.h>
TRACEPOINT_EVENT(ft_tracepoint,	message, 
	TP_ARGS(int, value),
	TP_FIELDS(ctf_integer(int, value, value)))
TRACEPOINT_LOGLEVEL(ft_tracepoint, message, TRACE_WARNING)
#endif /* FT_TRACEPOINT_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./ft_tracepoint.h"


#include <lttng/tracepoint-event.h>


