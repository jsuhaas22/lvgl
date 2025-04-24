/**
 * @file lv_demo_high_res_app_smart_home.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_demo_high_res_private.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#if LV_USE_DEMO_HIGH_RES

#include "../../src/widgets/image/lv_image.h"
#include "../../src/widgets/label/lv_label.h"
#include "../../src/widgets/slider/lv_slider.h"
#include "../../src/widgets/arc/lv_arc.h"
#include "../../src/widgets/switch/lv_switch.h"
#include "../../src/widgets/arc/lv_arc.h"
#include "../../src/widgets/button/lv_button.h"

/*********************
 *  Global variables
 *********************/
extern pthread_mutex_t playing_now_lock;
extern int playing_now;
static const char start_charging_string[] = "Start charging";
volatile int inmate_started = 0;

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void back_clicked_cb(lv_event_t * e);
static void create_widget1(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets);
static void charging_arc_observer(lv_observer_t * observer, lv_subject_t * subject);
static void charging_percent_label_observer(lv_observer_t * observer, lv_subject_t * subject);
static void charging_time_until_full_label_observer(lv_observer_t * observer, lv_subject_t * subject);
static void create_widget_charging(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets);
static lv_obj_t * create_widget5_lightbulbs(lv_demo_high_res_ctx_t * c, lv_obj_t * parent);
static void create_widget5(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static lv_demo_high_res_api_t *global_api;

#define PORT 12345
#define TIMEOUT_SEC 5

const char *restart_inmate_cmd = "jailhouse cell linux inmate.cell inmate.bin";

static long current_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}


static void start_jailhouse() {
    system("jailhouse enable /usr/share/jailhouse/cells/k3-am62l3-evm.cell");
}

static void start_inmate() {
    printf("Starting inmate...\n");
    //system("jailhouse enable /usr/share/jailhouse/cells/k3-am62p5-sk.cell");
    system("jailhouse cell create /usr/share/jailhouse/cells/k3-am62l3-evm-linux-demo.cell");
    system("jailhouse cell load k3-am62l3-evm-linux-demo /usr/libexec/jailhouse/linux-loader.bin -a 0x0 "
            "-s \"kernel=0xc0800000 dtb=0xc0600000\" -a 0x1000 /boot/Image -a 0xc0800000 "
            "/boot/rootfs.cpio -a 0xc30ca000 "
            "/usr/share/jailhouse/inmate-k3-am62l3-evm.dtb -a 0xc0600000");
    system("jailhouse cell start k3-am62l3-evm-linux-demo");
}

static void stop_inmate() {
    system("jailhouse cell shutdown 1");
    system("jailhouse cell destroy 1");
}

