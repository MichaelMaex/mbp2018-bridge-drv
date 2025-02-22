#include "queue.h"
#include "vhci.h"
#include "../apple_bce.h"


static void bce_vhci_message_queue_completion(struct bce_queue_sq *sq);

int bce_vhci_message_queue_create(struct bce_vhci *vhci, struct bce_vhci_message_queue *ret, const char *name)
{
    int status;
    ret->cq = bce_create_cq(vhci->dev, VHCI_EVENT_QUEUE_EL_COUNT);
    if (!ret->cq)
        return -EINVAL;
    ret->sq = bce_create_sq(vhci->dev, ret->cq, name, VHCI_EVENT_QUEUE_EL_COUNT, DMA_TO_DEVICE,
                            bce_vhci_message_queue_completion, ret);
    if (!ret->sq) {
        status = -EINVAL;
        goto fail_cq;
    }
    ret->data = dma_alloc_coherent(&vhci->dev->pci->dev, sizeof(struct bce_vhci_message) * VHCI_EVENT_QUEUE_EL_COUNT,
                                   &ret->dma_addr, GFP_KERNEL);
    if (!ret->data) {
        status = -EINVAL;
        goto fail_sq;
    }
    return 0;

fail_sq:
    bce_destroy_sq(vhci->dev, ret->sq);
    ret->sq = NULL;
fail_cq:
    bce_destroy_cq(vhci->dev, ret->cq);
    ret->cq = NULL;
    return status;
}

void bce_vhci_message_queue_destroy(struct bce_vhci *vhci, struct bce_vhci_message_queue *q)
{
    if (!q->cq)
        return;
    dma_free_coherent(&vhci->dev->pci->dev, sizeof(struct bce_vhci_message) * VHCI_EVENT_QUEUE_EL_COUNT,
                      q->data, q->dma_addr);
    bce_destroy_sq(vhci->dev, q->sq);
    bce_destroy_cq(vhci->dev, q->cq);
}

void bce_vhci_message_queue_write(struct bce_vhci_message_queue *q, struct bce_vhci_message *req)
{
    int sidx;
    struct bce_qe_submission *s;
    sidx = q->sq->tail;
    s = bce_next_submission(q->sq);
    pr_debug("bce-vhci: Send message: %x s=%x p1=%x p2=%llx\n", req->cmd, req->status, req->param1, req->param2);
    q->data[sidx] = *req;
    bce_set_submission_single(s,q->dma_addr + sizeof(struct bce_vhci_message) * sidx,
            sizeof(struct bce_vhci_message));
    bce_submit_to_device(q->sq);
}

static void bce_vhci_message_queue_completion(struct bce_queue_sq *sq)
{
    while (bce_next_completion(sq))
        bce_notify_submission_complete(sq);
}



static void bce_vhci_event_queue_completion(struct bce_queue_sq *sq);

int __bce_vhci_event_queue_create(struct bce_vhci *vhci, struct bce_vhci_event_queue *ret, const char *name,
                                  bce_sq_completion compl)
{
    ret->vhci = vhci;

    ret->sq = bce_create_sq(vhci->dev, vhci->ev_cq, name, VHCI_EVENT_QUEUE_EL_COUNT, DMA_FROM_DEVICE, compl, ret);
    if (!ret->sq)
        return -EINVAL;
    ret->data = dma_alloc_coherent(&vhci->dev->pci->dev, sizeof(struct bce_vhci_message) * VHCI_EVENT_QUEUE_EL_COUNT,
                                   &ret->dma_addr, GFP_KERNEL);
    if (!ret->data) {
        bce_destroy_sq(vhci->dev, ret->sq);
        ret->sq = NULL;
        return -EINVAL;
    }

    bce_vhci_event_queue_submit_pending(ret, VHCI_EVENT_PENDING_COUNT);
    return 0;
}

int bce_vhci_event_queue_create(struct bce_vhci *vhci, struct bce_vhci_event_queue *ret, const char *name,
        bce_vhci_event_queue_callback cb)
{
    ret->cb = cb;
    return __bce_vhci_event_queue_create(vhci, ret, name, bce_vhci_event_queue_completion);
}

void bce_vhci_event_queue_destroy(struct bce_vhci *vhci, struct bce_vhci_event_queue *q)
{
    if (!q->sq)
        return;
    dma_free_coherent(&vhci->dev->pci->dev, sizeof(struct bce_vhci_message) * VHCI_EVENT_QUEUE_EL_COUNT,
                      q->data, q->dma_addr);
    bce_destroy_sq(vhci->dev, q->sq);
}

