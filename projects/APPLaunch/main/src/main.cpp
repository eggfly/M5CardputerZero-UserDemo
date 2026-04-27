#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "ui/ui.h"
#include "keyboard_input.h"
#include "compat/input_keys.h"
#include <cstring>
#include <chrono>
// #include "ui/inter_process_comms.h"

// #define BACKWARD_HAS_DW 1
// #include "backward.hpp"
// #include "backward.h"

const char* lock_file = "/tmp/M5CardputerZero-APPLaunch_fcntl.lock";


static const char *getenv_default(const char *name, const char *dflt)
{
    return getenv(name) ? : dflt;
}

int get_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (dev_path == NULL || buf_size == 0) {
        return -1;
    }

    FILE *fp = fopen("/proc/fb", "r");
    if (fp == NULL) {
        perror("Failed to open /proc/fb");
        return -1;
    }

    char line[256];
    int  fb_num = -1;

    /* 逐行读取，查找包含 fb_st7789v 的行，格式如：0 fb_st7789v */
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "fb_st7789v") != NULL) {
            if (sscanf(line, "%d", &fb_num) == 1) {
                break;
            }
        }
    }

    fclose(fp);

    if (fb_num < 0) {
        fprintf(stderr, "fb_st7789v not found in /proc/fb\n");
        return -1;
    }

    snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}


static int _evdev_process_key(uint16_t code)
{
    switch(code) {
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_DELETE:
            return LV_KEY_DEL;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_ENTER:
            return LV_KEY_ENTER;
        case KEY_NEXT:
            return LV_KEY_NEXT;
        case KEY_TAB:
            return KEY_TAB;
        case KEY_PREVIOUS:
            return LV_KEY_PREV;
        case KEY_HOME:
            return LV_KEY_HOME;
        case KEY_END:
            return LV_KEY_END;
        default:
            return code;
    }
}


/* ----------------- LVGL read callback ----------------- */
static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    /* 每次 LVGL 调用，输出队列里一个事件 */
    // kp_evt_t e;
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
    // 出队列
    {
        pthread_mutex_lock(&keyboard_mutex);
        if (!STAILQ_EMPTY(&keyboard_queue))
        {
            struct key_item *elm = NULL;
            elm = STAILQ_FIRST(&keyboard_queue);
            STAILQ_REMOVE_HEAD(&keyboard_queue, entries);

            printf("Read key event from queue: code=%u state=%u\n", elm->key_code, elm->key_state);
            lv_obj_t *root = lv_screen_active();
            if (root) {
                lv_obj_send_event(root, (lv_event_code_t)LV_EVENT_KEYBOARD, elm);
            }
            // printf("lv_obj_send_event event to root object over\n");

            data->key = _evdev_process_key(elm->key_code);
            if(data->key)
            {
                data->state = (lv_indev_state_t)elm->key_state;
                data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
            }
            free(elm);
        }
        pthread_mutex_unlock(&keyboard_mutex);
    }
}

#if LV_USE_EVDEV

static void lv_linux_indev_init(void)
{
    const char *mouse_device = getenv_default("LV_LINUX_MOUSE_DEVICE", NULL);
    const char *keyboard_device = getenv_default("LV_LINUX_KEYBOARD_DEVICE", "/dev/input/by-path/platform-3f804000.i2c-event");
    const char *keyboard_map = getenv_default("LV_LINUX_KEYBOARD_MAP", "/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map");
    // /home/nihao/w2T/github/m5stack-linux-dtoverlays/modules/tca8418-1.0/tca8418_keypad_m5stack_keymap.map
    setenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE", keyboard_device, 1);
    setenv("APPLAUNCH_LINUX_KEYBOARD_MAP", keyboard_map, 1);
 
    {
        pthread_t keyboard_read_thread_id;
        pthread_create(&keyboard_read_thread_id,       // 线程ID（输出）
                                    NULL,       // 线程属性（NULL=默认）
                                    keyboard_read_thread,// 线程函数
                                    NULL);      // 传给线程函数的参数
        pthread_detach(keyboard_read_thread_id);
    }
 
 
 
    lv_indev_t * touch = NULL;
    if (mouse_device)
        touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, mouse_device);

    lv_indev_t * keyboard = NULL;
    // if (keyboard_device)
    //     keyboard = tca8418_keypad_init(keyboard_device, keyboard_map);
    // if (keyboard_device)
    //     keyboard = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keyboard_device);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, keypad_read_cb);
}
#endif


