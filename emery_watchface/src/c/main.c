#include <pebble.h>
#include "emery_watchface.h"

WatchConfig config;

// 0: Inner, 1: Middle, 2: Outer
RingDef rings[3] = {
  { .width = 70, .height = 90, .corner_radius = 15, .num_items = 12 }, // Inner (1-12 hours)
  { .width = 110, .height = 130, .corner_radius = 20, .num_items = 6 }, // Middle (0-5 tens minutes)
  { .width = 150, .height = 170, .corner_radius = 25, .num_items = 10 } // Outer (0-9 ones minutes)
};

static Window *s_main_window;
static Layer *s_canvas_layer;
static Layer *s_month_layer;
static Layer *s_day_layer;
static Layer *s_weekday_layer;

// State variables for time
static int current_hour = 0;
static int current_minute_tens = 0;
static int current_minute_ones = 0;

static int current_month = 1;
static int current_day = 1;
static int current_weekday = 0; // 0=Sun
static int battery_level = 100;

// Animation values (TRIG_MAX_ANGLE represents a full loop)
static int32_t anim_hour_angle = 0;
static int32_t anim_min_tens_angle = 0;
static int32_t anim_min_ones_angle = 0;

static PropertyAnimation *s_hour_anim = NULL;
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
  }
  free(ctx);
  free(ptr_tuple);
  animation_destroy(animation);
}

static int16_t anim_month_y = 24;
static int16_t anim_day_y = 24;
static int16_t anim_weekday_y = 24;

static int32_t target_hour_angle = 0;
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
  if (ctx->target_var == (int32_t*)&anim_month_y) layer_mark_dirty(s_month_layer);
  else if (ctx->target_var == (int32_t*)&anim_day_y) layer_mark_dirty(s_day_layer);
  else if (ctx->target_var == (int32_t*)&anim_weekday_y) layer_mark_dirty(s_weekday_layer);
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
static GFont s_time_font;
static GFont s_number_font;
static GFont s_date_font;

