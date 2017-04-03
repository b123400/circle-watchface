#include <pebble.h>
#include <stdio.h>
#include "math.h"

#define SETTINGS_KEY 1

static Window *s_window;
static Layer *bitmap_layer;

static GColor background_color;
static GColor dot_color;
static int vertex_count = 24;
static int vertex_shift = 6;

// y = mx+c
// m1x + c1 = m2x+c2
// c1 - c2 = m2x - m1x
// c1 - c2 = (m2 - m1)x
// (c1 - c2) / (m2 - m1) = x

typedef struct ClaySettings {
  GColor BackgroundColor;
  GColor DotColor;
} ClaySettings;

static ClaySettings settings;
static GRect bounds;
static GPoint center;

static GPath *hour_path_ptr = NULL;
static GPoint hour_points[4];
static GPath *min_path_ptr = NULL;
static GPoint min_points[4];

static void get_vertex(int index, GPoint *out_point) {
  index = index % vertex_count;
  int32_t angle = TRIG_MAX_ANGLE * index / vertex_count;
  // not quite sure where is the origin and direction of pebble
  // anyway i need this to make it good
  angle = (angle + (TRIG_MAX_ANGLE/4)) * -1;
  GPoint point;
  point.x = (-cos_lookup(angle) * (MIN(bounds.size.h, bounds.size.w)/2) / TRIG_MAX_RATIO) + center.y;
  point.y = (sin_lookup(angle) * (MIN(bounds.size.h, bounds.size.w)/2) / TRIG_MAX_RATIO) + center.x;
  *out_point = point;
}

static void get_line(int index, GPoint *from, GPoint *to) {
  get_vertex(index, from);
  get_vertex(index + vertex_shift, to);
}

static void draw_line(GContext *ctx, int index) {
  GPoint from;
  GPoint to;
  get_line(index, &from, &to);
  graphics_draw_line(ctx, from, to);
}

static void interaction(GPoint from1, GPoint to1, GPoint from2, GPoint to2, GPoint *out) {
  float A1 = to1.y - from1.y;
  float B1 = from1.x - to1.x;
  float C1 = (A1 * from1.x) + (B1 * from1.y);

  float A2 = to2.y - from2.y;
  float B2 = from2.x - to2.x;
  float C2 = (A2 * from2.x) + (B2 * from2.y);

  float delta = A1*B2 - A2*B1;
  if(delta == 0) return;

  float x = (B2*C1 - B1*C2)/delta;
  float y = (A1*C2 - A2*C1)/delta;
  *out = GPoint(x, y);
}

static void index_interaction(int index, GPoint *out1, GPoint *out2, GPoint *out3) {
  if (index < 1) {
    index += vertex_count;
  }
  GPoint from1;
  GPoint to1;
  GPoint from2;
  GPoint to2;

  get_line(index - vertex_shift, &from1, &to1);
  get_line(index - 1, &from2, &to2);
  interaction(from1, to1, from2, to2, out1);

  get_line(index - vertex_shift + 1, &from1, &to1);
  get_line(index, &from2, &to2);
  interaction(from1, to1, from2, to2, out2);

  get_line(index - vertex_shift + 1, &from1, &to1);
  get_line(index - 1, &from2, &to2);
  interaction(from1, to1, from2, to2, out3);
}

static void highlight_index(GContext *ctx, int index, GPoint points[], GPath **path_ptr) {
  GPoint target;
  GPoint p1, p2, p3;
  get_vertex(index, &target);
  index_interaction(index, &p1, &p2, &p3);

  if (*path_ptr != NULL) {
    gpath_destroy(*path_ptr);
  }

  points[0] = target;
  points[1] = p1;
  points[2] = p3;
  points[3] = p2;
  GPathInfo path_info = {
    .num_points = 4,
    .points = points
  };
  *path_ptr = gpath_create(&path_info);
  gpath_draw_filled(ctx, *path_ptr);
}

static void bitmap_layer_update_proc(Layer *layer, GContext* ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int minute = (*t).tm_min;
  int hour = (*t).tm_hour;

  bounds = layer_get_bounds(layer);
  center = grect_center_point(&bounds);

  // background color
  graphics_context_set_fill_color(ctx, background_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_width(ctx, 1);

  int hour_index = (hour / 24.0) * vertex_count;
  int min_index = (minute / 60.0) * vertex_count;

  for (int i = 0; i < vertex_count; i++) {
    draw_line(ctx, i);
  }

  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_stroke_width(ctx, 2);

  highlight_index(ctx, hour_index, hour_points, &hour_path_ptr);
  highlight_index(ctx, min_index, min_points, &min_path_ptr);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  bitmap_layer = layer_create(bounds);
  layer_set_update_proc(bitmap_layer, bitmap_layer_update_proc);
  layer_add_child(window_layer, bitmap_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(bitmap_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  int minute = (*tick_time).tm_min;
  int hour = (*tick_time).tm_hour;
  if ((minute + hour * 60) % 10 == 0) {
    layer_mark_dirty(bitmap_layer);
  }
}

static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {

}

static void prv_init(void) {

  // default settings
  settings.BackgroundColor = GColorFromRGBA(205, 34, 49, 255);
  settings.DotColor = GColorWhite;

  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));

  // apply saved data
  // background_color = settings.BackgroundColor;
  // dot_color = settings.DotColor;

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_open(128, 128);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  const bool animated = true;
  window_stack_push(s_window, animated);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", s_window);

  app_event_loop();
  prv_deinit();
}