//static void run_server(lv_demo_high_res_api_t *api)
static void run_server()
{
    int server_fd;
    struct sockaddr_in server_addr;

    // Start jailhouse
    start_jailhouse();
    start_inmate();

    while (1) {
	int i=0;
	while (i<=20) {                                                                                       
        	if (system("ifconfig enp0s1") == 0) {                                                              
                	printf("Interface is up\n");                                               
                	system("ifconfig enp0s1 192.168.0.2");                                    
                	break;                                                     
        	}                                                                  
        	sleep(1);                                                     
        	i++;                                                                  
  	}
	if(i==20)
		return;  
        // Step 1: Create server socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd, 1) < 0) {
            perror("Listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        printf("Waiting for inmate connection on port %d...\n", PORT);

        // Step 2: Accept connection
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Accept failed");
            close(server_fd);
            continue;
        }

        printf("Inmate connected.\n");
        long last_recv_time = current_time_sec();
        char line[256];

        inmate_started = 1;


        // Step 3: Receive loop
        while (1) {
            fd_set readfds;
            struct timeval timeout;
            char buffer[256];

            FD_ZERO(&readfds);
            FD_SET(client_fd, &readfds);
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(client_fd + 1, &readfds, NULL, NULL, &timeout);

            if (activity > 0 && FD_ISSET(client_fd, &readfds)) {
                int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0) {
                    printf("Inmate disconnected.\n");
                    break;
                }
                buffer[bytes] = '\0';
                float cpu_usage; 
                int mem_total, mem_free;
                printf("Received: %s", buffer);
                sscanf(buffer, "CPU: %f | MemTotal: %d | MemFree: %d", &cpu_usage, &mem_total, &mem_free);
                printf("CPU USAGE LVGL: %.2f\n", cpu_usage);
                int ddr_usage = ((mem_total - mem_free) / mem_total) * 100;
                lv_subject_set_int(&global_api->subjects.cpu_usage_sub, cpu_usage * 100);
                lv_subject_set_int(&global_api->subjects.ddr_usage_sub, ddr_usage);
                printf("GLOBAL API: %d\n", lv_subject_get_int(&global_api->subjects.cpu_usage_sub));
                printf("GLOBAL API: %d\n", lv_subject_get_int(&global_api->subjects.ddr_usage_sub));
                printf("NON GLOBAL: %d\n", ddr_usage);
                last_recv_time = current_time_sec();
            }

            if (current_time_sec() - last_recv_time >= TIMEOUT_SEC) {
                printf("Timeout: No data received for %d seconds.\n", TIMEOUT_SEC);
                close(client_fd);
                close(server_fd);
                stop_inmate();
                sleep(1);
                start_inmate();
                break;  // Break out and re-enter full loop to re-bind and listen again
            }
        }
    }
}

int *hypervisor_init(lv_demo_high_res_api_t* api)
{
    printf("Hypervisor init called!\n");
/*    if (access("/usr/share/jailhouse/root2", F_OK)) {
        printf("The program doesn't exist.\n");
        pthread_exit(0);
    } */

//    FILE *fp = popen("/usr/share/jailhouse/root2", "r");
/*    if (!fp) {
        perror("popen failed");
        pthread_exit(0);
    } */

/*    char line[256];
    printf("ETNERING LOOP\n");
    while (1) {
        if (fgets(line, sizeof(line), fp) != NULL) {
            printf("LOOP 1\n");
            int cpu_usage, mem_total, mem_free;
            sscanf(line, "Received: CPU: %f | MemTotal: %d | MemFree: %d", &cpu_usage, &mem_total, &mem_free);
            printf("LOOP 2\n");
            printf("LVGL CPU USAGE: %f\n", cpu_usage);
            lv_subject_set_int(&api->subjects.cpu_usage_sub, lv_map(cpu_usage, 0, 100, 0, 100));
        } else {
            printf("Condition failed\n");
        }
        sleep(1);
    }
    return NULL; */

    global_api = api;
    lv_subject_init_int(&global_api->subjects.cpu_usage_sub, 0);
    lv_subject_init_int(&global_api->subjects.ddr_usage_sub, 0);
    run_server();
}

