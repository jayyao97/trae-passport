// Dedicated full-screen Bloub firmware entry point.
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "demo.h"
#include "esp_log.h"

static const char *TAG = "main";

/* The callback only posts an event bit; rendering stays in the animation task. */
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    demo_bloub_key(btn, ev);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting full-screen Bloub");
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Buttons unavailable; autoplay will continue");
    }
    demo_bloub_enter();
    ESP_LOGI(TAG, "Bloub ready: 14 states, antialiased RGB565 renderer");
}
