#pragma once

/**
 * @file ens.h
 * @author Scott Newman
 *
 * @brief Email Notification System.
 *
 * A library that provides application level email logging. The main goal of
 * this library is to provide functionality to programmers for sending log
 * type emails in their programs. The library handles the logic which
 * determines  when emails should be sent and can operate in two modes as
 * described below:
 *
 * ---------------------------------------------------------------------------
 * ENS_GROUP_MODE_DROP
 * ---------------------------------------------------------------------------
 * Sends an email at most, every interval seconds at which the group is
 * configured. Any email that is attempted to be sent before the interval has
 * expired is ingored.
 *
 * ---------------------------------------------------------------------------
 * ENS_GROUP_MODE_COLLECT
 * ---------------------------------------------------------------------------
 * Sends an email at most, every interval seconds at which the group is
 * configured. Any email that is attempted to be sent before the interval has
 * expired is queued and when the timer expires, an email is sent with all the
 * queued emails concatenated into a single email.
 * ---------------------------------------------------------------------------
 */

/**
 * Error codes.
 */
#define ENS_ERROR_NOT_READY            (-1) //!< The group's interval hasn't expired yet.
#define ENS_ERROR_OK                   0    //!< The operation completed successfully.
#define ENS_ERROR_MEMORY               1    //!< A dynamic memory allocation failed.
#define ENS_ERROR_ALREADY_REGISTERED   2    //!< The group is already registered.
#define ENS_ERROR_NOT_REGISTERED       3    //!< The group is not registered.
#define ENS_ERROR_ALREADY_RUNNING      4    //!< The ENS context's thread is already running.
#define ENS_ERROR_NOT_RUNNING          5    //!< The ENG context's thread is not running.
#define ENS_ERROR_UNKNOWN_OPTION       6    //!< Unknown option.
#define ENS_ERROR_UNKNOWN_OPTION_VALUE 7    //!< Unknown option.
#define ENS_ERROR_EMAIL_FAILED         8    //!< The email failed to send.
#define ENS_ERROR_TOO_LONG             9    //!< A value for an option was too long.
#define ENS_ERROR_FILE_OPEN            10   //!< The group is writing to a file but the file couldn't be opened.
#define ENS_ERROR_THREAD               11   //!< The ENS context's thread couldn't be started.

/**
 * Log levels.
 */
#define ENS_LOG_LEVEL_NONE  0   //!< No logging.
#define ENS_LOG_LEVEL_FATAL 1   //!< Fatal logging.
#define ENS_LOG_LEVEL_ERROR 2   //!< Error logging.
#define ENS_LOG_LEVEL_WARN  3   //!< Warn logging.
#define ENS_LOG_LEVEL_INFO  4   //!< Info logging.

/**
 * Modes for the groups.
 */
#define ENS_GROUP_MODE_DROP    0                    //<! Drop messages between the interval.
#define ENS_GROUP_MODE_COLLECT 1                    //!< Collect messages between the interval.
#define ENS_GROUP_MODE_DEFAULT ENS_GROUP_MODE_DROP  //!< The default mode.

#define ENS_GROUP_INTERVAL_DEFAULT 30   //!< The default interval.

/**
 * The ENS context.
 */
typedef struct ens_t ens_t;

/**
 * The group ID type.
 */
typedef int ens_group_id_t;

/**
 * The ENS context's log function type.
 *
 * @param[in] level The log level.
 * @param[in] msg The log message.
 * @param[in] user_data The user's data provided to the log function.
 */
typedef void (*ens_log_function_t)(int level, const char *msg, void *user_data);

/**
 * Options that effect the entire ENS context.
 */ 
typedef enum {
    ENS_OPTION_LOG_FUNCTION,    //!< Sets a callback function to for logging.
    ENS_OPTION_LOG_LEVEL,       //!< Sets the maximum logging level for the
                                //!< logging function.
    ENS_OPTION_LOG_USER_DATA,   //!< Sets user data for the logging function.
    ENS_OPTION_CA_PATH          //!< Sets the path for the certificate
                                //!< authority.
} ens_option_t;

