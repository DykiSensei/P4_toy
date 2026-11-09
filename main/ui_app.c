#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lcd_display.h"
#include "touch_input.h"
#include "ui_canvas.h"
#include "ui_pages.h"

static const char *TAG="nature_ui";
static ui_page_t current_page=UI_HOME;
static bool nvs_ready,calibrated,calibration_retry;
static int calibration_step;
static int raw_points[4][2];
static const int target_points[4][2]={{36,96},{284,96},{284,376},{36,376}};
typedef struct {uint32_t version;float m[6];} touch_calibration_t;
static touch_calibration_t calibration;

static void show_page(ui_page_t next)
{
    current_page=next;
    const int64_t start=esp_timer_get_time();
    if(next==UI_CALIBRATE)ui_calibration_draw(calibration_step,calibration_retry);
    else ui_pages_draw(next,calibrated);
    ui_canvas_render(lcd_display_write_strip);
    ESP_LOGI(TAG,"PAGE %s; render=%lld ms; free_internal=%u",
        ui_page_name(next),(long long)((esp_timer_get_time()-start)/1000),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void start_calibration(void)
{
    calibration_step=0;
    calibration_retry=false;
    memset(raw_points,0,sizeof(raw_points));
    ESP_LOGI(TAG,"CAL start; touch targets clockwise from upper-left; release between points");
    show_page(UI_CALIBRATE);
}

static bool calibration_sane(const touch_calibration_t *c)
{
    if(c->version!=1)return false;
    for(int i=0;i<6;i++)if(!isfinite(c->m[i])||fabsf(c->m[i])>2000.0f)return false;
    const float det=c->m[0]*c->m[4]-c->m[1]*c->m[3];
    return fabsf(det)>0.0005f&&fabsf(det)<0.25f;
}

static void load_calibration(void)
{
    // Never erase an existing NVS partition to recover from an unrelated error.
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK){ESP_LOGW(TAG,"NVS unavailable (%s); calibration will be session-only",esp_err_to_name(err));return;}
    nvs_ready=true;
    nvs_handle_t handle;
    if(nvs_open("nature_ui",NVS_READONLY,&handle)!=ESP_OK)return;
    size_t size=sizeof(calibration);
    err=nvs_get_blob(handle,"touch_v1",&calibration,&size);
    nvs_close(handle);
    calibrated=err==ESP_OK&&size==sizeof(calibration)&&calibration_sane(&calibration);
    ESP_LOGI(TAG,"Touch calibration %s",calibrated?"loaded from NVS":"not stored");
}

static void store_calibration(void)
{
    if(!nvs_ready){ESP_LOGW(TAG,"Calibration active in RAM only");return;}
    nvs_handle_t handle;
    esp_err_t err=nvs_open("nature_ui",NVS_READWRITE,&handle);
    if(err==ESP_OK){
        err=nvs_set_blob(handle,"touch_v1",&calibration,sizeof(calibration));
        if(err==ESP_OK)err=nvs_commit(handle);
        nvs_close(handle);
    }
    if(err==ESP_OK)ESP_LOGI(TAG,"CAL saved to NVS");
    else ESP_LOGW(TAG,"CAL save failed: %s; active until power-off",esp_err_to_name(err));
}

static void raw_to_pixel(int raw_x,int raw_y,int *x,int *y)
{
    *x=(int)lroundf(calibration.m[0]*raw_x+calibration.m[1]*raw_y+calibration.m[2]);
    *y=(int)lroundf(calibration.m[3]*raw_x+calibration.m[4]*raw_y+calibration.m[5]);
}

static bool solve_calibration(void)
{
    float u=raw_points[0][0],v=raw_points[0][1];
    float du1=raw_points[1][0]-u,dv1=raw_points[1][1]-v;
    float du2=raw_points[2][0]-u,dv2=raw_points[2][1]-v;
    float det=du1*dv2-du2*dv1;
    if(fabsf(det)<100000.0f)return false;
    touch_calibration_t next={.version=1};
    for(int axis=0;axis<2;axis++){
        float ds1=target_points[1][axis]-target_points[0][axis];
        float ds2=target_points[2][axis]-target_points[0][axis];
        next.m[axis*3]=(ds1*dv2-ds2*dv1)/det;
        next.m[axis*3+1]=(du1*ds2-du2*ds1)/det;
        next.m[axis*3+2]=target_points[0][axis]-next.m[axis*3]*u-next.m[axis*3+1]*v;
    }
    if(!calibration_sane(&next))return false;
    float x=next.m[0]*raw_points[3][0]+next.m[1]*raw_points[3][1]+next.m[2];
    float y=next.m[3]*raw_points[3][0]+next.m[4]*raw_points[3][1]+next.m[5];
    float dx=x-target_points[3][0],dy=y-target_points[3][1];
    ESP_LOGI(TAG,"CAL independent fourth-point error=%.1f px",sqrtf(dx*dx+dy*dy));
    if(dx*dx+dy*dy>20.0f*20.0f)return false;
    calibration=next;
    return true;
}

static void accept_touch(int raw_x,int raw_y,int first_x,int first_y)
{
    ESP_LOGI(TAG,"TOUCH raw=(%d,%d)",raw_x,raw_y);
    if(current_page==UI_CALIBRATE){
        raw_points[calibration_step][0]=raw_x;raw_points[calibration_step][1]=raw_y;
        ESP_LOGI(TAG,"CAL sample %d/4 = (%d,%d)",calibration_step+1,raw_x,raw_y);
        if(++calibration_step<4){show_page(UI_CALIBRATE);return;}
        if(solve_calibration()){
            calibrated=true;store_calibration();show_page(UI_HOME);
        }else{
            ESP_LOGW(TAG,"CAL rejected; repeat four targets");
            calibration_step=0;calibration_retry=true;show_page(UI_CALIBRATE);
        }
        return;
    }
    if(!calibrated){start_calibration();return;}
    int x,y,sx,sy;
    raw_to_pixel(raw_x,raw_y,&x,&y);raw_to_pixel(first_x,first_y,&sx,&sy);
    if(x<0||x>=320||y<0||y>=480||abs(x-sx)>30||abs(y-sy)>30)return;
    ui_page_t next=ui_pages_tap(current_page,x,y);
    // A drag across different targets must not activate the release target.
    if(next!=ui_pages_tap(current_page,sx,sy))return;
    ESP_LOGI(TAG,"TOUCH pixel=(%d,%d) page=%s -> %s",x,y,ui_page_name(current_page),ui_page_name(next));
    if(next==UI_CALIBRATE)start_calibration();
    else if(next!=current_page)show_page(next);
}

static void serial_command(char command)
{
    switch(command){
        case '0':case 'h':show_page(UI_HOME);break;
        case '1':show_page(UI_CAMERA);break;
        case '2':show_page(UI_MAP);break;
        case '3':show_page(UI_COLLECTION);break;
        case '4':show_page(UI_BATTLE);break;
        case 's':show_page(UI_SETTINGS);break;
        case 'a':show_page(UI_ABOUT);break;
        case 'c':start_calibration();break;
        case '?':ESP_LOGI(TAG,"Commands: h/0 home, 1 camera, 2 map, 3 collection, 4 battle, s settings, a about, c calibrate; calibrated=%d IRQ_down=%d stack_free=%u",calibrated,touch_input_is_down(),(unsigned)uxTaskGetStackHighWaterMark(NULL));break;
        default:break;
    }
}

void app_main(void)
{
    lcd_display_init();
    load_calibration();
    const bool touch_ready=touch_input_init();
    usb_serial_jtag_driver_config_t usb=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t usb_err=usb_serial_jtag_driver_install(&usb);
    if(usb_err==ESP_OK)usb_serial_jtag_vfs_use_driver();
    else ESP_LOGW(TAG,"USB command input unavailable: %s",esp_err_to_name(usb_err));
    show_page(UI_HOME);
    lcd_display_backlight(true);
    ESP_LOGI(TAG,"READY: Nature journal UI, 320x480, LCD SPI2 10MHz; touch_gpio=%d calibrated=%d",touch_ready,calibrated);
    serial_command('?');

    bool active=false,have_first=false,discard_until_release=false;
    int samples=0,sum_x=0,sum_y=0,first_x=0,first_y=0;
    int64_t released_since=0,pressed_since=0,last_log=esp_timer_get_time();
    while(true){
        if(usb_err==ESP_OK){char commands[16];int n=usb_serial_jtag_read_bytes(commands,sizeof(commands),0);for(int i=0;i<n;i++)serial_command(commands[i]);}
        int64_t now=esp_timer_get_time();
        if(touch_ready&&touch_input_is_down()){
            released_since=0;
            if(!active){active=true;have_first=false;discard_until_release=false;samples=0;sum_x=sum_y=0;pressed_since=now;}
            // Recovery depends only on PENIRQ, never on a possibly bad coordinate
            // transform. Consume the initiating hold before accepting target 1.
            if(!discard_until_release&&now-pressed_since>=3000000){
                ESP_LOGI(TAG,"CAL long-press recovery; release before touching target 1");
                start_calibration();
                discard_until_release=true;samples=0;sum_x=sum_y=0;
            }
            int rx,ry;
            if(!discard_until_release&&touch_input_read_raw(&rx,&ry)){
                if(!have_first){first_x=rx;first_y=ry;have_first=true;}
                // Restart a settling burst, but preserve the original down position.
                if(samples>0&&(abs(rx-sum_x/samples)>90||abs(ry-sum_y/samples)>90)){
                    samples=0;sum_x=sum_y=0;
                }
                if(samples<8){sum_x+=rx;sum_y+=ry;samples++;}
            }
        }else if(active){
            if(!released_since)released_since=now;
            if(now-released_since>=60000){
                active=false;
                if(!discard_until_release&&samples>=3&&now-pressed_since>=80000)accept_touch(sum_x/samples,sum_y/samples,first_x,first_y);
                else if(!discard_until_release)ESP_LOGW(TAG,"Touch release discarded: only %d stable samples",samples);
                discard_until_release=false;
                samples=0;sum_x=sum_y=0;
            }
        }
        if(now-last_log>15000000){
            ESP_LOGI(TAG,"ALIVE page=%s calibrated=%d touch_down=%d free_internal=%u stack_free=%u",ui_page_name(current_page),calibrated,touch_input_is_down(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            last_log=now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
