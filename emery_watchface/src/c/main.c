#include <pebble.h>
#include "emery_watchface.h"

WatchConfig config;

// 0: Innermost, 1: Sub-Inner, 2: Middle, 3: Outer
RingDef rings[4] = {
  { .width = 60,  .height = 90,  .corner_radius = 10, .num_items = 3 },  // Innermost (0-2 hour tens)
  { .width = 100, .height = 130, .corner_radius = 15, .num_items = 10 }, // Sub-Inner (0-9 hour ones)
  { .width = 140, .height = 170, .corner_radius = 20, .num_items = 6 },  // Middle (0-5 minute tens)
  { .width = 180, .height = 210, .corner_radius = 25, .num_items = 10 }  // Outer (0-9 minute ones)
};

static Window *s_main_window;
static Layer *s_canvas_layer;
static Layer *s_month_layer;
static Layer *s_day_layer;
static Layer *s_weekday_layer;
static Layer *s_battery_layer;

// English Month Names
static const char *const MONTH_NAMES[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// State variables for time
static int current_hour_tens = 0;
static int current_hour_ones = 0;
static int current_minute_tens = 0;
static int current_minute_ones = 0;

static int current_month = 1;
static int current_day = 1;
static int current_weekday = 0; // 0=Sun
static int battery_level = 100;

// Animation values (TRIG_MAX_ANGLE represents a full loop)
static int32_t anim_hour_tens_angle = 0;
static int32_t anim_hour_ones_angle = 0;
static int32_t anim_min_tens_angle = 0;
static int32_t anim_min_ones_angle = 0;

static PropertyAnimation *s_hour_tens_anim = NULL;
static PropertyAnimation *s_hour_ones_anim = NULL;
static PropertyAnimation *s_min_tens_anim = NULL;
static PropertyAnimation *s_min_ones_anim = NULL;

static PropertyAnimation *s_month_anim = NULL;
static PropertyAnimation *s_day_anim = NULL;
static PropertyAnimation *s_weekday_anim = NULL;

typedef struct {
  int32_t from;
  int32_t to;
  int32_t *target_var;
} AnimCtx;

static void anim_stopped_handler(Animation *animation, bool finished, void *context) {
  void **ptr_tuple = (void **)context;
  PropertyAnimation **anim_ptr = (PropertyAnimation **)ptr_tuple[0];
  AnimCtx *ctx = (AnimCtx *)ptr_tuple[1];

  if (anim_ptr && *anim_ptr == (PropertyAnimation *)animation) {
    *anim_ptr = NULL;
    layer_mark_dirty(s_canvas_layer);
  }
  free(ctx);
  free(ptr_tuple);
  animation_destroy(animation);
}

static int16_t anim_month_y = 24;
static int16_t anim_day_y = 24;
static int16_t anim_weekday_y = 24;

static int32_t target_hour_tens_angle = 0;
static int32_t target_hour_ones_angle = 0;
static int32_t target_min_tens_angle = 0;
static int32_t target_min_ones_angle = 0;

static void angle_anim_update(Animation *anim, const AnimationProgress progress) {
  void **ptr_tuple = (void **)animation_get_context(anim);
  AnimCtx *ctx = (AnimCtx *)ptr_tuple[1];
  int32_t current = ctx->from + ((ctx->to - ctx->from) * (int32_t)progress) / ANIMATION_NORMALIZED_MAX;
  *ctx->target_var = current;
  layer_mark_dirty(s_canvas_layer);
}

static void date_anim_update(Animation *anim, const AnimationProgress progress) {
  void **ptr_tuple = (void **)animation_get_context(anim);
  AnimCtx *ctx = (AnimCtx *)ptr_tuple[1];
  int32_t current = ctx->from + ((ctx->to - ctx->from) * (int32_t)progress) / ANIMATION_NORMALIZED_MAX;
  *ctx->target_var = current;

  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(window_layer);
  GPoint center = grect_center_point(&bounds);
  int start_y = center.y - 39;

  if (ctx->target_var == (int32_t*)&anim_month_y) {
    GRect frame = layer_get_frame(s_month_layer);
    frame.origin.y = start_y + (int16_t)current - 3;
    layer_set_frame(s_month_layer, frame);
  } else if (ctx->target_var == (int32_t*)&anim_day_y) {
    GRect frame = layer_get_frame(s_day_layer);
    frame.origin.y = start_y + 20 + (int16_t)current - 3;
    layer_set_frame(s_day_layer, frame);
  } else if (ctx->target_var == (int32_t*)&anim_weekday_y) {
    GRect frame = layer_get_frame(s_weekday_layer);
    frame.origin.y = start_y + 40 + (int16_t)current - 3;
    layer_set_frame(s_weekday_layer, frame);
  }
}

static const AnimationImplementation angle_anim_impl = { .update = angle_anim_update };
static const AnimationImplementation date_anim_impl = { .update = date_anim_update };

static PropertyAnimation* create_anim(const AnimationImplementation *impl, int32_t from, int32_t to, int32_t *target, PropertyAnimation **ptr_to_store) {
  AnimCtx *ctx = malloc(sizeof(AnimCtx));
  ctx->from = from;
  ctx->to = to;
  ctx->target_var = target;

  Animation *anim = animation_create();
  animation_set_implementation(anim, impl);

  void **ptr_tuple = malloc(sizeof(void*) * 2);
  ptr_tuple[0] = ptr_to_store;
  ptr_tuple[1] = ctx;

  animation_set_handlers(anim, (AnimationHandlers) { .stopped = anim_stopped_handler }, ptr_tuple);
  return (PropertyAnimation*)anim;
}

// Font
static GFont s_number_font;
static GFont s_date_font;

// Config Init
void init_default_config() {
  config.inner_ring_color = GColorBlack;
  config.sub_inner_ring_color = GColorBlack;
  config.middle_ring_color = GColorBlack;
  config.outer_ring_color = GColorBlack;
  config.highlight_fill_color = GColorRed;
  config.line_color = GColorDarkGray;
  config.number_color = GColorLightGray;
  config.center_text_color = GColorWhite;
  config.highlight_number_color = GColorWhite;
  config.highlight_position = POS_RIGHT;
  config.animation_toggle = true;
}

// Math Utility: Calculate point on a rounded rect perimeter
GPoint get_point_on_rounded_rect(int w, int h, int r, int32_t angle) {
  angle = angle % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;

  int str_h = w - 2*r; // horizontal straight segment
  int str_v = h - 2*r; // vertical straight segment

  int approx_perimeter = 2 * str_h + 2 * str_v + (314 * 2 * r) / 100;

  int target_dist = (int)(((int64_t)angle * approx_perimeter) / TRIG_MAX_ANGLE);

  int x = 0, y = 0;
  int current_dist = 0;

  // Top right straight
  if (target_dist <= current_dist + str_h / 2) {
    x = (target_dist - current_dist);
    y = -h/2;
    return GPoint(x, y);
  }
  current_dist += str_h / 2;

  // Top right corner
  int q_arc = (314 * r) / 200; // quarter circle length
  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = w/2 - r + (sin_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    y = -h/2 + r - (cos_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    return GPoint(x, y);
  }
  current_dist += q_arc;

  // Right straight
  if (target_dist <= current_dist + str_v) {
    x = w/2;
    y = -h/2 + r + (target_dist - current_dist);
    return GPoint(x, y);
  }
  current_dist += str_v;

  // Bottom right corner
  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = TRIG_MAX_ANGLE/4 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = w/2 - r + (sin_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    y = h/2 - r - (cos_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    return GPoint(x, y);
  }
  current_dist += q_arc;

  // Bottom straight
  if (target_dist <= current_dist + str_h) {
    x = w/2 - r - (target_dist - current_dist);
    y = h/2;
    return GPoint(x, y);
  }
  current_dist += str_h;

  // Bottom left corner
  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = TRIG_MAX_ANGLE/2 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = -w/2 + r + (sin_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    y = h/2 - r - (cos_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    return GPoint(x, y);
  }
  current_dist += q_arc;

  // Left straight
  if (target_dist <= current_dist + str_v) {
    x = -w/2;
    y = h/2 - r - (target_dist - current_dist);
    return GPoint(x, y);
  }
  current_dist += str_v;

  // Top left corner
  if (target_dist <= current_dist + q_arc) {
    int arc_dist = target_dist - current_dist;
    int32_t arc_angle = 3*TRIG_MAX_ANGLE/4 + (arc_dist * TRIG_MAX_ANGLE / 4) / q_arc;
    x = -w/2 + r + (sin_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    y = -h/2 + r - (cos_lookup(arc_angle) * r) / TRIG_MAX_RATIO;
    return GPoint(x, y);
  }
  current_dist += q_arc;

  // Top left straight
  x = -w/2 + r + (target_dist - current_dist);
  y = -h/2;
  return GPoint(x, y);
}

// Drawing function
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, config.line_color);

  // Draw Rings Fills and Borders
  GColor ring_colors[4] = {config.inner_ring_color, config.sub_inner_ring_color, config.middle_ring_color, config.outer_ring_color};
  for(int i=3; i>=0; i--) { // draw outer first
    GRect rect = GRect(center.x - rings[i].width/2, center.y - rings[i].height/2, rings[i].width, rings[i].height);
    graphics_context_set_fill_color(ctx, ring_colors[i]);
    graphics_fill_rect(ctx, rect, rings[i].corner_radius, GCornersAll);
    graphics_draw_round_rect(ctx, rect, rings[i].corner_radius);
  }

  // Determine highlight target angle
  int32_t target_angle = 0;
  if(config.highlight_position == POS_RIGHT) target_angle = TRIG_MAX_ANGLE / 4;
  else if(config.highlight_position == POS_BOTTOM) target_angle = TRIG_MAX_ANGLE / 2;
  else if(config.highlight_position == POS_LEFT) target_angle = 3 * TRIG_MAX_ANGLE / 4;
  else target_angle = 0; // POS_TOP

  // Draw highlight boxes
  graphics_context_set_fill_color(ctx, config.highlight_fill_color);
  for(int i=0; i<4; i++) {
    GPoint pt = get_point_on_rounded_rect(rings[i].width, rings[i].height, rings[i].corner_radius, target_angle);
    GRect hl_rect = GRect(center.x + pt.x - 12, center.y + pt.y - 12, 24, 24);
    graphics_fill_rect(ctx, hl_rect, 0, GCornerNone);
    graphics_draw_rect(ctx, hl_rect);
  }

  // Draw Numbers
  graphics_context_set_text_color(ctx, config.number_color);
  char buf[8];

  // Ring 0: Hour tens (0-2)
  for(int i=0; i<rings[0].num_items; i++) {
    int32_t base_angle = anim_hour_tens_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[0].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    graphics_context_set_text_color(ctx, (s_hour_tens_anim == NULL && i == current_hour_tens) ? config.highlight_number_color : config.number_color);

    GPoint pt = get_point_on_rounded_rect(rings[0].width, rings[0].height, rings[0].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", i);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 13, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Ring 1: Hour ones (0-9)
  for(int i=0; i<rings[1].num_items; i++) {
    int32_t base_angle = anim_hour_ones_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[1].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    graphics_context_set_text_color(ctx, (s_hour_ones_anim == NULL && i == current_hour_ones) ? config.highlight_number_color : config.number_color);

    GPoint pt = get_point_on_rounded_rect(rings[1].width, rings[1].height, rings[1].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", i);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 13, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Ring 2: Minute tens (0-5)
  for(int i=0; i<rings[2].num_items; i++) {
    int32_t base_angle = anim_min_tens_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[2].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    graphics_context_set_text_color(ctx, (s_min_tens_anim == NULL && i == current_minute_tens) ? config.highlight_number_color : config.number_color);

    GPoint pt = get_point_on_rounded_rect(rings[2].width, rings[2].height, rings[2].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", i);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 13, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Ring 3: Minute ones (0-9)
  for(int i=0; i<rings[3].num_items; i++) {
    int32_t base_angle = anim_min_ones_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[3].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    graphics_context_set_text_color(ctx, (s_min_ones_anim == NULL && i == current_minute_ones) ? config.highlight_number_color : config.number_color);

    GPoint pt = get_point_on_rounded_rect(rings[3].width, rings[3].height, rings[3].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", i);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 13, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

}

static void update_time();

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_read_first(iterator);
  while(t != NULL) {
    if(t->key == MESSAGE_KEY_INNER_RING_COLOR) {
      config.inner_ring_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_SUB_INNER_RING_COLOR) {
      config.sub_inner_ring_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_MIDDLE_RING_COLOR) {
      config.middle_ring_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_OUTER_RING_COLOR) {
      config.outer_ring_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_HIGHLIGHT_FILL_COLOR) {
      config.highlight_fill_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_LINE_COLOR) {
      config.line_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_NUMBER_COLOR) {
      config.number_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_CENTER_TEXT_COLOR) {
      config.center_text_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_HIGHLIGHT_NUMBER_COLOR) {
      config.highlight_number_color = GColorFromHEX(t->value->int32);
    } else if(t->key == MESSAGE_KEY_HIGHLIGHT_POSITION) {
      config.highlight_position = atoi(t->value->cstring);
    } else if(t->key == MESSAGE_KEY_ANIMATION_TOGGLE) {
      config.animation_toggle = t->value->int32 == 1;
    }
    t = dict_read_next(iterator);
  }

  update_time();
}

static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int hour = tick_time->tm_hour;
  if (!clock_is_24h_style()) {
    if(hour == 0) hour = 12;
    if(hour > 12) hour -= 12;
  }

  current_hour_tens = hour / 10;
  current_hour_ones = hour % 10;

  int min = tick_time->tm_min;
  current_minute_tens = min / 10;
  current_minute_ones = min % 10;

  target_hour_tens_angle = (current_hour_tens * TRIG_MAX_ANGLE) / rings[0].num_items;
  target_hour_ones_angle = (current_hour_ones * TRIG_MAX_ANGLE) / rings[1].num_items;
  target_min_tens_angle = (current_minute_tens * TRIG_MAX_ANGLE) / rings[2].num_items;
  target_min_ones_angle = (current_minute_ones * TRIG_MAX_ANGLE) / rings[3].num_items;

  // Normalize current angles to [0, TRIG_MAX_ANGLE) to ensure forward animation
  anim_hour_tens_angle %= TRIG_MAX_ANGLE;
  if (anim_hour_tens_angle < 0) anim_hour_tens_angle += TRIG_MAX_ANGLE;
  anim_hour_ones_angle %= TRIG_MAX_ANGLE;
  if (anim_hour_ones_angle < 0) anim_hour_ones_angle += TRIG_MAX_ANGLE;
  anim_min_tens_angle %= TRIG_MAX_ANGLE;
  if (anim_min_tens_angle < 0) anim_min_tens_angle += TRIG_MAX_ANGLE;
  anim_min_ones_angle %= TRIG_MAX_ANGLE;
  if (anim_min_ones_angle < 0) anim_min_ones_angle += TRIG_MAX_ANGLE;

  static int prev_month = -1;
  static int prev_day = -1;
  static int prev_weekday = -1;

  bool month_changed = (current_month != prev_month);
  bool day_changed = (current_day != prev_day);
  bool weekday_changed = (current_weekday != prev_weekday);

  prev_month = current_month;
  prev_day = current_day;
  prev_weekday = current_weekday;

  if (config.animation_toggle) {
    if(target_min_ones_angle < anim_min_ones_angle) {
      target_min_ones_angle += TRIG_MAX_ANGLE;
    }
    if(target_min_tens_angle < anim_min_tens_angle) {
      target_min_tens_angle += TRIG_MAX_ANGLE;
    }
    if(target_hour_ones_angle < anim_hour_ones_angle) {
      target_hour_ones_angle += TRIG_MAX_ANGLE;
    }
    if(target_hour_tens_angle < anim_hour_tens_angle) {
      target_hour_tens_angle += TRIG_MAX_ANGLE;
    }

    if(s_hour_tens_anim) animation_unschedule((Animation*)s_hour_tens_anim);
    s_hour_tens_anim = create_anim(&angle_anim_impl, anim_hour_tens_angle, target_hour_tens_angle, &anim_hour_tens_angle, &s_hour_tens_anim);
    animation_set_duration((Animation*)s_hour_tens_anim, 500);
    animation_set_curve((Animation*)s_hour_tens_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_hour_tens_anim);

    if(s_hour_ones_anim) animation_unschedule((Animation*)s_hour_ones_anim);
    s_hour_ones_anim = create_anim(&angle_anim_impl, anim_hour_ones_angle, target_hour_ones_angle, &anim_hour_ones_angle, &s_hour_ones_anim);
    animation_set_duration((Animation*)s_hour_ones_anim, 500);
    animation_set_delay((Animation*)s_hour_ones_anim, 150);
    animation_set_curve((Animation*)s_hour_ones_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_hour_ones_anim);

    if(s_min_tens_anim) animation_unschedule((Animation*)s_min_tens_anim);
    s_min_tens_anim = create_anim(&angle_anim_impl, anim_min_tens_angle, target_min_tens_angle, &anim_min_tens_angle, &s_min_tens_anim);
    animation_set_duration((Animation*)s_min_tens_anim, 500);
    animation_set_delay((Animation*)s_min_tens_anim, 300);
    animation_set_curve((Animation*)s_min_tens_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_min_tens_anim);

    if(s_min_ones_anim) animation_unschedule((Animation*)s_min_ones_anim);
    s_min_ones_anim = create_anim(&angle_anim_impl, anim_min_ones_angle, target_min_ones_angle, &anim_min_ones_angle, &s_min_ones_anim);
    animation_set_duration((Animation*)s_min_ones_anim, 500);
    animation_set_delay((Animation*)s_min_ones_anim, 450);
    animation_set_curve((Animation*)s_min_ones_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_min_ones_anim);

    int32_t target_y = 3;

    if (month_changed) {
      if(s_month_anim) animation_unschedule((Animation*)s_month_anim);
      anim_month_y = 24;
      s_month_anim = create_anim(&date_anim_impl, (int32_t)anim_month_y, target_y, (int32_t*)&anim_month_y, &s_month_anim);
      animation_set_duration((Animation*)s_month_anim, 400);
      animation_set_curve((Animation*)s_month_anim, AnimationCurveEaseOut);
      animation_schedule((Animation*)s_month_anim);
    }

    if (day_changed) {
      if(s_day_anim) animation_unschedule((Animation*)s_day_anim);
      anim_day_y = 24;
      s_day_anim = create_anim(&date_anim_impl, (int32_t)anim_day_y, target_y, (int32_t*)&anim_day_y, &s_day_anim);
      animation_set_duration((Animation*)s_day_anim, 400);
      animation_set_delay((Animation*)s_day_anim, 200);
      animation_set_curve((Animation*)s_day_anim, AnimationCurveEaseOut);
      animation_schedule((Animation*)s_day_anim);
    }

    if (weekday_changed) {
      if(s_weekday_anim) animation_unschedule((Animation*)s_weekday_anim);
      anim_weekday_y = 24;
      s_weekday_anim = create_anim(&date_anim_impl, (int32_t)anim_weekday_y, target_y, (int32_t*)&anim_weekday_y, &s_weekday_anim);
      animation_set_duration((Animation*)s_weekday_anim, 400);
      animation_set_delay((Animation*)s_weekday_anim, 400);
      animation_set_curve((Animation*)s_weekday_anim, AnimationCurveEaseOut);
      animation_schedule((Animation*)s_weekday_anim);
    }

  } else {
    anim_month_y = 3;
    anim_day_y = 3;
    anim_weekday_y = 3;
    anim_hour_tens_angle = target_hour_tens_angle;
    anim_hour_ones_angle = target_hour_ones_angle;
    anim_min_tens_angle = target_min_tens_angle;
    anim_min_ones_angle = target_min_ones_angle;
    layer_mark_dirty(s_canvas_layer);
    layer_mark_dirty(s_month_layer);
    layer_mark_dirty(s_day_layer);
    layer_mark_dirty(s_weekday_layer);
  }
}

static void battery_callback(BatteryChargeState state) {
  battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  current_month = tick_time->tm_mon + 1;
  current_day = tick_time->tm_mday;
  current_weekday = tick_time->tm_wday;

  update_time();
}

static void draw_battery_icon(GContext *ctx, GRect rect, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_rect(ctx, GRect(rect.origin.x, rect.origin.y, rect.size.w - 3, rect.size.h));
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(rect.origin.x + rect.size.w - 3, rect.origin.y + rect.size.h/4, 3, rect.size.h/2), 0, GCornerNone);
}

static void month_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const char *name = MONTH_NAMES[(current_month - 1) % 12];
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, name, s_date_font, GRect(0, -4, bounds.size.w, bounds.size.h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void day_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", current_day);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, -4, bounds.size.w, bounds.size.h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void weekday_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  char buf[8];
  strftime(buf, sizeof(buf), "%a", tick_time);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, -4, bounds.size.w, bounds.size.h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  draw_battery_icon(ctx, GRect(0, 0, bounds.size.w, 18), config.line_color);

  // Fill battery level
  int fill_w = ((bounds.size.w - 7) * battery_level) / 100;
  graphics_context_set_fill_color(ctx, (battery_level <= 20) ? GColorRed : config.line_color);
  graphics_fill_rect(ctx, GRect(2, 2, fill_w, 14), 0, GCornerNone);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", battery_level);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, -4, bounds.size.w - 3, 18), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  GPoint center = grect_center_point(&bounds);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  // Vertical layout in the center
  // Fit within 60x90
  int item_w = 50;
  int item_h = 18;
  int spacing = 2;
  int total_height = item_h * 4 + spacing * 3;
  int start_y = center.y - total_height / 2;

  s_month_layer = layer_create(GRect(center.x - item_w / 2, start_y, item_w, item_h));
  layer_set_clips(s_month_layer, true);
  layer_set_update_proc(s_month_layer, month_update_proc);
  layer_add_child(s_canvas_layer, s_month_layer);

  s_day_layer = layer_create(GRect(center.x - item_w / 2, start_y + (item_h + spacing), item_w, item_h));
  layer_set_clips(s_day_layer, true);
  layer_set_update_proc(s_day_layer, day_update_proc);
  layer_add_child(s_canvas_layer, s_day_layer);

  s_weekday_layer = layer_create(GRect(center.x - item_w / 2, start_y + 2 * (item_h + spacing), item_w, item_h));
  layer_set_clips(s_weekday_layer, true);
  layer_set_update_proc(s_weekday_layer, weekday_update_proc);
  layer_add_child(s_canvas_layer, s_weekday_layer);

  s_battery_layer = layer_create(GRect(center.x - item_w / 2, start_y + 3 * (item_h + spacing), item_w, item_h));
  layer_set_clips(s_battery_layer, true);
  layer_set_update_proc(s_battery_layer, battery_update_proc);
  layer_add_child(s_canvas_layer, s_battery_layer);

  s_number_font = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  battery_callback(battery_state_service_peek());

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  current_month = tick_time->tm_mon + 1;
  current_day = tick_time->tm_mday;
  current_weekday = tick_time->tm_wday;

  // Set initial anim angles to 0 for the first animation on load
  anim_hour_tens_angle = 0;
  anim_hour_ones_angle = 0;
  anim_min_tens_angle = 0;
  anim_min_ones_angle = 0;

  update_time();
}

static void main_window_unload(Window *window) {
  if(s_hour_tens_anim) animation_unschedule((Animation*)s_hour_tens_anim);
  if(s_hour_ones_anim) animation_unschedule((Animation*)s_hour_ones_anim);
  if(s_min_tens_anim) animation_unschedule((Animation*)s_min_tens_anim);
  if(s_min_ones_anim) animation_unschedule((Animation*)s_min_ones_anim);
  if(s_month_anim) animation_unschedule((Animation*)s_month_anim);
  if(s_day_anim) animation_unschedule((Animation*)s_day_anim);
  if(s_weekday_anim) animation_unschedule((Animation*)s_weekday_anim);

  layer_destroy(s_month_layer);
  layer_destroy(s_day_layer);
  layer_destroy(s_weekday_layer);
  layer_destroy(s_battery_layer);
  layer_destroy(s_canvas_layer);
}

static void init() {
  setlocale(LC_ALL, "");
  init_default_config();

  app_message_register_inbox_received(inbox_received_callback);
  const int inbox_size = 512;
  const int outbox_size = 512;
  app_message_open(inbox_size, outbox_size);

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);

  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  battery_state_service_subscribe(battery_callback);
  battery_callback(battery_state_service_peek());
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