#if LV_USE_LINUX_FBDEV
static void lv_linux_disp_init(void)
{
    // export LV_LINUX_FBDEV_DEVICE="/dev/fb$(grep 'fb_st7789v' /proc/fb | awk '{print $1}')"
    const char *device = NULL;
    char fbdev[64] = {0};
    device = getenv_default("LV_LINUX_FBDEV_DEVICE", NULL);
    if ((device == NULL) && (get_st7789v_fbdev(fbdev, sizeof(fbdev)) == 0)) {
        device = fbdev;
    }
    setenv("APPLAUNCH_LINUX_FBDEV_DEVICE", device, 1);
    printf("Using framebuffer device: %s\n", device);
    lv_display_t * disp = lv_linux_fbdev_create();
    if(disp == NULL) {
        printf("Failed to create fbdev display!\n");
        return;
    }

    lv_linux_fbdev_set_file(disp, device);

    // 打印获取到的分辨率
    lv_coord_t w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t h = lv_display_get_vertical_resolution(disp);
    printf("Framebuffer resolution: %dx%d\n", w, h);
}
#if ! LV_USE_EVDEV && ! LV_USE_LIBINPUT
static void lv_linux_indev_init(void)
{
}
#endif

#elif LV_USE_LINUX_DRM
static void lv_linux_disp_init(void)
{
    const char *device = getenv_default("LV_LINUX_DRM_CARD", "/dev/dri/card0");
    lv_display_t * disp = lv_linux_drm_create();

    lv_linux_drm_set_file(disp, device, -1);
}
#elif LV_USE_SDL
static void lv_linux_disp_init(void)
{
    const int width = atoi(getenv("LV_SDL_VIDEO_WIDTH") ? : "320");
    const int height = atoi(getenv("LV_SDL_VIDEO_HEIGHT") ? : "170");

    lv_sdl_window_create(width, height);
}

static void lv_linux_indev_init(void)
{
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
}

#else
#error Unsupported configuration
#endif
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

void APPLaunch_lock()
{
    static std::chrono::time_point<std::chrono::steady_clock> start_time;
    static std::chrono::time_point<std::chrono::steady_clock> end_time;
    static int home_back_status = 0;

    int fd = open(lock_file, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("open failed");
        return ;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));

    /*
     * 查询：如果当前进程想加一把写锁，会不会被阻塞？
     */
    fl.l_type = F_WRLCK;      // 查询写锁冲突
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;             // 0 表示整个文件

    /*
     * F_GETLK：只查询锁，不会加锁
     */
    if (fcntl(fd, F_GETLK, &fl) == -1) {
        perror("fcntl F_GETLK failed");
        close(fd);
        return ;
    }
    static int lvgl_lock = 0;
    if (fl.l_type == F_UNLCK) {
        // printf("lock is not held by another process\n");
        if(lvgl_lock == 1)
        {
            LVGL_RUN_FLAGE = 1;
            lvgl_lock = 0;
            lv_obj_invalidate(lv_scr_act());
        }
        
    } else {
        // printf("lock is held by another process\n");
        // printf("lock type: %s\n", fl.l_type == F_WRLCK ? "write lock" : "read lock");
        // printf("lock holder pid: %d\n", fl.l_pid);
        {
            if(LVGL_HOME_KEY_FLAGE)
            {
                if(home_back_status == 0)
                {
                    home_back_status = 1;
                    start_time = std::chrono::steady_clock::now();
                }
                if(home_back_status == 1)
                {
                    end_time = std::chrono::steady_clock::now();
                    if(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count() >= 5)
                    {
                        home_back_status = 2;
                        start_time = std::chrono::steady_clock::now();
                    }
                }
                if(home_back_status == 2)
                {
                    kill(fl.l_pid, SIGINT);
                    end_time = std::chrono::steady_clock::now();
                    if(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count() >= 3)
                    {
                        home_back_status = 4;
                    }
                }
                if(home_back_status == 4)
                {
                    kill(fl.l_pid, SIGKILL);
                }
            }
            else
            {
                home_back_status = 0;
            }
        }
        lvgl_lock = 1;
        LVGL_RUN_FLAGE = 0;
    }

    close(fd);
}


int main(void)
{

    lv_init();

    /*Linux display device init*/
    lv_linux_disp_init();

    lv_linux_indev_init();

    LV_EVENT_KEYBOARD = lv_event_register_id();
    /*Create a Demo*/
    // lv_demo_widgets();
    // lv_demo_widgets_start_slideshow();
    // lv_demo_music();

    ui_init();
    // lv_demo_widgets(); // 用LVGL自带demo测试
    /*Handle LVGL tasks*/
    printf("Entering main loop...\n");
    while(1) {
        APPLaunch_lock();
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}

