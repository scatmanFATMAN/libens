#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ens.h>

/**
 * To run the test, create a file called "test.conf" with the following
 * structure:
 * 
 * host=<SMTP server>
 * email=<email to send to and from>
 * username=<user credentials>
 * password=<user credentials>
 * ca_path=</path/to/ca/certs>
 */

void
ens_log(int level, const char *msg, void *user_data) {
    printf("%s\n", msg);
}

static bool
read_config(char *host, char *email, char *username, char *password, char *ca_path) {
    char line[256], *key, *value;
    FILE *f;

    f = fopen("test.conf", "r");
    if (f == NULL) {
        fprintf(stderr, "Error opening test.conf: %s\n", strerror(errno));
        return false;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        key = strtok(line, " =");
        value = strtok(NULL, "\n");

        if (strcmp(key, "host") == 0) {
            strcpy(host, value);
        }
        else if (strcmp(key, "email") == 0) {
            strcpy(email, value);
        }
        else if (strcmp(key, "username") == 0) {
            strcpy(username, value);
        }
        else if (strcmp(key, "password") == 0) {
            strcpy(password, value);
        }
        else if (strcmp(key, "ca_path") == 0) {
            strcpy(ca_path, value);
        }
        else {
            fprintf(stderr, "Invalid key '%s' in test.conf\n", key);
        }
    }

    fclose(f);
    return true;
}

int
main(int argc, char **argv) {
    char host[64], email[64], username[64], password[64], ca_path[64];
    ens_t *ens;

    printf("ENS version %d.%d.%d\n", ens_version_major(), ens_version_minor(), ens_version_patch());

    if (!read_config(host, email, username, password, ca_path)) {
        return 1;
    }

    ens = ens_init();
    if (ens == NULL) {
        printf("Failed to initialize ENS\n");
        exit(EXIT_FAILURE);
    }

    ens_set_option(ens, ENS_OPTION_LOG_FUNCTION, ens_log);
    ens_set_option(ens, ENS_OPTION_CA_PATH, ca_path);

    ens_group_register(ens, 1);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_MODE, ENS_GROUP_MODE_COLLECT);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_HOST, host);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_FROM, email);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_TO, email);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_USERNAME, username);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_PASSWORD, password);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_INTERVAL, 5);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_FILE, "email_1.txt");

    ens_group_register(ens, 2);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_MODE, ENS_GROUP_MODE_DROP);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_HOST, host);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_FROM, email);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_TO, email);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_USERNAME, username);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_PASSWORD, password);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_INTERVAL, 5);
    ens_group_set_option(ens, 2, ENS_GROUP_OPTION_FILE, "email_2.txt");

    if (ens_start(ens) != ENS_ERROR_OK) {
        printf("Failed to start\n");
        exit(EXIT_FAILURE);
    }

    printf("Sending group 1 (collect) email, which should be the only email for the next 5 seconds\n");
    ens_group_send(ens, 1, "Group 1: Collect", "This should be the only email for the next 5 secoods");

    printf("Sending group 2 (drop) email and 5 more right after. Only the first email should go through\n");
    ens_group_send(ens, 2, "Group 2: Drop", "This should be the only email even after attempting to send a few more immediately since they're being dropped");
    ens_group_send(ens, 2, "Group 2: Drop", "drop me");
    ens_group_send(ens, 2, "Group 2: Drop", "drop me");
    ens_group_send(ens, 2, "Group 2: Drop", "drop me");
    ens_group_send(ens, 2, "Group 2: Drop", "drop me");
    ens_group_send(ens, 2, "Group 2: Drop", "drop me");

    usleep(1000 * 3000);
    printf("Sending group 1 (collection) 3 emails, which should all be concatenated into a single email\n");
    ens_group_send(ens, 1, "Group 1: Collect", "There should be 3 emails in this group now. This is email 1");
    ens_group_send(ens, 1, "Group 1: Collect", "There should be 3 emails in this group now. This is email 2");
    ens_group_send(ens, 1, "Group 1: Collect", "There should be 3 emails in this group now. This is email 3");

    usleep(1000 * 10000);
    ens_stop_join(ens);
    
    ens_free(ens);

    return 0;
}
