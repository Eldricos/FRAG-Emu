/* Minimal stand-in for Wine's debug.h so the driver builds with MinGW: logging is dropped. */
#ifndef FRAG_WINE_DEBUG_H
#define FRAG_WINE_DEBUG_H
#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define FIXME(...) do {} while (0)
#define ERR(...) do {} while (0)
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#endif
