#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <curl/curl.h>
#include <sys/mman.h>
#include "alist.h"
#include "buffer.h"
#include "queue.h"
#include "../api/ens.h"

#define ENS_VERSION_MAJOR 0
#define ENS_VERSION_MINOR 2
#define ENS_VERSION_PATCH 0

#define ENS_HOST_MAX_LEN     255
#define ENS_FROM_MAX_LEN     254
#define ENS_USERNAME_MAX_LEN 255
#define ENS_PASSWORD_MAX_LEN 255
#define ENS_PATH_MAX_LEN     255

typedef struct {
    uint64_t emails_sent;
    uint64_t emails_total;
} ens_group_stats_t;

typedef struct {
    int mode;
    time_t interval;
    alist_t *to;
    char host[ENS_HOST_MAX_LEN - 7 + 1]; //save room for smtp://
    char from[ENS_FROM_MAX_LEN + 1];
    char username[ENS_USERNAME_MAX_LEN + 1];
    char password[ENS_PASSWORD_MAX_LEN + 1];
    char ca_path[ENS_PATH_MAX_LEN + 1];
} ens_config_t;

typedef struct {
    ens_group_id_t id;
    ens_config_t config;
    volatile time_t expires;
    ens_group_stats_t stats;
    queue_t *emails;
    pthread_mutex_t emails_mutex;
    char f_path[ENS_PATH_MAX_LEN + 1];
    FILE *f;
} ens_group_t;

struct ens_t {
    ens_config_t config;
    ens_log_function_t log_function;
    int log_level;
    void *log_user_data;
    volatile bool running;
    pthread_t thread;
    alist_t *groups;
    pthread_rwlock_t groups_lock;
};

typedef struct {
    char *subject;
    char *body;
} ens_email_t;

typedef struct {
    ens_t *ens;
    ens_group_t *group;
    buffer_t *buffer;
} ens_curl_context_t;

int
ens_version_major() {
    return ENS_VERSION_MAJOR;
}

int
ens_version_minor() {
    return ENS_VERSION_MINOR;
}

int
ens_version_patch() {
    return ENS_VERSION_PATCH;
}

static void
ens_group_free(ens_group_t *group) {
    if (group == NULL) {
        return;
    }

    if (group->config.to != NULL) {
        alist_free_func(group->config.to, free);
    }

    //zero out sensitive memory before unlocking the pages
    memset(group->config.username, 0, sizeof(group->config.username));
    memset(group->config.password, 0, sizeof(group->config.password));

    munlock(group->config.username, sizeof(group->config.username));
    munlock(group->config.password, sizeof(group->config.password));

    if (group->emails != NULL) {
        queue_free_func(group->emails, free);
    }

    if (group->f != NULL) {
        fclose(group->f);
    }

    pthread_mutex_destroy(&group->emails_mutex);

    free(group);
}

static ens_group_t *
ens_group_init(ens_t *ens) {
    ens_group_t *group;
    unsigned int i;
    char *to;

    group = calloc(1, sizeof(*group));
    if (group == NULL) {
        return NULL;
    }

    group->config.mode = ens->config.mode;
    group->config.interval = ens->config.interval;

    group->config.to = alist_init();
    if (group->config.to == NULL) {
        goto fail;
    }
    for (i = 0; i < alist_size(ens->config.to); i++) {
        to = strdup(alist_get(ens->config.to, i));
        if (to == NULL) {
            goto fail;
        }

        if (!alist_add(group->config.to, to)) {
            free(to);
            goto fail;
        }
    }

    if (ens->config.host[0] != '\0') {
        strcpy(group->config.host, ens->config.host);
    }

    if (ens->config.from[0] != '\0') {
        strcpy(group->config.from, ens->config.from);
    }

    if (mlock(group->config.username, sizeof(group->config.username)) != 0) {
        goto fail;
    }
    if (ens->config.username[0] != '\0') {
        strcpy(group->config.username, ens->config.username);
    }

    if (mlock(group->config.password, sizeof(group->config.password)) != 0) {
        goto fail;
    }
    if (ens->config.password[0] != '\0') {
        strcpy(group->config.password, ens->config.password);
    }

    if (ens->config.ca_path[0] != '\0') {
        strcpy(group->config.ca_path, ens->config.ca_path);
    }

    group->emails = queue_init();
    if (group->emails == NULL) {
        goto fail;
    }

    if (pthread_mutex_init(&group->emails_mutex, NULL) != 0) {
        goto fail;
    }

    return group;

fail:
    ens_group_free(group);
    return NULL;
}

