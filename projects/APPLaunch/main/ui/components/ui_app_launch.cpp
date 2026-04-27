#include "../ui.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/wait.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/inotify.h>
#else
// macOS/Windows stubs
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <dirent.h>
#define IN_NONBLOCK    0
#define IN_CREATE      0x00000100
#define IN_DELETE      0x00000200
#define IN_MOVED_TO    0x00000080
#define IN_MOVED_FROM  0x00000040
#define IN_MODIFY      0x00000002
#define IN_CLOSE_WRITE 0x00000008
#define NAME_MAX       255
struct inotify_event { int wd; uint32_t mask; uint32_t cookie; uint32_t len; char name[]; };
static inline int inotify_init1(int) { return -1; }
static inline int inotify_add_watch(int, const char*, uint32_t) { return -1; }
#endif
#include <unordered_map>
#include <list>
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <fstream>
#include <sstream>
#include "ui_launch_page.hpp"
#include "ui_app_store.hpp"
#include "ui_app_music.hpp"
#include "ui_app_setup.hpp"
#include "ui_app_console.hpp"
#include "ui_app_IpPanel.hpp"
#include "ui_app_stock.hpp"

// ============================================================
// 启动快捷方式示例
// ============================================================
/*
root@pi:/home/pi# cat /usr/share/APPLaunch/applications/vim.desktop
[Desktop Entry]
Name=Vim
TryExec=vim
Exec=vim
Terminal=true
Icon=share/images/email.png
*/




// 前向声明
class app_launch_S;

// ============================================================
// 类型标签
// ============================================================
template <class PageT>
struct page_t
{
    using type = PageT;
};
template <class PageT>
inline constexpr page_t<PageT> page_v{};

// ============================================================
// app:统一的应用描述 + 发射器
// ============================================================
struct app
{
    std::string Name;
    std::string Icon;
    std::string Exec;
    std::function<void(app_launch_S *)> launch;

    // ① 外部命令
    app(std::string name,
        std::string icon,
        std::string exec,
        bool terminal);

    // ① 外部命令
    app(std::string name,
        std::string icon,
        std::string exec,
        bool terminal, bool sysplause);

    // ② 内置 UI 页面
    template <class PageT>
    app(std::string name,
        std::string icon,
        page_t<PageT> /*tag*/);
};

