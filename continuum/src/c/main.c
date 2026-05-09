#include <pebble.h>
#include "continuum.h"

#define PERSIST_KEY_CONFIG    1
#define ANIM_DURATION_MS      500
#define ANIM_DELAY_STEP_MS    150
#define DEFAULT_ANIM_FPS      30

static const char *const DIGIT_STRINGS[] = {
  "0","1","2","3","4","5","6","7","8","9"
};

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
static Layer  *s_bg_layer;
static Layer  *s_ring_layers[4];
static Layer  *s_month_layer;
static Layer  *s_day_layer;
static Layer  *s_weekday_layer;
static Layer  *s_battery_layer;

static const char *const MONTH_NAMES[] = {
  "Jan","Feb","Mar","Apr","May","Jun",
  "Jul","Aug","Sep","Oct","Nov","Dec"
};

static int current_hour_tens = 0, current_hour_ones = 0;
static int current_minute_tens = 0, current_minute_ones = 0;
static int current_month = 1, current_day = 1, current_weekday = 0;
static int battery_level = 100;

static int32_t anim_hour_tens_angle = 0, anim_hour_ones_angle = 0;
static int32_t anim_min_tens_angle  = 0, anim_min_ones_angle  = 0;
static bool s_app_focused = true;

// ---------------------------------------------------------------------------
// Animation system
// ---------------------------------------------------------------------------
typedef struct {
  int32_t   from;
  int32_t   to;
  int32_t  *target_var;
  Layer    *target_layer;
  uint16_t  frame;         // frames drawn so far (0 = not yet started)
  uint16_t  total_frames;  // ANIM_DURATION_MS / frame_interval_ms(), min 1
  AppTimer *timer;         // non-NULL while delay pending OR ticking
  bool      animating;     // false = waiting for delay, true = ticking
} RingAnim;

static RingAnim s_ring_anims[4];

// Clamp anim_fps to [1, 30] and return frame interval in ms.
static uint32_t frame_interval_ms(void) {
  int fps = config.anim_fps;
  if (fps <= 0 || fps > 30) fps = DEFAULT_ANIM_FPS;
  return (uint32_t)(1000 / fps);
}

// Cubic ease-in-out: f(t)=3t²−2t³, monotonic, no overshoot.
static AnimationProgress ease_in_out(AnimationProgress p) {
  const int64_t M = ANIMATION_NORMALIZED_MAX;
  int64_t p2 = (int64_t)p * p / M;
  int64_t p3 = p2 * p / M;
  return (AnimationProgress)(3 * p2 - 2 * p3);
}

// easeOutBack: overshoots ~10% then springs back.
static AnimationProgress inertia_curve(AnimationProgress linear) {
  const int32_t M = ANIMATION_NORMALIZED_MAX;
  int32_t t256    = (int32_t)((int64_t)linear * 256 / M);
  int32_t d       = t256 - 256;
  int32_t d2      = d * d;
  int64_t d3      = (int64_t)d2 * d;
  int32_t term2   = (int32_t)(1742 * d2 / (1024 * 256));
  int32_t term3   = (int32_t)(2767LL * d3 / (1024LL * 256 * 256));
  return (AnimationProgress)((256 + term2 + term3) * M / 256);
}

