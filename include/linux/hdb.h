/*
 * Definitions for HDB (Apple Desktop Bus) support.
 */
#ifndef __HDB_H
#define __HDB_H

/* HDB commands */
#define HDB_BUSRESET        0
#define HDB_FLUSH(id)       (0x01 | ((id) << 4))
#define HDB_WRITEREG(id, reg)   (0x08 | (reg) | ((id) << 4))
#define HDB_READREG(id, reg)    (0x0C | (reg) | ((id) << 4))

/* HDB default device IDs (upper 4 bits of HDB command byte) */
#define HDB_DONGLE  1   /* "software execution control" devices */
#define HDB_KEYBOARD    2
#define HDB_MOUSE   3
#define HDB_TABLET  4
#define HDB_MODEM   5
#define HDB_MISC    7   /* maybe a monitor */

#define HDB_RET_OK  0
#define HDB_RET_TIMEOUT 3

/* The kind of HDB request. The controller may emulate some
   or all of those CUDA/PMU packet kinds */
#define HDB_PACKET  0
#define CUDA_PACKET 1
#define ERROR_PACKET    2
#define TIMER_PACKET    3
#define POWER_PACKET    4
#define MACIIC_PACKET   5
#define PMU_PACKET  6
#define HDB_QUERY   7

/* HDB queries */

/* HDB_QUERY_GETDEVINFO
 * Query HDB slot for device presence
 * data[2] = id, rep[0] = orig addr, rep[1] = handler_id
 */
#define HDB_QUERY_GETDEVINFO    1

#ifdef __KERNEL__

struct hdb_request {
    unsigned char data[32];
    int nbytes;
    unsigned char reply[32];
    int reply_len;
    unsigned char reply_expected;
    unsigned char sent;
    unsigned char complete;
    void (*done)(struct hdb_request *);
    void *arg;
    struct hdb_request *next;
};

struct hdb_ids {
    int nids;
    unsigned char id[16];
};

/* Structure which encapsulates a low-level HDB driver */

struct hdb_driver {
    char name[16];
    int (*probe)(void);
    int (*init)(void);
    int (*send_request)(struct hdb_request *req, int sync);
    int (*autopoll)(int devs);
    void (*poll)(void);
    int (*reset_bus)(void);
};

/* Values for hdb_request flags */
#define HDBREQ_REPLY    1   /* expect reply */
#define HDBREQ_SYNC 2   /* poll until done */
#define HDBREQ_NOSEND   4   /* build the request, but don't send it */

/* Messages sent thru the client_list notifier. You should NOT stop
   the operation, at least not with this version */
enum hdb_message {
    HDB_MSG_POWERDOWN,  /* Currently called before sleep only */
    HDB_MSG_PRE_RESET,  /* Called before resetting the bus */
    HDB_MSG_POST_RESET  /* Called after resetting the bus (re-do init & register) */
};
extern struct blocking_notifier_head hdb_client_list;

int hdb_request(struct hdb_request *req, void (*done)(struct hdb_request *),
        int flags, int nbytes, ...);
int hdb_register(int default_id,int handler_id,struct hdb_ids *ids,
         void (*handler)(unsigned char *, int, int));
int hdb_unregister(int index);
void hdb_poll(void);
void hdb_input(unsigned char *, int, int);
int hdb_reset_bus(void);

int hdb_try_handler_change(int address, int new_id);
int hdb_get_infos(int address, int *original_address, int *handler_id);

#endif /* __KERNEL__ */

#endif /* __HDB_H */
