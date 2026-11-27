# 模块接口与数据契约

> 文档编号：05
> 状态：与实现同步（2026-11-27）
> 说明：本文是“实现前合同”。代码中的公共头文件必须与本文一致；如实现需要调整，先改本文与头文件，再改调用方。

## 1. 约定

- 语言：C99；所有公共头文件可被 C99 编译器独立包含。
- 返回值：`0` 成功；负数错误码（优先使用 `errno` 语义，如 `-EINVAL`、`-ENOMEM`、`-EIO`、`-ETIMEDOUT`、`-ENOENT`）。
- 所有权：
  - `xxx_create/load/clone` 返回的指针由调用方释放（对应 `xxx_destroy/free`）。
  - `xxx_get` 返回借用指针，调用方不得释放，生命周期见注释。
- 线程安全：默认**非线程安全**，除非明确标注；所有 UI 对象只能在 LVGL 主线程访问。
- 字符串：固定长度字段以 `\0` 结尾；外部输入必须截断；路径使用 `PATH_MAX`。
- 时间：`time_t` 为 Unix 秒；展示层负责本地时区格式。

错误码统一定义在 `core/include/board_core/board_error.h`：

```c
#define BOARD_OK          0
#define BOARD_ERR_INVALID (-EINVAL)
#define BOARD_ERR_NOMEM   (-ENOMEM)
#define BOARD_ERR_IO      (-EIO)
#define BOARD_ERR_TIMEOUT (-ETIMEDOUT)
#define BOARD_ERR_NOENT   (-ENOENT)
#define BOARD_ERR_AGAIN   (-EAGAIN)
#define BOARD_ERR_PARSE   (-EBADMSG)
#define BOARD_ERR_FULL    (-ENOSPC)
```

## 2. 应用与导航（UI 层）

### 2.1 `board_ui/ui_app_manager.h`

```c
typedef enum {
    UI_ICON_GENERIC = 0,
    UI_ICON_WEATHER,
    UI_ICON_GALLERY,
    UI_ICON_GAMES,
    UI_ICON_GAME_2048,
    UI_ICON_GAME_SNAKE,
    UI_ICON_GAME_BREAKOUT,
} ui_icon_id_t;

typedef struct {
    const char *id;              /* 唯一 id，如 "weather"；重复 id 只保留第一个 */
    const char *title;           /* 顶部标题/首页卡片标题 */
    const char *subtitle;        /* 首页卡片副标题；可为 NULL */
    const char *parent_id;       /* NULL=顶层；"games"=游戏中心子项 */
    const char *best_score_key;  /* 可选：最高分 storage key；非游戏为 NULL */
    ui_icon_id_t icon;           /* 内置图形图标；未知用 UI_ICON_GENERIC */
    uint32_t accent;             /* 0xRRGGBB；0 表示使用默认主色 */
    int order;                   /* 排序，小的在前；同值按 id */
    bool show_on_home;           /* 仅顶层有效：是否生成首页卡片 */
    lv_obj_t *(*create)(lv_obj_t *parent); /* 构建页面，返回根对象；失败返回 NULL */
    void (*destroy)(void);       /* 释放非对象资源；可为 NULL */
} board_app_desc_t;

#define BOARD_APP_REG_CAT_(a, b) a##b
#define BOARD_APP_REG_CAT(a, b) BOARD_APP_REG_CAT_(a, b)
#define BOARD_APP_REGISTER(desc) \
    static const board_app_desc_t *const BOARD_APP_REG_CAT(board_app_reg_, __LINE__) \
        __attribute__((used, section("board_apps"))) = &(desc)

int ui_app_manager_init(lv_obj_t *screen);   /* 扫描链接段并排序 */
int ui_app_manager_register(const board_app_desc_t *app); /* 手动注册，测试/备用 */
size_t ui_app_manager_count(void);
const board_app_desc_t *ui_app_manager_at(size_t index);
const board_app_desc_t *ui_app_manager_find(const char *id);
int ui_app_manager_open(const char *id);      /* 从当前页进入；压栈 */
int ui_app_manager_open_root(const char *id); /* 清栈后进入（Home 用） */
int ui_app_manager_back(void);                /* 弹栈；栈空回 home */
const char *ui_app_manager_current(void);
void ui_app_manager_shutdown(void);
```

- **自动注册**：应用 UI 文件定义 `static const board_app_desc_t s_app = {...};`，文件末尾 `BOARD_APP_REGISTER(s_app);`。链接器把指针放进 `board_apps` 段；`init` 读取 `__start_board_apps`/`__stop_board_apps`。
- **首页**：遍历 `parent_id == NULL && show_on_home`，按 `order` 生成卡片；不需要改首页代码。
- **游戏中心**：遍历 `parent_id == "games"`，按 `order` 生成游戏卡片；不需要改游戏中心代码。
- `create` 只能创建对象，不得阻塞 > 16 ms；耗时初始化放到 `lv_timer` 或 worker。
- `destroy` 中必须删除该页面的 `lv_timer`、停止线程、释放图片缓冲；随后管理器删除根对象。
- 管理器保证同一时刻只有一个页面；`open` 在转场中返回 `BOARD_ERR_AGAIN`。
- Home 注册为 `id="home"`、`parent_id=NULL`、`show_on_home=false`，并作为栈底；`back()` 在 Home 时返回 0 但不动作。
- `ui_app_manager_register()` 仅用于主机测试/特殊场景；正常应用不使用，避免重复注册。

