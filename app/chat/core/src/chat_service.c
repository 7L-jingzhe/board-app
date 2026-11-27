/**
 * @file chat_service.c
 * @brief AI 聊天工作线程：HTTP 请求、超时/取消、离线兜底。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "chat/chat_service.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "board_core/http_client.h"
#include "board_core/tls_client.h"
#include "chat/chat_llm.h"
#include "chat/chat_offline.h"

#define CHAT_HTTP_TIMEOUT_MS 25000

struct chat_service {
  board_async_queue_t *queue;
  pthread_mutex_t lock;
  pthread_t thread;
  bool thread_joinable;
  bool running;
  volatile int cancel; /* 必须是 int：http_client 按 volatile int* 读它 */
  uint32_t seq;

  /* worker 读取的快照 */
  chat_ai_config_t cfg;
  char user[CHAT_MSG_MAX];
  chat_message_t history[CHAT_CONTEXT_MESSAGES];
  char history_text[CHAT_CONTEXT_MESSAGES][CHAT_MSG_MAX];
  size_t history_count;

  /* 预分配的结果缓冲，避免在 worker 里频繁 malloc */
  char *reply_buf;
  size_t reply_cap;
};

static void publish(chat_service_t *svc, chat_reply_t *res) {
  if (svc->queue == NULL || board_async_queue_push(svc->queue, res) != 0) {
    free(res);
    BOARD_LOGW("chat", "reply queue full, reply dropped");
  }
}

static void offline_fallback(chat_service_t *svc, const char *user,
                             const char *note, chat_reply_t *res) {
  char buffer[CHAT_MSG_MAX];
  size_t n = chat_offline_reply(user, buffer, sizeof(buffer));
  snprintf(res->note, sizeof(res->note), "%.100s", note != NULL ? note : "");
  res->source = CHAT_AI_SOURCE_OFFLINE;
  res->reply = n > 0 ? strdup(buffer) : NULL;
  (void)svc;
}

