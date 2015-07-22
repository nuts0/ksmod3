#include "KSMOD3.h"
#include <pebble.h>

#define COLORS       true
#define ANTIALIASING true

#define HAND_MARGIN  10
#define FINAL_RADIUS 69				//半径

#define ANIMATION_DURATION 500
#define ANIMATION_DELAY    600

#define TAP_NOT_DATA true			//タップで切替

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *s_main_window;
static Layer *s_canvas_layer;
static Layer *s_date_layer;

static GPoint s_center;
static Time s_last_time, s_anim_time;
static int s_radius = 0,  s_color_channels[3];
static bool s_animating = false;

static TextLayer *s_battery_layer;
static TextLayer *s_connection_layer;

static TextLayer *s_day_label, *s_num_label, *s_month_label;
static char s_day_buffer[6],s_num_buffer[4],s_month_buffer[4] ;

static GPath *s_tick_paths[NUM_CLOCK_TICKS];	//目盛り

//カスタムバイブ設定
//Create an array of ON-OFF-ON etc durations in milliseconds
uint32_t segments1[] = {50,90,50,90,50};
//Create a VibePattern structure with the segments and length of the pattern as fields
VibePattern pattern1 = {
    .durations = segments1,
    .num_segments = ARRAY_LENGTH(segments1),
};

//------------------------------------------------------------------------------------
static void date_update_proc(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

	//month
	strftime(s_month_buffer, sizeof(s_month_buffer), "%b", t);
  text_layer_set_text(s_month_label, s_month_buffer);
	//曜日
	strftime(s_day_buffer, sizeof(s_day_buffer), "%a", t);
  text_layer_set_text(s_day_label, s_day_buffer);
	//日付
  strftime(s_num_buffer, sizeof(s_num_buffer), "%d", t);
  text_layer_set_text(s_num_label, s_num_buffer);

}
//------------------------------------------------------------------------------------
static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100%";

  if (charge_state.is_charging) {
    snprintf(battery_text, sizeof(battery_text), "chg");
  } else {
    snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, battery_text);
}
//------------------------------------------------------------------------------------
static void handle_bluetooth(bool connected) {
  text_layer_set_text(s_connection_layer, connected ? "c" : "d");
	//切れたらバイブ	
  if (APP_MSG_NOT_CONNECTED){
	}else{
		vibes_enqueue_custom_pattern(pattern1);
	}
	
}
/*************************** AnimationImplementation **************************/
static void animation_started(Animation *anim, void *context) {
  s_animating = true;
}
//------------------------------------------------------------------------------------
static void animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;
}
//------------------------------------------------------------------------------------
static void animate(int duration, int delay, AnimationImplementation *implementation, bool handlers) {
  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, implementation);
  if(handlers) {
    animation_set_handlers(anim, (AnimationHandlers) {
      .started = animation_started,
      .stopped = animation_stopped
    }, NULL);
  }
  animation_schedule(anim);
}
/************************************ UI **************************************/
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  // Store time
  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;

//	for(int i = 0; i < 3; i++) {
//    s_color_channels[i] = rand() % 256;
//  }

	// Redraw
  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

}
//------------------------------------------------------------------------------------
static int hours_to_minutes(int hours_out_of_12) {
  return (int)(float)(((float)hours_out_of_12 / 12.0F) * 60.0F);
}
//------------------------------------------------------------------------------------
//static void handle_accel(AccelData *data, uint32_t num_samples) {
//}
//------------------------------------------------------------------------------------
static void tap_handler(AccelAxisType axis, int32_t direction) {
  switch (axis) {
		case ACCEL_AXIS_X:
			s_color_channels[0] = rand() % 256;
			break;
		case ACCEL_AXIS_Y:
			s_color_channels[1] = rand() % 256;
			break;
		case ACCEL_AXIS_Z:
			s_color_channels[2] = rand() % 256;
			break;
	}
	// Redraw
  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}
//------------------------------------------------------------------------------------
static void update_proc(Layer *layer, GContext *ctx) {
  // Color background?
  if(COLORS) {
    graphics_context_set_fill_color(ctx, GColorFromRGB(s_color_channels[0], s_color_channels[1], s_color_channels[2]));
//    graphics_context_set_fill_color(ctx, GColorCadetBlue);
//		graphics_fill_rect(ctx, GRect(0, 0, 144, 168), 0,GCornerNone);
    graphics_fill_rect(ctx, GRect(10, 22, 124, 124), 0, GCornerNone);
  }

	/*針とふち*/
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 5);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // White clockface 文字盤
 // graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx,GColorOxfordBlue);
  graphics_fill_circle(ctx, s_center, s_radius);

  // Draw outline
  graphics_draw_circle(ctx, s_center, s_radius);

  // Don't use current time while animating
  Time mode_time = (s_animating) ? s_anim_time : s_last_time;

  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if(s_animating) {
    // Hours out of 60 for smoothness
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };

  // Draw hands with positive length only
  if(s_radius > 2 * HAND_MARGIN) {
		graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_draw_line(ctx, s_center, hour_hand);
  } 
  // minute hand
  if(s_radius > HAND_MARGIN) {
		graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_line(ctx, s_center, minute_hand);
  }

		//目盛り	