### 2.2 `board_ui/ui_theme.h`

```c
void ui_theme_init(const char *font_path);
const lv_font_t *ui_font_display(void);  /* 48 */
const lv_font_t *ui_font_h1(void);       /* 34 */
const lv_font_t *ui_font_h2(void);       /* 28 */
const lv_font_t *ui_font_h3(void);       /* 26 */
const lv_font_t *ui_font_body(void);     /* 22 */
const lv_font_t *ui_font_small(void);    /* 18 */
const lv_font_t *ui_font_caption(void);  /* 16 */
const lv_font_t *ui_font_number_large(void); /* Montserrat 48 */
const lv_font_t *ui_font_number(void);       /* Montserrat 28 */
```

颜色以 `#define UI_COLOR_*` 形式暴露（见 04）。`ui_theme_init(font_path)` 在 `lv_init()` 之后、创建任何控件之前调用；字体加载失败会回退 SimSun 16，重复调用幂等。

### 2.3 `board_ui/ui_widgets.h`

```c
lv_obj_t *ui_topbar_create(lv_obj_t *parent, const char *title,
                           lv_event_cb_t back_cb, void *user_data);
lv_obj_t *ui_topbar_add_action(lv_obj_t *topbar, const char *text,
                               lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_card_create(lv_obj_t *parent, int w, int h);
lv_obj_t *ui_button_primary_create(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_button_secondary_create(lv_obj_t *parent, const char *text,
                                     lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_icon_button_create(lv_obj_t *parent, const char *symbol,
                                lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_pill_create(lv_obj_t *parent, const char *text, lv_color_t fg,
                         lv_color_t bg);
lv_obj_t *ui_modal_create(lv_obj_t *parent, const char *title, int width);
void ui_modal_close(lv_obj_t *modal);
void ui_toast_show(const char *text);            /* 2 s 自动消失；同一时间一个 */
lv_obj_t *ui_loading_create(lv_obj_t *parent, const char *text);
void ui_empty_state_show(lv_obj_t *parent, const char *title, const char *detail);
```

- 所有组件使用主题令牌；调用方不直接写颜色/字号（特殊游戏棋盘除外）。
- `ui_modal_create` 返回内容容器；调用方添加正文/按钮，底部按钮区由组件提供。
- Toast 在屏幕层创建，不随页面销毁；页面销毁时若 Toast 仍显示，允许其自然消失。

### 2.4 `board_ui/ui_async.h`

用于把 worker 结果安全地送回 UI 线程。核心不依赖 LVGL；UI 用定时器轮询核心的线程安全队列。

```c
typedef void (*ui_async_handler_t)(void *item, void *user_data);

typedef struct ui_async_pump ui_async_pump_t;

ui_async_pump_t *ui_async_pump_create(board_async_queue_t *queue,
                                      ui_async_handler_t handler,
                                      void *user_data,
                                      uint32_t period_ms);
void ui_async_pump_destroy(ui_async_pump_t *pump);
```

- pump 内部创建一个 `lv_timer`，每 `period_ms`（默认 30）从队列 `pop` 所有元素，逐个调用 `handler(item, user_data)`。
- `handler` 在 UI 线程执行，负责消费并释放 `item`。
- `destroy` 删除定时器；队列由创建者销毁。销毁后不得再 pop。

### 2.4.1 `board_ui/ui_ime.h`（可复用输入法）

| API | 说明 |
|---|---|
| `ui_ime_t *ui_ime_create(lv_obj_t *parent, lv_obj_t *ta)` | 在页面上创建键盘并绑定 textarea |
| `ui_ime_set_geometry(ime, align, x, y, w, h)` | 摆放位置/尺寸（`LV_ALIGN_*`） |
| `ui_ime_set_ready_cb(ime, cb, user)` | 「送出」回调 |
| `ui_ime_insert(ime, text)` | 直接插入文本 |
| `ui_ime_clear_input(ime)` / `ui_ime_set_visible` / `ui_ime_set_chinese` | 清空/显隐/中英切换 |
| `ui_ime_get_keyboard(ime)` | 取底层 `lv_keyboard`（自定义样式用） |
| `ui_ime_preload(void)` | 启动时后台预读拼音词库（`board_app_init` 调用） |
| `ui_ime_dictionary_loaded(void)` | 词库是否就绪 |
| `ui_ime_load_quick_keys(ime, path)` | 从文件加载常用语 |
| `ui_ime_destroy(ime)` | 销毁 |

### 2.5 `board_app.h`（应用入口）

```c
int board_app_init(const char *argv0);
void board_app_deinit(void);
```

- `board_app_init`：初始化 paths/storage/theme、注册 Home/Weather/Gallery/Games、打开 Home。
- 由顶层 `main.c` 在显示/输入初始化后调用。
- `board_app_deinit` 仅用于测试/正常退出；嵌入式主循环通常不返回。

## 3. 核心公共服务

### 3.1 `core/include/board_core/board_paths.h`

