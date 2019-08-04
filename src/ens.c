#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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
#define ENS_VERSION_MINOR 1
#define ENS_VERSION_PATCH 0

#define ENS_HOST_MAX_LEN     255
#define ENS_FROM_MAX_LEN     254
#define ENS_USERNAME_MAX_LEN 255
#define ENS_PASSWORD_MAX_LEN 255
#define ENS_PATH_MAX_LEN     255

typedef struct {
    ens_group_id_t id;
    int mode;
    time_t interval;
    time_t expires;
    alist_t *to;
    char host[ENS_HOST_MAX_LEN - 7 + 1]; //save from for smtp://
    char from[ENS_FROM_MAX_LEN + 1];
    char username[ENS_USERNAME_MAX_LEN + 1];
    char password[ENS_PASSWORD_MAX_LEN + 1];
    FILE *f;
    char f_path[ENS_PATH_MAX_LEN + 1];
    queue_t *emails;
} ens_group_t;

struct ens_t {
    ens_log_function_t log_function;
    int log_level;
    void *log_user_data;
    volatile bool running;
    pthread_t thread;
    pthread_mutex_t groups_mutex;
    char ca_path[ENS_PATH_MAX_LEN + 1];
    alist_t *groups;
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

    alist_free_func(group->to, free);
    queue_free_func(group->emails, free);
    munlock(group->username, sizeof(group->username));
    munlock(group->password, sizeof(group->password));

    if (group->f != NULL) {
        fclose(group->f);
    }

    //make sure we zero out the memory before free'ing since it's possible we have usernames and passwords in memory
    memset(group, 0, sizeof(*group));
    free(group);
}

static ens_group_t *
ens_group_init(ens_t *ens) {
    ens_group_t *group;

    group = calloc(1, sizeof(*group));
    if (group == NULL) {
        return NULL;
    }

    group->mode = ENS_GROUP_MODE_DEFAULT;
    group->interval = ENS_GROUP_INTERVAL_DEFAULT;
    group->to = alist_init();
    group->emails = queue_init();
    if (group->to == NULL || group->emails == NULL) {
        ens_group_free(group);
        return NULL;
    }

    if (mlock(group->username, sizeof(group->username)) != 0) {
        ens_group_free(group);
        return NULL;
    }

    if (mlock(group->password, sizeof(group->password)) != 0) {
        ens_group_free(group);
        return NULL;
    }

    return group;
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

    if (ens->groups != NULL) {
        for (i = 0; i < alist_size(ens->groups); i++) {
            group = alist_get(ens->groups, i);
            ens_group_free(group);
        }
        alist_free(ens->groups);
    }

    pthread_mutex_destroy(&ens->groups_mutex);
    free(ens);
}

