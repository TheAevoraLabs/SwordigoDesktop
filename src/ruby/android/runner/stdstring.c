#include <string.h>
//#include <malloc.h>
#include <stdlib.h> // better malloc idk
#include "stdstring.h"

void String_create(String *out, const char *s) {
	if (!out || !s) return;

	size_t len = strlen(s);

	if (len < STRING_SHORT_CAP) {
		out->s.size = (unsigned char)(len << 1);
		memcpy(out->s.data, s, len);
		out->s.data[len] = '\0';
	} else {
		size_t bufs = (len + 2) & ~(size_t)1;
		char *buf = malloc(bufs);
		if (!buf) return;

		memcpy(buf, s, len);
		buf[len] = '\0';

		out->l.cap = bufs | (size_t)1;
		out->l.size = len;
		out->l.data = buf;
	}
}

void String_destroy(String *s) {
	if (s && String_isLong(s) && s->l.data) {
		free(s->l.data);
		s->l.data = NULL;
	}
}