```c
typedef struct {
    char root[PATH_MAX];
    char assets[PATH_MAX];
    char data[PATH_MAX];
    char photos[PATH_MAX];
    char edited[PATH_MAX];
    char cache[PATH_MAX];
    char font[PATH_MAX];
    char settings[PATH_MAX];
} board_paths_t;

int board_paths_init(board_paths_t *paths, const char *argv0);
const char *board_paths_asset(const board_paths_t *paths, const char *rel);
const char *board_paths_data(const board_paths_t *paths, const char *rel);
int board_paths_ensure_data_dirs(const board_paths_t *paths);
```

解析顺序：

1. `BOARD_APP_ROOT` 环境变量（绝对/相对均可，转为绝对路径）。
2. 当前工作目录存在 `assets/`。
3. `argv0` 所在目录存在 `assets/`。
4. `argv0` 所在目录的上一级存在 `assets/`。

`BOARD_APP_DATA_DIR` 可单独覆盖 `data/`；`BOARD_APP_PHOTO_DIR` 覆盖 `photos/`。`font` 固定为 `assets/fonts/NotoSansSC-Board.ttf`。

### 3.2 `board_core/board_time.h`

```c
typedef struct {
    int year, month, day;
    int hour, minute, second;
    int weekday; /* 0=周日, 1=周一 ... 6=周六 */
} board_datetime_t;

int board_time_now(board_datetime_t *out);
int board_time_format_hhmm(char *buf, size_t cap);
int board_time_format_date_cn(char *buf, size_t cap);  /* "11月25日 星期三" */
int board_time_format_ymd(char *buf, size_t cap);      /* "2026-11-25" */
int board_time_weekday_cn(int weekday, char *buf, size_t cap);
int64_t board_time_now_ms(void);
```

线程安全；内部使用 `localtime_r`。

### 3.3 `board_core/board_log.h`

```c
typedef enum {
    BOARD_LOG_ERROR = 0,
    BOARD_LOG_WARN  = 1,
    BOARD_LOG_INFO  = 2,
    BOARD_LOG_DEBUG = 3,
} board_log_level_t;

void board_log_set_level(board_log_level_t level);
void board_log(board_log_level_t level, const char *module, const char *fmt, ...);
#define BOARD_LOGE(mod, ...) board_log(BOARD_LOG_ERROR, mod, __VA_ARGS__)
#define BOARD_LOGW(mod, ...) board_log(BOARD_LOG_WARN,  mod, __VA_ARGS__)
#define BOARD_LOGI(mod, ...) board_log(BOARD_LOG_INFO,  mod, __VA_ARGS__)
#define BOARD_LOGD(mod, ...) board_log(BOARD_LOG_DEBUG, mod, __VA_ARGS__)
```

- 默认级别 `INFO`；`BOARD_APP_LOG_LEVEL=debug|info|warn|error` 可覆盖。
- 输出到 `stderr`，格式 `[HH:MM:SS.mmm] [I] [weather] msg`。
- 不在日志中输出用户图片内容、完整文件内容、未来可能的 API Key。

### 3.4 `board_core/board_storage.h`

```c
int board_storage_init(const char *settings_path);
void board_storage_shutdown(void);

const char *board_storage_get_str(const char *key, const char *default_value);
int board_storage_set_str(const char *key, const char *value);
int board_storage_get_int(const char *key, int default_value);
int board_storage_set_int(const char *key, int value);
int board_storage_save(void);
```

- 线程安全（内部 `pthread_mutex_t`）。
- 启动时读取 `key=value` 文本，忽略空行/`#` 注释；值可包含 `=`，按第一个 `=` 分割。
- **`board_storage_get_str` 返回线程内共享缓冲区**；拿到后若要再取另一个 key，必须先
  `snprintf` 复制，否则第二个值会覆盖第一个指针的内容（天气城市 code 曾因此变成城市名）。
- `set_*` 只改内存；`save` 原子写（`.tmp` + `fsync` + `rename`）。UI 在关键操作后调用 `save`。
- 键名约定：
  - `weather.city_code`（默认 `101010100`）
  - `weather.city_name`（默认 `北京`）
  - `game.2048.best`、`game.snake.best`、`game.breakout.best`
  - `ui.reduce_motion`（0/1）
- 最大键/值长度：键 64，值 512；超长返回 `BOARD_ERR_INVALID`。

### 3.5 `board_core/http_client.h`

```c
typedef struct {
    char *body;        /* NUL 结尾；调用方 http_response_free */
    size_t len;
    int status_code;   /* HTTP 状态码，如 200 */
} http_response_t;

int http_get(const char *url, int timeout_ms, http_response_t *resp);
void http_response_free(http_response_t *resp);
```

- 仅支持 `http://`；`https://` 返回 `BOARD_ERR_INVALID`。
- 支持 301/302/303/307/308 重定向，最多 3 次；只跟随 `http://`。
- 支持 `Content-Length` 与 `Transfer-Encoding: chunked`；响应上限 256 KB。
- 连接超时、读取超时均为 `timeout_ms`；使用非阻塞 connect + `poll/select`，每 200 ms 检查取消（通过 `http_client_cancel_all()` 或句柄）。
- 线程安全：每次调用独立 socket，无全局可变状态（取消标志用原子/互斥）。
- 请求头：`Host`、`User-Agent: board-app/1.0`、`Accept: application/json`、`Connection: close`。
- 不发送 `Accept-Encoding`，避免 gzip 解压依赖。