static ens_email_t *
ens_email_init() {
    ens_email_t *email;

    email = calloc(1, sizeof(*email));
    if (email == NULL) {
        return NULL;
    }

    return email;
}

static void
ens_email_free(ens_email_t *email) {
    if (email == NULL) {
        return;
    }

    if (email->subject != NULL) {
        free(email->subject);
    }
    if (email->body != NULL) {
        free(email->body);
    }

    free(email);
}

void
ens_free(ens_t *ens) {
    ens_group_t *group;
    unsigned int i;

    if (ens == NULL) {
        return;
    }

    if (ens->config.to != NULL) {
        alist_free_func(ens->config.to, free);
    }

    //zero sensitive fields before unlocking pages
    memset(ens->config.username, 0, sizeof(ens->config.username));
    memset(ens->config.password, 0, sizeof(ens->config.password));

    munlock(ens->config.username, sizeof(ens->config.username));
    munlock(ens->config.password, sizeof(ens->config.password));

    if (ens->groups != NULL) {
        for (i = 0; i < alist_size(ens->groups); i++) {
            group = alist_get(ens->groups, i);
            ens_group_free(group);
        }
        alist_free(ens->groups);
    }

    pthread_rwlock_destroy(&ens->groups_lock);

    free(ens);
}

ens_t *
ens_init() {
    ens_t *ens;

    ens = calloc(1, sizeof(*ens));
    if (ens == NULL) {
        return NULL;
    }

    ens->config.mode = ENS_GROUP_MODE_DROP;
    ens->config.interval = 30;
    ens->config.to = alist_init();
    if (ens->config.to == NULL) {
        goto fail;
    }
    if (mlock(ens->config.username, sizeof(ens->config.username)) != 0) {
        goto fail;
    }
    if (mlock(ens->config.password, sizeof(ens->config.password)) != 0) {
        goto fail;
    }
    ens->log_level = ENS_LOG_LEVEL_WARN;

    ens->groups = alist_init(ens->groups);
    if (ens->groups == NULL) {
        goto fail;
    }

    if (pthread_rwlock_init(&ens->groups_lock, NULL) != 0) {
        goto fail;
    }

    return ens;

fail:
    ens_free(ens);
    return NULL;
}

static int
ens_log(ens_t *ens, int err, int level, const char *fmt, ...) {
    char msg[256];
    va_list ap;

    if (ens->log_function == NULL) {
        return err;
    }
    if (level > ens->log_level) {
        return err;
    }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    ens->log_function(level, msg, ens->log_user_data);
    return err;
}