/**
 * Options that effect the group within the ENS context.
 */
typedef enum {
    ENS_GROUP_OPTION_MODE,      //!< Sets the mode that the group operates in.
    ENS_GROUP_OPTION_HOST,      //!< Sets the SMTP host for this group.
    ENS_GROUP_OPTION_FROM,      //!< Sets who the emails are coming from for
                                //!< this group.
    ENS_GROUP_OPTION_TO,        //!< Sets who the emails are going to for this
                                //!< group.
    ENS_GROUP_OPTION_USERNAME,  //!< Sets the SMTP username credentials for
                                //!< this group.
    ENS_GROUP_OPTION_PASSWORD,  //!< Sets the SMTP password credentials for
                                //!< this group.
    ENS_GROUP_OPTION_INTERVAL,  //!< Sets the interval and which emails are
                                //!< sent for this group.
    ENS_GROUP_OPTION_FILE       //!< Sets the file path to write emails to
                                //!< instead of sending them.
} ens_group_option_t;

/**
 * @brief Returns the major version of the library.
 *
 * @return The major version.
 */
int ens_version_major();

/**
 * @brief Returns the minor version of the library.
 *
 * @return The minor version.
 */
int ens_version_minor();

/**
 * @brief Returns the patch version of the library.
 *
 * @return The patch version.
 */
int ens_version_patch();

/**
 * @brief Initializes an ENS context for use.
 *
 * Allocates and initializes an ENS context for use with future library
 * functions. This function must be called before any other library functions
 * are called, with the except of ens_version_major(), ens_version_minor(), and
 * ens_version_patch(). Memory regions for sensitive data such as username and
 * password are placed in protected memory so they do not get swapped to disk.
 *
 * @return An ENS context.
 */
ens_t * ens_init();

/**
 * @brief Frees the ENS context.
 *
 * Deallocates and uninitalizes an ENS context. This should be the last library
 * function called for this context. The memory in the ENS context is zero'd
 * before returning it to the operating system.
 *
 * @param[in] ens The ENS context.
 */
void ens_free(ens_t *ens);

/**
 * @brief Start the ENS context for use.
 *
 * Starts the ENS context's thread that handles and sends emails.
 * 
 * If any groups are writing emails to files, they're opened here.
 *
 * @param[in] ens The ENS context.
 * @return ENS_ERROR_OK: The ENS context was started successfully.
 *         ENS_ERROR_ALREADY_RUNNING: The ENS context is already running.
 *         ENS_ERROR_FILE: A group is writing emails to a file and the file
 *                         couldn't be opened.
 *         ENS_ERROR_THREAD: The thread could not be started.
 */
int ens_start(ens_t *ens);

/**
 * @brief Stops the ENS context.
 *
 * Stops the ENS context's thread so no more emails will be sent out. Any
 * emails currently queued are not sent. This function returns immediately and
 * does not wait for the thread to shutdown. See ens_stop_join() to wait for
 * the thread to stop.
 *
 * If any groups are writing emails to files, they're closed here.
 *
 * @param[in] ens The ENS context.
 * @return ENS_ERROR_OK: The ENS context was stopped successfully.
 *         ENS_ERROR_NOT_RUNNING: The ENS context is not running.
 */
int ens_stop(ens_t *ens);

/**
 * @brief Stops the ENS context and waits for the context's thread to stop.
 *
 * Does the same thing as ens_stop() but also waits for the context's thread
 * to finish.
 *
 * @param[in] ens The ENS context.
 * @return ENS_ERROR_OK: The ENS context was stopped successfully.
 *         ENS_ERROR_NOT_RUNNING: The ENS context is not running; nothing to
 */
int ens_stop_join(ens_t *ens);