static void bce_vhci_event_queue_completion(struct bce_queue_sq *sq)
{
    struct bce_vhci_event_queue *ev = sq->userdata;
    struct bce_vhci_message *msg;
    size_t cnt = 0;

    while (bce_next_completion(sq)) {
        msg = &ev->data[sq->head];
        pr_debug("bce-vhci: Got event: %x s=%x p1=%x p2=%llx\n", msg->cmd, msg->status, msg->param1, msg->param2);
        ev->cb(ev, msg);

        bce_notify_submission_complete(sq);
        ++cnt;
    }
    bce_vhci_event_queue_submit_pending(ev, cnt);
}

void bce_vhci_event_queue_submit_pending(struct bce_vhci_event_queue *q, size_t count)
{
    int idx;
    struct bce_qe_submission *s;
    while (count--) {
        if (bce_reserve_submission(q->sq, NULL)) {
            pr_err("bce-vhci: Failed to reserve an event queue submission\n");
            break;
        }
        idx = q->sq->tail;
        s = bce_next_submission(q->sq);
        bce_set_submission_single(s,
                                  q->dma_addr + idx * sizeof(struct bce_vhci_message), sizeof(struct bce_vhci_message));
    }
    bce_submit_to_device(q->sq);
}


void bce_vhci_command_queue_create(struct bce_vhci_command_queue *ret, struct bce_vhci_message_queue *mq)
{
    ret->mq = mq;
    ret->completion.result = NULL;
    init_completion(&ret->completion.completion);
    spin_lock_init(&ret->completion_lock);
    mutex_init(&ret->mutex);
}

void bce_vhci_command_queue_destroy(struct bce_vhci_command_queue *cq)
{
    spin_lock(&cq->completion_lock);
    if (cq->completion.result) {
        memset(cq->completion.result, 0, sizeof(struct bce_vhci_message));
        cq->completion.result->status = BCE_VHCI_ABORT;
        complete(&cq->completion.completion);
        cq->completion.result = NULL;
    }
    spin_unlock(&cq->completion_lock);
    mutex_lock(&cq->mutex);
    mutex_unlock(&cq->mutex);
    mutex_destroy(&cq->mutex);
}

void bce_vhci_command_queue_deliver_completion(struct bce_vhci_command_queue *cq, struct bce_vhci_message *msg)
{
    struct bce_vhci_command_queue_completion *c = &cq->completion;

    spin_lock(&cq->completion_lock);
    if (c->result) {
        *c->result = *msg;
        complete(&c->completion);
        c->result = NULL;
    }
    spin_unlock(&cq->completion_lock);
}

static int bce_vhci_command_queue_cancel(struct bce_vhci_command_queue *cq, struct bce_vhci_message *req,
        struct bce_vhci_message *res);

int __bce_vhci_command_queue_execute(struct bce_vhci_command_queue *cq, struct bce_vhci_message *req,
        struct bce_vhci_message *res, unsigned long timeout)
{
    int status;
    struct bce_vhci_command_queue_completion *c;
    c = &cq->completion;

    if ((status = bce_reserve_submission(cq->mq->sq, &timeout)))
        return status;

    spin_lock(&cq->completion_lock);
    c->result = res;
    reinit_completion(&c->completion);
    spin_unlock(&cq->completion_lock);

    bce_vhci_message_queue_write(cq->mq, req);

    if (!wait_for_completion_timeout(&c->completion, timeout)) {
        /* we ran out of time, clean up info */
        spin_lock(&cq->completion_lock);
        c->result = NULL;
        spin_unlock(&cq->completion_lock);

        if (!(req->cmd & 0x4000))
            return bce_vhci_command_queue_cancel(cq, req, res);

        return -ETIMEDOUT;
    }

    if ((res->cmd & ~0xC000) != (req->cmd & ~0x4000)) {
        pr_err("bce-vhci: Possible desync, cmd reply mismatch req=%x, res=%x\n", req->cmd, res->cmd);
        return -EIO;
    }
    if (res->status == BCE_VHCI_SUCCESS)
        return 0;
    return res->status;
}

static int bce_vhci_command_queue_cancel(struct bce_vhci_command_queue *cq, struct bce_vhci_message *req,
        struct bce_vhci_message *res)
{
    int status;
    struct bce_vhci_message creq;
    creq = *req;
    creq.cmd |= 0x4000;
    status = __bce_vhci_command_queue_execute(cq, &creq, res, 1000);
    if (status == -ETIMEDOUT) {
        pr_err("bce-vhci: Possible desync, cancel timeout\n");
        return -ETIMEDOUT;
    }
    if (!(res->cmd & 0x4000)) /* The abort did not get through probably */
        return status;
    return -ETIMEDOUT;
}

int bce_vhci_command_queue_execute(struct bce_vhci_command_queue *cq, struct bce_vhci_message *req,
                                   struct bce_vhci_message *res, unsigned long timeout)
{
    int status;
    mutex_lock(&cq->mutex);
    status = __bce_vhci_command_queue_execute(cq, req, res, timeout);
    mutex_unlock(&cq->mutex);
    return status;
}