//TODO: Need to do multiple writes if the email is bigger than size * nmemb bytes.
static size_t
email_read(void *ptr, size_t size, size_t nmemb, void *user_data) {
    bool success;
    unsigned int i;
    ens_email_t *email;
    ens_curl_context_t *context;

    context = (ens_curl_context_t *)user_data;
    if (buffer_length(context->buffer) > 0) {
        return 0;
    }

    //write each recipient
    for (i = 0; i < alist_size(context->group->config.to); i++) {
        if (!buffer_writef(context->buffer, "To: %s\r\n", alist_get(context->group->config.to, i))) {
            ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
            return CURL_READFUNC_ABORT;
        }
    }

    //write the sender
    if (!buffer_writef(context->buffer, "From: %s\r\n", context->group->config.from)) {
        ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
        return CURL_READFUNC_ABORT;
    }

    //write the subject
    switch (context->group->config.mode) {
        case ENS_GROUP_MODE_DROP:
            email = queue_pop(context->group->emails);

            success = buffer_writef(context->buffer, "Subject: %s\r\n", email->subject) &&
                      buffer_writef(context->buffer, "\r\n") &&
                      buffer_writef(context->buffer, "%s\n", email->body);

            ens_email_free(email);

            if (!success) {
                ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                return CURL_READFUNC_ABORT;
            }

            break;
        case ENS_GROUP_MODE_COLLECT:
            i = 0;

            success = buffer_writef(context->buffer, "Subject: %u Emails\r\n", queue_size(context->group->emails)) &&
                      buffer_writef(context->buffer, "\r\n");

            while (success && queue_size(context->group->emails) > 0) {
                email = queue_pop(context->group->emails);

                if (i > 0) {
                    success = buffer_writef(context->buffer, "\n\n");
                }

                if (success) {
                    success = buffer_writef(context->buffer, "Subject: %s\n", email->subject) &&
                              buffer_writef(context->buffer, "%s", email->body);
                }

                ens_email_free(email);

                if (!success) {
                    ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                    return CURL_READFUNC_ABORT;
                }
            }

            break;
    }

    //TODO: Handle larger emails instead of aborting
    if (buffer_length(context->buffer) > size * nmemb) {
        ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_ERROR, "Failed to send email for group %d: The email is %u bytes but cURL's buffer is only %zu", context->group->id, buffer_length(context->buffer), size * nmemb);
        return CURL_READFUNC_ABORT;
    }

    memcpy(ptr, buffer_data(context->buffer), buffer_length(context->buffer));

    return buffer_length(context->buffer);
}

static void
ens_send_email(ens_t *ens, ens_group_t *group) {
    struct curl_slist *to = NULL;
    unsigned int i;
    long code;
    char error[CURL_ERROR_SIZE];
    bool success = false;
    ens_curl_context_t context;
    CURL *curl;
    CURLcode ret;

    context.ens = ens;
    context.group = group;
    context.buffer = buffer_init(4096);
    if (context.buffer == NULL) {
        ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", group->id);
        return;
    }

    for (i = 0; i < alist_size(group->config.to); i++) {
        to = curl_slist_append(to, alist_get(group->config.to, i));
    }

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, group->config.host);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, group->config.from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, to);
    if (group->config.username[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_USERNAME, group->config.username);
    }
    if (group->config.password[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, group->config.password);
    }
    if (group->config.ca_path[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_CAINFO, group->config.ca_path);
    }
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, email_read);
    curl_easy_setopt(curl, CURLOPT_READDATA, &context);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    ret = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (ret == CURLE_OK) {
        success = true;
    }
    else {
        ens_log(ens, ENS_ERROR_EMAIL_FAILED, ENS_LOG_LEVEL_ERROR, "Failed to send email for group %d: %s: SMTP code %d: %s", group->id, curl_easy_strerror(ret), code, error);
    }

    curl_slist_free_all(to);
    curl_easy_cleanup(curl);

    buffer_free(context.buffer);

    //make sure the emails are always cleared
    if (!success) {
        while (queue_size(group->emails) > 0) {
            ens_group_free(queue_pop(group->emails));
        }
    }
}