// Config Init
void init_default_config() {
  config.inner_ring_color = GColorBlack;
  config.middle_ring_color = GColorBlack;
  config.outer_ring_color = GColorBlack;
  config.highlight_fill_color = GColorRed;
  config.line_color = GColorWhite;
  config.number_color = GColorWhite;
  config.center_text_color = GColorWhite;
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

  center.y -= 25;

  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_stroke_color(ctx, config.line_color);

  // Draw Rings Fills and Borders
  GColor ring_colors[3] = {config.inner_ring_color, config.middle_ring_color, config.outer_ring_color};
  for(int i=2; i>=0; i--) { // draw outer first
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
  for(int i=0; i<3; i++) {
    GPoint pt = get_point_on_rounded_rect(rings[i].width, rings[i].height, rings[i].corner_radius, target_angle);
    GRect hl_rect = GRect(center.x + pt.x - 12, center.y + pt.y - 12, 24, 24);
    graphics_fill_rect(ctx, hl_rect, 0, GCornerNone);
    graphics_draw_rect(ctx, hl_rect);
  }

  // Draw Numbers
  graphics_context_set_text_color(ctx, config.number_color);
  char buf[8];

  for(int i=0; i<rings[0].num_items; i++) {
    int num = i + 1; // 1 to 12
    if (clock_is_24h_style()) {
      time_t temp = time(NULL);
      struct tm *tick_time = localtime(&temp);
      if (tick_time->tm_hour >= 12) {
        // PM sequence: 13, 14, ..., 23, 0
        num = i + 13;
        if (num >= 24) num -= 24;
      }
    }
    int32_t base_angle = anim_hour_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[0].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    GPoint pt = get_point_on_rounded_rect(rings[0].width, rings[0].height, rings[0].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", num);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 10, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  for(int i=0; i<rings[1].num_items; i++) {
    int num = i; // 0 to 5
    int32_t base_angle = anim_min_tens_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[1].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    GPoint pt = get_point_on_rounded_rect(rings[1].width, rings[1].height, rings[1].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", num);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 10, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  for(int i=0; i<rings[2].num_items; i++) {
    int num = i; // 0 to 9
    int32_t base_angle = anim_min_ones_angle;
    int32_t item_offset = (i * TRIG_MAX_ANGLE) / rings[2].num_items;
    int32_t angle = target_angle + item_offset - base_angle;

    GPoint pt = get_point_on_rounded_rect(rings[2].width, rings[2].height, rings[2].corner_radius, angle);
    snprintf(buf, sizeof(buf), "%d", num);
    graphics_draw_text(ctx, buf, s_number_font, GRect(center.x + pt.x - 15, center.y + pt.y - 10, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Draw Center Text (HH:MM)
  char time_buf[6];
  int display_hour = current_hour;
  if (!clock_is_24h_style()) {
    if(display_hour == 0) display_hour = 12;
    if(display_hour > 12) display_hour -= 12;
  }
  snprintf(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%02d:%02d" : "%d:%02d", display_hour, current_minute_tens*10 + current_minute_ones);
  graphics_context_set_text_color(ctx, config.center_text_color);
  graphics_draw_text(ctx, time_buf, s_time_font, GRect(center.x - 40, center.y - 20, 80, 40), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // Draw Date Section (Month, Day, Weekday)
  int date_y = center.y + 75;
  int box_w = 30;
  int box_h = 24;
  int gap = 10;
  int total_w = box_w * 3 + gap * 2;
  int start_x = center.x - total_w / 2;

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, config.line_color);

  for(int i=0; i<3; i++) {
    graphics_draw_rect(ctx, GRect(start_x + i * (box_w + gap), date_y, box_w, box_h));
  }

  // Draw Battery
  int batt_y = date_y + box_h + 6;
  GRect batt_rect = GRect(center.x - 15, batt_y, 30, 14);
  graphics_draw_rect(ctx, batt_rect);
  graphics_draw_rect(ctx, GRect(center.x + 15, batt_y + 3, 3, 8));

  // Battery Fill & Text
  int fill_w = (26 * battery_level) / 100;
  if (battery_level <= 20) {
    graphics_context_set_fill_color(ctx, GColorRed);
  } else {
    graphics_context_set_fill_color(ctx, config.line_color);
  }
  graphics_fill_rect(ctx, GRect(center.x - 13, batt_y + 2, fill_w, 10), 0, GCornerNone);

  char batt_buf[8];
  snprintf(batt_buf, sizeof(batt_buf), "%d%%", battery_level);
  graphics_draw_text(ctx, batt_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(center.x + 20, batt_y - 3, 30, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void update_time();

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_read_first(iterator);
  while(t != NULL) {
    if(t->key == MESSAGE_KEY_INNER_RING_COLOR) {
      config.inner_ring_color = GColorFromHEX(t->value->int32);
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
  if (clock_is_24h_style()) {
    int h_idx;
    if (hour >= 12) {
       // 12PM is index 11 (number 0), 13PM is index 0 (number 13)
       if (hour == 12) h_idx = 11;
       else h_idx = hour - 13;
    } else {
       // 0AM is index 11 (number 0), 1AM is index 0 (number 1)
       if (hour == 0) h_idx = 11;
       else h_idx = hour - 1;
    }
    target_hour_angle = (h_idx * TRIG_MAX_ANGLE) / rings[0].num_items;
  }

  // Normalize current angles to [0, TRIG_MAX_ANGLE) to ensure forward animation
  anim_hour_angle %= TRIG_MAX_ANGLE;
  if (anim_hour_angle < 0) anim_hour_angle += TRIG_MAX_ANGLE;
  anim_min_tens_angle %= TRIG_MAX_ANGLE;
  if (anim_min_tens_angle < 0) anim_min_tens_angle += TRIG_MAX_ANGLE;
  anim_min_ones_angle %= TRIG_MAX_ANGLE;
  if (anim_min_ones_angle < 0) anim_min_ones_angle += TRIG_MAX_ANGLE;

  current_hour = tick_time->tm_hour;

  int min = tick_time->tm_min;
  current_minute_tens = min / 10;
  current_minute_ones = min % 10;

  if (!clock_is_24h_style()) {
    int h = current_hour;
    if(h == 0) h = 12;
    if(h > 12) h -= 12;
    target_hour_angle = ((h - 1) * TRIG_MAX_ANGLE) / rings[0].num_items;
  }
  target_min_tens_angle = (current_minute_tens * TRIG_MAX_ANGLE) / rings[1].num_items;
  target_min_ones_angle = (current_minute_ones * TRIG_MAX_ANGLE) / rings[2].num_items;

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
    if(target_hour_angle < anim_hour_angle) {
      target_hour_angle += TRIG_MAX_ANGLE;
    }

    if(s_hour_anim) animation_unschedule((Animation*)s_hour_anim);
    s_hour_anim = create_anim(&angle_anim_impl, anim_hour_angle, target_hour_angle, &anim_hour_angle, &s_hour_anim);
    animation_set_duration((Animation*)s_hour_anim, 500);
    animation_set_curve((Animation*)s_hour_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_hour_anim);

    if(s_min_tens_anim) animation_unschedule((Animation*)s_min_tens_anim);
    s_min_tens_anim = create_anim(&angle_anim_impl, anim_min_tens_angle, target_min_tens_angle, &anim_min_tens_angle, &s_min_tens_anim);
    animation_set_duration((Animation*)s_min_tens_anim, 500);
    animation_set_delay((Animation*)s_min_tens_anim, 200);
    animation_set_curve((Animation*)s_min_tens_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_min_tens_anim);

    if(s_min_ones_anim) animation_unschedule((Animation*)s_min_ones_anim);
    s_min_ones_anim = create_anim(&angle_anim_impl, anim_min_ones_angle, target_min_ones_angle, &anim_min_ones_angle, &s_min_ones_anim);
    animation_set_duration((Animation*)s_min_ones_anim, 500);
    animation_set_delay((Animation*)s_min_ones_anim, 400);
    animation_set_curve((Animation*)s_min_ones_anim, AnimationCurveEaseInOut);
    animation_schedule((Animation*)s_min_ones_anim);

    int32_t target_y = 2;

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
    anim_month_y = 2;
    anim_day_y = 2;
    anim_weekday_y = 2;
    anim_hour_angle = target_hour_angle;
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
  layer_mark_dirty(s_canvas_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  current_month = tick_time->tm_mon + 1;
  current_day = tick_time->tm_mday;
  current_weekday = tick_time->tm_wday;

  update_time();
}

static void month_update_proc(Layer *layer, GContext *ctx) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", current_month);
  graphics_context_set_text_color(ctx, config.line_color);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, anim_month_y, 30, 24), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void day_update_proc(Layer *layer, GContext *ctx) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", current_day);
  graphics_context_set_text_color(ctx, config.line_color);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, anim_day_y, 30, 24), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void weekday_update_proc(Layer *layer, GContext *ctx) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  char buf[8];
  strftime(buf, sizeof(buf), "%a", tick_time);
  // Strftime might return capitalized "Sun", "Mon"... we keep it for consistency with system localization
  graphics_context_set_text_color(ctx, config.line_color);
  graphics_draw_text(ctx, buf, s_date_font, GRect(0, anim_weekday_y, 30, 24), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  GPoint center = grect_center_point(&bounds);
  center.y -= 25;

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  int date_y = center.y + 75;
  int box_w = 30;
  int box_h = 24;
  int gap = 10;
  int total_w = box_w * 3 + gap * 2;
  int start_x = center.x - total_w / 2;

  s_month_layer = layer_create(GRect(start_x, date_y, box_w, box_h));
  layer_set_clips(s_month_layer, true);
  layer_set_update_proc(s_month_layer, month_update_proc);
  layer_add_child(s_canvas_layer, s_month_layer);

  s_day_layer = layer_create(GRect(start_x + box_w + gap, date_y, box_w, box_h));
  layer_set_clips(s_day_layer, true);
  layer_set_update_proc(s_day_layer, day_update_proc);
  layer_add_child(s_canvas_layer, s_day_layer);

  s_weekday_layer = layer_create(GRect(start_x + 2*(box_w + gap), date_y, box_w, box_h));
  layer_set_clips(s_weekday_layer, true);
  layer_set_update_proc(s_weekday_layer, weekday_update_proc);
  layer_add_child(s_canvas_layer, s_weekday_layer);

  s_time_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_number_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  current_month = tick_time->tm_mon + 1;
  current_day = tick_time->tm_mday;
  current_weekday = tick_time->tm_wday;

  // Set initial anim angles to 0 for the first animation on load
  anim_hour_angle = 0;
  anim_min_tens_angle = 0;
  anim_min_ones_angle = 0;

  update_time();
}

static void main_window_unload(Window *window) {
  if(s_hour_anim) animation_unschedule((Animation*)s_hour_anim);
  if(s_min_tens_anim) animation_unschedule((Animation*)s_min_tens_anim);
  if(s_min_ones_anim) animation_unschedule((Animation*)s_min_ones_anim);
  if(s_month_anim) animation_unschedule((Animation*)s_month_anim);
  if(s_day_anim) animation_unschedule((Animation*)s_day_anim);
  if(s_weekday_anim) animation_unschedule((Animation*)s_weekday_anim);

  layer_destroy(s_month_layer);
  layer_destroy(s_day_layer);
  layer_destroy(s_weekday_layer);
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