**取消扩展**（实现阶段采用）：

```c
typedef struct http_request http_request_t;
http_request_t *http_get_async(const char *url, int timeout_ms);
int http_request_wait(http_request_t *req, http_response_t *resp);
void http_request_cancel(http_request_t *req); /* 线程安全，可从其他线程调用 */
void http_request_free(http_request_t *req);
```

天气服务使用 `http_request_t` 以便页面退出时 `cancel`，socket 轮询在 200 ms 内退出。

### 3.6 `board_core/board_async_queue.h`

```c
typedef struct board_async_queue board_async_queue_t;

board_async_queue_t *board_async_queue_create(size_t capacity);
void board_async_queue_destroy(board_async_queue_t *q);
int board_async_queue_push(board_async_queue_t *q, void *item); /* 满则 BOARD_ERR_FULL */
void *board_async_queue_pop(board_async_queue_t *q);            /* 空则 NULL */
size_t board_async_queue_count(board_async_queue_t *q);
```

- 线程安全，固定容量（建议 16），存放 `void*`；队列不负责释放 item，由消费者释放。
- 用于 worker → UI 的结果投递；UI 的 `ui_async_pump` 每 30 ms 排空。

## 4. 天气核心

### 4.1 `app/weather/core/include/weather/weather_types.h`

```c
#define WEATHER_MAX_DAYS 7
#define WEATHER_CITY_MAX 32
#define WEATHER_TEXT_MAX 32

typedef struct {
    char ymd[12];        /* 2026-11-25 */
    char week[20];       /* 星期三 */
    char type[WEATHER_TEXT_MAX]; /* 晴/多云/小雨... */
    int high_c;
    int low_c;
    int aqi;
    char wind_dir[16];
    char wind_level[16];
} weather_day_t;

typedef struct {
    char city[WEATHER_CITY_MAX];
    char city_code[16];
    char updated[32];     /* 接口 updateTime 或本地时间 */
    int temp_c;
    int humidity;         /* % */
    char condition[WEATHER_TEXT_MAX];
    char quality[16];     /* 优/良/... */
    int aqi;
    int pm25;
    char wind_dir[16];
    char wind_level[16];
    int high_c;
    int low_c;
    bool has_humidity;    /* false 时 UI 显示 "--" */
    bool has_aqi;
    bool has_pm25;
    bool has_wind;
    bool has_high_low;
    weather_day_t days[WEATHER_MAX_DAYS];
    int day_count;
    bool offline;         /* 来自缓存 */
    bool demo;            /* 演示数据 */
    time_t fetched_at;    /* 缓存写入/接口时间 */
} weather_data_t;
```

- `day_count` 0..7；不足 7 天时 UI 只显示已有行。
- `temp_c` 允许负值；`humidity` 为 0..100；未知字段用对应的 `has_*` 标志为 false，UI 显示 `--`，不要用 0 冒充真实值。

### 4.2 `weather_parse.h`

```c
int weather_parse_json(const char *json, size_t len, weather_data_t *out);
```

- 输入为 wttr.in / worldweatheronline JSON；同时兼容 itboy 格式（旧缓存）。
  关键字段（`current_condition`、`weather`）缺失返回 `BOARD_ERR_PARSE`。
- 温度提取：从 `"高温 9℃"`/`"低温 -4℃"`/`"9℃"` 中解析第一个带符号整数；`℃`/`°C` 均可。
- 最多解析 7 天；超出忽略。
- 不修改输入；`out` 先清零再填充。
- 线程安全（纯函数）。

### 4.3 `weather_cache.h`

```c
int weather_cache_load(const board_paths_t *paths, const char *city_code,
                       weather_data_t *out);
int weather_cache_age(const board_paths_t *paths, const char *city_code,
                      int64_t *age_seconds);
int weather_cache_save(const board_paths_t *paths, const char *city_code,
                       const char *raw_json, size_t len);
int weather_cache_clear(const board_paths_t *paths, const char *city_code);
```

- 缓存路径：`data/cache/weather_<city_code>.json`；保存原始 JSON，读取时再解析。
- `fetched_at` 取文件 `st_mtime`；`offline=true`；`weather_cache_age` 返回缓存秒数。
- **缓存有效期 1 小时**：UI 只立即展示 age ≤ 3600 的缓存，过期缓存不冒充当前天气；
  后台仍会请求新数据，失败时 worker 可用旧缓存兜底并标“离线数据”。
- 写入使用 `.tmp` + `rename`，失败返回 `BOARD_ERR_IO`。

### 4.4 `weather_service.h`

```c
typedef struct weather_service weather_service_t;

typedef enum {
    WEATHER_EVT_OK = 0,
    WEATHER_EVT_NETWORK_ERROR,
    WEATHER_EVT_PARSE_ERROR,
    WEATHER_EVT_CANCELLED,
} weather_event_status_t;

typedef struct {
    weather_event_status_t status;
    weather_data_t data;   /* status==OK 时有效 */
    bool from_cache;
    char message[128];
} weather_result_t;

weather_service_t *weather_service_create(const board_paths_t *paths,
                                          board_async_queue_t *queue);
void weather_service_destroy(weather_service_t *svc);

/* 发起一次请求；若已有请求在跑，返回 BOARD_ERR_AGAIN。 */
int weather_service_refresh(weather_service_t *svc,
                            const char *city_code,
                            const char *city_name);

/* 取消并等待线程退出；最多等待 ~1.5 s。 */
void weather_service_cancel(weather_service_t *svc);

const weather_data_t *weather_service_last(weather_service_t *svc); /* 借用 */
```