//TODO: error handling for fprintf?
static int
ens_send_email_file(ens_t *ens, ens_group_t *group) {
    ens_email_t *email;
    time_t now;
    struct tm now_tm;
    char now_buf[32];
    const char *to;
    unsigned int i;

    if (group->f == NULL) {
        group->f = fopen(group->f_path, "w");
        if (group->f == NULL) {
            return ens_log(ens, ENS_ERROR_FILE, ENS_LOG_LEVEL_ERROR, "Failed to write to file for group %d: Could not open file: %s", group->id, strerror(errno));
        }
    }

    now = time(NULL);
    localtime_r(&now, &now_tm);
    strftime(now_buf, sizeof(now_buf), "%Y-%m-%d %H:%M:%S", &now_tm);

    while (queue_size(group->emails) > 0) {
        email = queue_pop(group->emails);

        if (group->stats.emails_sent > 0) {
            fprintf(group->f, "\n");
        }

        fprintf(group->f, "[%s]\n", now_buf);
        for (i = 0; i < alist_size(group->config.to); i++) {
            to = alist_get(group->config.to, i);
            fprintf(group->f, "To: %s\n", to);
        }
        fprintf(group->f, "From: %s\n", group->config.from);
        fprintf(group->f, "Subject: %s\n", email->subject);
        fprintf(group->f, "%s\n", email->body);

        ens_email_free(email);
    }

    return ENS_ERROR_OK;
}

static void
ens_check_groups(ens_t *ens) {
    ens_group_t *group;
    unsigned int i;
    time_t now;

    pthread_rwlock_rdlock(&ens->groups_lock);
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        now = time(NULL);

        pthread_mutex_lock(&group->emails_mutex);
        if (now >= group->expires && queue_size(group->emails) > 0) {
            if (group->f != NULL) {
                ens_send_email_file(ens, group);
            }
            else {
                ens_send_email(ens, group);
            }

            ++group->stats.emails_sent;
            group->expires = now + group->config.interval;
        }
        pthread_mutex_unlock(&group->emails_mutex);
    }
    pthread_rwlock_unlock(&ens->groups_lock);
}

static void *
ens_process(void *user_data) {
    ens_t *ens;

    ens = (ens_t *)user_data;
    ens->running = true;

    while (ens->running) {
        ens_check_groups(ens);
        usleep(1000 * 100);
    }

    return NULL;
}

int
ens_start(ens_t *ens) {
    int ret = ENS_ERROR_OK;

    if (ens->running) {
        return ENS_ERROR_ALREADY_RUNNING;
    }

    //start the context's thread
    if (ret == ENS_ERROR_OK) {
        if (pthread_create(&ens->thread, NULL, ens_process, ens) != 0) {
            ret = ens_log(ens, ENS_ERROR_THREAD, ENS_LOG_LEVEL_FATAL, "Failed to start the thread: %s", strerror(errno));
        }
    }

    return ret;
}

static int
ens_stop_helper(ens_t *ens, bool join) {
    ens_group_t *group;
    unsigned int i;

    if (!ens->running) {
        return ENS_ERROR_NOT_RUNNING;
    }

    ens->running = false;

    if (join) {
        pthread_join(ens->thread, NULL);
    }

    //if any groups are writing to a file, close them now
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        if (group->f != NULL) {
            fclose(group->f);
            group->f = NULL;
        }
    }

    return ENS_ERROR_OK;
}

int
ens_stop(ens_t *ens) {
    return ens_stop_helper(ens, false);
}

int
ens_stop_join(ens_t *ens) {
    return ens_stop_helper(ens, true);
}

static ens_group_t *
ens_group_find(ens_t *ens, ens_group_id_t id) {
    ens_group_t *group;
    unsigned int i;

    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        if (group->id == id) {
            return group;
        }
    }

    return NULL;
}

int
ens_group_register(ens_t *ens, ens_group_id_t id) {
    int ret = ENS_ERROR_OK;
    ens_group_t *group;

    pthread_rwlock_wrlock(&ens->groups_lock);
    group = ens_group_find(ens, id);
    if (group != NULL) {
        ret = ens_log(ens, ENS_ERROR_ALREADY_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to register group %d Already registered", id);
        goto done;
    }

    group = ens_group_init(ens);
    if (group == NULL) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to register group %d: Out of memory", id);
        goto done;
    }

    if (!alist_add(ens->groups, group)) {
        ens_group_free(group);
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to register group %d: Out of memory", id);
        goto done;
    }

    group->id = id;

done:
    pthread_rwlock_unlock(&ens->groups_lock);

    return ret;
}

