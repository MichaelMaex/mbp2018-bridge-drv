#ifndef BCEDRIVER_TRANSFER_H
#define BCEDRIVER_TRANSFER_H

#include <linux/usb.h>
#include "queue.h"
#include "command.h"
#include "../queue.h"

struct bce_vhci_list_message {
    struct list_head list;
    struct bce_vhci_message msg;
};
struct bce_vhci_transfer_queue {
    struct bce_vhci *vhci;
    struct usb_host_endpoint *endp;
    enum bce_vhci_endpoint_state state;
    bool active;
    bce_vhci_device_t dev_addr;
    u8 endp_addr;
    struct bce_queue_cq *cq;
    struct bce_queue_sq *sq_in;
    struct bce_queue_sq *sq_out;
    struct list_head evq;
    struct spinlock urb_lock;
    struct list_head giveback_urb_list;

    struct work_struct w_reset;
};
enum bce_vhci_urb_state {
    BCE_VHCI_URB_INIT_PAUSED,

    BCE_VHCI_URB_WAITING_FOR_TRANSFER_REQUEST,
    BCE_VHCI_URB_WAITING_FOR_COMPLETION,
    BCE_VHCI_URB_DATA_TRANSFER_COMPLETE,

    BCE_VHCI_URB_CONTROL_WAITING_FOR_SETUP_REQUEST,
    BCE_VHCI_URB_CONTROL_WAITING_FOR_SETUP_COMPLETION,
    BCE_VHCI_URB_CONTROL_COMPLETE
};
struct bce_vhci_urb {
    struct urb *urb;
    struct bce_vhci_transfer_queue *q;
    enum dma_data_direction dir;
    bool is_control;
    enum bce_vhci_urb_state state;
    int received_status;
    u32 send_offset;
    u32 receive_offset;
};

struct bce_vhci_transfer_queue_urb_cancel_work {
    struct work_struct ws;
    struct bce_vhci_transfer_queue *q;
    struct urb *urb;
    int status;
};

void bce_vhci_create_transfer_queue(struct bce_vhci *vhci, struct bce_vhci_transfer_queue *q,
        struct usb_host_endpoint *endp, bce_vhci_device_t dev_addr, enum dma_data_direction dir);
void bce_vhci_destroy_transfer_queue(struct bce_vhci *vhci, struct bce_vhci_transfer_queue *q);
void bce_vhci_transfer_queue_event(struct bce_vhci_transfer_queue *q, struct bce_vhci_message *msg);
int bce_vhci_transfer_queue_pause(struct bce_vhci_transfer_queue *q);
int bce_vhci_transfer_queue_resume(struct bce_vhci_transfer_queue *q);

int bce_vhci_urb_create(struct bce_vhci_transfer_queue *q, struct urb *urb);
int bce_vhci_urb_request_cancel(struct bce_vhci_transfer_queue *q, struct urb *urb, int status);

#endif //BCEDRIVER_TRANSFER_H
