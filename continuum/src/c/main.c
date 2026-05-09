#include <pebble.h>
#include "continuum.h"

// Persist storage key
#define PERSIST_KEY_CONFIG    1

// Animation constants
#define ANIM_DURATION_MS      500
#define ANIM_DELAY_STEP_MS    150
#define DEFAULT_ANIM_FPS      30

static const char *const DIGIT_STRINGS[] = {
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

// Drawing constants
#if PBL_DISPLAY_WIDTH >= 200
#define HIGHLIGHT_BOX_SIZE    21
#define NUMBER_TEXT_W         21
#define NUMBER_TEXT_H         21
#define NUMBER_TEXT_OFF_X     10
#define NUMBER_TEXT_OFF_Y     10
#define CENTER_ITEM_W         50
#define CENTER_ITEM_H         18
#define CENTER_SPACING         2
#define BATTERY_ICON_W        26
#define BATTERY_ICON_H        12
#else
#define HIGHLIGHT_BOX_SIZE    15
#define NUMBER_TEXT_W         15
#define NUMBER_TEXT_H         15
#define NUMBER_TEXT_OFF_X     7
#define NUMBER_TEXT_OFF_Y     10
#define CENTER_ITEM_W         36
#define CENTER_ITEM_H         16
#define CENTER_SPACING         0
#define BATTERY_ICON_W        18
#define BATTERY_ICON_H         8
#endif
#define BATTERY_BODY_W  (BATTERY_ICON_W - 3)

#if defined(PBL_ROUND) && PBL_DISPLAY_WIDTH >= 200
#define CENTER_MONTHDAY_W  100
#elif defined(PBL_ROUND)
#define CENTER_MONTHDAY_W   72
#else
#define CENTER_MONTHDAY_W   CENTER_ITEM_W
#endif

WatchConfig config;

#if defined(PBL_ROUND) && PBL_DISPLAY_WIDTH == 260
RingDef rings[4] = {
  { .width = 120, .height = 120, .corner_radius = 36,  .num_items = 3 },
  { .width = 160, .height = 160, .corner_radius = 60,  .num_items = 10 },
  { .width = 200, .height = 200, .corner_radius = 86,  .num_items = 6 },
  { .width = 240, .height = 240, .corner_radius = 110, .num_items = 10 }
};
#elif defined(PBL_ROUND)
RingDef rings[4] = {
  { .width = 82,  .height = 82,  .corner_radius = 24, .num_items = 3 },
  { .width = 110, .height = 110, .corner_radius = 46, .num_items = 10 },
  { .width = 138, .height = 138, .corner_radius = 62, .num_items = 6 },
  { .width = 166, .height = 166, .corner_radius = 76, .num_items = 10 }
};
#elif PBL_DISPLAY_WIDTH == 144
RingDef rings[4] = {
  { .width = 46,  .height = 70,  .corner_radius = 7,  .num_items = 3 },
  { .width = 74,  .height = 98,  .corner_radius = 11, .num_items = 10 },
  { .width = 102, .height = 126, .corner_radius = 15, .num_items = 6 },
  { .width = 130, .height = 154, .corner_radius = 18, .num_items = 10 }
};
#else
RingDef rings[4] = {
  { .width = 60,  .height = 88,  .corner_radius = 10, .num_items = 3 },
  { .width = 100, .height = 128, .corner_radius = 15, .num_items = 10 },
  { .width = 140, .height = 168, .corner_radius = 20, .num_items = 6 },
  { .width = 180, .height = 208, .corner_radius = 25, .num_items = 10 }
};
#endif

static Window *s_main_window;

static Layer *s_bg_layer;
static Layer *s_ring_layers[4];
static Layer *s_month_layer;
static Layer *s_day_layer;
static Layer *s_weekday_layer;
static Layer *s_battery_layer;

static const char *const MONTH_NAMES[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int current_hour_tens = 0;
static int current_hour_ones = 0;
static int current_minute_tens = 0;
static int current_minute_ones = 0;

static int current_month = 1;
static int current_day = 1;
static int current_weekday = 0;
static int battery_level = 100;

static int32_t anim_hour_tens_angle = 0;
static int32_t anim_hour_ones_angle = 0;
static int32_t anim_min_tens_angle = 0;
static int32_t anim_min_ones_angle = 0;

static PropertyAnimation *s_hour_tens_anim = NULL;
static PropertyAnimation *s_hour_ones_anim = NULL;
static PropertyAnimation *s_min_tens_anim = NULL;
static PropertyAnimation *s_min_ones_anim = NULL;

typedef struct {
  PropertyAnimation **anim_ptr;
  int32_t from;
  int32_t to;
  int32_t *target_var;
  Layer   *target_layer;
} DelayedAnimArgs;

static DelayedAnimArgs s_delayed[4];
static AppTimer        *s_delay_timers[4];

typedef struct {
  int32_t from;
  int32_t to;
  int32_t *target_var;
  Layer   *target_layer;
  uint32_t last_frame_time;
} AnimCtx;

static void anim_stopped_handler(Animation *animation, bool finished, void *context) {
  void **ptr_tuple = (void **)context;
  PropertyAnimation **anim_ptr = (PropertyAnimation **)ptr_tuple[0];
  AnimCtx *ctx = (AnimCtx *)ptr_tuple[1];

  if (finished) {
    int32_t final = ctx->to % TRIG_MAX_ANGLE;
    if (final < 0) final += TRIG_MAX_ANGLE;
    *ctx->target_var = final;
    layer_mark_dirty(ctx->target_layer);
  }

  if (anim_ptr && *anim_ptr == (PropertyAnimation *)animation) {
    *anim_ptr = NULL;
  }

  free(ctx);
  free(ptr_tuple);

  if (finished) {
    animation_destroy(animation);
  }
}

static int32_t target_hour_tens_angle = 0;
static int32_t target_hour_ones_angle = 0;
static int32_t target_min_tens_angle = 0;
static int32_t target_min_ones_angle = 0;

static void angle_anim_update(Animation *anim, const AnimationProgress progress) {
  void **ptr_tuple = (void **)animation_get_context(anim);
  AnimCtx *ctx = (AnimCtx *)ptr_tuple[1];

  time_t t;
  uint16_t ms;
  time_ms(&t, &ms);
  uint32_t now = (uint32_t)t * 1000 + ms;

  uint32_t interval = 1000 / config.anim_fps;
  if (progress < ANIMATION_NORMALIZED_MAX && now - ctx->last_frame_time < interval) {
    return;
  }
  ctx->last_frame_time = now;

  int32_t current = ctx->from + (int32_t)(
    ((int64_t)(ctx->to - ctx->from) * (int32_t)progress) / ANIMATION_NORMALIZED_MAX);
  *ctx->target_var = current;
  layer_mark_dirty(ctx->target_layer);
}

static const AnimationImplementation angle_anim_impl = { .update = angle_anim_update };

static PropertyAnimation* create_anim(int32_t from, int32_t to,
                                      int32_t *target, Layer *target_layer,
                                      PropertyAnimation **ptr_to_store) {
  AnimCtx *ctx = malloc(sizeof(AnimCtx));
  if (!ctx) return NULL;
  ctx->from = from;
  ctx->to   = to;
  ctx->target_var   = target;
  ctx->target_layer = target_layer;
  ctx->last_frame_time = 0;

  Animation *anim = animation_create();
  if (!anim) { free(ctx); return NULL; }
  animation_set_implementation(anim, &angle_anim_impl);

  void **ptr_tuple = malloc(sizeof(void*) * 2);
  if (!ptr_tuple) { free(ctx); animation_destroy(anim); return NULL; }
  ptr_tuple[0] = ptr_to_store;
  ptr_tuple[1] = ctx;

  animation_set_handlers(anim, (AnimationHandlers) { .stopped = anim_stopped_handler }, ptr_tuple);
  return (PropertyAnimation*)anim;
}

static AnimationProgress inertia_curve(AnimationProgress linear) {
  const int32_t M = ANIMATION_NORMALIZED_MAX;
  int32_t t256     = (int32_t)((int64_t)linear * 256 / M);
  int32_t d        = t256 - 256;
  int32_t d2       = d * d;
  int64_t d3       = (int64_t)d2 * d;
  int32_t term2    = (int32_t)(1742 * d2 / (1024 * 256));
  int32_t term3    = (int32_t)(2767LL * d3 / (1024LL * 256 * 256));
  int32_t result256 = 256 + term2 + term3;
  return (AnimationProgress)(result256 * M / 256);
}

static void start_ring_anim(PropertyAnimation **anim_ptr, int32_t from, int32_t to,
                             int32_t *target, Layer *target_layer) {
  if (to == from) return;

  if (*anim_ptr) {
    Animation *old = (Animation*)*anim_ptr;
    *anim_ptr = NULL;
    animation_unschedule(old);
    animation_destroy(old);
  }

  *anim_ptr = create_anim(from, to, target, target_layer, anim_ptr);
  if (!*anim_ptr) return;

  animation_set_duration((Animation*)*anim_ptr, ANIM_DURATION_MS);
  if (config.inertia_toggle) {
    animation_set_custom_curve((Animation*)*anim_ptr, inertia_curve);
  } else {
    animation_set_curve((Animation*)*anim_ptr, AnimationCurveEaseInOut);
  }
  animation_schedule((Animation*)*anim_ptr);
}

static void delayed_anim_callback(void *callback_data) {
  int idx = (int)(uintptr_t)callback_data;
  s_delay_timers[idx] = NULL; 

  DelayedAnimArgs *a = &s_delayed[idx];
  int32_t from = *a->target_var % TRIG_MAX_ANGLE;
  if (from < 0) from += TRIG_MAX_ANGLE;
  int32_t to = a->to;
  if (to < from) to += TRIG_MAX_ANGLE;

  start_ring_anim(a->anim_ptr, from, to, a->target_var, a->target_layer);
}

static void schedule_ring_anim(PropertyAnimation **anim_ptr, int32_t from, int32_t to,
                               int32_t *target, uint32_t delay_ms, Layer *target_layer) {
  if (to == from) return;

  int idx = -1;
  if      (anim_ptr == &s_hour_tens_anim) idx = 0;
  else if (anim_ptr == &s_hour_ones_anim) idx = 1;
  else if (anim_ptr == &s_min_tens_anim)  idx = 2;
  else if (anim_ptr == &s_min_ones_anim)  idx = 3;

  if (idx >= 0 && s_delay_timers[idx]) {
    app_timer_cancel(s_delay_timers[idx]);
    s_delay_timers[idx] = NULL;
  }

  if (delay_ms == 0) {
    start_ring_anim(anim_ptr, from, to, target, target_layer);
  } else {
    s_delayed[idx] = (DelayedAnimArgs){
      .anim_ptr     = anim_ptr,
      .from         = from,     
      .to           = to,
      .target_var   = target,
      .target_layer = target_layer,
    };
    s_delay_timers[idx] = app_timer_register(delay_ms, delayed_anim_callback,
                                              (void*)(uintptr_t)idx);
  }
}

static GFont s_number_font;
static GFont s_date_font;

void init_default_config() {
  config.highlight_position   = POS_RIGHT;
  config.anim_fps             = DEFAULT_ANIM_FPS;
  config.animation_toggle     = true;
  config.inertia_toggle       = true;
  config.battery_toggle       = true;
  config.invert_bw            = false;
#ifdef PBL_COLOR
  config.inner_ring_color       = GColorBlack;
  config.sub_inner_ring_color   = GColorBlack;
  config.middle_ring_color      = GColorBlack;
  config.outer_ring_color       = GColorBlack;
  config.highlight_fill_color   = GColorRed;
  config.line_color             = GColorDarkGray;
  config.number_color           = GColorLightGray;
  config.center_text_color      = GColorWhite;
  config.highlight_number_color = GColorWhite;
  config.background_color       = GColorBlack;
#else
  config.inner_ring_color       = GColorBlack;
  config.sub_inner_ring_color   = GColorBlack;
  config.middle_ring_color      = GColorBlack;
  config.outer_ring_color       = GColorBlack;
  config.highlight_fill_color   = GColorWhite;
  config.line_color             = GColorWhite;
  config.number_color           = GColorWhite;
  config.center_text_color      = GColorWhite;
  config.highlight_number_color = GColorBlack;
  config.background_color       = GColorBlack;
#endif
}

static void save_config() {
  persist_write_data(PERSIST_KEY_CONFIG, &config, sizeof(WatchConfig));
}

static void load_config() {
  if (persist_exists(PERSIST_KEY_CONFIG)) {
    persist_read_data(PERSIST_KEY_CONFIG, &config, sizeof(WatchConfig));
  }
}

static int32_t round_div_trig(int32_t num) {
  if (num >= 0) return (num + TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO;
  else          return (num - TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO;
}

GPoint get_point_on_rounded_rect(int w, int h, int r, int32_t angle) {
  angle = angle % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;

  if (angle == 0)                       return GPoint(0,    -h/2);
  if (angle == TRIG_MAX_ANGLE / 4)      return GPoint(w/2,  0);
  if (angle == TRIG_MAX_ANGLE / 2)      return GPoint(0,    h/2);
  if (angle == 3 * TRIG_MAX_ANGLE / 4)  return GPoint(-w/2, 0);

  int str_h = w - 2*r;
  int str_v = h - 2*r;
  int approx_perimeter = 2*str_h + 2*str_v + (314 * 2 * r) / 100;
  int32_t target_dist = (int32_t)((int32_t)angle * approx_perimeter / TRIG_MAX_ANGLE);

  int x = 0, y = 0;
  int current_dist = 0;

  if (target_dist <= current_dist + str_h / 2) {
    return GPoint(target_dist - current_dist, -h/2);
  }
  current_dist += str_h / 2;
  int q_arc = (314 * r) / 200;

  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = w/2 - r + round_div_trig(sin_lookup(arc_angle) * r);
    y = -h/2 + r - round_div_trig(cos_lookup(arc_angle) * r);
    return GPoint(x, y);
  }
  current_dist += q_arc;

  if (target_dist <= current_dist + str_v) {
    return GPoint(w/2, -h/2 + r + (target_dist - current_dist));
  }
  current_dist += str_v;

  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = TRIG_MAX_ANGLE/4 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = w/2 - r + round_div_trig(sin_lookup(arc_angle) * r);
    y = h/2 - r - round_div_trig(cos_lookup(arc_angle) * r);
    return GPoint(x, y);
  }
  current_dist += q_arc;

  if (target_dist <= current_dist + str_h) {
    return GPoint(w/2 - r - (target_dist - current_dist), h/2);
  }
  current_dist += str_h;

  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = TRIG_MAX_ANGLE/2 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = -w/2 + r + round_div_trig(sin_lookup(arc_angle) * r);
    y = h/2 - r - round_div_trig(cos_lookup(arc_angle) * r);
    return GPoint(x, y);
  }
  current_dist += q_arc;

  if (target_dist <= current_dist + str_v) {
    return GPoint(-w/2, h/2 - r - (target_dist - current_dist));
  }
  current_dist += str_v;

  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = 3*TRIG_MAX_ANGLE/4 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = -w/2 + r + round_div_trig(sin_lookup(arc_angle) * r);
    y = -h/2 + r - round_div_trig(cos_lookup(arc_angle) * r);
    return GPoint(x, y);
  }
  current_dist += q_arc;

  return GPoint(-w/2 + r + (target_dist - current_dist), -h/2);
}