static void *worker(void *arg) {
  chat_service_t *svc = arg;
  chat_reply_t *res;
  char url[640];
  char *request = NULL;
  http_response_t resp;

  memset(&resp, 0, sizeof(resp));
  res = calloc(1, sizeof(*res));
  if (res == NULL) {
    pthread_mutex_lock(&svc->lock);
    svc->running = false;
    pthread_mutex_unlock(&svc->lock);
    return NULL;
  }

  pthread_mutex_lock(&svc->lock);
  res->seq = svc->seq;
  {
    char user[CHAT_MSG_MAX];
    chat_ai_config_t cfg = svc->cfg;
    chat_message_t hist[CHAT_CONTEXT_MESSAGES];
    size_t count = svc->history_count;
    snprintf(user, sizeof(user), "%s", svc->user);
    for (size_t i = 0; i < count; i++) {
      hist[i] = svc->history[i];
    }
    pthread_mutex_unlock(&svc->lock);

    if (!cfg.enabled) {
      offline_fallback(svc, user, "", res);
      goto publish;
    }
    if (chat_ai_build_url(&cfg, url, sizeof(url)) != 0) {
      BOARD_LOGW("chat", "bad AI base url: %s", cfg.base_url);
      offline_fallback(svc, user, "大模型地址无效（仅支持 http://）", res);
      goto publish;
    }
    if (chat_ai_build_request(&cfg, hist, count, user, &request) != 0) {
      offline_fallback(svc, user, "请求构造失败", res);
      goto publish;
    }
    BOARD_LOGI("chat", "POST %s model=%s key=%s", url, cfg.model,
               cfg.api_key[0] != '\0' ? "yes" : "no");
    {
      /* 有 key 就带 Authorization 头（API key 只能走请求头，不能放 body） */
      char auth[224];
      const char *extra = NULL;
      int ret;

      if (cfg.api_key[0] != '\0') {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n",
                 cfg.api_key);
        extra = auth;
      }
      ret = http_post_headers(url, request, -1, NULL, extra,
                              CHAT_HTTP_TIMEOUT_MS, &svc->cancel, &resp);
      if (svc->cancel) {
        http_response_free(&resp);
        free(request);
        free(res);
        pthread_mutex_lock(&svc->lock);
        svc->running = false;
        pthread_mutex_unlock(&svc->lock);
        return NULL;
      }
      if (ret != 0) {
        BOARD_LOGW("chat", "http_post failed rc=%d (%s) tls=%s tls_err=%s", ret,
                   strerror(ret < 0 ? -ret : 0), tls_available() ? "yes" : "no",
                   tls_last_error());
        offline_fallback(svc, user, "网络不可用，已切换到离线助手", res);
      } else if (resp.status_code != 200) {
        char err[160];
        char note[200];
        err[0] = '\0';
        (void)chat_ai_parse_response(resp.body != NULL ? resp.body : "",
                                     resp.body != NULL ? resp.len : 0,
                                     &res->reply, err, sizeof(err));
        free(res->reply);
        res->reply = NULL;
        if (err[0] != '\0') {
          snprintf(note, sizeof(note), "大模型返回 HTTP %d：%s",
                   resp.status_code, err);
        } else {
          snprintf(note, sizeof(note), "大模型返回 HTTP %d", resp.status_code);
        }
        BOARD_LOGW("chat", "%s", note);
        offline_fallback(svc, user, note, res);
      } else {
        char err[160];
        err[0] = '\0';
        if (chat_ai_parse_response(resp.body != NULL ? resp.body : "",
                                   resp.body != NULL ? resp.len : 0,
                                   &res->reply, err, sizeof(err)) == 0) {
          res->source = CHAT_AI_SOURCE_LLM;
        } else {
          char note[200];
          snprintf(note, sizeof(note), "大模型响应解析失败：%s", err);
          BOARD_LOGW("chat", "%s", note);
          offline_fallback(svc, user, note, res);
        }
      }
    }
    http_response_free(&resp);
    free(request);
  }

publish:
  if (res->reply == NULL) {
    res->reply = strdup("（暂时无法生成回复）");
  }
  publish(svc, res);
  pthread_mutex_lock(&svc->lock);
  svc->running = false;
  pthread_mutex_unlock(&svc->lock);
  return NULL;
}

/** 本地配置里的键名 */
#define AI_KEY_STORE_URL "ai.url"
#define AI_KEY_STORE_MODEL "ai.model"
#define AI_KEY_STORE_KEY "ai.key"

/** board_storage_get_str 返回共享缓冲，必须立刻拷走。 */
static void store_str(const char *key, char *out, size_t cap) {
  const char *v = board_storage_get_str(key, "");

  snprintf(out, cap, "%s", v != NULL ? v : "");
}

int chat_ai_save_key(const char *key) {
  char url[512];
  char model[96];

  if (key == NULL || key[0] == '\0') {
    return -EINVAL;
  }
  /* 同时把默认地址/模型写下来，方便以后只改 key 或换服务商 */
  store_str(AI_KEY_STORE_URL, url, sizeof(url));
  store_str(AI_KEY_STORE_MODEL, model, sizeof(model));
  if (url[0] == '\0') {
    board_storage_set_str(AI_KEY_STORE_URL, "https://api.deepseek.com/v1");
  }
  if (model[0] == '\0') {
    board_storage_set_str(AI_KEY_STORE_MODEL, "deepseek-flash");
  }
  board_storage_set_str(AI_KEY_STORE_KEY, key);
  return board_storage_save();
}

int chat_ai_save_endpoint(const char *url, const char *model) {
  if (url != NULL && url[0] != '\0') {
    board_storage_set_str(AI_KEY_STORE_URL, url);
  }
  if (model != NULL && model[0] != '\0') {
    board_storage_set_str(AI_KEY_STORE_MODEL, model);
  }
  return board_storage_save();
}

