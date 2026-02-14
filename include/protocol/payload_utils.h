#ifndef PAYLOAD_UTILS_H
#define PAYLOAD_UTILS_H

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

typedef struct {
    const uint8_t *ptr;
    size_t remaining;
    bool error;
} payload_cursor_t;

static inline void cursor_init(payload_cursor_t *c, const uint8_t *data, size_t len) {
    c->ptr = data;
    c->remaining = len;
    c->error = false;
}

static inline uint8_t cursor_get_u8(payload_cursor_t *c) {
    if (c->remaining < 1) {
        c->error = true;
        return 0;
    }
    uint8_t val = *c->ptr;
    c->ptr += 1;
    c->remaining -= 1;
    return val;
}

static inline uint16_t cursor_get_u16(payload_cursor_t *c) {
    if (c->remaining < 2) {
        c->error = true;
        return 0;
    }
    uint16_t val = sys_get_le16(c->ptr);
    c->ptr += 2;
    c->remaining -= 2;
    return val;
}

static inline uint32_t cursor_get_u32(payload_cursor_t *c) {
    if (c->remaining < 4) {
        c->error = true;
        return 0;
    }
    uint32_t val = sys_get_le32(c->ptr);
    c->ptr += 4;
    c->remaining -= 4;
    return val;
}

static inline uint64_t cursor_get_u64(payload_cursor_t *c) {
    if (c->remaining < 8) {
        c->error = true;
        return 0;
    }
    uint64_t val = sys_get_le64(c->ptr);
    c->ptr += 8;
    c->remaining -= 8;
    return val;
}

static inline void cursor_get_bytes(payload_cursor_t *c, uint8_t *dest, size_t size) {
    if (c->remaining < size) {
        c->error = true;
        memset(dest, 0, size);
        return;
    }
    memcpy(dest, c->ptr, size);
    c->ptr += size;
    c->remaining -= size;
}

static inline void cursor_get_pstr(payload_cursor_t *c, char *dest, size_t max_dest_len) {
    if (c->remaining < 1) {
        c->error = true;
        dest[0] = '\0';
        return;
    }

    uint8_t str_len = *c->ptr;
    c->ptr++;
    c->remaining--;

    if (c->remaining < str_len) {
        c->error = true;
        dest[0] = '\0';
        return;
    }

    size_t copy_len = (str_len >= max_dest_len) ? (max_dest_len - 1) : str_len;

    memcpy(dest, c->ptr, copy_len);
    dest[copy_len] = '\0';

    c->ptr += str_len;
    c->remaining -= str_len;
}

typedef struct {
    uint8_t *ptr;
    size_t remaining;
    bool error;
} payload_writer_t;

static inline void writer_init(payload_writer_t *w, uint8_t *buffer, size_t len) {
    w->ptr = buffer;
    w->remaining = len;
    w->error = false;
}

static inline void writer_put_u8(payload_writer_t *w, uint8_t val) {
    if (w->remaining < 1) {
        w->error = true;
        return;
    }
    *w->ptr = val;
    w->ptr += 1;
    w->remaining -= 1;
}

static inline void writer_put_u16(payload_writer_t *w, uint16_t val) {
    if (w->remaining < 2) {
        w->error = true;
        return;
    }
    sys_put_le16(val, w->ptr);
    w->ptr += 2;
    w->remaining -= 2;
}

static inline void writer_put_u32(payload_writer_t *w, uint32_t val) {
    if (w->remaining < 4) {
        w->error = true;
        return;
    }
    sys_put_le32(val, w->ptr);
    w->ptr += 4;
    w->remaining -= 4;
}

static inline void writer_put_u64(payload_writer_t *w, uint64_t val) {
    if (w->remaining < 8) {
        w->error = true;
        return;
    }
    sys_put_le64(val, w->ptr);
    w->ptr += 8;
    w->remaining -= 8;
}

static inline void writer_put_bytes(payload_writer_t *w, const uint8_t *src, size_t size) {
    if (w->remaining < size) {
        w->error = true;
        return;
    }
    memcpy(w->ptr, src, size);
    w->ptr += size;
    w->remaining -= size;
}

static inline void writer_put_pstr(payload_writer_t *w, const char *src) {
    size_t len = strlen(src);

    if (len > 255) {
        w->error = true;
        return; 
    }

    if (w->remaining < (1 + len)) {
        w->error = true;
        return;
    }

    *w->ptr = (uint8_t)len;
    w->ptr++;
    w->remaining--;

    memcpy(w->ptr, src, len);
    w->ptr += len;
    w->remaining -= len;
}

static inline size_t writer_get_length(payload_writer_t *w, uint8_t *start_buffer) {
    return (size_t)(w->ptr - start_buffer);
}

#endif // PAYLOAD_UTILS_H