ens_t *
ens_init() {
    ens_t *ens;

    ens = calloc(1, sizeof(*ens));
    if (ens == NULL) {
        return NULL;
    }

    ens->groups = alist_init(ens->groups);
    if (ens->groups == NULL) {
        ens_free(ens);
        return NULL;
    }

    ens->log_level = ENS_LOG_LEVEL_WARN;

    if (pthread_mutex_init(&ens->groups_mutex, NULL) != 0) {
    }

    return ens;
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
    unsigned int i;
    ens_email_t *email;
    ens_curl_context_t *context;

    context = (ens_curl_context_t *)user_data;
    if (buffer_length(context->buffer) > 0) {
        return 0;
    }

    //write each recipient
    for (i = 0; i < alist_size(context->group->to); i++) {
        if (!buffer_writef(context->buffer, "To: %s\r\n", alist_get(context->group->to, i))) {
            ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
            return CURL_READFUNC_ABORT;
        }
    }

    //write the sender
    if (!buffer_writef(context->buffer, "From: %s\r\n", context->group->from)) {
        ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
        return CURL_READFUNC_ABORT;
    }

    //write the subject
    switch (context->group->mode) {
        case ENS_GROUP_MODE_DROP:
            email = queue_peek(context->group->emails);

            if (!buffer_writef(context->buffer, "Subject: %s\r\n", email->subject)) {
                ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                return CURL_READFUNC_ABORT;
            }

            break;
        case ENS_GROUP_MODE_COLLECT:
            if (!buffer_writef(context->buffer, "Subject: %u Emails\r\n", queue_size(context->group->emails))) {
                ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                return CURL_READFUNC_ABORT;
            }

            break;
    }

    //write the body, which is separated by another terminator
    if (!buffer_writef(context->buffer, "\r\n")) {
        ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
        return CURL_READFUNC_ABORT;
    }

    switch (context->group->mode) {
        case ENS_GROUP_MODE_DROP:
            email = queue_pop(context->group->emails);

            if (!buffer_writef(context->buffer, "%s\n", email->body)) {
                ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                return CURL_READFUNC_ABORT;
            }

            ens_email_free(email);
            break;
        case ENS_GROUP_MODE_COLLECT:
            while (queue_size(context->group->emails) > 0) {
                email = queue_pop(context->group->emails);
                if (i > 0) {
                    if (!buffer_writef(context->buffer, "\n\n")) {
                        ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                        return CURL_READFUNC_ABORT;
                    }
                }

                if (!buffer_writef(context->buffer, "Subject: %s\n", email->subject)) {
                    ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                    return CURL_READFUNC_ABORT;
                }

                if (!buffer_writef(context->buffer, "%s", email->body)) {
                    ens_log(context->ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", context->group->id);
                    return CURL_READFUNC_ABORT;
                }

                ens_email_free(email);
            }

            break;
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

    for (i = 0; i < alist_size(group->to); i++) {
        to = curl_slist_append(to, alist_get(group->to, i));
    }

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, group->host);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, group->from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, to);
    if (group->username[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_USERNAME, group->username);
    }
    if (group->password[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, group->password);
    }
    if (ens->ca_path[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_CAINFO, ens->ca_path);
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
static void
ens_send_email_file(ens_t *ens, ens_group_t *group) {
    ens_email_t *email;
    time_t now;
    struct tm now_tm;
    char now_buf[32];
    unsigned int i = 0;

    now = time(NULL);
    localtime_r(&now, &now_tm);
    strftime(now_buf, sizeof(now_buf), "%Y-%m-%d %H:%M:%S", &now_tm);

    while (queue_size(group->emails) > 0) {
        email = queue_pop(group->emails);

        fprintf(group->f, "%s[%s]\n", i > 0 ? "\n" : "", now_buf);
        fprintf(group->f, "%s\n\n", email->subject);
        fprintf(group->f, "%s\n", email->body);

        ens_email_free(email);
        ++i;
    }
}

static void
ens_check_groups(ens_t *ens) {
    ens_group_t *group;
    unsigned int i;
    time_t now;

    pthread_mutex_lock(&ens->groups_mutex);
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        now = time(NULL);
        if (now >= group->expires && queue_size(group->emails) > 0) {
            if (group->f != NULL) {
                ens_send_email_file(ens, group);
            }
            else {
                ens_send_email(ens, group);
            }

            group->expires = now + group->interval;
        }
    }
    pthread_mutex_unlock(&ens->groups_mutex);
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
    ens_group_t *group;
    unsigned int i;

    if (ens->running) {
        return ENS_ERROR_ALREADY_RUNNING;
    }

    //if any groups are writing to a file, open them now
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        if (group->f_path[0] != '\0') {
            group->f = fopen(group->f_path, "w");
            if (group->f == NULL) {
                ret = ens_log(ens, ENS_ERROR_FILE_OPEN, ENS_LOG_LEVEL_ERROR, "Failed to open file: %s", strerror(errno));
                break;
            }
        }
    }

    //start the context's thread
    if (ret == ENS_ERROR_OK) {
        if (pthread_create(&ens->thread, NULL, ens_process, ens) != 0) {
            ret = ens_log(ens, ENS_ERROR_THREAD, ENS_LOG_LEVEL_FATAL, "Failed to start the thread: %s", strerror(errno));
        }
    }

    //stay squeaky clean...
    if (ret != ENS_ERROR_OK) {
        for (i = 0; i < alist_size(ens->groups); i++) {
            group = alist_get(ens->groups, i);

            if (group->f != NULL) {
                fclose(group->f);
                group->f = NULL;
            }
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

    pthread_mutex_lock(&ens->groups_mutex);
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
    pthread_mutex_unlock(&ens->groups_mutex);

    return ret;
}

int
ens_group_unregister(ens_t *ens, ens_group_id_t id) {
    bool found = false;
    ens_group_t *group;
    unsigned int i;

    pthread_mutex_lock(&ens->groups_mutex);
    for (i = 0; i < alist_size(ens->groups); i++) {
        group = alist_get(ens->groups, i);

        if (group->id == id) {
            alist_remove(ens->groups, i);
            ens_group_free(group);
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&ens->groups_mutex);

    if (!found) {
        return ens_log(ens, ENS_ERROR_NOT_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to unregister group %d: Not registered", id);
    }

    return ENS_ERROR_OK;
}

int
ens_group_send(ens_t *ens, ens_group_id_t id, const char *subject, const char *body) {
    int ret = ENS_ERROR_OK;
    time_t now;
    ens_group_t *group;
    ens_email_t *email;

    email = ens_email_init();
    if (email == NULL) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
        return ret;
    }

    email->subject = strdup(subject);
    email->body = strdup(body);

    pthread_mutex_lock(&ens->groups_mutex);
    group = ens_group_find(ens, id);
    if (group == NULL) {
        ret = ens_log(ens, ENS_ERROR_NOT_REGISTERED, ENS_LOG_LEVEL_ERROR, "Failed to send email for group %d: Not registered", id);
        goto done;
    }

    now = time(NULL);
printf("%d, %ld %ld %u\n", group->id, now, group->expires, queue_size(group->emails));
    if (group->mode == ENS_GROUP_MODE_DROP && queue_size(group->emails) > 0) {
        ret = ENS_ERROR_NOT_READY;
        goto done;
    }

    if (!queue_push(group->emails, email)) {
        ret = ens_log(ens, ENS_ERROR_MEMORY, ENS_LOG_LEVEL_FATAL, "Failed to send email for group %d: Out of memory", id);
        goto done;
    }

done:
    pthread_mutex_unlock(&ens->groups_mutex);
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

static int
ens_set_option_ca_path(ens_t *ens, va_list ap) {
    const char *ca_path;

    ca_path = va_arg(ap, const char *);

    if (strlen(ca_path) > ENS_PATH_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_OPTION_CA_PATH: Value must not exceed %d characters", ENS_PATH_MAX_LEN);
    }

    strcpy(ens->ca_path, ca_path);

    return ENS_ERROR_OK;
}

int
ens_set_option(ens_t *ens, ens_option_t option, ...) {
    int ret = ENS_ERROR_OK;
    va_list ap;

    va_start(ap, option);

    switch (option) {
        case ENS_OPTION_LOG_FUNCTION:
            ret = ens_set_option_log_function(ens, ap);
            break;
        case ENS_OPTION_LOG_LEVEL:
            ret = ens_set_option_log_level(ens, ap);
            break;
        case ENS_OPTION_LOG_USER_DATA:
            ret = ens_set_option_log_user_data(ens, ap);
            break;
        case ENS_OPTION_CA_PATH:
            ret = ens_set_option_ca_path(ens, ap);
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
            group->mode = mode;
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

    snprintf(group->host, sizeof(group->host), "smtp://%s", host);

    return ENS_ERROR_OK;
}


static int
ens_group_set_option_from(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *from;

    from = va_arg(ap, const char *);

    if (strlen(from) > ENS_FROM_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_FROM for group %d: Value must not exceed %d characters", group->id, ENS_FROM_MAX_LEN);
    }

    strcpy(group->from, from);

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

    if (!alist_add(group->to, to_copy)) {
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

    strcpy(group->username, username);

    return ENS_ERROR_OK;
}

static int
ens_group_set_option_password(ens_t *ens, ens_group_t *group, va_list ap) {
    const char *password;

    password = va_arg(ap, const char *);

    if (strlen(password) > ENS_PASSWORD_MAX_LEN) {
        return ens_log(ens, ENS_ERROR_TOO_LONG, ENS_LOG_LEVEL_ERROR, "Failed to set option ENS_GROUP_OPTION_PASSWORD for group %d: Value must not exceed %d characters", group->id, ENS_PASSWORD_MAX_LEN);
    }

    strcpy(group->password, password);

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

int
ens_group_set_option(ens_t *ens, ens_group_id_t id, ens_group_option_t option, ...) {
    int ret = ENS_ERROR_OK;
    ens_group_t *group;
    va_list ap;

    va_start(ap, option);
    pthread_mutex_lock(&ens->groups_mutex);

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
            group->interval = va_arg(ap, int);
            break;
        case ENS_GROUP_OPTION_FILE:
            ret = ens_group_set_option_file(ens, group, ap);
            break;
        default:
            ret = ens_log(ens, ENS_ERROR_UNKNOWN_OPTION, ENS_LOG_LEVEL_ERROR, "Failed to set option for group %d: Option %d not found", id, option);
            break;
    }

done:
    pthread_mutex_unlock(&ens->groups_mutex);
    va_end(ap);

    return ret;
}
