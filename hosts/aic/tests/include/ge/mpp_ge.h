/* Host-test mirror of the luban-lite mpp_ge API surface pocketjs_ge.c uses.
 *
 * This header only exists to compile the port on a development machine; the
 * firmware build resolves the same names from packages/artinchip/mpp and
 * bsp/artinchip. The cache operations record their calls so a test can
 * assert the clean -> GE -> invalidate ordering, and the engine functions
 * capture their argument structures instead of touching hardware. */
#ifndef POCKETJS_TEST_MPP_GE_H
#define POCKETJS_TEST_MPP_GE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Recorded cache maintenance; the signatures follow the platform variants
 * the port compiles against. */
void test_ge_cache_log(const char *operation, const void *address,
                       size_t size);

static inline void aicos_dcache_clean_range(unsigned long *address,
                                            unsigned long size) {
  test_ge_cache_log("clean", address, (size_t)size);
}

static inline void aicos_dcache_invalid_range(unsigned long *address,
                                              unsigned long size) {
  test_ge_cache_log("invalid", address, (size_t)size);
}

enum mpp_buf_type {
  MPP_DMA_BUF_FD,
  MPP_PHY_ADDR,
};

enum mpp_pixel_format {
  MPP_FMT_ARGB_8888 = 0x00,
  MPP_FMT_RGB_565 = 0x0e,
  MPP_FMT_BGR_565 = 0x0f,
};

struct mpp_rect {
  int x;
  int y;
  int width;
  int height;
};

struct mpp_size {
  int width;
  int height;
};

struct mpp_buf {
  enum mpp_buf_type buf_type;
  union {
    int fd[3];
    unsigned int phy_addr[3];
  };
  unsigned int stride[3];
  struct mpp_size size;
  unsigned int crop_en;
  struct mpp_rect crop;
  enum mpp_pixel_format format;
  unsigned int flags;
};

enum ge_fillrect_type {
  GE_NO_GRADIENT = 0,
  GE_H_LINEAR_GRADIENT = 1,
  GE_V_LINEAR_GRADIENT = 2,
};

struct ge_ctrl {
  unsigned int alpha_en;
  unsigned int alpha_rules;
  unsigned int src_alpha_mode;
  unsigned int src_global_alpha;
  unsigned int dst_alpha_mode;
  unsigned int dst_global_alpha;
  unsigned int ck_en;
  unsigned int ck_value;
  unsigned int dither_en;
  unsigned int flags;
};

struct ge_fillrect {
  enum ge_fillrect_type type;
  unsigned int start_color;
  unsigned int end_color;
  struct mpp_buf dst_buf;
  struct ge_ctrl ctrl;
};

struct ge_bitblt {
  struct mpp_buf src_buf;
  struct mpp_buf dst_buf;
  struct ge_ctrl ctrl;
};

#define MPP_FLIP_H (1 << 8)
#define MPP_FLIP_V (1 << 9)

enum ge_mode {
  GE_MODE_NORMAL,
  GE_MODE_CMDQ,
};

struct mpp_ge;

struct mpp_ge *mpp_ge_open(void);
void mpp_ge_close(struct mpp_ge *ge);
enum ge_mode mpp_ge_get_mode(struct mpp_ge *ge);
int mpp_ge_fillrect(struct mpp_ge *ge, struct ge_fillrect *fill);
int mpp_ge_bitblt(struct mpp_ge *ge, struct ge_bitblt *blt);

#endif /* POCKETJS_TEST_MPP_GE_H */
