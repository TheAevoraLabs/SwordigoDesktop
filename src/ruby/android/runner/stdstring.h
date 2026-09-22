#ifndef LAWNCHER_STDSTRING_H
#define LAWNCHER_STDSTRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* libc++ stores short strings inline in a buffer of sizeof(__long) - 1 bytes:
 *   23 on LP64  (arm64-v8a)  -> 22 usable characters
 *   11 on ILP32 (armeabi-v7a) -> 10 usable characters
 * Writing the LP64 threshold into an arm32 engine string overruns the engine's own
 * inline buffer, so the split must follow the ABI.
 * Cross-checked against `Caver::NewCaverShell`, which uses the same 0x17 (23) split on
 * arm64, and against CaverShell's 0x68-byte layout on arm32 (std::string == 12 bytes). */
#if defined(STRING_SHORT_CAP)
/* Caller override (tests / tooling); leave as-is. */
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define STRING_SHORT_CAP 23
#else
#  define STRING_SHORT_CAP 11
#endif

typedef struct {
	size_t cap;
	size_t size;
	char *data;
} String_Long;

typedef struct {
	unsigned char size;
	char data[STRING_SHORT_CAP];
} String_Short;

typedef struct String {
	union {
		String_Long l;
		String_Short s;
	}; // sonion
} String;

static inline int String_isLong(const String *s) {
	return s->s.size & 1u;
}

static inline size_t String_size(const String *s) {
	return String_isLong(s) ? s->l.size : (s->s.size >> 1);
}

static inline const char *String_get(const String *s) { // basically s->cstr
	return String_isLong(s) ? s->l.data : s->s.data;
}

void String_create(String *out, const char *s);
void String_destroy(String *s);

#ifdef __cplusplus
}
#endif

#endif //LAWNCHER_STDSTRING_H