- `weather_service_refresh` 内部创建 detach 还是 join 线程？采用**单 worker + joinable**：服务持有一个 `pthread_t`，`refresh` 时若空闲则创建；worker 结束前把结果 push 到队列。`destroy/cancel` 置取消位并 `join`。
- 结果 `weather_result_t*` 由 worker `malloc`，push 到队列；UI pump 消费后 `free`。
- 服务负责写缓存（仅真实接口数据，不写 mock）。
- 环境变量 `BOARD_APP_WEATHER_URL` 可覆盖 base URL（默认 `http://wttr.in/`）；用于内网镜像/测试。
- 线程安全：`refresh/cancel/last` 内部加锁；`last` 返回的指针在下次 refresh 前有效。

### 4.5 城市列表

```c
typedef struct { const char *code; const char *name; } weather_city_t;
const weather_city_t *weather_cities(size_t *count);
const weather_city_t *weather_city_find(const char *code);
```

内置至少 8 个城市：北京 `101010100`、上海 `101020100`、广州 `101280101`、深圳 `101280601`、成都 `101270101`、杭州 `101210101`、武汉 `101200101`、西安 `101110101`。

### 4.6 演示数据

```c
void weather_mock_fill(weather_data_t *out);
```

- 固定城市“北京”、当前 `6°` 晴、7 天示例；`demo=true`、`offline=true`。
- 仅在没有缓存且网络失败时使用；UI 必须显示“演示数据”。

## 5. 相册核心

### 5.1 `image.h`

```c
typedef struct {
    int w;
    int h;
    int stride;       /* 字节，通常 w*4 */
    uint8_t *rgba;    /* w*h*4, top-down, R,G,B,A */
} image_t;

image_t *image_create(int w, int h);
image_t *image_load(const char *path);          /* PNG/JPEG/BMP via stb */
image_t *image_clone(const image_t *src);
void image_free(image_t *img);
int image_save_png(const image_t *img, const char *path);

/* 等比缩放到不超过 max_w x max_h；不放大。返回新图。 */
image_t *image_resize_fit(const image_t *src, int max_w, int max_h);

/* 转换到 LVGL ARGB8888 字节序（B,G,R,A），供 ui 层包装 lv_image_dsc_t。
 * dst_size 必须 >= w*h*4。core 不包含 LVGL。 */
int image_copy_to_bgra(const image_t *img, uint8_t *dst, size_t dst_size);
```

- 解码失败返回 `NULL`；`image_load` 内部把 EXIF 方向忽略（本期）。
- `image_create` 清零；`image_free(NULL)` 安全。
- `image_save_png` 自动创建父目录？**不**；由 gallery core 确保目录存在。
- 线程安全：不同 `image_t` 可并行；同一对象不加锁，调用方保证串行。

### 5.2 `image_ops.h`

```c
int image_rotate90(image_t **img, bool clockwise); /* 可能重新分配，失败不改 */
int image_flip(image_t *img, bool horizontal);
int image_grayscale(image_t *img);
int image_adjust(image_t *img, int brightness, int contrast); /* -100..100 */
int image_crop(image_t **img, int x, int y, int w, int h);
```

- 参数越界返回 `BOARD_ERR_INVALID`；成功返回 0。
- 旋转/裁剪会重新分配，失败时保留原指针。
- 亮度：`out = clamp(in + brightness * 255 / 100)`；对比度：围绕 128 缩放，`factor = (259*(c+255))/(255*(259-c))`（c 为 -100..100 映射到 -255..255），clamp 0..255。
- 灰度：`Y = 0.299R + 0.587G + 0.114B`，写回 R=G=B=Y，A 不变。
- 裁剪坐标相对当前图；宽高至少 1；结果尺寸 = w×h。

### 5.3 `image_history.h`

```c
typedef struct image_history image_history_t;

image_history_t *image_history_create(const image_t *initial);
int image_history_push(image_history_t *h, const image_t *current); /* 克隆当前 */
image_t *image_history_undo(image_history_t *h);              /* caller owns */
int image_history_can_undo(const image_history_t *h);
void image_history_reset(image_history_t *h, const image_t *initial);
void image_history_destroy(image_history_t *h);
```

- 历史保存的是**操作后**的状态（初始状态由 `create(initial)` 放入）。每次成功编辑后 `push(current)`。
- 最多保存 4 个状态；超出丢弃最旧，内存有界。
- `undo` 丢弃当前状态，返回**上一个状态的克隆**；调用方拥有返回值，负责 `image_free`。
- 历史内部拥有其余 `image_t`，`destroy` 全部释放。
- `can_undo` 仅在历史中至少有两个状态时为真。

### 5.4 `gallery_scan.h`

```c
#define GALLERY_MAX_ITEMS 128
#define GALLERY_NAME_MAX 128

typedef struct {
    char path[PATH_MAX];
    char name[GALLERY_NAME_MAX];
} gallery_item_t;

int gallery_scan(const char *dir, gallery_item_t *items, int capacity);
bool gallery_is_supported(const char *filename);
```

