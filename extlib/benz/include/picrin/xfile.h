#ifndef XFILE_H
#define XFILE_H

#if defined(__cplusplus)
extern "C" {
#endif

#define EOF (-1)

typedef struct {
  int ungot;
  int flags;
  /* operators */
  struct {
    void *cookie;
    int (*read)(void *, char *, int);
    int (*write)(void *, const char *, int);
    long (*seek)(void *, long, int);
    int (*flush)(void *);
    int (*close)(void *);
  } vtable;
} xFILE;

enum {
  XF_SEEK_SET,
  XF_SEEK_CUR,
  XF_SEEK_END
};

/* generic file constructor */
PIC_INLINE xFILE *xfunopen(void *cookie, int (*read)(void *, char *, int), int (*write)(void *, const char *, int), long (*seek)(void *, long, int), int (*flush)(void *), int (*close)(void *));

/* resource aquisition */
PIC_INLINE xFILE *xmopen();
PIC_INLINE int xfclose(xFILE *);

/* buffer management */
PIC_INLINE int xfflush(xFILE *);

/* direct IO with buffering */
PIC_INLINE size_t xfread(void *, size_t, size_t, xFILE *);
PIC_INLINE size_t xfwrite(const void *, size_t, size_t, xFILE *);

/* indicator positioning */
PIC_INLINE long xfseek(xFILE *, long offset, int whence);
PIC_INLINE long xftell(xFILE *);
PIC_INLINE void xrewind(xFILE *);

/* stream status */
PIC_INLINE void xclearerr(xFILE *);
PIC_INLINE int xfeof(xFILE *);
PIC_INLINE int xferror(xFILE *);

/* character IO */
PIC_INLINE int xfgetc(xFILE *);
PIC_INLINE char *xfgets(char *, int, xFILE *);
PIC_INLINE int xfputc(int, xFILE *);
PIC_INLINE int xfputs(const char *, xFILE *);
PIC_INLINE int xgetc(xFILE *);
PIC_INLINE int xputc(int, xFILE *);
PIC_INLINE int xungetc(int, xFILE *);

/* formatted I/O */
PIC_INLINE int xfprintf(xFILE *, const char *, ...);
PIC_INLINE int xvfprintf(xFILE *, const char *, va_list);


/* private */

#define XF_EOF 1
#define XF_ERR 2

PIC_INLINE xFILE *
xfunopen(void *cookie, int (*read)(void *, char *, int), int (*write)(void *, const char *, int), long (*seek)(void *, long, int), int (*flush)(void *), int (*close)(void *))
{
  xFILE *file;

  file = (xFILE *)malloc(sizeof(xFILE));
  if (! file) {
    return NULL;
  }
  file->ungot = -1;
  file->flags = 0;
  /* set vtable */
  file->vtable.cookie = cookie;
  file->vtable.read = read;
  file->vtable.write = write;
  file->vtable.seek = seek;
  file->vtable.flush = flush;
  file->vtable.close = close;

  return file;
}

/*
 * Derieved xFILE Classes
 */

struct xf_membuf {
  char *buf;
  long pos, end, capa;
};

PIC_INLINE int
xf_mem_read(void *cookie, char *ptr, int size)
{
  struct xf_membuf *mem;

  mem = (struct xf_membuf *)cookie;

  if (size > (int)(mem->end - mem->pos))
    size = (int)(mem->end - mem->pos);
  memcpy(ptr, mem->buf + mem->pos, size);
  mem->pos += size;
  return size;
}

PIC_INLINE int
xf_mem_write(void *cookie, const char *ptr, int size)
{
  struct xf_membuf *mem;

  mem = (struct xf_membuf *)cookie;

  if (mem->pos + size >= mem->capa) {
    mem->capa = (mem->pos + size) * 2;
    mem->buf = realloc(mem->buf, (size_t)mem->capa);
  }
  memcpy(mem->buf + mem->pos, ptr, size);
  mem->pos += size;
  if (mem->end < mem->pos)
    mem->end = mem->pos;
  return size;
}

PIC_INLINE long
xf_mem_seek(void *cookie, long pos, int whence)
{
  struct xf_membuf *mem;

  mem = (struct xf_membuf *)cookie;

  switch (whence) {
  case XF_SEEK_SET:
    mem->pos = pos;
    break;
  case XF_SEEK_CUR:
    mem->pos += pos;
    break;
  case XF_SEEK_END:
    mem->pos = mem->end + pos;
    break;
  }

  return mem->pos;
}

PIC_INLINE int
xf_mem_flush(void *cookie)
{
  (void)cookie;

  return 0;
}

PIC_INLINE int
xf_mem_close(void *cookie)
{
  struct xf_membuf *mem;

  mem = (struct xf_membuf *)cookie;
  free(mem->buf);
  free(mem);
  return 0;
}

PIC_INLINE xFILE *
xmopen()
{
  static const size_t size = 128;
  struct xf_membuf *mem;

  mem = (struct xf_membuf *)malloc(sizeof(struct xf_membuf));
  mem->buf = (char *)malloc(size);
  mem->pos = 0;
  mem->end = 0;
  mem->capa = size;

  return xfunopen(mem, xf_mem_read, xf_mem_write, xf_mem_seek, xf_mem_flush, xf_mem_close);
}

PIC_INLINE int
xfclose(xFILE *file)
{
  int r;

  r = file->vtable.close(file->vtable.cookie);
  if (r == EOF) {
    return -1;
  }

  free(file);
  return 0;
}

PIC_INLINE int
xfflush(xFILE *file)
{
  return file->vtable.flush(file->vtable.cookie);
}

PIC_INLINE size_t
xfread(void *ptr, size_t block, size_t nitems, xFILE *file)
{
  char cbuf[256], *buf;
  char *dst = (char *)ptr;
  size_t i, offset;
  int n;

  if (block <= 256) {
    buf = cbuf;
  } else {
    buf = malloc(block);
  }

  for (i = 0; i < nitems; ++i) {
    offset = 0;
    if (file->ungot != -1 && block > 0) {
      buf[0] = (char)file->ungot;
      offset += 1;
      file->ungot = -1;
    }
    while (offset < block) {
      n = file->vtable.read(file->vtable.cookie, buf + offset, (int)(block - offset));
      if (n < 0) {
        file->flags |= XF_ERR;
        goto exit;
      }
      if (n == 0) {
        file->flags |= XF_EOF;
        goto exit;
      }
      offset += (unsigned)n;
    }
    memcpy(dst, buf, block);
    dst += block;
  }

 exit:

  if (cbuf != buf) {
    free(buf);
  }
  return i;
}

PIC_INLINE size_t
xfwrite(const void *ptr, size_t block, size_t nitems, xFILE *file)
{
  char *dst = (char *)ptr;
  size_t i, offset;
  int n;

  for (i = 0; i < nitems; ++i) {
    offset = 0;
    while (offset < block) {
      n = file->vtable.write(file->vtable.cookie, dst + offset, (int)(block - offset));
      if (n < 0) {
        file->flags |= XF_ERR;
        goto exit;
      }
      offset += (unsigned)n;
    }
    dst += block;
  }

 exit:
  return i;
}

PIC_INLINE long
xfseek(xFILE *file, long offset, int whence)
{
  file->ungot = -1;
  return file->vtable.seek(file->vtable.cookie, offset, whence);
}

PIC_INLINE long
xftell(xFILE *file)
{
  return xfseek(file, 0, XF_SEEK_CUR);
}

PIC_INLINE void
xrewind(xFILE *file)
{
  xfseek(file, 0, XF_SEEK_SET);
}

PIC_INLINE void
xclearerr(xFILE *file)
{
  file->flags = 0;
}

PIC_INLINE int
xfeof(xFILE *file)
{
  return file->flags & XF_EOF;
}

PIC_INLINE int
xferror(xFILE *file)
{
  return file->flags & XF_ERR;
}

PIC_INLINE int
xfgetc(xFILE *file)
{
  char buf[1];

  xfread(buf, 1, 1, file);

  if (xfeof(file) || xferror(file)) {
    return EOF;
  }

  return buf[0];
}

PIC_INLINE int
xgetc(xFILE *file)
{
  return xfgetc(file);
}

PIC_INLINE char *
xfgets(char *str, int size, xFILE *file)
{
  int c = EOF, i;

  for (i = 0; i < size - 1 && c != '\n'; ++i) {
    if ((c = xfgetc(file)) == EOF) {
      break;
    }
    str[i] = (char)c;
  }
  if (i == 0 && c == EOF) {
    return NULL;
  }
  if (xferror(file)) {
    return NULL;
  }
  str[i] = '\0';

  return str;
}

PIC_INLINE int
xungetc(int c, xFILE *file)
{
  file->ungot = c;
  if (c != EOF) {
    file->flags &= ~XF_EOF;
  }
  return c;
}

PIC_INLINE int
xfputc(int c, xFILE *file)
{
  char buf[1];

  buf[0] = (char)c;
  xfwrite(buf, 1, 1, file);

  if (xferror(file)) {
    return EOF;
  }
  return buf[0];
}

PIC_INLINE int
xputc(int c, xFILE *file)
{
  return xfputc(c, file);
}

PIC_INLINE int
xfputs(const char *str, xFILE *file)
{
  size_t len;

  len = strlen(str);
  xfwrite(str, len, 1, file);

  if (xferror(file)) {
    return EOF;
  }
  return 0;
}

PIC_INLINE int
xfprintf(xFILE *stream, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = xvfprintf(stream, fmt, ap);
  va_end(ap);
  return n;
}

static void
xfile_printint(xFILE *stream, long x, int base)
{
  static char digits[] = "0123456789abcdef";
  char buf[20];
  int i, neg;

  neg = 0;
  if (x < 0) {
    neg = 1;
    x = -x;
  }

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (neg) {
    buf[i++] = '-';
  }

  while (i-- > 0) {
    xputc(buf[i], stream);
  }
}

#define XFILE_ABS(x) ((x) < 0 ? -(x) : (x))

PIC_INLINE int
xvfprintf(xFILE *stream, const char *fmt, va_list ap)
{
  const char *p;
  char *sval;
  int ival;
#if PIC_ENABLE_FLOAT
  double dval;
#endif
  void *vp;
  long seekr = xftell(stream);

  for (p = fmt; *p; p++) {
    if (*p != '%') {
      xputc(*p, stream);
      continue;
    }
    switch (*++p) {
    case 'd':
    case 'i':
      ival = va_arg(ap, int);
      xfile_printint(stream, ival, 10);
      break;
#if PIC_ENABLE_FLOAT
    case 'f':
      dval = va_arg(ap, double);
      xfile_printint(stream, dval, 10);
      xputc('.', stream);
      xfile_printint(stream, XFILE_ABS((dval - (int)dval) * 1e4), 10);
      break;
#endif
    case 's':
      sval = va_arg(ap, char*);
      xfputs(sval, stream);
      break;
    case 'p':
      vp = va_arg(ap, void*);
      xfputs("0x", stream);
      xfile_printint(stream, (long)vp, 16);
      break;
    case '%':
      xputc(*(p-1), stream);
      break;
    default:
      xputc('%', stream);
      xputc(*(p-1), stream);
      break;
    }
  }
  return xftell(stream) - seekr;
}

#if defined(__cplusplus)
}
#endif

#endif
