#include <pebble.h>
#include <pebble-fctx/ffont.h>
#include <pebble-fctx/fctx.h>

#ifdef PBL_BW
#define bw_bitmap_data_get_value(BMP, BPR, X, Y) (((*((BMP)+(Y)*(BPR)+(X)/8)) & (1 << (X)%8)) ? 1 : 0)
#define bw_bitmap_data_set_pixel(BMP, BPR, X, Y) (*((BMP)+(Y)*(BPR)+(X)/8)) |= (1 << (X)%8)
#endif

static Window* my_window;

static FFont* the_font;
static FContext fctx;
GPoint cut[2];
static uint16_t copy_window_start;
static uint16_t copy_window_height;
static GBitmap* copy_bitmap = NULL;
static BitmapLayer* bitmap_layer;

static Layer* top_layer;
static Layer* bottom_layer;

static Layer* top_cutting_layer;
static GPath* top_cutting_path = NULL;
static GPathInfo top_cutting_path_info = {
  .num_points = 4,
  .points = (GPoint[]) {{0, 0}, {0, 0}, {0, 0}, {0, 0}}
};
static Layer* top_copy_layer;
static char top_text[3] = "23";

static Layer* bottom_cutting_layer;
static GPath* bottom_cutting_path = NULL;
static GPathInfo bottom_cutting_path_info = {
  .num_points = 4,
  .points = (GPoint[]) {{0, 0}, {0, 0}, {0, 0}, {0, 0}}
};
static Layer* bottom_copy_layer;
static char bottom_text[3] = "59";

static Layer* cut_layer;

static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
  strftime(top_text, 3, clock_is_24h_style() ? "%H" : "%h", tick_time);
  strftime(bottom_text, 3, "%M", tick_time);
  layer_mark_dirty(top_layer);
  layer_mark_dirty(bottom_layer);
}

static void top_layer_update_proc(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  fctx_init_context(&fctx, ctx);
  fctx_begin_fill(&fctx);
  fctx_set_text_em_height(&fctx, the_font, 100);
  fctx_set_fill_color(&fctx, GColorWhite);
  fctx_set_pivot(&fctx, FPointZero);
#ifdef PBL_ROUND  
  fctx_set_offset(&fctx, FPointI(bounds.size.w/2-8,bounds.size.h/3+12));
#else
  fctx_set_offset(&fctx, FPointI(bounds.size.w/2-10,bounds.size.h/3+10));
#endif
  fctx_set_rotation(&fctx, 0);
  fctx_draw_string(&fctx, top_text, the_font, GTextAlignmentCenter, FTextAnchorCapMiddle);
  fctx_end_fill(&fctx);
  fctx_deinit_context(&fctx);
}

static void top_cutting_layer_update_proc(Layer* layer, GContext* ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_antialiased(ctx, false);
  gpath_draw_filled(ctx, top_cutting_path);
}

static void top_copy_layer_update_proc(Layer* layer, GContext* ctx) {
  GBitmap* buffer = graphics_capture_frame_buffer(ctx);
  GSize buffer_size = gbitmap_get_bounds(buffer).size;
  if(!copy_bitmap) {
    copy_bitmap = gbitmap_create_blank(GSize(buffer_size.w, copy_window_height), PBL_IF_COLOR_ELSE(GBitmapFormat8Bit, GBitmapFormat1Bit));
    bitmap_layer_set_bitmap(bitmap_layer, copy_bitmap);
  }
  for(int i = copy_window_start; i < copy_window_start+copy_window_height; ++i) {
#ifdef PBL_COLOR
    GBitmapDataRowInfo from_row_info = gbitmap_get_data_row_info(buffer, i);
    GBitmapDataRowInfo to_row_info = gbitmap_get_data_row_info(copy_bitmap, i-copy_window_start);
    for(int j = from_row_info.min_x; j < from_row_info.max_x; ++j) {
      to_row_info.data[j] = from_row_info.data[j];
    }
#else
    uint16_t bytes_per_row = gbitmap_get_bytes_per_row(buffer);
    memcpy(gbitmap_get_data(copy_bitmap), &(gbitmap_get_data(buffer)[copy_window_start*bytes_per_row]), copy_window_height*bytes_per_row);
#endif
  }
  graphics_release_frame_buffer(ctx, buffer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, copy_window_start, buffer_size.w, copy_window_height), 0, 0);
  layer_mark_dirty(bitmap_layer_get_layer(bitmap_layer));
}

static void bottom_layer_update_proc(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  fctx_init_context(&fctx, ctx);
  fctx_begin_fill(&fctx);
  fctx_set_text_em_height(&fctx, the_font, 100);
  fctx_set_fill_color(&fctx, GColorPictonBlue);
  fctx_set_pivot(&fctx, FPointZero);
#ifdef PBL_ROUND  
  fctx_set_offset(&fctx, FPointI(bounds.size.w/2+16,(bounds.size.h/3*2)-6));
#else
  fctx_set_offset(&fctx, FPointI(bounds.size.w/2+10,(bounds.size.h/3*2)-2));
#endif
  fctx_set_rotation(&fctx, 0);
  fctx_draw_string(&fctx, bottom_text, the_font, GTextAlignmentCenter, FTextAnchorCapMiddle);
  fctx_end_fill(&fctx);
  fctx_deinit_context(&fctx);
}

static void bottom_cutting_layer_update_proc(Layer* layer, GContext* ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_antialiased(ctx, false);
  gpath_draw_filled(ctx, bottom_cutting_path);
}