graphics_context_set_fill_color(ctx, GColorLightGray);
for (int i = 0; i < NUM_CLOCK_TICKS; ++i) {
  gpath_draw_filled(ctx, s_tick_paths[i]);
}
	
	//BATT更新
	handle_battery(battery_state_service_peek());
}
//------------------------------------------------------------------------------------
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
	
  GRect window_bounds = layer_get_bounds(window_layer);

  s_center = grect_center_point(&window_bounds);

  s_canvas_layer = layer_create(window_bounds);
  layer_set_update_proc(s_canvas_layer, update_proc);
  layer_add_child(window_layer, s_canvas_layer);

	// youbi 	
  s_date_layer = layer_create(window_bounds);
  layer_set_update_proc(s_date_layer, date_update_proc);
  layer_add_child(window_layer, s_date_layer);

  s_day_label = text_layer_create(GRect(5, 0, 27, 20));
  text_layer_set_text(s_day_label, s_day_buffer);
  text_layer_set_background_color(s_day_label, GColorClear);
  text_layer_set_text_color(s_day_label, GColorWhite);
  text_layer_set_font(s_day_label, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  layer_add_child(s_date_layer, text_layer_get_layer(s_day_label));

	//month
	s_month_label = text_layer_create(GRect(105, 0, 27, 20));
  text_layer_set_text(s_month_label, s_month_buffer);
  text_layer_set_background_color(s_month_label, GColorClear);
  text_layer_set_text_color(s_month_label, GColorWhite);
  text_layer_set_font(s_month_label, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  layer_add_child(s_date_layer, text_layer_get_layer(s_month_label));

	// date 
  s_num_label = text_layer_create(GRect(125, 0, 18, 20));
  text_layer_set_text(s_num_label, s_num_buffer);
  text_layer_set_background_color(s_num_label, GColorClear);
  text_layer_set_text_color(s_num_label, GColorWhite);
  text_layer_set_font(s_num_label, fonts_get_system_font(FONT_KEY_GOTHIC_18));

  layer_add_child(s_date_layer, text_layer_get_layer(s_num_label));

	/* bluetooth */	
	s_connection_layer = text_layer_create(GRect(0, 143,20, 20));
  text_layer_set_text_color(s_connection_layer, GColorWhite);
  text_layer_set_background_color(s_connection_layer, GColorClear);
  text_layer_set_font(s_connection_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_connection_layer, GTextAlignmentCenter);
  handle_bluetooth(bluetooth_connection_service_peek());

	/* Battery */	
  s_battery_layer = text_layer_create(GRect(112, 145, 33, 20));
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentCenter);
  text_layer_set_text(s_battery_layer, "100%");

  battery_state_service_subscribe(handle_battery);
  bluetooth_connection_service_subscribe(handle_bluetooth);

	layer_add_child(window_layer, text_layer_get_layer(s_connection_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));


	
}
//------------------------------------------------------------------------------------
static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
	
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  text_layer_destroy(s_connection_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_month_label);
  text_layer_destroy(s_day_label);
  text_layer_destroy(s_num_label);
}
/*********************************** App **************************************/
static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(float)(((float)dist_normalized / (float)ANIMATION_NORMALIZED_MAX) * (float)max);
}
//------------------------------------------------------------------------------------
static void radius_update(Animation *anim, AnimationProgress dist_normalized) {
	s_radius = anim_percentage(dist_normalized, FINAL_RADIUS);
  layer_mark_dirty(s_canvas_layer);
}
//------------------------------------------------------------------------------------
static void hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_anim_time.hours = anim_percentage(dist_normalized, hours_to_minutes(s_last_time.hours));
  s_anim_time.minutes = anim_percentage(dist_normalized, s_last_time.minutes);

  layer_mark_dirty(s_canvas_layer);
}
//------------------------------------------------------------------------------------
static void init() {
  srand(time(NULL));

  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  tick_handler(time_now, MINUTE_UNIT);

  s_main_window = window_create();
	/*背景くろ*/
  window_set_background_color(s_main_window, COLOR_FALLBACK(GColorBlack, GColorBlack));
	
	window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // Prepare animations
  AnimationImplementation radius_impl = {
    .update = radius_update
  };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &radius_impl, false);

  AnimationImplementation hands_impl = {
    .update = hands_update
  };
  animate(2 * ANIMATION_DURATION, ANIMATION_DELAY, &hands_impl, true);

  //目盛り
	for (int i = 0; i < NUM_CLOCK_TICKS; ++i) {
    s_tick_paths[i] = gpath_create(&ANALOG_BG_POINTS[i]);
  }
		
  // Use tap service? If not, use data service
  if (TAP_NOT_DATA) {
    // Subscribe to the accelerometer tap service
    accel_tap_service_subscribe(tap_handler);
  } else {
    // Subscribe to the accelerometer data service
//    int num_samples = 3;
//		accel_data_service_subscribe(num_samples, handle_accel);
		accel_data_service_subscribe(0, NULL);
    // Choose update rate
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  }

}
//------------------------------------------------------------------------------------
static void deinit() {
  window_destroy(s_main_window);

	//目盛り
	for (int i = 0; i < NUM_CLOCK_TICKS; ++i) {
    gpath_destroy(s_tick_paths[i]);
  }
	
	//tap event
  if (TAP_NOT_DATA) {
    accel_tap_service_unsubscribe();
  } else {
    accel_data_service_unsubscribe();
  }
}
//------------------------------------------------------------------------------------
int main() {
  init();
  app_event_loop();
  deinit();
}