- 返回图片数量（≥0）或负错误码；目录不存在返回 `BOARD_ERR_NOENT`。
- 支持扩展名（大小写不敏感）：`.png`、`.jpg`、`.jpeg`、`.bmp`。
- 只扫描一层，不递归；按文件名 `strcasecmp` 排序。
- 忽略隐藏文件（`.` 开头）与超过容量的条目（返回容量并记录警告）。
- `path` 为完整路径；`name` 为 basename。

### 5.5 `gallery_store.h`

```c
int gallery_store_init(const board_paths_t *paths);
int gallery_store_list(gallery_item_t *items, int capacity);
int gallery_store_save_edited(const image_t *img, char *out_path, size_t cap);
int gallery_store_save_near(const image_t *img, const char *source_path,
                            char *out_path, size_t cap);
int gallery_store_overwrite_edited(const image_t *img, const char *path);
int gallery_store_delete_edited(const char *path);
```

- `gallery_store_init` 复制路径并尽力创建 `data/edited`；即使根文件系统只读，浏览仍可用。
- `gallery_store_save_edited` 生成 `data/edited/edited_YYYYmmdd_HHMMSS.png`，保留给旧调用。
- `gallery_store_save_near` 在**源图片所在目录**生成
  `<原名>_edited_YYYYmmdd_HHMMSS.png`，冲突追加 `_01`…`_99`；这是 UI“另存为/原图保存”使用的路径。
- `gallery_store_overwrite_edited` 只覆盖可删除图片（`data/edited` 或 `assets/photos` 下的支持格式）；
  实际 UI 只在 `_edited_` 副本上走覆盖路径，普通原图另存为新副本。
- `gallery_store_can_delete` 判断路径是否为支持格式的可删除图片；
  `gallery_store_delete_edited` 删除后返回 0/−EACCES/−EIO。

## 6. 游戏核心（第三方）

### 6.1 上游接口与放置

直接使用 Little World Arcade 的 `arcade_core`：

- `arcade_common.h`：`arcade_dir_t`、`arcade_state_t`、`arcade_rng_t`。
- `arcade_2048.h`：`arcade_2048_t`、`arcade_2048_init/move/can_move/max_tile`。
- `arcade_snake.h`：`arcade_snake_t`、`arcade_snake_init/turn/step/step_ms/place_food`。
- `arcade_breakout.h`：`arcade_breakout_t`、`arcade_breakout_init/launch/move_paddle/tick/brick_rect`。

文件放置（避免 `arcade_rng.c` 重复链接）：

```text
app/games/core/src/arcade_rng.c                 # 共享，只编译一次
app/games/core/include/arcade/arcade_common.h   # 共享头
app/games/core/include/arcade/arcade_*.h
app/games/g2048/core/src/arcade_2048.c          # 各游戏规则
app/games/snake/core/src/arcade_snake.c
app/games/breakout/core/src/arcade_breakout.c
```

**不得修改上游算法**。如需适配（例如 UI 只需要只读字段），在 UI 层做，不改核心；确需修改时在 `third_party/littleworld_arcade/UPSTREAM.md` 记录补丁与理由，并保持测试通过。

### 6.2 UI 适配与自动注册

每个游戏 UI 定义描述符并注册，**不写中心 getter**：

```c
static const board_app_desc_t s_game2048 = {
    .id = "g2048",
    .title = "2048",
    .subtitle = "合并数字，挑战 2048",
    .parent_id = "games",
    .best_score_key = "game.2048.best",
    .icon = UI_ICON_GAME_2048,
    .accent = 0xE8A33D,
    .order = 10,
    .show_on_home = false,
    .create = game2048_create,
    .destroy = game2048_destroy,
};
BOARD_APP_REGISTER(s_game2048);
```

游戏中心本身：

```c
static const board_app_desc_t s_games = {
    .id = "games", .title = "小游戏", .subtitle = "2048 · 贪吃蛇 · 打砖块",
    .parent_id = NULL, .show_on_home = true, .order = 30,
    .icon = UI_ICON_GAMES, .accent = 0xFF8A3D,
    .create = games_center_create, .destroy = games_center_destroy,
};
BOARD_APP_REGISTER(s_games);
```

- 游戏中心 `create` 遍历 `ui_app_manager_at()`，筛选 `parent_id == "games"`，自动生成卡片；新增游戏不需要改游戏中心。
- `create(parent)` 中初始化核心状态、构建控件、启动定时器（如需要）。
- `destroy()` 中删除定时器、保存最高分、释放图像/临时对象。
- UI 负责：
  - 2048：手势方向 → `arcade_2048_move`；结果 `merged_mask/spawn_index` → 颜色/缩放动效。
  - 贪吃蛇：方向键/滑动 → `arcade_snake_turn`；定时器 → `arcade_snake_step`；`step_ms` → `lv_timer_set_period`。
  - 打砖块：拖动坐标 → `arcade_breakout_move_paddle`（固定点：`x * 256`）；定时器 16ms → `arcade_breakout_tick`；点击 → `arcade_breakout_launch`。
- 最高分键来自描述符 `best_score_key`：`game.2048.best`、`game.snake.best`、`game.breakout.best`；仅当新分数更高时写入并 `board_storage_save()`。

