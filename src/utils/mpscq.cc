/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/mpscq.h"
#include <assert.h>

namespace connx {

MultiProducerSingleConsumerQueue::MultiProducerSingleConsumerQueue()
    : newest_{&stub_}
    , oldest_(&stub_) {}

MultiProducerSingleConsumerQueue::~MultiProducerSingleConsumerQueue() {
    assert(newest_.Load() == &stub_);
    assert(oldest_ == &stub_);
}

bool MultiProducerSingleConsumerQueue::push(Node* node) {
    // Oldest -> prev -> newest(node) -> null
    node->next.Store(nullptr);
    Node* prev = newest_.Exchange(node, MemoryOrder::ACQ_REL);
    prev->next.Store(node, MemoryOrder::RELEASE);
    return prev == &stub_;
}

MultiProducerSingleConsumerQueue::Node* MultiProducerSingleConsumerQueue::pop() {
    bool empty = false;
    return PopAndCheckEnd(&empty);
}

MultiProducerSingleConsumerQueue::Node*
MultiProducerSingleConsumerQueue::PopAndCheckEnd(bool* empty) {
    Node* tail = oldest_;
    Node* obj = oldest_->next.Load(MemoryOrder::ACQUIRE);
    if (tail == &stub_) {
        // first to pop, first element is in stub_->next
        // the list is empty
        if (obj == nullptr) {
            *empty = true;
            return nullptr;
        }
        // Get the earliest inserted node,
        // and move oldest_ pointer
        oldest_ = obj;
        tail = obj;
        obj = tail->next.Load(MemoryOrder::ACQUIRE);
    }
    // else // Not the first to pop

    // tail is the last one (oldest)
    // obj is the penultimate

    if (obj != nullptr) {
        *empty = false;
        oldest_ = obj;
        return tail;
    }

    // reach the end of the list
    Node* head = newest_.Load(MemoryOrder::ACQUIRE);
    if (tail != head) {
        *empty = false;
        // we're still adding
        return nullptr;
    }
    // only one element left in the list
    // we need to reinitialize the list
    push(&stub_);
    obj = tail->next.Load(MemoryOrder::ACQUIRE);
    if (obj != nullptr) {
        *empty = false;
        oldest_ = obj;
        return tail;
    }
    // maybe? we're still adding
    *empty = false;
    return nullptr;
}

} // namespace connx