int chat_ai_clear_key(void) {
  board_storage_set_str(AI_KEY_STORE_KEY, "");
  return board_storage_save();
}

bool chat_ai_has_stored_key(void) {
  char key[160];

  store_str(AI_KEY_STORE_KEY, key, sizeof(key));
  return key[0] != '\0';
}

void chat_service_load_config(chat_ai_config_t *cfg) {
  const char *env_url;
  const char *env_model;
  const char *env_key;
  char saved_url[512] = {0};
  char saved_model[96] = {0};
  char saved_key[160] = {0};

  if (cfg == NULL) {
    return;
  }
  memset(cfg, 0, sizeof(*cfg));
  env_url = getenv("BOARD_APP_AI_URL");
  env_model = getenv("BOARD_APP_AI_MODEL");
  env_key = getenv("BOARD_APP_AI_KEY");

  /* 先取本地保存的配置；环境变量优先级更高（临时覆盖用） */
  store_str(AI_KEY_STORE_URL, saved_url, sizeof(saved_url));
  store_str(AI_KEY_STORE_MODEL, saved_model, sizeof(saved_model));
  store_str(AI_KEY_STORE_KEY, saved_key, sizeof(saved_key));
  if (saved_url[0] == '\0' && saved_key[0] != '\0') {
    snprintf(saved_url, sizeof(saved_url), "%s", "https://api.deepseek.com/v1");
  }
  if (saved_model[0] == '\0') {
    snprintf(saved_model, sizeof(saved_model), "%s", "deepseek-flash");
  }

  if (env_url != NULL && env_url[0] != '\0') {
    snprintf(cfg->base_url, sizeof(cfg->base_url), "%s", env_url);
    snprintf(cfg->model, sizeof(cfg->model), "%s",
             (env_model != NULL && env_model[0] != '\0') ? env_model
                                                         : "deepseek-flash");
    if (env_key != NULL) {
      snprintf(cfg->api_key, sizeof(cfg->api_key), "%s", env_key);
    }
    snprintf(cfg->system_prompt, sizeof(cfg->system_prompt), "%s",
             "我是运行在 LVGL 开发板上的中文聊天助手——可林，回复简洁口语化，"
             "单次回复不超过 120 个汉字，不要使用 Markdown 语法。");
    cfg->enabled = true;
  } else if (env_key != NULL && env_key[0] != '\0') {
    /* 只给了 key：默认用 DeepSeek 官方接口 */
    snprintf(cfg->base_url, sizeof(cfg->base_url), "%s",
             "https://api.deepseek.com/v1");
    snprintf(cfg->model, sizeof(cfg->model), "%s",
             (env_model != NULL && env_model[0] != '\0') ? env_model
                                                         : "deepseek-flash");
    snprintf(cfg->api_key, sizeof(cfg->api_key), "%s", env_key);
    snprintf(cfg->system_prompt, sizeof(cfg->system_prompt), "%s",
             "我是运行在 LVGL 开发板上的中文聊天助手——可林，回复简洁口语化，"
             "单次回复不超过 120 个汉字，不要使用 Markdown 语法。");
    cfg->enabled = true;
  } else if (saved_key[0] != '\0') {
    /* 没给环境变量，但本地配置里有 key（./ai_key 写入的） */
    snprintf(cfg->base_url, sizeof(cfg->base_url), "%s", saved_url);
    snprintf(cfg->model, sizeof(cfg->model), "%s",
             (env_model != NULL && env_model[0] != '\0') ? env_model
                                                         : saved_model);
    snprintf(cfg->api_key, sizeof(cfg->api_key), "%s", saved_key);
    snprintf(cfg->system_prompt, sizeof(cfg->system_prompt), "%s",
             "我是运行在 LVGL 开发板上的中文聊天助手——可林，回复简洁口语化，"
             "单次回复不超过 120 个汉字，不要使用 Markdown 语法。");
    cfg->enabled = true;
    BOARD_LOGI("chat", "AI key loaded from local config (%s, model=%s)",
               saved_url, cfg->model);
  }
}