void lv_demo_high_res_app_hypervisor(lv_obj_t * base_obj)
{
    lv_demo_high_res_ctx_t * c = lv_obj_get_user_data(base_obj);

    /* background */

    lv_obj_t * bg = base_obj;
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));

    lv_obj_t * bg_img = lv_image_create(bg);
    lv_subject_add_observer_obj(&c->th, lv_demo_high_res_theme_observer_image_src_cb, bg_img,
                                &c->imgs[IMG_LIGHT_BG_SMART_HOME]);

    lv_obj_t * bg_cont = lv_obj_create(bg);
    lv_obj_remove_style_all(bg_cont);
    lv_obj_set_size(bg_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(bg_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(bg_cont, c->sz->gap[7], 0);

    /* top margin */

    lv_obj_t * top_margin = lv_demo_high_res_top_margin_create(bg_cont,
                                                               c->sz == &lv_demo_high_res_sizes_all[SIZE_SM] ? c->sz->gap[9] : c->sz->gap[10], true, c);

    /* app info */

    lv_obj_t * app_info = lv_demo_high_res_simple_container_create(bg_cont, true, c->sz->gap[4], LV_FLEX_ALIGN_START);
    lv_obj_add_flag(app_info, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align_to(app_info, top_margin, LV_ALIGN_OUT_BOTTOM_LEFT, c->sz->gap[10], c->sz->gap[10]);

    lv_obj_t * back = lv_demo_high_res_simple_container_create(app_info, false, c->sz->gap[2], LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(back, back_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * back_icon = lv_image_create(back);
    lv_image_set_src(back_icon, c->imgs[IMG_ARROW_LEFT]);
    lv_obj_add_style(back_icon, &c->styles[STYLE_COLOR_BASE][STYLE_TYPE_A8_IMG], 0);
    lv_obj_add_flag(back_icon, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t * back_label = lv_label_create(back);
    lv_label_set_text_static(back_label, "Back");
    lv_obj_set_style_text_opa(back_label, LV_OPA_60, 0);
    lv_obj_add_style(back_label, &c->styles[STYLE_COLOR_BASE][STYLE_TYPE_TEXT], 0);
    lv_obj_add_style(back_label, &c->fonts[FONT_HEADING_MD], 0);

    lv_obj_t * app_label = lv_label_create(app_info);
    lv_label_set_text_static(app_label, "Hypervisor");
    lv_obj_add_style(app_label, &c->styles[STYLE_COLOR_BASE][STYLE_TYPE_TEXT], 0);
    lv_obj_add_style(app_label, &c->fonts[FONT_HEADING_LG], 0);

    /* widgets */

    lv_obj_t * widgets = lv_obj_create(bg_cont);
    lv_obj_remove_style_all(widgets);
    lv_obj_set_width(widgets, LV_PCT(100));
    lv_obj_set_flex_grow(widgets, 1);
    lv_obj_set_style_pad_bottom(widgets, c->sz->gap[10], 0);
    lv_obj_set_style_pad_left(widgets, c->sz->gap[10], 0);
    lv_obj_set_style_pad_right(widgets, c->sz->gap[10], 0);
    lv_obj_set_flex_flow(widgets, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(widgets, c->sz->gap[7], 0);
    lv_obj_set_flex_align(widgets, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    create_widget1(c, widgets);
    create_widget_charging(c, widgets);
    //create_widget2(c, widgets);
    //create_widget3(c, widgets);
    //create_widget4(c, widgets);
    create_widget5(c, widgets);

    /* bring app info to top so the back button can be clicked */
    lv_obj_move_to_index(app_info, -1);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void back_clicked_cb(lv_event_t * e)
{
    lv_obj_t * back = lv_event_get_target_obj(e);

    lv_obj_t * base_obj = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(back)));
    lv_obj_clean(base_obj);
    lv_demo_high_res_home(base_obj);
}

static void create_widget1(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets)
{
    lv_obj_t * widget = lv_obj_create(widgets);
    lv_obj_remove_style_all(widget);
    lv_obj_set_size(widget, c->sz->card_long_edge, c->sz->card_long_edge);
    lv_obj_set_style_bg_image_src(widget, c->imgs[IMG_SMART_HOME_WIDGET2_BG], 0);
    lv_obj_set_style_pad_all(widget, c->sz->gap[7], 0);
    lv_obj_set_flex_flow(widget, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(widget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * top_label = lv_label_create(widget);
    lv_label_set_text_static(top_label, "CPU Load");
    lv_obj_set_width(top_label, LV_PCT(100));
    lv_obj_add_style(top_label, &c->fonts[FONT_LABEL_MD], 0);
    lv_obj_set_style_text_color(top_label, lv_color_white(), 0);

    lv_obj_t * arc_cont = lv_obj_create(widget);
    lv_obj_remove_style_all(arc_cont);
    lv_obj_set_width(arc_cont, LV_PCT(100));
    lv_obj_set_flex_grow(arc_cont, 1);

    lv_obj_t * arc = lv_arc_create(arc_cont);
    lv_obj_set_align(arc, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_size(arc, c->sz->smart_home_arc_diameter, c->sz->smart_home_arc_diameter);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_rounded(arc, false, 0);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, 0);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_white(), 0);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_20, 0);
    lv_subject_add_observer_obj(&global_api->subjects.cpu_usage_sub, charging_arc_observer, arc, NULL);

    lv_obj_t * percent_label = lv_label_create(arc);
    lv_obj_add_style(percent_label, &c->fonts[FONT_LABEL_XL], 0);
    lv_obj_set_style_text_color(percent_label, lv_color_white(), 0);
    lv_obj_center(percent_label);
    lv_subject_add_observer_obj(&global_api->subjects.cpu_usage_sub, charging_percent_label_observer, percent_label, NULL);

    lv_obj_t * num_label_cont = lv_demo_high_res_simple_container_create(widget,
                                                                         false,
                                                                         c->sz->gap[1],
                                                                         LV_FLEX_ALIGN_END);
    lv_obj_set_width(num_label_cont, LV_PCT(100));

    lv_obj_t * time_to_full_num_label = lv_label_create(num_label_cont);
    lv_obj_add_style(time_to_full_num_label, &c->fonts[FONT_LABEL_XL], 0);
    lv_obj_set_style_text_color(time_to_full_num_label, lv_color_white(), 0);
    lv_subject_add_observer_obj(&global_api->subjects.cpu_usage_sub, charging_time_until_full_label_observer, time_to_full_num_label,
                                NULL);
}

static void charging_arc_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    printf("CHARGING_ARC_OBSERVER called!\n");
    lv_obj_t * arc = lv_observer_get_target_obj(observer);
//    lv_arc_set_value(arc, lv_subject_get_int(subject), 0, EV_CHARGING_RANGE_END, 0, 100));
    lv_arc_set_value(arc, lv_subject_get_int(subject));
//    lv_arc_set_value(arc, 90);
}

static void charging_percent_label_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * label = lv_observer_get_target_obj(observer);
    lv_label_set_text_fmt(label, "%"LV_PRId32"%%", lv_subject_get_int(subject));
    printf("CHARGING PERCENT LABEL: %d\n", lv_subject_get_int(subject));
}

static void charging_time_until_full_label_observer(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_obj_t * label = lv_observer_get_target_obj(observer);
    int32_t v_range_time_to_full = 100 - lv_subject_get_int(subject);
    lv_label_set_text_fmt(label, "%"LV_PRId32".%"LV_PRId32, v_range_time_to_full);
}

static void create_widget_charging(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets)
{
    lv_obj_t * widget = lv_obj_create(widgets);
    lv_obj_remove_style_all(widget);
    lv_obj_set_size(widget, c->sz->card_long_edge, c->sz->card_long_edge);
    lv_obj_set_style_bg_image_src(widget, c->imgs[IMG_SMART_HOME_WIDGET2_BG], 0);
    lv_obj_set_style_pad_all(widget, c->sz->gap[7], 0);
    lv_obj_set_flex_flow(widget, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(widget, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * top_label = lv_label_create(widget);
    lv_label_set_text_static(top_label, "Memory Usage");
    lv_obj_set_width(top_label, LV_PCT(100));
    lv_obj_add_style(top_label, &c->fonts[FONT_LABEL_MD], 0);
    lv_obj_set_style_text_color(top_label, lv_color_white(), 0);

    lv_obj_t * arc_cont = lv_obj_create(widget);
    lv_obj_remove_style_all(arc_cont);
    lv_obj_set_width(arc_cont, LV_PCT(100));
    lv_obj_set_flex_grow(arc_cont, 1);

    lv_obj_t * arc = lv_arc_create(arc_cont);
    lv_obj_set_align(arc, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_size(arc, c->sz->smart_home_arc_diameter, c->sz->smart_home_arc_diameter);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_rounded(arc, false, 0);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, 0);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_white(), 0);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_20, 0);
    lv_subject_add_observer_obj(&global_api->subjects.ddr_usage_sub, charging_arc_observer, arc, NULL);

    lv_obj_t * percent_label = lv_label_create(arc);
    lv_obj_add_style(percent_label, &c->fonts[FONT_LABEL_XL], 0);
    lv_obj_set_style_text_color(percent_label, lv_color_white(), 0);
    lv_obj_center(percent_label);
    //lv_subject_add_observer_obj(&c->ev_charging_progress, charging_percent_label_observer, percent_label, NULL);
    lv_subject_add_observer_obj(&c->api.subjects.ddr_usage_sub, charging_percent_label_observer, percent_label, NULL);

    lv_obj_t * num_label_cont = lv_demo_high_res_simple_container_create(widget,
                                                                         false,
                                                                         c->sz->gap[1],
                                                                         LV_FLEX_ALIGN_END);
    lv_obj_set_width(num_label_cont, LV_PCT(100));

    lv_obj_t * time_to_full_num_label = lv_label_create(num_label_cont);
    lv_obj_add_style(time_to_full_num_label, &c->fonts[FONT_LABEL_XL], 0);
    lv_obj_set_style_text_color(time_to_full_num_label, lv_color_white(), 0);
//    lv_subject_add_observer_obj(&c->ev_charging_progress, charging_time_until_full_label_observer, time_to_full_num_label,
 //                               NULL);
}

static lv_obj_t * create_widget5_lightbulbs(lv_demo_high_res_ctx_t * c, lv_obj_t * parent)
{
    lv_obj_t * lightbulbs_box = lv_obj_create(parent);
    lv_obj_remove_style_all(lightbulbs_box);
    lv_obj_set_height(lightbulbs_box, c->sz->indicator_height);
    lv_obj_set_flex_flow(lightbulbs_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lightbulbs_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lightbulbs_box, 0, 0);
    lv_obj_set_style_bg_color(lightbulbs_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lightbulbs_box, LV_OPA_20, 0);
    lv_obj_set_style_radius(lightbulbs_box, c->sz->gap[3], 0);
    lv_obj_set_style_pad_ver(lightbulbs_box, c->sz->gap[5], 0);

    lv_obj_t * switch_box = lv_demo_high_res_simple_container_create(lightbulbs_box, false, c->sz->gap[7],
                                                                     LV_FLEX_ALIGN_END);

    return lightbulbs_box;
}

static lv_obj_t * create_widget3_info(lv_demo_high_res_ctx_t * c, lv_obj_t * parent, const lv_image_dsc_t * img_dsc,
    const char * text, const char * unit, const char *value)
{
lv_obj_t * info = lv_obj_create(parent);
lv_obj_remove_style_all(info);
lv_obj_set_height(info, LV_SIZE_CONTENT);
lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
lv_obj_set_style_pad_row(info, c->sz->gap[4], 0);
lv_obj_set_flex_grow(info, 1);
lv_obj_set_style_bg_color(info, lv_color_white(), 0);
lv_obj_set_style_bg_opa(info, 16 * 255 / 100, 0);
lv_obj_set_style_radius(info, c->sz->gap[3], 0);
lv_obj_set_style_pad_all(info, c->sz->gap[5], 0);

lv_obj_t * sub_box1 = lv_demo_high_res_simple_container_create(info, true, c->sz->gap[2], LV_FLEX_ALIGN_START);

lv_obj_t * img = lv_image_create(sub_box1);
lv_image_set_src(img, img_dsc);
lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
lv_obj_set_style_image_recolor(img, lv_color_white(), 0);

lv_obj_t * label = lv_label_create(sub_box1);
lv_label_set_text_static(label, text);
lv_obj_add_style(label, &c->fonts[FONT_LABEL_SM], 0);
lv_obj_set_style_text_color(label, lv_color_white(), 0);

lv_obj_t * sub_box2 = lv_demo_high_res_simple_container_create(info, false, c->sz->gap[1], LV_FLEX_ALIGN_END);

lv_obj_t * label_number = lv_label_create(sub_box2);
lv_obj_add_style(label_number, &c->fonts[FONT_LABEL_LG], 0);
lv_obj_set_style_text_color(label_number, lv_color_white(), 0);
lv_label_set_text(label_number, value);

lv_obj_t * label_unit = lv_label_create(sub_box2);
lv_label_set_text_static(label_unit, unit);
lv_obj_add_style(label_unit, &c->fonts[FONT_LABEL_SM], 0);
lv_obj_set_style_text_color(label_unit, lv_color_white(), 0);
lv_obj_set_style_text_opa(label_unit, LV_OPA_60, 0);

return label_number;
}

static void charging_status_box_clicked_cb(lv_event_t * e)
{
}

static void widget5_crash_btn_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        printf("CALLED THIS\n");
        if (inmate_started == 0)
            return;
        lv_subject_set_int(&global_api->subjects.cpu_usage_sub, 0);
        lv_subject_set_int(&global_api->subjects.ddr_usage_sub, 0);
        system("timeout 3 echo \"\" > /root/.ssh/known_hosts");
        system("timeout 3 ssh -y root@192.168.0.3 \"echo c > /proc/sysrq-trigger\"");
        inmate_started = 0;
//        run_server();
    }
}

static void create_widget5(lv_demo_high_res_ctx_t * c, lv_obj_t * widgets)
{
    lv_obj_t * widget = lv_obj_create(widgets);
    lv_obj_remove_style_all(widget);
    lv_obj_set_size(widget, c->imgs[IMG_LIGHT_WIDGET5_BG]->header.w, c->imgs[IMG_LIGHT_WIDGET5_BG]->header.h);
    lv_subject_add_observer_obj(&c->th, lv_demo_high_res_theme_observer_obj_bg_image_src_cb, widget,
                                &c->imgs[IMG_LIGHT_WIDGET5_BG]);
    lv_obj_set_style_pad_all(widget, c->sz->gap[7], 0);
    lv_obj_set_flex_flow(widget, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(widget, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t * title_label = lv_label_create(widget);
    lv_label_set_text_static(title_label, "Inmate");
    lv_obj_add_style(title_label, &c->fonts[FONT_LABEL_MD], 0);
    lv_obj_add_style(title_label, &c->styles[STYLE_COLOR_BASE][STYLE_TYPE_TEXT], 0);

    lv_obj_t * cluster_1 = lv_obj_create(widget);
    lv_obj_remove_style_all(cluster_1);
    lv_obj_set_size(cluster_1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cluster_1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cluster_1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cluster_1, 0, 0);

    lv_obj_t * start_btn = lv_button_create(cluster_1);
//    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(start_btn, LV_PCT(50), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(start_btn, NULL, LV_EVENT_ALL, NULL);
    lv_obj_t * start_btn_label = lv_label_create(start_btn);
    lv_label_set_text(start_btn_label, "Start");
    lv_obj_center(start_btn_label);

    lv_obj_t * crash_btn = lv_button_create(cluster_1);
//    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(crash_btn, LV_PCT(50), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(crash_btn, widget5_crash_btn_cb, LV_EVENT_ALL, NULL);
    lv_obj_t * crash_btn_label = lv_label_create(crash_btn);
    lv_label_set_text(crash_btn_label, "Crash");
    lv_obj_center(crash_btn_label);

    lv_obj_t * lightbulbs_box = create_widget5_lightbulbs(c, cluster_1);
    lv_obj_set_flex_grow(lightbulbs_box, 1);

    lv_obj_t * info_box = lv_demo_high_res_simple_container_create(widget, false, c->sz->gap[5], LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(info_box, LV_PCT(100));
    create_widget3_info(c, info_box, c->imgs[IMG_TIME_ICON], "CPU cores", "h", "1");
    create_widget3_info(c, info_box, c->imgs[IMG_ENERGY_ICON], "DDR memory", "MB", "1001364");

}

#endif /*LV_USE_DEMO_HIGH_RES*/
