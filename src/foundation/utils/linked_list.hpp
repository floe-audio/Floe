// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

template <typename NodeType, typename ShouldRemoveFunctionType, typename DeleteFunctionType>
PUBLIC inline void SinglyLinkedListRemoveIf(NodeType*& first,
                                            ShouldRemoveFunctionType&& should_remove,
                                            DeleteFunctionType&& delete_function) {
    NodeType* previous = nullptr;
    for (auto i = first; i != nullptr;) {
        ASSERT(i->next != i);
        ASSERT(previous != i);
        if (previous) ASSERT(previous != i->next);

        if (should_remove(*i)) {
            if (!previous)
                first = i->next;
            else
                previous->next = i->next;
            auto next = i->next;
            delete_function(i);
            i = next;
        } else {
            previous = i;
            i = i->next;
        }
    }
}

template <typename NodeType>
PUBLIC void SinglyLinkedListRemove(NodeType*& head, NodeType* node, NodeType* previous) {
    if (previous)
        previous->next = node->next;
    else
        head = node->next;
}

template <typename NodeType>
PUBLIC void SinglyLinkedListPrepend(NodeType*& head, NodeType* new_node) {
    new_node->next = head;
    head = new_node;
}

template <typename NodeType>
PUBLIC NodeType* SinglyLinkedListPop(NodeType*& head) {
    ASSERT(head);
    auto node = head;
    head = head->next;
    return node;
}

template <typename NodeType>
PUBLIC NodeType* SinglyLinkedListLast(NodeType* head) {
    auto temp = head;
    while (temp != nullptr && temp->next != nullptr)
        temp = temp->next;
    return temp;
}

template <typename NodeType>
PUBLIC usize SinglyLinkedListSize(NodeType* head) {
    usize count = 0;
    for (auto i = head; i != nullptr; i = i->next)
        ++count;
    return count;
}

template <typename NodeType>
PUBLIC NodeType* SinglyLinkedListPartition(NodeType* first, NodeType* last, auto less_than_function) {
    // Get first node of given linked list
    NodeType* pivot = first;
    NodeType* front = first;
    while (front != nullptr && front != last) {
        if (less_than_function(front->data, last->data)) {
            pivot = first;

            // Swapping node values
            auto temp = first->data;
            first->data = front->data;
            front->data = temp;

            // Visiting the next node
            first = first->next;
        }

        // Visiting the next node
        front = front->next;
    }

    // Change last node value to current node
    auto temp = first->data;
    first->data = last->data;
    last->data = temp;
    return pivot;
}

template <typename NodeType>
PUBLIC void SinglyLinkedListSort(NodeType* first, NodeType* last, auto less_than_function) {
    if (first == last) return;
    NodeType* pivot = SinglyLinkedListPartition(first, last, less_than_function);
    if (pivot != nullptr && pivot->next != nullptr)
        SinglyLinkedListSort(pivot->next, last, less_than_function);
    if (pivot != nullptr && first != pivot) SinglyLinkedListSort(first, pivot, less_than_function);
}

template <typename NodeType>
PUBLIC void SinglyLinkedListInsertInMemoryOrder(NodeType*& head, NodeType* new_node) {
    if (!head) {
        head = new_node;
        new_node->next = nullptr;
        return;
    }

    NodeType* previous = nullptr;
    for (auto i = head; i != nullptr; i = i->next) {
        if (i > new_node) {
            if (previous)
                previous->next = new_node;
            else
                head = new_node;
            new_node->next = i;
            return;
        }
        previous = i;
    }

    // all nodes are less than new_node, previous is the last node
    previous->next = new_node;
}

template <typename List, typename Node>
PUBLIC void DoublyLinkedListAppend(List& list, Node* new_node) {
    if constexpr (requires { new_node->prev; }) new_node->prev = list.last;
    new_node->next = nullptr;
    if (list.last) {
        list.last->next = new_node;
        list.last = new_node;
    } else {
        ASSERT(!list.first);
        list.first = new_node;
        list.last = new_node;
    }
}

template <typename List, typename Node>
PUBLIC void DoublyLinkedListPrepend(List& list, Node* new_node) {
    new_node->next = list.first;
    new_node->prev = nullptr;
    if (list.first) {
        list.first->prev = new_node;
    } else {
        ASSERT(!list.last);
        list.last = new_node;
    }
    list.first = new_node;
}

template <typename List>
PUBLIC void DoublyLinkedListRemoveFirst(List& list) {
    list.first = list.first->next;
    if (list.first)
        list.first->prev = nullptr;
    else
        list.last = nullptr;
}

template <typename List, typename Node>
PUBLIC void DoublyLinkedListRemove(List& list, Node* node) {
    if (node->prev)
        node->prev->next = node->next;
    else
        list.first = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list.last = node->prev;
}

template <typename NodeType, typename ValueType>
struct SinglyLinkedListIterator {
    friend bool operator==(SinglyLinkedListIterator const& a, SinglyLinkedListIterator const& b) {
        return a.node == b.node;
    }
    friend bool operator!=(SinglyLinkedListIterator const& a, SinglyLinkedListIterator const& b) {
        return a.node != b.node;
    }
    ValueType& operator*() const { return node->data; }
    ValueType* operator->() { return &node->data; }
    SinglyLinkedListIterator& operator++() {
        node = node->next;
        return *this;
    }
    NodeType* node;
};

template <typename NodeType>
struct IntrusiveSinglyLinkedListIterator {
    friend bool operator==(IntrusiveSinglyLinkedListIterator const& a,
                           IntrusiveSinglyLinkedListIterator const& b) {
        return a.node == b.node;
    }
    friend bool operator!=(IntrusiveSinglyLinkedListIterator const& a,
                           IntrusiveSinglyLinkedListIterator const& b) {
        return a.node != b.node;
    }
    NodeType& operator*() const { return *node; }
    NodeType* operator->() { return node; }
    IntrusiveSinglyLinkedListIterator& operator++() {
        node = node->next;
        return *this;
    }
    NodeType* node;
};

template <typename Node>
struct IntrusiveSinglyLinkedList {
    using Iterator = IntrusiveSinglyLinkedListIterator<Node>;

    auto begin() { return Iterator {first}; }
    auto end() { return Iterator {nullptr}; }

    bool Empty() const { return first == nullptr; }

    Node* first {};
};