GPoint get_point_on_circle(int radius, int32_t angle) {
  angle = angle % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;
  return GPoint(
    round_div_trig(sin_lookup(angle) * radius),
    -round_div_trig(cos_lookup(angle) * radius)
  );
}

static void draw_ring_numbers(GContext *ctx, GPoint center, int ring_idx,
                              int32_t ring_anim_angle, int current_digit,
                              bool is_animating, int32_t target_angle) {
  int n = rings[ring_idx].num_items;
  for (int i = 0; i < n; i++) {
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / n;
    int32_t angle = target_angle + item_offset - ring_anim_angle;
    bool is_current = (!is_animating && i == current_digit);

#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, is_current ? config.highlight_number_color : config.number_color);
#else
    GColor bw_fg = config.invert_bw ? GColorBlack : GColorWhite;
    GColor bw_bg = config.invert_bw ? GColorWhite : GColorBlack;
    graphics_context_set_text_color(ctx, is_current ? bw_bg : bw_fg);
#endif

#ifdef PBL_ROUND
    GPoint pt = get_point_on_circle(rings[ring_idx].width / 2, angle);
#else
    GPoint pt = get_point_on_rounded_rect(rings[ring_idx].width, rings[ring_idx].height,
                                          rings[ring_idx].corner_radius, angle);
#endif
    
    graphics_draw_text(ctx, DIGIT_STRINGS[i], s_number_font,
      GRect(center.x + pt.x - NUMBER_TEXT_OFF_X, center.y + pt.y - NUMBER_TEXT_OFF_Y - 2,
            NUMBER_TEXT_W, NUMBER_TEXT_H),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

#ifdef PBL_COLOR
  GColor bg_col      = config.background_color;
  GColor line_col    = config.line_color;
  GColor hl_fill_col = config.highlight_fill_color;
  GColor ring_colors[4] = {
    config.inner_ring_color, config.sub_inner_ring_color,
    config.middle_ring_color, config.outer_ring_color
  };
#else
  GColor bw_fg = config.invert_bw ? GColorBlack : GColorWhite;
  GColor bw_bg = config.invert_bw ? GColorWhite : GColorBlack;
  GColor bg_col      = bw_bg;
  GColor line_col    = bw_fg;
  GColor hl_fill_col = bw_fg;
  GColor ring_colors[4] = { bw_bg, bw_bg, bw_bg, bw_bg };
#endif

  graphics_context_set_fill_color(ctx, bg_col);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_width(ctx, 1);

  for (int i = 3; i >= 0; i--) {
    graphics_context_set_fill_color(ctx, ring_colors[i]);
    graphics_context_set_stroke_color(ctx, line_col);
#ifdef PBL_ROUND
    graphics_fill_circle(ctx, center, rings[i].width / 2);
    graphics_draw_circle(ctx, center, rings[i].width / 2);
#else
    GRect rect = GRect(center.x - rings[i].width/2, center.y - rings[i].height/2,
                       rings[i].width, rings[i].height);
    graphics_fill_rect(ctx, rect, rings[i].corner_radius, GCornersAll);
    graphics_draw_round_rect(ctx, rect, rings[i].corner_radius);
#endif
  }

  int32_t target_angle = 0;
  if      (config.highlight_position == POS_RIGHT)  target_angle = TRIG_MAX_ANGLE / 4;
  else if (config.highlight_position == POS_BOTTOM) target_angle = TRIG_MAX_ANGLE / 2;
  else if (config.highlight_position == POS_LEFT)   target_angle = 3 * TRIG_MAX_ANGLE / 4;

  graphics_context_set_fill_color(ctx, hl_fill_col);
  graphics_context_set_stroke_color(ctx, line_col);
  for (int i = 0; i < 4; i++) {
#ifdef PBL_ROUND
    GPoint pt = get_point_on_circle(rings[i].width / 2, target_angle);
    GPoint hl_c = GPoint(center.x + pt.x, center.y + pt.y);
    GRect hl = GRect(hl_c.x - HIGHLIGHT_BOX_SIZE/2, hl_c.y - HIGHLIGHT_BOX_SIZE/2,
                     HIGHLIGHT_BOX_SIZE, HIGHLIGHT_BOX_SIZE);
#else
    GPoint pt = get_point_on_rounded_rect(rings[i].width, rings[i].height,
                                          rings[i].corner_radius, target_angle);
    GRect hl = GRect(center.x + pt.x - HIGHLIGHT_BOX_SIZE/2,
                     center.y + pt.y - HIGHLIGHT_BOX_SIZE/2,
                     HIGHLIGHT_BOX_SIZE, HIGHLIGHT_BOX_SIZE);
#endif
    graphics_fill_rect(ctx, hl, 0, GCornerNone);
    graphics_draw_rect(ctx, hl);
  }
}

static void ring_update_proc(Layer *layer, GContext *ctx) {
  int ring_idx = *(int*)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  int32_t target_angle = 0;
  
  if      (config.highlight_position == POS_RIGHT)  target_angle = TRIG_MAX_ANGLE / 4;
  else if (config.highlight_position == POS_BOTTOM) target_angle = TRIG_MAX_ANGLE / 2;
  else if (config.highlight_position == POS_LEFT)   target_angle = 3 * TRIG_MAX_ANGLE / 4;

  int32_t anim_angle   = 0;
  int     cur_digit    = 0;
  bool    is_animating = false;
  
  switch (ring_idx) {
    case 0: 
      anim_angle = anim_hour_tens_angle; 
      cur_digit = current_hour_tens; 
      // 同時檢查動畫是否正在執行，或者是否有動畫正在排隊等待 (Delay)
      is_animating = (s_hour_tens_anim != NULL) || (s_delay_timers[0] != NULL); 
      break;
    case 1: 
      anim_angle = anim_hour_ones_angle; 
      cur_digit = current_hour_ones; 
      is_animating = (s_hour_ones_anim != NULL) || (s_delay_timers[1] != NULL); 
      break;
    case 2: 
      anim_angle = anim_min_tens_angle; 
      cur_digit = current_minute_tens; 
      is_animating = (s_min_tens_anim != NULL)  || (s_delay_timers[2] != NULL);  
      break;
    case 3: 
      anim_angle = anim_min_ones_angle;  
      cur_digit = current_minute_ones; 
      is_animating = (s_min_ones_anim != NULL)  || (s_delay_timers[3] != NULL);  
      break;
  }

  draw_ring_numbers(ctx, center, ring_idx, anim_angle, cur_digit, is_animating, target_angle);
}

static void update_time(void);

static void reposition_center_layers(void) {
  Layer *wl = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(wl);
  GPoint center = grect_center_point(&bounds);
  int step = CENTER_ITEM_H + CENTER_SPACING;
  GRect f;

  bool is_three_line_layout = false;
#ifdef PBL_ROUND
  is_three_line_layout = true;
#endif

  if (config.highlight_position == POS_TOP || config.highlight_position == POS_BOTTOM) {
    is_three_line_layout = true;
  }

  if (is_three_line_layout) {
    int total_height = config.battery_toggle ? 2 * step + BATTERY_ICON_H : step + CENTER_ITEM_H;
    int start_y = center.y - total_height / 2;

    f = layer_get_frame(s_weekday_layer); f.origin.y = start_y;        layer_set_frame(s_weekday_layer, f);
    f = layer_get_frame(s_month_layer);   f.origin.y = start_y + step; layer_set_frame(s_month_layer, f);
    layer_set_hidden(s_day_layer, true);

    if (config.battery_toggle) {
      f = layer_get_frame(s_battery_layer); f.origin.y = start_y + 2 * step; layer_set_frame(s_battery_layer, f);
      layer_set_hidden(s_battery_layer, false);
      layer_mark_dirty(s_battery_layer);
    } else {
      layer_set_hidden(s_battery_layer, true);
    }
  } else {
    int total_height = config.battery_toggle ? 3 * step + BATTERY_ICON_H : 2 * step + CENTER_ITEM_H;
    int start_y = center.y - total_height / 2;

    f = layer_get_frame(s_weekday_layer); f.origin.y = start_y;            layer_set_frame(s_weekday_layer, f);
    f = layer_get_frame(s_month_layer);   f.origin.y = start_y + step;     layer_set_frame(s_month_layer, f);
    f = layer_get_frame(s_day_layer);     f.origin.y = start_y + 2 * step; layer_set_frame(s_day_layer, f);
    layer_set_hidden(s_day_layer, false);

    if (config.battery_toggle) {
      f = layer_get_frame(s_battery_layer); f.origin.y = start_y + 3 * step; layer_set_frame(s_battery_layer, f);
      layer_set_hidden(s_battery_layer, false);
      layer_mark_dirty(s_battery_layer);
    } else {
      layer_set_hidden(s_battery_layer, true);
    }
  }
  
  layer_mark_dirty(s_weekday_layer);
  layer_mark_dirty(s_month_layer);
  layer_mark_dirty(s_day_layer);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_read_first(iterator);
  while (t != NULL) {
    if      (t->key == MESSAGE_KEY_INNER_RING_COLOR)       config.inner_ring_color       = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_SUB_INNER_RING_COLOR)   config.sub_inner_ring_color   = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_MIDDLE_RING_COLOR)      config.middle_ring_color      = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_OUTER_RING_COLOR)       config.outer_ring_color       = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_HIGHLIGHT_FILL_COLOR)   config.highlight_fill_color   = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_LINE_COLOR)             config.line_color             = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_NUMBER_COLOR)           config.number_color           = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_CENTER_TEXT_COLOR)      config.center_text_color      = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_HIGHLIGHT_NUMBER_COLOR) config.highlight_number_color = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_BACKGROUND_COLOR)       config.background_color       = GColorFromHEX(t->value->int32);
    else if (t->key == MESSAGE_KEY_HIGHLIGHT_POSITION)     config.highlight_position     = atoi(t->value->cstring);
    else if (t->key == MESSAGE_KEY_ANIM_FPS)               config.anim_fps               = t->value->int32;
    else if (t->key == MESSAGE_KEY_ANIMATION_TOGGLE)       config.animation_toggle       = t->value->int32 == 1;
    else if (t->key == MESSAGE_KEY_INERTIA_TOGGLE)         config.inertia_toggle         = t->value->int32 == 1;
    else if (t->key == MESSAGE_KEY_BATTERY_TOGGLE)         config.battery_toggle         = t->value->int32 == 1;
    else if (t->key == MESSAGE_KEY_INVERT_BW)              config.invert_bw              = t->value->int32 == 1;
    t = dict_read_next(iterator);
  }

  save_config();
  reposition_center_layers();
  
  layer_mark_dirty(s_bg_layer);
  for (int i = 0; i < 4; i++) layer_mark_dirty(s_ring_layers[i]);
  layer_mark_dirty(s_month_layer);
  layer_mark_dirty(s_day_layer);
  layer_mark_dirty(s_weekday_layer);
  update_time();
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int hour = tick_time->tm_hour;
  if (!clock_is_24h_style()) {
    if (hour == 0) hour = 12;
    if (hour > 12) hour -= 12;
  }
  int min = tick_time->tm_min;

  static int prev_hour = -1;
  static int prev_min  = -1;
  bool time_changed = (hour != prev_hour || min != prev_min);

  current_hour_tens = hour / 10;
  current_hour_ones = hour % 10;
  current_minute_tens = min / 10;
  current_minute_ones = min % 10;

  target_hour_tens_angle = (current_hour_tens * TRIG_MAX_ANGLE) / rings[0].num_items;
  target_hour_ones_angle = (current_hour_ones * TRIG_MAX_ANGLE) / rings[1].num_items;
  target_min_tens_angle  = (current_minute_tens * TRIG_MAX_ANGLE) / rings[2].num_items;
  target_min_ones_angle  = (current_minute_ones * TRIG_MAX_ANGLE) / rings[3].num_items;

  if (time_changed) {
    prev_hour = hour;
    prev_min  = min;

    anim_hour_tens_angle %= TRIG_MAX_ANGLE; if (anim_hour_tens_angle < 0) anim_hour_tens_angle += TRIG_MAX_ANGLE;
    anim_hour_ones_angle %= TRIG_MAX_ANGLE; if (anim_hour_ones_angle < 0) anim_hour_ones_angle += TRIG_MAX_ANGLE;
    anim_min_tens_angle  %= TRIG_MAX_ANGLE; if (anim_min_tens_angle  < 0) anim_min_tens_angle  += TRIG_MAX_ANGLE;
    anim_min_ones_angle  %= TRIG_MAX_ANGLE; if (anim_min_ones_angle  < 0) anim_min_ones_angle  += TRIG_MAX_ANGLE;

    if (config.animation_toggle) {
      if (target_min_ones_angle  < anim_min_ones_angle)  target_min_ones_angle  += TRIG_MAX_ANGLE;
      if (target_min_tens_angle  < anim_min_tens_angle)  target_min_tens_angle  += TRIG_MAX_ANGLE;
      if (target_hour_ones_angle < anim_hour_ones_angle) target_hour_ones_angle += TRIG_MAX_ANGLE;
      if (target_hour_tens_angle < anim_hour_tens_angle) target_hour_tens_angle += TRIG_MAX_ANGLE;

      schedule_ring_anim(&s_hour_tens_anim, anim_hour_tens_angle, target_hour_tens_angle, &anim_hour_tens_angle, 0,                    s_ring_layers[0]);
      schedule_ring_anim(&s_hour_ones_anim, anim_hour_ones_angle, target_hour_ones_angle, &anim_hour_ones_angle, ANIM_DELAY_STEP_MS,   s_ring_layers[1]);
      schedule_ring_anim(&s_min_tens_anim,  anim_min_tens_angle,  target_min_tens_angle,  &anim_min_tens_angle,  ANIM_DELAY_STEP_MS*2, s_ring_layers[2]);
      schedule_ring_anim(&s_min_ones_anim,  anim_min_ones_angle,  target_min_ones_angle,  &anim_min_ones_angle,  ANIM_DELAY_STEP_MS*3, s_ring_layers[3]);
    }
  }

  if (!config.animation_toggle) {
    anim_hour_tens_angle = target_hour_tens_angle;
    anim_hour_ones_angle = target_hour_ones_angle;
    anim_min_tens_angle  = target_min_tens_angle;
    anim_min_ones_angle  = target_min_ones_angle;
    for (int i = 0; i < 4; i++) layer_mark_dirty(s_ring_layers[i]);
  }

  static int prev_month   = -1;
  static int prev_day     = -1;
  static int prev_weekday = -1;

  bool month_changed   = (current_month   != prev_month);
  bool day_changed     = (current_day     != prev_day);
  bool weekday_changed = (current_weekday != prev_weekday);

  prev_month   = current_month;
  prev_day     = current_day;
  prev_weekday = current_weekday;

  bool is_three_line_layout = false;
