#include <pebble.h>

#define MAIN_TEXT    "Pain App"
#define SAMPLING_RATE  ACCEL_SAMPLING_10HZ
#define KEY_DATA       1337
#define KEY_COUNT      1338
#define KEY_TIME       1339
#define KEY_PAIN       1340
#define KEY_PAIN_TIME  1341
#define SAMPLE_COUNT    1
#define EMPTY_VALUE    -1

int16_t *acc_data;
time_t acc_time;
time_t pain_time;
uint16_t sample_count=0;
uint16_t sent_count=1;
uint16_t ack_count=0;
uint16_t fail_count=0;
bool waiting_accel_data = false;
bool waiting_pain_data = false;
bool waiting_pain_data_update = false;
bool msg_run = false;
bool painlayer_hidden;

typedef struct {
  uint32_t tag;
  TextLayer *text_layer;
  GBitmap *bitmap;
} ArrowSelection;

typedef struct {
  //int16_t *acc_data;
  uint8_t x, y, z, pain_level;
  time_t time;
  time_t pain_time;
} acc_data_struct;

static const int RESOURCE_IDS[3] = {
  RESOURCE_ID_IMAGE_ACTION_ICON_UP,
  RESOURCE_ID_IMAGE_ACTION_ICON_SELECT,
  RESOURCE_ID_IMAGE_ACTION_ICON_DOWN
};

static Window * window;
static TextLayer *time_layer;
static TextLayer *text_layer;
static TextLayer *painlevel_layer;
static BitmapLayer *up_arrow_layer;
static BitmapLayer *down_arrow_layer;
static ArrowSelection s_arrow_selection[3]; // 0 = Up arrow, 1 = Select arrow, 2 = Down arrow
static acc_data_struct acc_data_array[SAMPLE_COUNT];
static uint8_t num_samples = (SAMPLE_COUNT*3); //Total x, y, z samples
static int8_t num_pain;
static int8_t num_pain_send;
static char placeholder[3];
static const uint32_t inbound_size = 64;
static const uint32_t outbound_size = 600;
static void toggle_painlayer();

// Called once per second
static void handle_second_tick(struct tm* tick_time, TimeUnits units_changed) {
  static char time_text[] = "00:00:00"; 

  static char hours[2];
  static char minutes[2];
  static char seconds[2];

  strftime(time_text, sizeof(time_text), "%T", tick_time);
  text_layer_set_text(time_layer, time_text);

  strncpy(hours, time_text, 2);
  strncpy(minutes, time_text+3, 2);
  strncpy(seconds, time_text+6, 2);
  
  if (atoi(seconds) % 15 == 0) { //Display pain gauge every 15th second
    if ((waiting_pain_data == false) &&
      (layer_get_hidden(text_layer_get_layer(painlevel_layer))) == true) {
        toggle_painlayer();
    }

    if ((waiting_pain_data == true) &&
      (layer_get_hidden(text_layer_get_layer(painlevel_layer))) == false) {
        toggle_painlayer();
    }
  }
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message failed to Send! fail count: %3d | error: 0x%02X ",++fail_count,reason);
  msg_run = false;
}

void out_received_handler(DictionaryIterator *received, void *context) {
  waiting_accel_data = false;
  if (num_pain_send != EMPTY_VALUE)
    waiting_pain_data = false;
  ack_count++;
  msg_run = false;
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  AccelData *d = data;
  uint16_t cnt = 0;
  for (uint8_t i = 0; i < num_samples; i++, d++) {
    acc_data_struct *acc = &acc_data_array[i];

    acc[i].x = d->x;
    acc[i].y = d->y;
    acc[i].z = d->z;
    acc[i].time = d->timestamp;
    if (waiting_pain_data_update == true) {
      acc[i].pain_level = num_pain;
      acc[i].pain_time = pain_time;
      waiting_pain_data_update = false;
    }
    else {
      acc[i].pain_level = -1;
      acc[i].pain_time = -1;
    }
    waiting_accel_data = true;
    sample_count++;
  }
}

static void init_arrows(Window *window) {
  for (int i = 0; i < 3; i++) {
    ArrowSelection *arrow_selection = &s_arrow_selection[i];
    arrow_selection->bitmap = gbitmap_create_with_resource(RESOURCE_IDS[i]);
  }
}

