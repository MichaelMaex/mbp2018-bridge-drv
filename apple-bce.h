#pragma once

#include <linux/pci.h>
#include <linux/spinlock.h>
#include "mailbox.h"
#include "queue.h"
#include "vhci/vhci.h"

#define BC_PROTOCOL_VERSION 0x20001
#define BCE_MAX_QUEUE_COUNT 0x100

#define BCE_QUEUE_USER_MIN 2
#define BCE_QUEUE_USER_MAX (BCE_MAX_QUEUE_COUNT - 1)

struct apple_bce_device {
    struct pci_dev *pci;
    dev_t devt;
    struct device *dev;
    void __iomem *reg_mem_mb;
    void __iomem *reg_mem_dma;
    struct bce_mailbox mbox;
    struct bce_timestamp timestamp;
    struct bce_queue *queues[BCE_MAX_QUEUE_COUNT];
    struct ida queue_ida;
    struct bce_queue_cq *cmd_cq;
    struct bce_queue_cmdq *cmd_cmdq;
    struct bce_queue_sq *int_sq_list[BCE_MAX_QUEUE_COUNT];
    bool is_being_removed;

    struct bce_vhci vhci;
};

extern struct apple_bce_device *global_bce;