/**
 * @brief Registers an email group within this ENS context.
 *
 * Registers an ENS group identified by <tt>id</tt>. This ID is simply a unique
 * identifier to be used with other ens_group_* commands and can be any ID you
 * choose.
 *
 * @param[in] ens The ENS context.
 * @param[in] id The group ID to register.
 * @return ENS_ERROR_OK: The group was registered successfully.
 *         ENS_ERROR_MEMORY: Memory allocation failed.
 *         ENS_ERROR_ALREADY_REGISTERED: The group ID is already registered.
 */
int ens_group_register(ens_t *ens, ens_group_id_t id);

/**
 * @brief Unregisters an email group within this ENS context.
 *
 * Unregisters the ENS group identified by <tt>id</tt>. Any emails currently
 * queued are not sent.
 *
 * @param[in] ens The ENS context.
 * @param[in] id The group ID to unregister.
 * @return ENS_ERROR_OK: The group was unregistered successfully.
 *         ENS_ERROR_NOT_REGISTERED: The group ID is not registered.
 */
int ens_group_unregister(ens_t *ens, ens_group_id_t id);

/**
 * @brief Queues an email for the group within this ENS context.
 *
 * Queues an email for the group identified by <tt>id</tt> within this ENS
 * context. Exactly what happens with the email depends upon what mode the
 * group is in.
 *
 * @param[in] ens The ENS context.
 * @param[in] id The group ID to queue an email for.
 * @param[in] subject The subject of the email.
 * @param[in] body The body of the email.
 * @return ENS_ERROR_OK The email was queued succesfully.
 *         ENS_ERROR_MEMORY: Memory allocation failed.
 *         ENS_ERROR_NOT_REGISTERED: The group is not registered.
 *         ENS_ERROR_NOT_READY: The email was not queued because the group's
 *                              mode is ENS_GROUP_MODE_DROP and its timeout
 *                              has not expired yet.
 */
int ens_group_send(ens_t *ens, ens_group_id_t id, const char *subject, const char *body);

/**
 * @brief Queues an email for the group within this ENS context.
 *
 * Does the same thing as ens_group_send() but lets you specify and printf()
 * styled formatted string for the body of the email.
 *
 * @param[in] ens The ENS context.
 * @param[in] id The group ID to queue an email for.
 * @param[in] subject The subject of the email.
 * @param[in] fmt The printf() styled format string for the body of the email.
 * @return ENS_ERROR_OK The email was queued succesfully.
 *         ENS_ERROR_MEMORY: Memory allocation failed.
 *         ENS_ERROR_NOT_REGISTERED: The group is not registered.
 *         ENS_ERROR_NOT_READY: The email was not queued because the group's
 *                              mode is ENS_GROUP_MODE_DROP and its timeout
 *                              has not expired yet.

 */
int ens_group_sendf(ens_t *ens, ens_group_id_t id, const char *subject, const char *fmt, ...);

/**
 * @brief Set an option for this ENS context.
 *
 * Sets an option for the ENS context. All groups within this ENS context are
 * affected by the option.
 *
 * @param[in] ens The ENS context.
 * @param[in] option The option.
 * @param[in] ... Any arguments for the option.
 * @return ENS_ERROR_OK: The option was set successfully.
 *         ENS_ERROR_UNKNOWN_OPTION_VALUE: An unknown option was supplied.
 *         Various other others.
 */
int ens_set_option(ens_t *ens, ens_option_t option, ...);

/**
 * @brief Set an option for the group identified by <tt>id</tt> witin this ENS
 * context.
 *
 * Sets an option for the group within this ENS context.
 *
 * @param[in] ens The ENS context
 * @param[in] id The group ID to set the option for.
 * @param[in] option The option.
 * @param[in] ... Any arguments for the option.
 * @return ENS_ERROR_OK: The option was set successfully.
 *         ENS_ERROR_UNKNOWN_OPTION_VALUE: An unknown option was supplied.
 *         Various other others.
 */
int ens_group_set_option(ens_t *ens, ens_group_id_t id, ens_group_option_t option, ...);