int
ens_group_unregister(ens_t *ens, ens_group_id_t id) {
    bool found = false;
    ens_group_t *group;
    unsigned int i;

    pthread_rwlock_wrlock(&ens->groups_lock);
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        if (group->id == id) {
            alist_remove(ens->groups, i);
            ens_group_free(group);
            found = true;
            break;
        }
    }
    pthread_rwlock_unlock(&ens->groups_lock);

    if (!found) {
        return ens_log(ens, ENS_ERROR_NOT_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to unregister group %d: Not registered", id);
    }

    return ENS_ERROR_OK;
}

int
ens_group_send(ens_t *ens, ens_group_id_t id, const char *subject, const char *body) {
    int ret = ENS_ERROR_OK;
    bool success;
    ens_group_t *group;
    ens_email_t *email;

    email = ens_email_init();
    if (email == NULL) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
        return ret;
    }

    email->subject = strdup(subject);
    email->body = strdup(body);
    if (email->subject == NULL || email->body == NULL) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
        goto done;
    }

    pthread_rwlock_rdlock(&ens->groups_lock);
    group = ens_group_find(ens, id);
    if (group == NULL) {
        ret = ens_log(ens, ENS_ERROR_NOT_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to send email for group %d: Not registered", id);
        goto done;
    }

    ++group->stats.emails_total;

    if (group->config.mode == ENS_GROUP_MODE_DROP && queue_size(group->emails) > 0) {
        ret = ENS_ERROR_NOT_READY;
        goto done;
    }

    pthread_mutex_lock(&group->emails_mutex);
    success = queue_push(group->emails, email);
    pthread_mutex_unlock(&group->emails_mutex);

    if (!success) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
        goto done;
    }

done:
    pthread_rwlock_unlock(&ens->groups_lock);
    if (ret != ENS_ERROR_OK) {
        ens_email_free(email);
    }

    return ret;
}

int
ens_group_sendf(ens_t *ens, ens_group_id_t id, const char *subject, const char *fmt, ...) {
    va_list ap;
    int ret, count;
    char *body;

    va_start(ap, fmt);
    count = vasprintf(&body, fmt, ap);
    va_end(ap);

    if (count == -1) {
        return ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
    }

    ret = ens_group_send(ens, id, subject, body);
    free(body);

    return ret;
}

