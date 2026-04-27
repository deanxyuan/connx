/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_MPSCQ_H_
#define CONNX_SRC_UTILS_MPSCQ_H_

#include "src/utils/atomic.h"

namespace connx {

class MultiProducerSingleConsumerQueue final {
public:
    struct Node {
        /* data */
        Atomic<Node*> next;
    };
    MultiProducerSingleConsumerQueue();
    ~MultiProducerSingleConsumerQueue();

    // Return true if it's the first element,
    // otherwise return false;
    bool push(Node* node);

    Node* pop();
    Node* PopAndCheckEnd(bool* empty);

private:
    union {
        char padding[64];
        Atomic<Node*> newest_;
    };
    Node* oldest_;
    Node stub_;
};

} // namespace connx
#endif // CONNX_SRC_UTILS_MPSCQ_H_