static void bottom_copy_layer_update_proc(Layer* layer, GContext* ctx) {
  GBitmap* buffer = graphics_capture_frame_buffer(ctx);
  GSize buffer_size = gbitmap_get_bounds(buffer).size;

  for(int i = copy_window_start; i < copy_window_start+copy_window_height; ++i) {
#ifdef PBL_COLOR
    GBitmapDataRowInfo from_row_info = gbitmap_get_data_row_info(buffer, i);
    GBitmapDataRowInfo to_row_info = gbitmap_get_data_row_info(copy_bitmap, i-copy_window_start);
    for(int j = from_row_info.min_x; j < from_row_info.max_x; ++j) {
      if(   GColorBlack.argb != from_row_info.data[j]
         && GColorBlack.argb == to_row_info.data[j]) {
        to_row_info.data[j] = from_row_info.data[j];
      }
    }
#else
    uint16_t bytes_per_row = gbitmap_get_bytes_per_row(buffer);
    uint8_t* bytes = gbitmap_get_data(buffer);
    uint16_t copy_bytes_per_row = gbitmap_get_bytes_per_row(copy_bitmap);
    uint8_t* copy_bytes = gbitmap_get_data(copy_bitmap);
    for(int j = 0; j < buffer_size.w; ++j) {
      if(   bw_bitmap_data_get_value(bytes, bytes_per_row, j, i)
        && !bw_bitmap_data_get_value(copy_bytes, copy_bytes_per_row, j, i-copy_window_start)) {
          bw_bitmap_data_set_pixel(copy_bytes, copy_bytes_per_row, j, i-copy_window_start);
      }
    }
#endif
  }
  graphics_release_frame_buffer(ctx, buffer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, copy_window_start, buffer_size.w, copy_window_height), 0, 0);
  layer_mark_dirty(bitmap_layer_get_layer(bitmap_layer));
}

static void cut_layer_update_proc(Layer* layer, GContext* ctx) {
  graphics_context_set_stroke_width(ctx, 5);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, cut[0], cut[1]);
  
#if 0 // center line to help with adjustment
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_draw_line(ctx, GPoint(0, bounds.size.h/2), GPoint(bounds.size.w, bounds.size.h/2));
#endif
}

static void my_window_load(Window *window) {
  
  window_set_background_color(window, GColorBlack);
  Layer* root_layer = window_get_root_layer(window);
  GRect root_bounds = layer_get_bounds(root_layer);

  the_font = ffont_create_from_resource(RESOURCE_ID_THE_FONT);
  
  cut[0] = GPoint(0, root_bounds.size.h/2+10);
  cut[1] = GPoint(root_bounds.size.w-1, root_bounds.size.h/2-10);

  copy_window_start = root_bounds.size.h/3;
  copy_window_height = root_bounds.size.h/3;

  top_layer = layer_create(root_bounds);
  layer_set_update_proc(top_layer, top_layer_update_proc);
  layer_add_child(root_layer, top_layer);

  top_cutting_path_info.points[0] = cut[0];
  top_cutting_path_info.points[1] = cut[1];
  top_cutting_path_info.points[2] = GPoint(root_bounds.size.w-1, copy_window_start+copy_window_height);
  top_cutting_path_info.points[3] = GPoint(0, copy_window_start+copy_window_height);
  top_cutting_path = gpath_create(&top_cutting_path_info);
  
  top_cutting_layer = layer_create(root_bounds);
  layer_set_update_proc(top_cutting_layer, top_cutting_layer_update_proc);
  layer_add_child(root_layer, top_cutting_layer);
  
  top_copy_layer = layer_create(root_bounds);
  layer_set_update_proc(top_copy_layer, top_copy_layer_update_proc);
  layer_add_child(root_layer, top_copy_layer);

  bottom_layer = layer_create(root_bounds);
  layer_set_update_proc(bottom_layer, bottom_layer_update_proc);
  layer_add_child(root_layer, bottom_layer);

  bottom_cutting_path_info.points[0] = cut[0];
  bottom_cutting_path_info.points[1] = cut[1];
  bottom_cutting_path_info.points[2] = GPoint(root_bounds.size.w-1, copy_window_start);
  bottom_cutting_path_info.points[3] = GPoint(0, copy_window_start);
  bottom_cutting_path = gpath_create(&bottom_cutting_path_info);
  
  bottom_cutting_layer = layer_create(root_bounds);
  layer_set_update_proc(bottom_cutting_layer, bottom_cutting_layer_update_proc);
  layer_add_child(root_layer, bottom_cutting_layer);
  
  bottom_copy_layer = layer_create(root_bounds);
  layer_set_update_proc(bottom_copy_layer, bottom_copy_layer_update_proc);
  layer_add_child(root_layer, bottom_copy_layer);

  bitmap_layer = bitmap_layer_create(GRect(0, copy_window_start, root_bounds.size.w, copy_window_height));
  bitmap_layer_set_background_color(bitmap_layer, GColorClear);
#ifdef PBL_COLOR  
  bitmap_layer_set_compositing_mode(bitmap_layer, GCompOpSet);
#endif
  layer_add_child(root_layer, bitmap_layer_get_layer(bitmap_layer));

  cut_layer = layer_create(root_bounds);
  layer_set_update_proc(cut_layer, cut_layer_update_proc);
  layer_add_child(root_layer, cut_layer);

  time_t now = time(NULL);
  struct tm* tick_time = localtime(&now);
  tick_handler(tick_time, 0);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

void handle_init(void) {
  my_window = window_create();
    
  window_set_window_handlers(my_window, (WindowHandlers) {
    .load = my_window_load,
  });
  window_stack_push(my_window, true);
}

void handle_deinit(void) {
  window_destroy(my_window);
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}