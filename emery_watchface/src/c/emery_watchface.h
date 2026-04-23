#pragma once
#include <pebble.h>

// Screen Resolution for Emery
#define EMERY_WIDTH 200
#define EMERY_HEIGHT 228

// Highlight Positions
#define POS_TOP 0
#define POS_BOTTOM 1
#define POS_LEFT 2
#define POS_RIGHT 3

// Config
typedef struct {
  GColor inner_ring_color;
  GColor sub_inner_ring_color;
  GColor middle_ring_color;
  GColor outer_ring_color;
  GColor highlight_fill_color;
  GColor line_color;
  GColor number_color;
  GColor center_text_color;
  GColor highlight_number_color;
  int highlight_position;
  bool animation_toggle;
} WatchConfig;

extern WatchConfig config;

// Default config
void init_default_config();

// Rings structure
typedef struct {
  int width;
  int height;
  int corner_radius;
  int num_items;
} RingDef;

extern RingDef rings[4];

// Math utilities
GPoint get_point_on_rounded_rect(int w, int h, int r, int32_t angle);