static void bitmap_init(Window *window, Layer *window_layer) {
  GRect bounds = layer_get_frame(window_layer);
  up_arrow_layer = bitmap_layer_create(bounds);
  down_arrow_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(up_arrow_layer, s_arrow_selection[0].bitmap);
  bitmap_layer_set_bitmap(down_arrow_layer, s_arrow_selection[2].bitmap);
  bitmap_layer_set_alignment(up_arrow_layer, GAlignTopRight);
  bitmap_layer_set_alignment(down_arrow_layer, GAlignBottomRight);
  layer_add_child(window_layer, bitmap_layer_get_layer(up_arrow_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(down_arrow_layer));
}

static void deinit_arrow_selection(void) {
  for (int i = 0; i < 3; i++) {
    ArrowSelection *arrow_selection = &s_arrow_selection[i];
    text_layer_destroy(arrow_selection->text_layer);
    gbitmap_destroy(arrow_selection->bitmap);
  }
}

static void toggle_painlayer() {
  if (layer_get_hidden(text_layer_get_layer(painlevel_layer)) == false) {
    layer_set_hidden(bitmap_layer_get_layer(up_arrow_layer), true);
    layer_set_hidden(text_layer_get_layer(painlevel_layer), true);
    layer_set_hidden(bitmap_layer_get_layer(down_arrow_layer), true);
    painlayer_hidden = true;
  }
  else {
    text_layer_set_text(painlevel_layer, "5"); //Default pain level value
    num_pain = atoi(text_layer_get_text(painlevel_layer));
    layer_set_hidden(bitmap_layer_get_layer(up_arrow_layer), false);
    layer_set_hidden(text_layer_get_layer(painlevel_layer), false);
    layer_set_hidden(bitmap_layer_get_layer(down_arrow_layer), false);
    painlayer_hidden = false;
  }
}
//Adjusts the pain level, keeping it from going above 10.
static void up_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (painlayer_hidden == false) {
    num_pain = atoi(text_layer_get_text(painlevel_layer));
    if (num_pain < 10) {
      snprintf(placeholder, sizeof(placeholder), "%d", ++num_pain);
      text_layer_set_text(painlevel_layer, placeholder);
    }
  }
}

static void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (painlayer_hidden == false) {
    waiting_pain_data = true;
    waiting_pain_data_update = true;
    pain_time = time(NULL);
    toggle_painlayer();
    painlayer_hidden = true;
  }
}

//Adjusts the pain level, keeping it from going below 0.
static void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (painlayer_hidden == false) { 
    num_pain = atoi(text_layer_get_text(painlevel_layer));
      if (num_pain > 0) {
        snprintf(placeholder, sizeof(placeholder), "%d", --num_pain);
        text_layer_set_text(painlevel_layer, placeholder);
    }
    else num_pain = 0;
  }
}

static void config_provider(Window *window) {
  window_single_click_subscribe(BUTTON_ID_UP, up_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler);
}


static void on_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  init_arrows(window);
  bitmap_init(window, window_layer);
  window_set_background_color(window, GColorBlack);
  window_set_click_config_provider(window, (ClickConfigProvider) config_provider);

  // Init the text layers
  painlevel_layer = text_layer_create(GRect(130, 68, 144-40 /* width */, 168-54 /* height */));
  time_layer = text_layer_create(GRect(35, 61, 144-40, 168-54));
  text_layer = text_layer_create(GRect(35, 87, 144-40, 168-54));

  text_layer_set_text(painlevel_layer, "5");
  text_layer_set_text_color(painlevel_layer, GColorWhite);
  text_layer_set_background_color(painlevel_layer, GColorClear);
  text_layer_set_font(painlevel_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));


  text_layer_set_text(text_layer, MAIN_TEXT);
  text_layer_set_text_color(time_layer, GColorWhite);
  text_layer_set_text_color(text_layer, GColorWhite);
  text_layer_set_background_color(time_layer, GColorClear);
  text_layer_set_background_color(text_layer, GColorClear);
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_font(text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  // Ensures time is displayed immediately (will break if NULL tick event accessed).
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  handle_second_tick(current_time, SECOND_UNIT);
  tick_timer_service_subscribe(SECOND_UNIT, &handle_second_tick);

  layer_add_child(window_layer, text_layer_get_layer(painlevel_layer));
  layer_add_child(window_layer, text_layer_get_layer(time_layer));
  layer_add_child(window_layer, text_layer_get_layer(text_layer));

  accel_data_service_subscribe(num_samples, &accel_data_handler);
  accel_service_set_sampling_rate(SAMPLING_RATE);
  acc_data = malloc(num_samples*6);

  app_message_register_outbox_failed(out_failed_handler);
  app_message_register_outbox_sent(out_received_handler);
  app_message_open(inbound_size, outbound_size);
}

void on_window_unload(Window *window) {
  accel_data_service_unsubscribe();
  free(acc_data);
  text_layer_destroy(painlevel_layer);
  text_layer_destroy(time_layer);
  text_layer_destroy(text_layer);
  bitmap_layer_destroy(up_arrow_layer);
  bitmap_layer_destroy(down_arrow_layer);
  deinit_arrow_selection();
}

int main(void) {
  window = window_create();
  window_set_fullscreen(window, true);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = on_window_load,
    .unload = on_window_unload,
  });
  window_stack_push(window, true);
  app_event_loop();
  window_destroy(window);
}