static void ring_anim_tick(void *data) {
  int idx     = (int)(uintptr_t)data;
  RingAnim *a = &s_ring_anims[idx];
  a->timer    = NULL;   // cleared first; re-set below unless completing

  if (!a->animating) {
    uint32_t fi       = frame_interval_ms();
    uint16_t tf       = (uint16_t)(ANIM_DURATION_MS / fi);
    a->total_frames   = (tf < 1) ? 1 : tf;
    a->frame          = 0;
    a->animating      = true;
  } else {
    a->frame++;
  }

  bool completing = (a->frame >= a->total_frames);

  AnimationProgress linear = completing
    ? ANIMATION_NORMALIZED_MAX
    : (AnimationProgress)((uint64_t)a->frame * ANIMATION_NORMALIZED_MAX / a->total_frames);

  AnimationProgress progress = config.inertia_toggle
                               ? inertia_curve(linear)
                               : ease_in_out(linear);

  *a->target_var = a->from + (int32_t)(
    ((int64_t)(a->to - a->from) * (int32_t)progress) / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(a->target_layer);

  if (completing) {
    int32_t final = a->to % TRIG_MAX_ANGLE;
    if (final < 0) final += TRIG_MAX_ANGLE;
    *a->target_var = final;
    a->animating   = false;
    return;   // timer stays NULL
  }

  a->timer = app_timer_register(frame_interval_ms(), ring_anim_tick, data);
}

static void schedule_ring_anim(int idx, int32_t from, int32_t to,
                               int32_t *target, uint32_t delay_ms,
                               Layer *target_layer) {
  RingAnim *a = &s_ring_anims[idx];

  if (a->timer) { app_timer_cancel(a->timer); a->timer = NULL; }
  if (from == to) return;

  a->from         = from;
  a->to           = to;
  a->target_var   = target;
  a->target_layer = target_layer;
  a->frame        = 0;
  a->total_frames = 1;   // placeholder; overwritten in the first tick
  a->animating    = false;

  a->timer = app_timer_register(delay_ms > 0 ? delay_ms : 1u,
                                ring_anim_tick, (void*)(uintptr_t)idx);
}

// ---------------------------------------------------------------------------
// Config & fonts
// ---------------------------------------------------------------------------
static GFont s_number_font;
static GFont s_date_font;

void init_default_config(void) {
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

static void save_config(void) {
  persist_write_data(PERSIST_KEY_CONFIG, &config, sizeof(WatchConfig));
}
static void load_config(void) {
  if (persist_exists(PERSIST_KEY_CONFIG))
    persist_read_data(PERSIST_KEY_CONFIG, &config, sizeof(WatchConfig));
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
static int32_t round_div_trig(int32_t num) {
  return num >= 0 ? (num + TRIG_MAX_RATIO/2) / TRIG_MAX_RATIO
                  : (num - TRIG_MAX_RATIO/2) / TRIG_MAX_RATIO;
}

GPoint get_point_on_rounded_rect(int w, int h, int r, int32_t angle) {
  angle = angle % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;
  if (angle == 0)                      return GPoint(0,    -h/2);
  if (angle == TRIG_MAX_ANGLE/4)       return GPoint(w/2,  0);
  if (angle == TRIG_MAX_ANGLE/2)       return GPoint(0,    h/2);
  if (angle == 3*TRIG_MAX_ANGLE/4)     return GPoint(-w/2, 0);

  int str_h = w-2*r, str_v = h-2*r;
  int perim = 2*str_h + 2*str_v + (314*2*r)/100;
  int32_t td = (int32_t)((int32_t)angle * perim / TRIG_MAX_ANGLE);
  int cd = 0, x = 0, y = 0;

  if (td <= cd+str_h/2) return GPoint(td-cd, -h/2);
  cd += str_h/2;
  int qa = (314*r)/200;

  if (td <= cd+qa) { int32_t aa=(td-cd)*TRIG_MAX_ANGLE/4/qa;
    return GPoint(w/2-r+round_div_trig(sin_lookup(aa)*r), -h/2+r-round_div_trig(cos_lookup(aa)*r)); }
  cd += qa;
  if (td <= cd+str_v) return GPoint(w/2, -h/2+r+(td-cd));
  cd += str_v;
  if (td <= cd+qa) { int32_t aa=TRIG_MAX_ANGLE/4+(td-cd)*TRIG_MAX_ANGLE/4/qa;
    return GPoint(w/2-r+round_div_trig(sin_lookup(aa)*r), h/2-r-round_div_trig(cos_lookup(aa)*r)); }
  cd += qa;
  if (td <= cd+str_h) return GPoint(w/2-r-(td-cd), h/2);
  cd += str_h;
  if (td <= cd+qa) { int32_t aa=TRIG_MAX_ANGLE/2+(td-cd)*TRIG_MAX_ANGLE/4/qa;
    x=-w/2+r+round_div_trig(sin_lookup(aa)*r); y=h/2-r-round_div_trig(cos_lookup(aa)*r);
    return GPoint(x,y); }
  cd += qa;
  if (td <= cd+str_v) return GPoint(-w/2, h/2-r-(td-cd));
  cd += str_v;
  if (td <= cd+qa) { int32_t aa=3*TRIG_MAX_ANGLE/4+(td-cd)*TRIG_MAX_ANGLE/4/qa;
    x=-w/2+r+round_div_trig(sin_lookup(aa)*r); y=-h/2+r-round_div_trig(cos_lookup(aa)*r);
    return GPoint(x,y); }
  cd += qa;
  return GPoint(-w/2+r+(td-cd), -h/2);
}

GPoint get_point_on_circle(int radius, int32_t angle) {
  angle = angle % TRIG_MAX_ANGLE;
  if (angle < 0) angle += TRIG_MAX_ANGLE;
  return GPoint(round_div_trig(sin_lookup(angle)*radius),
               -round_div_trig(cos_lookup(angle)*radius));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_ring_numbers(GContext *ctx, GPoint center, int ring_idx,
                              int32_t anim_angle, int cur_digit,
                              bool is_animating, int32_t target_angle) {
  int n = rings[ring_idx].num_items;
  for (int i = 0; i < n; i++) {
    int32_t angle = target_angle + (i * TRIG_MAX_ANGLE)/n - anim_angle;
    bool is_cur   = (!is_animating && i == cur_digit);
#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, is_cur ? config.highlight_number_color : config.number_color);
#else
    GColor fg = config.invert_bw ? GColorBlack : GColorWhite;
    GColor bg = config.invert_bw ? GColorWhite : GColorBlack;
    graphics_context_set_text_color(ctx, is_cur ? bg : fg);
#endif
#ifdef PBL_ROUND
    GPoint pt = get_point_on_circle(rings[ring_idx].width/2, angle);
#else
    GPoint pt = get_point_on_rounded_rect(rings[ring_idx].width, rings[ring_idx].height,
                                          rings[ring_idx].corner_radius, angle);
#endif
    graphics_draw_text(ctx, DIGIT_STRINGS[i], s_number_font,
      GRect(center.x+pt.x-NUMBER_TEXT_OFF_X, center.y+pt.y-NUMBER_TEXT_OFF_Y-2,
            NUMBER_TEXT_W, NUMBER_TEXT_H),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void bg_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
#ifdef PBL_COLOR
  GColor bg_col = config.background_color, line_col = config.line_color;
  GColor hl_col = config.highlight_fill_color;
  GColor rc[4]  = { config.inner_ring_color, config.sub_inner_ring_color,
                    config.middle_ring_color, config.outer_ring_color };
#else
  GColor fg = config.invert_bw ? GColorBlack : GColorWhite;
  GColor bg = config.invert_bw ? GColorWhite : GColorBlack;
  GColor bg_col = bg, line_col = fg, hl_col = fg;
  GColor rc[4] = { bg, bg, bg, bg };
#endif
  graphics_context_set_fill_color(ctx, bg_col);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 3; i >= 0; i--) {
    graphics_context_set_fill_color(ctx, rc[i]);
    graphics_context_set_stroke_color(ctx, line_col);
#ifdef PBL_ROUND
    graphics_fill_circle(ctx, center, rings[i].width/2);
    graphics_draw_circle(ctx, center, rings[i].width/2);
#else
    GRect r = GRect(center.x-rings[i].width/2, center.y-rings[i].height/2,
                    rings[i].width, rings[i].height);
    graphics_fill_rect(ctx, r, rings[i].corner_radius, GCornersAll);
    graphics_draw_round_rect(ctx, r, rings[i].corner_radius);
#endif
  }
  int32_t ta = 0;
  if      (config.highlight_position == POS_RIGHT)  ta = TRIG_MAX_ANGLE/4;
  else if (config.highlight_position == POS_BOTTOM) ta = TRIG_MAX_ANGLE/2;
  else if (config.highlight_position == POS_LEFT)   ta = 3*TRIG_MAX_ANGLE/4;
  graphics_context_set_fill_color(ctx, hl_col);
  graphics_context_set_stroke_color(ctx, line_col);
  for (int i = 0; i < 4; i++) {
#ifdef PBL_ROUND
    GPoint pt  = get_point_on_circle(rings[i].width/2, ta);
    GPoint hlc = GPoint(center.x+pt.x, center.y+pt.y);
    GRect  hl  = GRect(hlc.x-HIGHLIGHT_BOX_SIZE/2, hlc.y-HIGHLIGHT_BOX_SIZE/2,
                       HIGHLIGHT_BOX_SIZE, HIGHLIGHT_BOX_SIZE);
#else
    GPoint pt = get_point_on_rounded_rect(rings[i].width, rings[i].height,
                                          rings[i].corner_radius, ta);
    GRect hl  = GRect(center.x+pt.x-HIGHLIGHT_BOX_SIZE/2,
                      center.y+pt.y-HIGHLIGHT_BOX_SIZE/2,
                      HIGHLIGHT_BOX_SIZE, HIGHLIGHT_BOX_SIZE);
#endif
    graphics_fill_rect(ctx, hl, 0, GCornerNone);
    graphics_draw_rect(ctx, hl);
  }
}

static void ring_update_proc(Layer *layer, GContext *ctx) {
  int    idx    = *(int*)layer_get_data(layer);
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  int32_t ta = 0;
  if      (config.highlight_position == POS_RIGHT)  ta = TRIG_MAX_ANGLE/4;
  else if (config.highlight_position == POS_BOTTOM) ta = TRIG_MAX_ANGLE/2;
  else if (config.highlight_position == POS_LEFT)   ta = 3*TRIG_MAX_ANGLE/4;

  int32_t anim_angle; int cur_digit;
  switch (idx) {
    case 0: anim_angle = anim_hour_tens_angle; cur_digit = current_hour_tens; break;
    case 1: anim_angle = anim_hour_ones_angle; cur_digit = current_hour_ones; break;
    case 2: anim_angle = anim_min_tens_angle;  cur_digit = current_minute_tens; break;
    default:anim_angle = anim_min_ones_angle;  cur_digit = current_minute_ones; break;
  }
  bool is_animating = s_ring_anims[idx].timer != NULL || s_ring_anims[idx].animating;
  draw_ring_numbers(ctx, center, idx, anim_angle, cur_digit, is_animating, ta);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static bool three_line_layout(void) {
#ifdef PBL_ROUND
  return true;
#else
  return (config.highlight_position == POS_TOP ||
          config.highlight_position == POS_BOTTOM);
#endif
}

static void update_time(void);

static void reposition_center_layers(void) {
  Layer *wl = window_get_root_layer(s_main_window);
  GRect  b  = layer_get_bounds(wl);
  GPoint c  = grect_center_point(&b);
  int    step = CENTER_ITEM_H + CENTER_SPACING;
  GRect  f;

  if (three_line_layout()) {
    int h = config.battery_toggle ? 2*step+BATTERY_ICON_H : step+CENTER_ITEM_H;
    int y = c.y - h/2;
    f = layer_get_frame(s_weekday_layer); f.origin.y = y;        layer_set_frame(s_weekday_layer, f);
    f = layer_get_frame(s_month_layer);   f.origin.y = y+step;   layer_set_frame(s_month_layer, f);
    layer_set_hidden(s_day_layer, true);
    if (config.battery_toggle) {
      f = layer_get_frame(s_battery_layer); f.origin.y = y+2*step; layer_set_frame(s_battery_layer, f);
      layer_set_hidden(s_battery_layer, false); layer_mark_dirty(s_battery_layer);
    } else { layer_set_hidden(s_battery_layer, true); }
  } else {
    int h = config.battery_toggle ? 3*step+BATTERY_ICON_H : 2*step+CENTER_ITEM_H;
    int y = c.y - h/2;
    f = layer_get_frame(s_weekday_layer); f.origin.y = y;          layer_set_frame(s_weekday_layer, f);
    f = layer_get_frame(s_month_layer);   f.origin.y = y+step;     layer_set_frame(s_month_layer, f);
    f = layer_get_frame(s_day_layer);     f.origin.y = y+2*step;   layer_set_frame(s_day_layer, f);
    layer_set_hidden(s_day_layer, false);
    if (config.battery_toggle) {
      f = layer_get_frame(s_battery_layer); f.origin.y = y+3*step; layer_set_frame(s_battery_layer, f);
      layer_set_hidden(s_battery_layer, false); layer_mark_dirty(s_battery_layer);
    } else { layer_set_hidden(s_battery_layer, true); }
  }
  layer_mark_dirty(s_weekday_layer);
  layer_mark_dirty(s_month_layer);
  layer_mark_dirty(s_day_layer);
}

// ---------------------------------------------------------------------------
// Message inbox
// ---------------------------------------------------------------------------
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_read_first(iterator);
  while (t) {
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
  update_time();
}

// ---------------------------------------------------------------------------
// Time update
// ---------------------------------------------------------------------------
static void update_time(void) {
  time_t tmp = time(NULL); struct tm *tk = localtime(&tmp);

  int hour = tk->tm_hour;
  if (!clock_is_24h_style()) {
    if (hour == 0) hour = 12;
    if (hour > 12) hour -= 12;
  }
  int min = tk->tm_min;

  static int prev_hour = -1, prev_min = -1;
  static bool last_focused = true;
  bool focus_changed = (s_app_focused != last_focused);
  last_focused = s_app_focused;

  bool changed = (hour != prev_hour || min != prev_min || focus_changed);

  current_hour_tens = hour/10; current_hour_ones = hour%10;
  current_minute_tens = min/10; current_minute_ones = min%10;

  int32_t target_hour_tens_angle = (current_hour_tens   * TRIG_MAX_ANGLE) / rings[0].num_items;
  int32_t target_hour_ones_angle = (current_hour_ones   * TRIG_MAX_ANGLE) / rings[1].num_items;
  int32_t target_min_tens_angle  = (current_minute_tens * TRIG_MAX_ANGLE) / rings[2].num_items;
  int32_t target_min_ones_angle  = (current_minute_ones * TRIG_MAX_ANGLE) / rings[3].num_items;

  if (changed) {
    prev_hour = hour; prev_min = min;

    #define NORM(a) do { (a) %= TRIG_MAX_ANGLE; if ((a)<0) (a) += TRIG_MAX_ANGLE; } while(0)
    NORM(anim_hour_tens_angle); NORM(anim_hour_ones_angle);
    NORM(anim_min_tens_angle);  NORM(anim_min_ones_angle);
    #undef NORM

    if (config.animation_toggle && s_app_focused) {
      if (target_hour_tens_angle < anim_hour_tens_angle) target_hour_tens_angle += TRIG_MAX_ANGLE;
      if (target_hour_ones_angle < anim_hour_ones_angle) target_hour_ones_angle += TRIG_MAX_ANGLE;
      if (target_min_tens_angle  < anim_min_tens_angle)  target_min_tens_angle  += TRIG_MAX_ANGLE;
      if (target_min_ones_angle  < anim_min_ones_angle)  target_min_ones_angle  += TRIG_MAX_ANGLE;

      schedule_ring_anim(0, anim_hour_tens_angle, target_hour_tens_angle, &anim_hour_tens_angle, 0,                    s_ring_layers[0]);
      schedule_ring_anim(1, anim_hour_ones_angle, target_hour_ones_angle, &anim_hour_ones_angle, ANIM_DELAY_STEP_MS,   s_ring_layers[1]);
      schedule_ring_anim(2, anim_min_tens_angle,  target_min_tens_angle,  &anim_min_tens_angle,  ANIM_DELAY_STEP_MS*2, s_ring_layers[2]);
      schedule_ring_anim(3, anim_min_ones_angle,  target_min_ones_angle,  &anim_min_ones_angle,  ANIM_DELAY_STEP_MS*3, s_ring_layers[3]);
    } else {
      anim_hour_tens_angle = target_hour_tens_angle;
      anim_hour_ones_angle = target_hour_ones_angle;
      anim_min_tens_angle  = target_min_tens_angle;
      anim_min_ones_angle  = target_min_ones_angle;
      for (int i = 0; i < 4; i++) layer_mark_dirty(s_ring_layers[i]);
    }
  }

  static int prev_month = -1, prev_day = -1, prev_weekday = -1;
  bool mc = (current_month != prev_month), dc = (current_day != prev_day), wc = (current_weekday != prev_weekday);
  prev_month = current_month; prev_day = current_day; prev_weekday = current_weekday;

  if (mc) layer_mark_dirty(s_month_layer);
  if (dc) layer_mark_dirty(three_line_layout() ? s_month_layer : s_day_layer);
  if (wc) layer_mark_dirty(s_weekday_layer);
}

// ---------------------------------------------------------------------------
// Battery & tick
// ---------------------------------------------------------------------------
static char s_day_buffer[4];
static bool battery_is_charging = false;

static void battery_callback(BatteryChargeState state) {
  battery_level = state.charge_percent; battery_is_charging = state.is_charging;
  layer_mark_dirty(s_battery_layer);
}

static void tick_handler(struct tm *tk, TimeUnits units_changed) {
  current_month = tk->tm_mon + 1;
  if (current_day != tk->tm_mday) {
    current_day = tk->tm_mday;
    snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", current_day);
  }
  current_weekday = tk->tm_wday;
  update_time();
}

// ---------------------------------------------------------------------------
// Center layer draw procs
// ---------------------------------------------------------------------------
static void draw_battery_icon(GContext *ctx, GRect r, GColor c) {
  graphics_context_set_stroke_color(ctx, c);
  graphics_draw_rect(ctx, GRect(r.origin.x, r.origin.y, r.size.w-3, r.size.h));
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(r.origin.x+r.size.w-3, r.origin.y+r.size.h/4, 3, r.size.h/2), 0, GCornerNone);
}
static GColor center_text_color(void) {
#ifdef PBL_COLOR
  return config.center_text_color;
#else
  return config.invert_bw ? GColorBlack : GColorWhite;
#endif
}
static void month_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());
  if (three_line_layout()) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", MONTH_NAMES[(current_month-1)%12], current_day);
    graphics_draw_text(ctx, buf, s_date_font, GRect(0,-4,b.size.w,b.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(ctx, MONTH_NAMES[(current_month-1)%12], s_date_font,
      GRect(0,-4,b.size.w,b.size.h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}
static void day_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());
  graphics_draw_text(ctx, s_day_buffer, s_date_font, GRect(0,-4,b.size.w,b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
static void weekday_update_proc(Layer *layer, GContext *ctx) {
  static const char *const WD[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  GRect b = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, center_text_color());
  graphics_draw_text(ctx, WD[current_weekday%7], s_date_font, GRect(0,-4,b.size.w,b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
static void battery_update_proc(Layer *layer, GContext *ctx) {
  (void)layer;
  GColor ic = center_text_color();
  draw_battery_icon(ctx, GRect(0,0,BATTERY_ICON_W,BATTERY_ICON_H), ic);
  int fw = ((BATTERY_BODY_W-4)*battery_level)/100;
#ifdef PBL_COLOR
  GColor fc = battery_is_charging ? GColorGreen : (battery_level<=20) ? GColorRed : ic;
#else
  GColor fc = ic;
#endif
  graphics_context_set_fill_color(ctx, fc);
  graphics_fill_rect(ctx, GRect(2,2,fw,BATTERY_ICON_H-4), 0, GCornerNone);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------
static void main_window_load(Window *window) {
  Layer *wl = window_get_root_layer(window);
  GRect  b  = layer_get_bounds(wl);
  GPoint c  = grect_center_point(&b);

  s_bg_layer = layer_create(b);
  layer_set_update_proc(s_bg_layer, bg_update_proc);
  layer_add_child(wl, s_bg_layer);

  for (int i = 0; i < 4; i++) {
    s_ring_layers[i] = layer_create_with_data(b, sizeof(int));
    *(int*)layer_get_data(s_ring_layers[i]) = i;
    layer_set_update_proc(s_ring_layers[i], ring_update_proc);
    layer_add_child(wl, s_ring_layers[i]);
  }

  s_weekday_layer = layer_create(GRect(c.x-CENTER_ITEM_W/2, 0, CENTER_ITEM_W, CENTER_ITEM_H));
  layer_set_clips(s_weekday_layer, true);
  layer_set_update_proc(s_weekday_layer, weekday_update_proc);
  layer_add_child(wl, s_weekday_layer);

  s_month_layer = layer_create(GRect(c.x-CENTER_MONTHDAY_W/2, 0, CENTER_MONTHDAY_W, CENTER_ITEM_H));
  layer_set_clips(s_month_layer, true);
  layer_set_update_proc(s_month_layer, month_update_proc);
  layer_add_child(wl, s_month_layer);

  s_day_layer = layer_create(GRect(c.x-CENTER_ITEM_W/2, 0, CENTER_ITEM_W, CENTER_ITEM_H));
  layer_set_clips(s_day_layer, true);
  layer_set_update_proc(s_day_layer, day_update_proc);
  layer_add_child(wl, s_day_layer);

  s_battery_layer = layer_create(GRect(c.x-BATTERY_BODY_W/2, 0, BATTERY_ICON_W, BATTERY_ICON_H));
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

  time_t tmp = time(NULL); struct tm *tk = localtime(&tmp);
  current_month = tk->tm_mon+1; current_day = tk->tm_mday;
  snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", current_day);
  current_weekday = tk->tm_wday;

  anim_hour_tens_angle = anim_hour_ones_angle = 0;
  anim_min_tens_angle  = anim_min_ones_angle  = 0;
  for (int i = 0; i < 4; i++) s_ring_anims[i] = (RingAnim){0};

  reposition_center_layers();
  update_time();
}

static void main_window_unload(Window *window) {
  for (int i = 0; i < 4; i++) {
    if (s_ring_anims[i].timer) {
      app_timer_cancel(s_ring_anims[i].timer);
      s_ring_anims[i].timer = NULL;
    }
  }
  layer_destroy(s_month_layer);  layer_destroy(s_day_layer);
  layer_destroy(s_weekday_layer); layer_destroy(s_battery_layer);
  for (int i = 0; i < 4; i++) layer_destroy(s_ring_layers[i]);
  layer_destroy(s_bg_layer);
}

// ---------------------------------------------------------------------------
// App focus
// ---------------------------------------------------------------------------
static void app_focus_handler(bool in_focus) {
  s_app_focused = in_focus;
  if (!s_app_focused) {
    for (int i = 0; i < 4; i++) {
      if (s_ring_anims[i].timer) {
        app_timer_cancel(s_ring_anims[i].timer);
        s_ring_anims[i].timer = NULL;
      }
      s_ring_anims[i].animating = false;
    }
  }
  // Ensure the UI is updated immediately on focus change (loss or gain)
  update_time();
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------
static void init(void) {
  init_default_config(); load_config();
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(512, 512);
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load = main_window_load, .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  app_focus_service_subscribe(app_focus_handler);
}
static void deinit(void) {
  app_focus_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_main_window);
}
int main(void) {
  init();
  app_event_loop();
  deinit();
}
