# libens
Email Notification System

A library that provides application level email logging. The main goal of
this library is to provide functionality to programmers for sending log
type emails in their programs. The library handles the logic which
determines  when emails should be sent and can operate in two modes as
described in the next section.

ENS is configured by groupings. Each group has their own set of characteristics so emails can be controlled in each group individually.

---------------------------------------------------------------------------
ENS_GROUP_MODE_DROP
---------------------------------------------------------------------------
Sends an email at most, every "interval" seconds at which the group is
configured. Any email that is attempted to be sent before the interval has
expired is ingored.

---------------------------------------------------------------------------
ENS_GROUP_MODE_COLLECT
---------------------------------------------------------------------------
Sends an email at most, every "interval" seconds at which the group is
configured. Any email that is attempted to be sent before the interval has
expired is queued and when the timer expires, an email is sent with all the
queued emails concatenated into a single email.

---------------------------------------------------------------------------

### Prerequisites
ENS uses libcurl internally to send emails.

### Installing
```
make
make install
```

## Linkage
gcc -o myprogram -lens mysource.c

## Examples
Send an email only every 10 seconds

```
int main(int argc, char **argv) {
    ens_t *ens;

    ens = ens_init();`
    ens_group_register(ens, 1);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_MODE, ENS_GROUP_MODE_DROP);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_HOST, "smtp.server.com:587");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_FROM, "scott.newman50@gmail.com");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_TO, "some.email@domain.com");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_INTERVAL, 10);

    while (/** doing work **/) {
        if (/** some condition **/) {
            ens_group_send(ens, 1, "An error occured", "Some error happened and here's an email.");
        }
    }
```

Send an email only every 10 seconds. However, any email that was attempted to be sent before the timeout expires is queued and concatenated into one big email.

```
int main(int argc, char **argv) {
    ens_t *ens;

    ens = ens_init();`
    ens_group_register(ens, 1);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_COLLECT, ENS_GROUP_MODE_DROP);
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_HOST, "smtp.server.com:587");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_FROM, "scott.newman50@gmail.com");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_TO, "some.email@domain.com");
    ens_group_set_option(ens, 1, ENS_GROUP_OPTION_INTERVAL, 10);

    while (/** doing work **/) {
        if (/** some condition **/) {
            ens_group_send(ens, 1, "An error occured", "Some error happened and here's an email.");
        }
    }
	
    ens_free(ens);
    return 0;
}

```