chat_service_t *chat_service_create(board_async_queue_t *queue) {
  chat_service_t *svc;
  if (queue == NULL) {
    return NULL;
  }
  svc = calloc(1, sizeof(*svc));
  if (svc == NULL) {
    return NULL;
  }
  svc->queue = queue;
  pthread_mutex_init(&svc->lock, NULL);
  return svc;
}

void chat_service_destroy(chat_service_t *svc) {
  if (svc == NULL) {
    return;
  }
  chat_service_cancel(svc);
  pthread_mutex_destroy(&svc->lock);
  free(svc->reply_buf);
  free(svc);
}

int chat_service_ask(chat_service_t *svc, const chat_ai_config_t *cfg,
                     const chat_message_t *history, size_t count,
                     const char *user, uint32_t seq) {
  pthread_t old_thread;
  bool old_joinable;
  int ret;

  if (svc == NULL || user == NULL || user[0] == '\0') {
    return -EINVAL;
  }
  if (strlen(user) >= CHAT_MSG_MAX) {
    return -EINVAL;
  }

  pthread_mutex_lock(&svc->lock);
  if (svc->running) {
    pthread_mutex_unlock(&svc->lock);
    return -EAGAIN;
  }
  old_thread = svc->thread;
  old_joinable = svc->thread_joinable;
  svc->thread_joinable = false;
  pthread_mutex_unlock(&svc->lock);
  if (old_joinable) {
    pthread_join(old_thread, NULL);
  }

  pthread_mutex_lock(&svc->lock);
  if (svc->running) {
    pthread_mutex_unlock(&svc->lock);
    return -EAGAIN;
  }
  svc->cancel = 0;
  svc->seq = seq;
  if (cfg != NULL) {
    svc->cfg = *cfg;
  } else {
    memset(&svc->cfg, 0, sizeof(svc->cfg));
  }
  snprintf(svc->user, sizeof(svc->user), "%s", user);
  {
    size_t n = count > CHAT_CONTEXT_MESSAGES ? CHAT_CONTEXT_MESSAGES : count;
    /* 只保留最近的 n 条，并把文本复制进自有缓冲，避免 UI 线程释放后悬空。 */
    size_t start = count - n;
    for (size_t i = 0; i < n; i++) {
      snprintf(svc->history_text[i], CHAT_MSG_MAX, "%s",
               history[start + i].text != NULL ? history[start + i].text : "");
      svc->history[i].role = history[start + i].role;
      svc->history[i].text = svc->history_text[i];
    }
    svc->history_count = n;
  }
  svc->running = true;
  ret = pthread_create(&svc->thread, NULL, worker, svc);
  if (ret != 0) {
    svc->running = false;
    pthread_mutex_unlock(&svc->lock);
    BOARD_LOGE("chat", "pthread_create failed: %d", ret);
    return -EAGAIN;
  }
  svc->thread_joinable = true;
  pthread_mutex_unlock(&svc->lock);
  return 0;
}

void chat_service_cancel(chat_service_t *svc) {
  pthread_t th;
  bool joinable;
  if (svc == NULL) {
    return;
  }
  pthread_mutex_lock(&svc->lock);
  svc->cancel = 1;
  th = svc->thread;
  joinable = svc->thread_joinable;
  svc->thread_joinable = false;
  pthread_mutex_unlock(&svc->lock);
  if (joinable) {
    pthread_join(th, NULL);
  }
}

int chat_service_busy(chat_service_t *svc) {
  int busy;
  if (svc == NULL) {
    return 0;
  }
  pthread_mutex_lock(&svc->lock);
  busy = svc->running;
  pthread_mutex_unlock(&svc->lock);
  return busy;
}