// ============================================================
// app_launch_S
// ============================================================
class app_launch_S
{
private:
    int current_app = 2;
    int inotify_fd  = -1; // inotify 实例句柄
    lv_timer_t *watch_timer = nullptr; // LVGL 3s 定时器

public:
    std::list<app> app_list;
    std::shared_ptr<void> app_Page;

public:
    app_launch_S()
    {
        // 固定图标，不允许用户修改
        app_list.emplace_back("Python",
                              "share/images/PYTHON_logo.png", "python3", true, false);
        app_list.emplace_back("STORE",
                              "share/images/Store_logo.png", page_v<UIStorePage>);
        app_list.emplace_back("CLI",
                              "share/images/CLI_logo.png", "bash", true, false);
        app_list.emplace_back("CLAW",
                              "share/images/CLAW_logo.png", "/home/pi/zeroclaw agent", true);
        app_list.emplace_back("SETTING",
                              "share/images/SETTING_logo.png", page_v<UISetupPage>);

        {
            auto it = std::next(app_list.begin(), 0);
            lv_label_set_text(ui_zuoLabelout, it->Name.c_str());
            lv_obj_set_style_bg_img_src(ui_outPanelzuo, it->Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            auto it = std::next(app_list.begin(), 1);
            lv_label_set_text(ui_zuoLabel, it->Name.c_str());
            lv_obj_set_style_bg_img_src(ui_zuoPanel, it->Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            auto it = std::next(app_list.begin(), 2);
            lv_label_set_text(ui_switchLabel, it->Name.c_str());
            lv_obj_set_style_bg_img_src(ui_switchPanel, it->Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            auto it = std::next(app_list.begin(), 3);
            lv_label_set_text(ui_youLabel, it->Name.c_str());
            lv_obj_set_style_bg_img_src(ui_youPanel, it->Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            auto it = std::next(app_list.begin(), 4);
            lv_label_set_text(ui_youLabelout, it->Name.c_str());
            lv_obj_set_style_bg_img_src(ui_outPanelyou, it->Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }


        // 动态图标，允许用户自定义
        app_list.emplace_back("MUSIC",
                              "share/images/MUSIC_logo.png", page_v<UIMusicPage>);
        app_list.emplace_back("AUDIO_PLAYER",
                              "share/images/MUSIC_logo.png",
                              "tinyplay -D1 -d0 /home/pi/zhou.wav",
                              true);
        app_list.emplace_back("IP_PANEL",
                              "share/images/ssh.png", page_v<UIIpPanelPage>);

        app_list.emplace_back("MATH",
                              "share/images/math.png", 
                              "/home/pi/M5CardputerZero-Calculator-linux-aarch64", false);


        app_list.emplace_back("STOCKS",
                              "share/images/stocks_macos_bigsur_icon_189691.png", page_v<UIStockPage>);


        applications_load();

        // 初始化 inotify，监听 applications 目录
        inotify_init_watch();

        // 创建 LVGL 3s 定时器，周期性检查目录变化
        watch_timer = lv_timer_create(app_dir_watch_cb, 3000, this);
    }

    void launch_app()
    {
        auto it = std::next(app_list.begin(), current_app);
        it->launch(this);
    }

    static void lv_go_back_home(void *arg)
    {
        auto self = (app_launch_S *)arg;
        lv_timer_enable(true);
        lv_indev_set_group(lv_indev_get_next(NULL), Screen1group);
        lv_disp_load_scr(ui_Screen1);
        lv_refr_now(NULL);
        if (self->app_Page)
            self->app_Page.reset();
    }

    void go_back_home()
    {
        lv_async_call(lv_go_back_home, this);
    }

    // 改为接收 std::string，不再依赖 app::Exec 成员
    void launch_Exec_in_terminal(const std::string &exec, bool sysplause = true)
    {
        printf("Launching terminal app: %s\n", exec.c_str());
        auto p = std::make_shared<UIConsolePage>();
        app_Page = p;
        lv_disp_load_scr(p->get_ui());
        lv_indev_set_group(lv_indev_get_next(NULL), p->get_key_group());
        p->go_back_home = std::bind(&app_launch_S::go_back_home, this);
        p->terminal_sysplause = sysplause;
        p->exec(exec);
    }

    void launch_Exec(const std::string &exec)
    {
        printf("Launching external app: %s\n", exec.c_str());
        lv_disp_t *disp = lv_disp_get_default();
        lv_indev_t *indev = lv_indev_get_next(NULL);
        LVGL_RUN_FLAGE = 0;
        if (indev)
            lv_indev_set_group(indev, NULL);
        lv_timer_enable(false);
        lv_refr_now(disp);

        pid_t pid = fork();
        if (pid == 0)
        {
            execlp(exec.c_str(), exec.c_str(), (char *)NULL);
            perror("execlp failed");
            _exit(EXIT_FAILURE);
        }
        else if (pid > 0)
        {
            pid_t pid_ret;
            int status;
            int end_status = 0;
            std::chrono::time_point<std::chrono::steady_clock> start_time;
            std::chrono::time_point<std::chrono::steady_clock> end_time;
            int ctrl_c_count = 0;
            for(;;)
            {
                if(end_status == 0)
                {
                    pid_ret = waitpid(pid, &status, WNOHANG);
                    if (pid_ret > 0)
                        break;
                    usleep(100000); // 100ms
                    if(LVGL_HOME_KEY_FLAGE)
                    {
                        end_status = 1;
                        start_time = std::chrono::steady_clock::now();
                    }
                }
                if(end_status == 1)
                {
                    if(LVGL_HOME_KEY_FLAGE)
                    {
                        end_time = std::chrono::steady_clock::now();
                        if(std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count() >= 5)
                        {
                            // kill(pid, SIGINT);
                            end_status = 2;
                        }
                    }
                    else
                    {
                        end_status = 0;
                    }
                }
                if(end_status == 2)
                {
                    ctrl_c_count ++;
                    kill(pid, SIGINT);
                    usleep(100000); // 100ms
                    pid_ret = waitpid(pid, &status, WNOHANG);
                    if (pid_ret > 0)
                        break;
                    if(ctrl_c_count >= 30)
                    {
                        // kill(pid, SIGKILL);
                        end_status = 3;
                        ctrl_c_count = 0 ;
                    }
                }
                if(end_status == 3)
                {
                    ctrl_c_count ++;
                    kill(pid, SIGKILL);
                    usleep(100000); // 100ms
                    pid_ret = waitpid(pid, &status, WNOHANG);
                    if (pid_ret > 0)
                        break;
                    if (pid_ret < 0)
                        break;
                    if(ctrl_c_count >= 300)
                    {
                        break;
                    }
                }
            }
            
            // waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                printf("App %s exited normally, code=%d\n", exec.c_str(), WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("App %s killed by signal %d\n", exec.c_str(), WTERMSIG(status));
            }
            lv_timer_enable(true);
            if (indev)
                lv_indev_set_group(indev, Screen1group);
            lv_disp_load_scr(ui_Screen1);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(disp);
        }
        else
        {
            perror("fork failed");
            lv_timer_enable(true);
            if (indev)
                lv_indev_set_group(indev, lv_group_get_default());
        }
        LVGL_RUN_FLAGE = 1;
    }


    void zuo(lv_obj_t *panel, lv_obj_t *label)
    {
        current_app = current_app == (int)app_list.size() - 1 ? 0 : current_app + 1;
        int next_app = current_app;
        next_app = next_app == (int)app_list.size() - 1 ? 0 : next_app + 1;
        next_app = next_app == (int)app_list.size() - 1 ? 0 : next_app + 1;
        auto it = std::next(app_list.begin(), next_app);
        lv_label_set_text(label, it->Name.c_str());
        lv_obj_set_style_bg_img_src(panel, it->Icon.c_str(),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    void you(lv_obj_t *panel, lv_obj_t *label)
    {
        current_app = current_app == 0 ? (int)app_list.size() - 1 : current_app - 1;
        int next_app = current_app;
        next_app = next_app == 0 ? (int)app_list.size() - 1 : next_app - 1;
        next_app = next_app == 0 ? (int)app_list.size() - 1 : next_app - 1;
        auto it = std::next(app_list.begin(), next_app);
        lv_label_set_text(label, it->Name.c_str());
        lv_obj_set_style_bg_img_src(panel, it->Icon.c_str(),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    void applications_load()
    {
        const char *app_dir = "/usr/share/APPLaunch/applications";
        DIR *dir = opendir(app_dir);
        if (!dir)
        {
            perror("applications_load: opendir failed");
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // 仅处理 *.desktop 文件
            const char *name = entry->d_name;
            size_t len = strlen(name);
            if (len <= 8 || strcmp(name + len - 8, ".desktop") != 0)
                continue;

            std::string filepath = std::string(app_dir) + "/" + name;
            std::ifstream ifs(filepath);
            if (!ifs.is_open())
            {
                fprintf(stderr, "applications_load: cannot open %s\n", filepath.c_str());
                continue;
            }

            // 解析 INI 文件
            std::string app_name, app_icon, app_exec;
            bool app_terminal = false;
            bool app_sysplause = true;
            bool in_desktop_entry = false;

            std::string line;
            while (std::getline(ifs, line))
            {
                // 去除行尾的 \r（Windows 换行符）
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                // 跳过空行和注释
                if (line.empty() || line[0] == '#' || line[0] == ';')
                    continue;

                // 检测节头
                if (line[0] == '[')
                {
                    in_desktop_entry = (line == "[Desktop Entry]");
                    continue;
                }

                if (!in_desktop_entry)
                    continue;

                // 解析 key=value
                auto eq = line.find('=');
                if (eq == std::string::npos)
                    continue;

                std::string key   = line.substr(0, eq);
                std::string value = line.substr(eq + 1);

                // 去除 key 首尾空格
                auto ltrim = [](std::string &s) {
                    size_t i = 0;
                    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
                    s = s.substr(i);
                };
                auto rtrim = [](std::string &s) {
                    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                        s.pop_back();
                };
                ltrim(key); rtrim(key);
                ltrim(value); rtrim(value);

                if (key == "Name")
                    app_name = value;
                else if (key == "Icon")
                    app_icon = value;
                else if (key == "Exec")
                    app_exec = value;
                else if (key == "Terminal")
                    app_terminal = (value == "true" || value == "True" || value == "1");
                else if (key == "Sysplause")
                    app_sysplause = (value == "true" || value == "True" || value == "1");
            }

            // 必须有 Name 和 Exec 才能注册
            if (app_name.empty() || app_exec.empty())
            {
                fprintf(stderr, "applications_load: skip %s (missing Name or Exec)\n", filepath.c_str());
                continue;
            }
            bool in_list = false;
            for(auto it: app_list)
            {
                if(it.Exec == app_exec)
                {
                    in_list = true;
                    break;
                }
            }
            if(in_list)
            {
                fprintf(stderr, "applications_load: skip %s (duplicate Exec)\n", filepath.c_str());
                continue;
            }
                
            app_list.emplace_back(app_name, app_icon, app_exec, app_terminal, app_sysplause);
        }

        closedir(dir);
    }

    // ============================================================
    // inotify 初始化：以非阻塞模式监听 applications 目录
    // ============================================================
    void inotify_init_watch()
    {
        const char *app_dir = "/usr/share/APPLaunch/applications";
        inotify_fd = inotify_init1(IN_NONBLOCK);
        if (inotify_fd < 0)
        {
            perror("inotify_init1 failed");
            return;
        }
        if (inotify_add_watch(inotify_fd, app_dir,
                              IN_CREATE | IN_DELETE | IN_MODIFY |
                              IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE) < 0)
        {
            perror("inotify_add_watch failed");
            close(inotify_fd);
            inotify_fd = -1;
        }
    }

    // ============================================================
    // 刷新 UI 面板（根据当前 current_app 更新 5 个槽位）
    // ============================================================
    void refresh_ui_panels()
    {
        int sz = (int)app_list.size();
        if (sz == 0) return;

        // 确保 current_app 在合法范围内
        if (current_app >= sz)
            current_app = sz - 1;

        auto app_at = [&](int idx) -> app & {
            idx = ((idx % sz) + sz) % sz;
            return *std::next(app_list.begin(), idx);
        };

        // 最左外（隐藏）
        {
            auto &a = app_at(current_app - 2);
            lv_label_set_text(ui_zuoLabelout, a.Name.c_str());
            lv_obj_set_style_bg_img_src(ui_outPanelzuo, a.Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // 左
        {
            auto &a = app_at(current_app - 1);
            lv_label_set_text(ui_zuoLabel, a.Name.c_str());
            lv_obj_set_style_bg_img_src(ui_zuoPanel, a.Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // 中心
        {
            auto &a = app_at(current_app);
            lv_label_set_text(ui_switchLabel, a.Name.c_str());
            lv_obj_set_style_bg_img_src(ui_switchPanel, a.Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // 右
        {
            auto &a = app_at(current_app + 1);
            lv_label_set_text(ui_youLabel, a.Name.c_str());
            lv_obj_set_style_bg_img_src(ui_youPanel, a.Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // 最右外（隐藏）
        {
            auto &a = app_at(current_app + 2);
            lv_label_set_text(ui_youLabelout, a.Name.c_str());
            lv_obj_set_style_bg_img_src(ui_outPanelyou, a.Icon.c_str(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // ============================================================
    // 重新加载动态应用列表（保留固定条目，重扫描 applications 目录）
    // ============================================================
    void applications_reload()
    {
        // 固定条目数量（Python/STORE/CLI/CLAW/SETTING/MUSIC/AUDIO_PLAYER/IP_PANEL/MATH/STOCKS）
        // 即构造函数中 applications_load() 之前加入的条目
        const int fixed_count = 10;
        int sz = (int)app_list.size();
        if (sz > fixed_count)
        {
            auto it = std::next(app_list.begin(), fixed_count);
            app_list.erase(it, app_list.end());
        }
        applications_load();
        refresh_ui_panels();
    }

    // ============================================================
    // LVGL 定时器回调：检测 inotify 事件，有变化则刷新列表
    // ============================================================
    static void app_dir_watch_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<app_launch_S *>(lv_timer_get_user_data(timer));
        if (!self || self->inotify_fd < 0)
            return;

        // 读取所有待处理事件（非阻塞）
        char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
        bool changed = false;
        for (;;)
        {
            ssize_t len = read(self->inotify_fd, buf, sizeof(buf));
            if (len <= 0)
                break; // 没有更多事件
            // 遍历本次读取到的所有事件
            for (char *ptr = buf; ptr < buf + len; )
            {
                struct inotify_event *ev = reinterpret_cast<struct inotify_event *>(ptr);
                // 只关心 .desktop 文件相关事件
                if (ev->len > 0)
                {
                    const char *fname = ev->name;
                    size_t flen = strlen(fname);
                    if (flen > 8 && strcmp(fname + flen - 8, ".desktop") == 0)
                        changed = true;
                }
                else
                {
                    // 目录级别的事件（无文件名）也视为变化
                    changed = true;
                }
                ptr += sizeof(struct inotify_event) + ev->len;
            }
        }

        if (changed)
        {
            printf("app_dir_watch_cb: applications dir changed, reloading...\n");
            self->applications_reload();
        }
    }

    ~app_launch_S();
};

// ============================================================
// app 构造函数的实现(放到 app_launch_S 定义之后)
// ============================================================
inline app::app(std::string name,
                std::string icon,
                std::string exec,
                bool terminal)
    : Name(std::move(name)), Icon(std::move(icon))
{
    launch = [exec = std::move(exec), terminal](app_launch_S *ctx)
    {
        if (terminal)
            ctx->launch_Exec_in_terminal(exec);
        else
            ctx->launch_Exec(exec);
    };
}

inline app::app(std::string name,
                std::string icon,
                std::string exec,
                bool terminal,
                bool sysplause)
    : Name(std::move(name)), Icon(std::move(icon))
{
    launch = [exec = std::move(exec), terminal, sysplause](app_launch_S *ctx)
    {
        if (terminal)
            ctx->launch_Exec_in_terminal(exec, sysplause);
        else
            ctx->launch_Exec(exec);
    };
}

template <class PageT>
app::app(std::string name,
         std::string icon,
         page_t<PageT> /*tag*/)
    : Name(std::move(name)), Icon(std::move(icon))
{
    launch = [](app_launch_S *self)
    {
        auto p = std::make_shared<PageT>();
        self->app_Page = p;
        lv_disp_load_scr(p->get_ui());
        lv_indev_set_group(lv_indev_get_next(NULL),
                           p->get_key_group());
        p->go_back_home =
            std::bind(&app_launch_S::go_back_home, self);
    };
}

// ============================================================
// app_launch_S 析构函数实现
// ============================================================
app_launch_S::~app_launch_S()
{
    if (watch_timer)
    {
        lv_timer_delete(watch_timer);
        watch_timer = nullptr;
    }
    if (inotify_fd >= 0)
    {
        close(inotify_fd);
        inotify_fd = -1;
    }
}

// ============================================================
std::unique_ptr<app_launch_S> app_launch_Ser;

extern "C"
{
    
    void ui_info_bind()
    {
        app_launch_Ser = std::make_unique<app_launch_S>();        
    }
    void cpp_app_zuo(lv_obj_t *panel, lv_obj_t *label)
    {
        app_launch_Ser->zuo(panel, label);
    }
    void cpp_app_you(lv_obj_t *panel, lv_obj_t *label)
    {
        app_launch_Ser->you(panel, label);
    }
    void cpp_app_launch()
    {
        app_launch_Ser->launch_app();
    }
}