#ifdef PBL_ROUND
  is_three_line_layout = true;
#endif
  if (config.highlight_position == POS_TOP || config.highlight_position == POS_BOTTOM) {
    is_three_line_layout = true;
  }

  if (month_changed) {
    layer_mark_dirty(s_month_layer);
  }

  if (day_changed) {
    if (is_three_line_layout) {
      layer_mark_dirty(s_month_layer);
    } else {
      layer_mark_dirty(s_day_layer);
    }
  }
  
  if (weekday_changed) layer_mark_dirty(s_weekday_layer);
}

static char s_day_buffer[4];
static bool battery_is_charging = false;

static void battery_callback(BatteryChargeState state) {
  battery_level       = state.charge_percent;
  battery_is_charging = state.is_charging;
  layer_mark_dirty(s_battery_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  current_month = tick_time->tm_mon + 1;
  if (current_day != tick_time->tm_mday) {
    current_day = tick_time->tm_mday;
    snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", current_day);
  }
  current_weekday = tick_time->tm_wday;
  update_time();
}

static void draw_battery_icon(GContext *ctx, GRect rect, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_rect(ctx, GRect(rect.origin.x, rect.origin.y, rect.size.w - 3, rect.size.h));
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(rect.origin.x + rect.size.w - 3,
                                rect.origin.y + rect.size.h/4, 3, rect.size.h/2), 0, GCornerNone);
}