### 6.3 围棋规则核心 `weiqi/weiqi.h`

> 移植自 [cjlarose/weiqi.js](https://github.com/cjlarose/weiqi.js)（ISC），
> 上游原文与许可在 `third_party/weiqi-js/`。**服务器与客户端共用**同一份实现。

| API | 说明 |
|---|---|
| `int weiqi_init(weiqi_t *g, int size)` | 9/13/19 路，黑先行 |
| `int weiqi_play(weiqi_t *g, int x, int y)` | 落子（含提子/自杀/位置超劫）；返回 `WEIQI_OK` 或负错误码 |
| `int weiqi_pass(weiqi_t *g)` | 停一手；连续两次 → 终局并数子 |
| `void weiqi_resign(weiqi_t *g, int color)` | 认输 |
| `int weiqi_score(weiqi_t *g, double *b, double *w)` | 中国规则数子 + 贴目 3.5（黑贴 7 目半） |
| `void weiqi_area_score(...)` | 只算子/地（不含贴目） |
| `void weiqi_board_string(g, out, cap)` | 棋盘转文本 `B/W/.`（`size*size` 字节，网络同步用） |
| `int weiqi_board_parse / weiqi_set_state(...)` | 复刻服务器局面（不做规则校验） |

错误码**故意避开 errno 区间**：`-101` 非法落子、`-102` 自杀、`-103` 劫、`-104` 非本方回合
（早期和 `-ENOENT(-2)` 撞码，导致“没有对局”显示成“禁着点”）。

### 6.4 通信模块 `comm/*.h`

| API | 说明 |
|---|---|
| `comm_frame_pack / comm_frame_peek` | TCP 分帧：`len:4` + `type:1` + JSON |
| `comm_name_valid / comm_text_valid` | 用户名/消息文本校验 |
| `comm_tcp_listen/connect`、`comm_send_all/recv_all` | socket 封装（阻塞 + 超时） |
| `comm_udp_bind/send/recv` | UDP（在线名单/群聊，丢了不重传） |
| `comm_client_create/destroy` | 客户端网络核心（收发线程 + 事件队列） |
| `comm_client_login/register/add_friend/send_text/send_group/game_*` | 业务请求 |
| `comm_history_init(paths)` | 建 `data/comm_history`，**登录前**调用 |
| `comm_history_append/load/clear` | 聊天记录 JSONL 读写（每会话保留 200 条） |
| `comm_event_free` | 释放事件（FRIENDS 的数组） |

### 6.5 通信会话 `board_ui/ui_comm_session.h`

单例，聊天室 / 围棋 / 账户页共用一条连接：

| API | 说明 |
|---|---|
| `int comm_session_init(const board_paths_t *paths)` | 幂等；首次调用建队列、客户端、事件泵、**聊天记录目录** |
| `comm_session_client()` | 取底层客户端发请求 |
| `comm_session_subscribe/unsubscribe` | 事件多播（最多 4 个订阅者），页面销毁只退订 |
| `comm_session_logged_in/user/set_user` | 登录态 |
| `comm_session_friend_count/name/online/state` | 好友缓存（无人订阅也更新） |
| `comm_session_auto_login()` | 环境变量 `BOARD_APP_COMM_AUTOLOGIN` 或本地记住的账号 |
| `comm_session_remember(user, pass)` | 记住账号（弱混淆后写本地配置） |
| `comm_session_take_pending_invite()` | 取走页面没打开时收到的对局邀请 |
| `comm_session_logout()` | 断开并清除记住的密码 |

### 6.6 TLS 客户端 `board_core/tls_client.h`

| API | 说明 |
|---|---|
| `bool tls_available(void)` | 运行时 `dlopen` 板端 `libssl.so.1.1`（结果缓存） |
| `tls_conn_t *tls_connect(int fd, const char *host)` | 在已连接的 socket 上握手（SNI + 证书校验 `/etc/ssl/certs`） |
| `tls_write / tls_read` | 收发；`-EAGAIN` 表示可重试 |
| `tls_close / tls_last_error` | 关闭 / 最近错误 |

### 6.7 AI 聊天 `chat/*.h`

| API | 说明 |
|---|---|
| `chat_service_create/destroy` | worker 线程 + 结果队列 |
| `int chat_service_ask(svc, cfg, history, count, text, seq)` | 异步发起；`seq` 用于丢弃过期回复 |
| `void chat_service_cancel(svc)` | 取消并在途请求（内部 `cancel` 必须是 `volatile int`） |
| `chat_service_load_config(cfg)` | 读配置：环境变量 > 本地 `ai.*` > 关闭 |
| `chat_ai_build_url/build_request/parse_response` | URL/请求体/响应解析（可单测） |
| `chat_ai_save_key/save_endpoint/clear_key/has_stored_key` | API key 本地持久化（`./ai_key` 用） |
| `chat_offline_reply(...)` | 离线助手（问候/时间/四则运算/使用说明） |

## 7. 数据文件格式

### 7.1 `data/settings.conf`

```ini
# Board App settings (UTF-8)
weather.city_code=101020100
weather.city_name=上海
game.2048.best=4096
game.snake.best=180
game.breakout.best=320
ui.reduce_motion=0

# ---- 系统账户（登录成功后写入；密码为本地弱混淆，非明文）----
comm.user=alice
comm.pass=3b0d010c134cb6b8
comm.host=192.168.1.10:20000
comm.remember=1

# ---- AI（./ai_key 写入；key 为明文，靠文件权限 0600 保护）----
ai.url=https://api.deepseek.com/v1
ai.model=deepseek-flash
ai.key=sk-xxxxxxxx
```

- UTF-8，行尾 `\n`；允许 `#` 注释；键值按第一个 `=` 分割；值两端空白保留（城市名不含首尾空格）。
- 原子写；文件损坏时忽略无法解析的行，保留可用键。

### 7.2 `data/cache/weather_<code>.json`

- 接口原始 JSON；文件 mtime 即抓取时间。
- 读取时解析；解析失败视为无缓存。
- 不缓存演示数据。

### 7.3 `<原图目录>/<原名>_edited_YYYYmmdd_HHMMSS.png`

- RGBA→PNG（stb 写出为 RGB/RGBA，取决于 alpha；本项目不透明图写 RGB）。
- 不覆盖原图；文件名冲突时追加序号。
- `data/edited/` 仅保留为旧版本兼容目录，不再作为默认保存位置。

### 7.4 日志

- 默认 stderr；不写文件，避免磨损/占满。需要时重定向到文件。

## 8. 环境变量

| 变量 | 默认 | 说明 |
|---|---|---|
| `BOARD_APP_ROOT` | 自动探测 | 资源/数据根目录 |
| `BOARD_APP_DATA_DIR` | `<root>/data` | 可写数据目录 |
| `BOARD_APP_PHOTO_DIR` | `<root>/assets/photos` | 相册图片目录 |
| `BOARD_APP_WEATHER_URL` | `http://wttr.in/` | 天气接口前缀（必须以 `/` 结尾） |
| `BOARD_APP_LOG_LEVEL` | `info` | `debug/info/warn/error` |
| `BOARD_APP_START` | `home` | 启动直接进入的应用 id：`home`/`weather`/`gallery`/`games`/`g2048`/`snake`/`breakout`/`chat`/`comm`/`weiqi`/`account` |
| `BOARD_APP_REDUCE_MOTION` | `0` | `1` 关闭转场/缩放动画 |
| `BOARD_APP_PERF` | 关闭 | `1` 每 2 秒打印 fps / 帧耗时 / LVGL 堆 |
| `BOARD_APP_AI_URL` | 本地配置或 `https://api.deepseek.com/v1` | 大模型基础地址（http/https 均可） |
| `BOARD_APP_AI_MODEL` | `deepseek-flash` | 模型名 |
| `BOARD_APP_AI_KEY` | 本地配置 | API key（优先级高于 `data/settings.conf` 里的 `ai.key`） |
| `BOARD_APP_CHAT_AUTOSEND` | 空 | 自检用：聊天页 2.5 秒后自动发送这段文本 |
| `BOARD_APP_COMM_AUTOLOGIN` | 空 | `host:port,user,pass`，自动登录聊天/围棋账号 |
| `LV_LINUX_FBDEV_DEVICE` | `/dev/fb0` | 现有 LVGL fbdev 变量 |
| `LV_LINUX_INPUT_DEVICE` | `/dev/input/event6` | 新增：触摸设备节点（实现阶段加入 main.c） |

## 9. 线程安全矩阵

| API / 对象 | 线程安全 | 备注 |
|---|---|---|
| `board_storage_*` | ✅（内部互斥） | 初始化/销毁需在无并发时调用 |
| `board_async_queue_*` | ✅ | 固定容量；push/pop 内部锁 |
| `http_get` / `http_request_*` | ✅（每次独立） | 取消标志原子/互斥 |
| `weather_parse_json` | ✅（纯函数） | 不共享全局 |
| `weather_service_*` | ✅（内部锁） | `last` 借用指针需尽快使用 |
| `image_*` | ❌（同一对象） | 不同对象可并行 |
| `gallery_scan` | ✅（只读目录） | 与保存并发时结果可能变化 |
| `arcade_*` | ❌ | 每个页面独立状态，无全局 |
| `weiqi_*`（规则核心） | ❌（同一对象） | 服务器每局独立；客户端只读显示 |
| `comm_history_*` | ✅（内部互斥） | 按会话分文件，写入前读改写，原子替换 |
| `comm_client_*` | ✅（发送加锁） | 收发线程内部；事件经队列回 UI |
| `comm_session_*` | ✅（仅 UI 线程调用） | 事件多播 + 待投递缓存；不要在 worker 里调用 |
| `tls_*` | ✅ | 首次 `dlopen` 有互斥；每个连接独立 SSL 对象 |
| `chat_service_*` | ✅（内部锁） | worker 与 UI 通过队列+seq 交互 |
| 所有 `lv_*` / UI | ❌ | 仅 UI 主线程 |

## 10. 版本与兼容

- 公共头文件变更遵循“先加后删”：新增字段放在结构体末尾；删除/改语义需升 minor 并更新本文。
- 第三方核心版本记录在 `third_party/littleworld_arcade/UPSTREAM.md`；升级需跑 `test_game*` 全量。
- `settings.conf` 新增键必须有默认值，旧文件可直接读。
- 缓存 JSON 结构由接口决定；解析器必须容错，不能因新增字段失败。