static int
ens_set_option_mode(ens_t *ens, va_list ap) {
    int mode, ret = ENS_ERROR_OK;

    mode = va_arg(ap, int);

    switch (mode) {
        case ENS_GROUP_MODE_DROP:
        case ENS_GROUP_MODE_COLLECT:
            ens->config.mode = mode;
            break;
        default:
            ret = ens_log(ens, ENS_ERROR_UNKNOWN_OPTION_VALUE, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_MODE: Unknown value");
            break;
    }

    return ret;
}

static int
ens_set_option_host(ens_t *ens, va_list ap) {
    const char *host;

    host = va_arg(ap, const char *);

    if (strlen(host) > ENS_HOST_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_HOST: Value must not exceed %d characters", ENS_HOST_MAX_LEN);
    }

    snprintf(ens->config.host, sizeof(ens->config.host), "smtp://%s", host);

    return ENS_ERROR_OK;
}


static int
ens_set_option_from(ens_t *ens, va_list ap) {
    const char *from;

    from = va_arg(ap, const char *);

    if (strlen(from) > ENS_FROM_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_FROM: Value must not exceed %d characters", ENS_FROM_MAX_LEN);
    }

    strcpy(ens->config.from, from);

    return ENS_ERROR_OK;
}

static int
ens_set_option_to(ens_t *ens, va_list ap) {
    char *to_copy;
    const char *to;

    to = va_arg(ap, const char *);

    to_copy = strdup(to);
    if (to_copy == NULL) {
        return ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to set option ENS_OPTION_TO: Out of memory");
    }

    if (!alist_add(ens->config.to, to_copy)) {
        free(to_copy);
        return ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to set option ENS_OPTION_TO: Out of memory");
    }

    return ENS_ERROR_OK;
}

static int
ens_set_option_username(ens_t *ens, va_list ap) {
    const char *username;

    username = va_arg(ap, const char *);

    if (strlen(username) > ENS_USERNAME_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_USERNAME: Value must not exceed %d characters", ENS_USERNAME_MAX_LEN);
    }

    strcpy(ens->config.username, username);

    return ENS_ERROR_OK;
}

static int
ens_set_option_password(ens_t *ens, va_list ap) {
    const char *password;

    password = va_arg(ap, const char *);

    if (strlen(password) > ENS_PASSWORD_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_PASSWORD: Value must not exceed %d characters", ENS_PASSWORD_MAX_LEN);
    }

    strcpy(ens->config.password, password);

    return ENS_ERROR_OK;
}

static int
ens_set_option_ca_path(ens_t *ens, va_list ap) {
    const char *ca_path;

    ca_path = va_arg(ap, const char *);

    if (strlen(ca_path) > ENS_PATH_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_CA_PATH: Value must not exceed %d characters", ENS_PATH_MAX_LEN);
    }

    strcpy(ens->config.ca_path, ca_path);

    return ENS_ERROR_OK;
}

static int
ens_set_option_log_function(ens_t *ens, va_list ap) {
    ens->log_function = va_arg(ap, ens_log_function_t);

    return ENS_ERROR_OK;
}

static int
ens_set_option_log_level(ens_t *ens, va_list ap) {
    ens->log_level = va_arg(ap, int);

    return ENS_ERROR_OK;
}

static int
ens_set_option_log_user_data(ens_t *ens, va_list ap) {
    ens->log_user_data = va_arg(ap, void *);

    return ENS_ERROR_OK;
}

int
ens_set_option(ens_t *ens, ens_option_t option, ...) {
    int ret = ENS_ERROR_OK;
    va_list ap;

    va_start(ap, option);

    switch (option) {
        case ENS_OPTION_MODE:
            ret = ens_set_option_mode(ens, ap);
            break;
        case ENS_OPTION_HOST:
            ret = ens_set_option_host(ens, ap);
            break;
        case ENS_OPTION_FROM:
            ret = ens_set_option_from(ens, ap);
            break;
        case ENS_OPTION_TO:
            ret = ens_set_option_to(ens, ap);
            break;
        case ENS_OPTION_USERNAME:
            ret = ens_set_option_username(ens, ap);
            break;
        case ENS_OPTION_PASSWORD:
            ret = ens_set_option_password(ens, ap);
            break;
        case ENS_OPTION_INTERVAL:
            ens->config.interval = va_arg(ap, int);
            break;
        case ENS_OPTION_CA_PATH:
            ret = ens_set_option_ca_path(ens, ap);
            break;
        case ENS_OPTION_LOG_FUNCTION:
            ret = ens_set_option_log_function(ens, ap);
            break;
        case ENS_OPTION_LOG_LEVEL:
            ret = ens_set_option_log_level(ens, ap);
            break;
        case ENS_OPTION_LOG_USER_DATA:
            ret = ens_set_option_log_user_data(ens, ap);
            break;
        default:
            ret = ens_log(ens, ENS_ERROR_UNKNOWN_OPTION, ENS_LOG_LEVEL_ERROR, "Failed to set option: Option %d not found", option);
            break;
    }

    va_end(ap);

    return ret;
}

static int
ens_group_set_option_mode(ens_t *ens, ens_group_t *group, va_list ap) {
    int mode, ret = ENS_ERROR_OK;

    mode = va_arg(ap, int);

    switch (mode) {
        case ENS_GROUP_MODE_DROP:
        case ENS_GROUP_MODE_COLLECT:
            group->config.mode = mode;
            break;
        default:
            ret = ens_log(ens, ENS_ERROR_UNKNOWN_OPTION_VALUE, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_MODE for group %d: Unknown value", group->id);
            break;
    }

    return ret;
}

static int
ens_group_set_option_host(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *host;

    host = va_arg(ap, const char *);

    if (strlen(host) > ENS_HOST_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_HOST for group %d: Value must not exceed %d characters", group->id, ENS_HOST_MAX_LEN);
    }

    snprintf(group->config.host, sizeof(group->config.host), "smtp://%s", host);

    return ENS_ERROR_OK;
}


static int
ens_group_set_option_from(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *from;

    from = va_arg(ap, const char *);

    if (strlen(from) > ENS_FROM_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_FROM for group %d: Value must not exceed %d characters", group->id, ENS_FROM_MAX_LEN);
    }

    strcpy(group->config.from, from);

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_to(ens_t *ens, ens_group_t *group, va_list ap) {
    char *to_copy;
    const char *to;

    to = va_arg(ap, const char *);

    to_copy = strdup(to);
    if (to_copy == NULL) {
        return ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to set option ENS_GROUP_OPTION_TO for group %d: Out of memory", group->id);
    }

    if (!alist_add(group->config.to, to_copy)) {
        free(to_copy);
        return ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to set option ENS_GROUP_OPTION_TO for group %d: Out of memory", group->id);
    }

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_username(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *username;

    username = va_arg(ap, const char *);

    if (strlen(username) > ENS_USERNAME_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_USERNAME for group %d: Value must not exceed %d characters", group->id, ENS_USERNAME_MAX_LEN);
    }

    strcpy(group->config.username, username);

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_password(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *password;

    password = va_arg(ap, const char *);

    if (strlen(password) > ENS_PASSWORD_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_PASSWORD for group %d: Value must not exceed %d characters", group->id, ENS_PASSWORD_MAX_LEN);
    }

    strcpy(group->config.password, password);

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_file(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *f_path;

    f_path = va_arg(ap, const char *);

    if (strlen(f_path) > ENS_PATH_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_FILE for group %d: Value must not exceed %d characters", group->id, ENS_PATH_MAX_LEN);
    }

    strcpy(group->f_path, f_path);

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_ca_path(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *ca_path;

    ca_path = va_arg(ap, const char *);

    if (strlen(ca_path) > ENS_PATH_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_CA_PATH for group %d: Value must not exceed %d characters", group->id, ENS_PATH_MAX_LEN);
    }

    strcpy(group->config.ca_path, ca_path);

    return ENS_ERROR_OK;
}

int
ens_group_set_option(ens_t *ens, ens_group_id_t id, ens_group_option_t option, ...) {
    int ret = ENS_ERROR_OK;
    ens_group_t *group;
    va_list ap;

    va_start(ap, option);
    pthread_rwlock_rdlock(&ens->groups_lock);

    group = ens_group_find(ens, id);
    if (group == NULL) {
        ret = ens_log(ens, ENS_ERROR_NOT_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to set option for group %d: Not registered", id);
        goto done;
    }

    switch (option) {
        case ENS_GROUP_OPTION_MODE:
            ret = ens_group_set_option_mode(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_HOST:
            ret = ens_group_set_option_host(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_FROM:
            ret = ens_group_set_option_from(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_TO:
            ret = ens_group_set_option_to(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_USERNAME:
            ret = ens_group_set_option_username(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_PASSWORD:
            ret = ens_group_set_option_password(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_INTERVAL:
            group->config.interval = va_arg(ap, int);
            break;
        case ENS_GROUP_OPTION_FILE:
            ret = ens_group_set_option_file(ens, group, ap);
            break;
        case ENS_GROUP_OPTION_CA_PATH:
            ret = ens_group_set_option_ca_path(ens, group, ap);
            break;
        default:
            ret = ens_log(ens, ENS_ERROR_UNKNOWN_OPTION, ENS_LOG_LEVEL_ERROR, "Failed to set option for group %d: Option %d not found", id, option);
            break;
    }

done:
    pthread_rwlock_unlock(&ens->groups_lock);
    va_end(ap);

    return ret;
}