static GColor center_text_color(void) {
#ifdef PBL_COLOR
  return config.center_text_color;
#else
  return config.invert_bw ? GColorBlack : GColorWhite;
#endif
}

static void month_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());

  bool is_three_line_layout = false;
#ifdef PBL_ROUND
  is_three_line_layout = true;
#endif
  if (config.highlight_position == POS_TOP || config.highlight_position == POS_BOTTOM) {
    is_three_line_layout = true;
  }

  if (is_three_line_layout) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", MONTH_NAMES[(current_month - 1) % 12], current_day);
    graphics_draw_text(ctx, buf, s_date_font,
      GRect(0, -4, bounds.size.w, bounds.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(ctx, MONTH_NAMES[(current_month - 1) % 12], s_date_font,
      GRect(0, -4, bounds.size.w, bounds.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void day_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());
  graphics_draw_text(ctx, s_day_buffer, s_date_font,
    GRect(0, -4, bounds.size.w, bounds.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void weekday_update_proc(Layer *layer, GContext *ctx) {
  static const char *const WEEKDAY_NAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());
  graphics_draw_text(ctx, WEEKDAY_NAMES[current_weekday % 7], s_date_font,
    GRect(0, -4, bounds.size.w, bounds.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  (void)layer;
  GColor icon_col = center_text_color();
  draw_battery_icon(ctx, GRect(0, 0, BATTERY_ICON_W, BATTERY_ICON_H), icon_col);
  int fill_w = ((BATTERY_BODY_W - 4) * battery_level) / 100;
#ifdef PBL_COLOR
  GColor fill_color = battery_is_charging   ? GColorGreen
                    : (battery_level <= 20) ? GColorRed
                    :                         icon_col;
#else
  GColor fill_color = icon_col;
#endif
  graphics_context_set_fill_color(ctx, fill_color);
  graphics_fill_rect(ctx, GRect(2, 2, fill_w, BATTERY_ICON_H - 4), 0, GCornerNone);
}

static void main_window_load(Window *window) {
  Layer *wl = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(wl);
  GPoint center = grect_center_point(&bounds);

  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_update_proc);
  layer_add_child(wl, s_bg_layer);

  for (int i = 0; i < 4; i++) {
    s_ring_layers[i] = layer_create_with_data(bounds, sizeof(int));
    *(int*)layer_get_data(s_ring_layers[i]) = i;
    layer_set_update_proc(s_ring_layers[i], ring_update_proc);
    layer_add_child(wl, s_ring_layers[i]);
  }

  s_weekday_layer = layer_create(GRect(center.x - CENTER_ITEM_W/2, 0, CENTER_ITEM_W, CENTER_ITEM_H));
  layer_set_clips(s_weekday_layer, true);
  layer_set_update_proc(s_weekday_layer, weekday_update_proc);
  layer_add_child(wl, s_weekday_layer);

  s_month_layer = layer_create(GRect(center.x - CENTER_MONTHDAY_W/2, 0, CENTER_MONTHDAY_W, CENTER_ITEM_H));
  layer_set_clips(s_month_layer, true);
  layer_set_update_proc(s_month_layer, month_update_proc);
  layer_add_child(wl, s_month_layer);

  s_day_layer = layer_create(GRect(center.x - CENTER_ITEM_W/2, 0, CENTER_ITEM_W, CENTER_ITEM_H));
  layer_set_clips(s_day_layer, true);
  layer_set_update_proc(s_day_layer, day_update_proc);
  layer_add_child(wl, s_day_layer);

  s_battery_layer = layer_create(GRect(center.x - BATTERY_BODY_W/2, 0, BATTERY_ICON_W, BATTERY_ICON_H));
  layer_set_clips(s_battery_layer, true);
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(wl, s_battery_layer);

#if PBL_DISPLAY_WIDTH >= 200
  s_number_font = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  s_date_font   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#else
  s_number_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  #ifdef PBL_ROUND
  s_date_font   = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  #else
  s_date_font   = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  #endif
#endif

  battery_callback(battery_state_service_peek());

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  current_month   = tick_time->tm_mon + 1;
  current_day     = tick_time->tm_mday;
  snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", current_day);
  current_weekday = tick_time->tm_wday;

  anim_hour_tens_angle = anim_hour_ones_angle = 0;
  anim_min_tens_angle  = anim_min_ones_angle  = 0;

  for (int i = 0; i < 4; i++) s_delay_timers[i] = NULL;

  reposition_center_layers();
  update_time();
}

static void main_window_unload(Window *window) {
  for (int i = 0; i < 4; i++) {
    if (s_delay_timers[i]) {
      app_timer_cancel(s_delay_timers[i]);
      s_delay_timers[i] = NULL;
    }
  }

  PropertyAnimation **slots[4] = {
    &s_hour_tens_anim, &s_hour_ones_anim, &s_min_tens_anim, &s_min_ones_anim
  };
  for (int i = 0; i < 4; i++) {
    if (*slots[i]) {
      Animation *old = (Animation*)*slots[i];
      *slots[i] = NULL;
      animation_unschedule(old);
      animation_destroy(old);
    }
  }

  layer_destroy(s_month_layer);
  layer_destroy(s_day_layer);
  layer_destroy(s_weekday_layer);
  layer_destroy(s_battery_layer);
  for (int i = 0; i < 4; i++) layer_destroy(s_ring_layers[i]);
  layer_destroy(s_bg_layer);
}

static void init() {
  init_default_config();
  load_config();

  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(512, 512);

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load   = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
}

static void deinit() {
  battery_state_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
