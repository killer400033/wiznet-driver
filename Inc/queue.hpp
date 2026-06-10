/**
 * @file queue.hpp
 * @brief Generic queue implementation using C++ templates with C-style API
 * @author AI Assistant
 * @date 2025-11-19
 */

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Queue structure
 * @tparam T Type of elements to store
 * @tparam SIZE Maximum capacity of the queue
 */
template<typename T, uint16_t SIZE>
struct Queue {
    T data[SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    void (*on_first_push)(void); // Callback when pushing to empty queue
};

/**
 * @brief Initialize queue
 * @param q Pointer to queue
 */
template<typename T, uint16_t SIZE>
void queueInit(Queue<T, SIZE>* q);

/**
 * @brief Set callback to be called when pushing to an empty queue
 * @param q Pointer to queue
 * @param callback Function to call when queue transitions from empty to non-empty
 */
template<typename T, uint16_t SIZE>
void queueSetFirstPushCallback(Queue<T, SIZE>* q, void (*callback)(void));

/**
 * @brief Check if queue is empty
 * @param q Pointer to queue
 * @return true if empty, false otherwise
 */
template<typename T, uint16_t SIZE>
bool queueIsEmpty(const Queue<T, SIZE>* q);

/**
 * @brief Check if queue is full
 * @param q Pointer to queue
 * @return true if full, false otherwise
 */
template<typename T, uint16_t SIZE>
bool queueIsFull(const Queue<T, SIZE>* q);

/**
 * @brief Get number of elements in queue
 * @param q Pointer to queue
 * @return Number of elements
 */
template<typename T, uint16_t SIZE>
uint16_t queueSize(const Queue<T, SIZE>* q);

/**
 * @brief Get capacity of queue
 * @return Maximum capacity
 */
template<typename T, uint16_t SIZE>
uint16_t queueCapacity(const Queue<T, SIZE>* q);

/**
 * @brief Push element to back of queue (enqueue)
 * @param q Pointer to queue
 * @param item Item to push
 * @return true if successful, false if queue is full
 */
template<typename T, uint16_t SIZE>
bool queuePushBack(Queue<T, SIZE>* q, const T& item);

/**
 * @brief Push element to front of queue
 * @param q Pointer to queue
 * @param item Item to push
 * @return true if successful, false if queue is full
 */
template<typename T, uint16_t SIZE>
bool queuePushFront(Queue<T, SIZE>* q, const T& item);

/**
 * @brief Pop element from front of queue (dequeue)
 * @param q Pointer to queue
 * @param item Pointer to store popped item
 * @return true if successful, false if queue is empty
 */
template<typename T, uint16_t SIZE>
bool queuePopFront(Queue<T, SIZE>* q, T* item);

/**
 * @brief Pop element from back of queue
 * @param q Pointer to queue
 * @param item Pointer to store popped item
 * @return true if successful, false if queue is empty
 */
template<typename T, uint16_t SIZE>
bool queuePopBack(Queue<T, SIZE>* q, T* item);

/**
 * @brief Peek at front element without removing it
 * @param q Pointer to queue
 * @param item Pointer to store peeked item
 * @return true if successful, false if queue is empty
 */
template<typename T, uint16_t SIZE>
bool queuePeekFront(const Queue<T, SIZE>* q, T* item);

/**
 * @brief Peek at back element without removing it
 * @param q Pointer to queue
 * @param item Pointer to store peeked item
 * @return true if successful, false if queue is empty
 */
template<typename T, uint16_t SIZE>
bool queuePeekBack(const Queue<T, SIZE>* q, T* item);

/**
 * @brief Get pointer to front element (without removing)
 * @param q Pointer to queue
 * @return Pointer to front element, NULL if empty
 */
template<typename T, uint16_t SIZE>
T* queueFront(Queue<T, SIZE>* q);

/**
 * @brief Get pointer to back element (without removing)
 * @param q Pointer to queue
 * @return Pointer to back element, NULL if empty
 */
template<typename T, uint16_t SIZE>
T* queueBack(Queue<T, SIZE>* q);

/**
 * @brief Clear the queue
 * @param q Pointer to queue
 */
template<typename T, uint16_t SIZE>
void queueClear(Queue<T, SIZE>* q);

// ============================================================================
// Template Implementations
// ============================================================================

template<typename T, uint16_t SIZE>
void queueInit(Queue<T, SIZE>* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->on_first_push = nullptr;
}

template<typename T, uint16_t SIZE>
void queueSetFirstPushCallback(Queue<T, SIZE>* q, void (*callback)(void)) {
    q->on_first_push = callback;
}

template<typename T, uint16_t SIZE>
bool queueIsEmpty(const Queue<T, SIZE>* q) {
    return q->count == 0;
}

template<typename T, uint16_t SIZE>
bool queueIsFull(const Queue<T, SIZE>* q) {
    return q->count == SIZE;
}

template<typename T, uint16_t SIZE>
uint16_t queueSize(const Queue<T, SIZE>* q) {
    return q->count;
}

template<typename T, uint16_t SIZE>
uint16_t queueCapacity(const Queue<T, SIZE>* q) {
    return SIZE;
}

template<typename T, uint16_t SIZE>
bool queuePushBack(Queue<T, SIZE>* q, const T& item) {
    if (queueIsFull(q)) {
        return false;
    }
    bool was_empty = queueIsEmpty(q);
    q->data[q->tail] = item;
    q->tail = (q->tail + 1) % SIZE;
    q->count++;
    
    // Call callback if queue was empty and callback is set
    if (was_empty && q->on_first_push != nullptr) {
        q->on_first_push();
    }
    return true;
}

template<typename T, uint16_t SIZE>
bool queuePushFront(Queue<T, SIZE>* q, const T& item) {
    if (queueIsFull(q)) {
        return false;
    }
    bool was_empty = queueIsEmpty(q);
    q->head = (q->head == 0) ? (SIZE - 1) : (q->head - 1);
    q->data[q->head] = item;
    q->count++;
    
    // Call callback if queue was empty and callback is set
    if (was_empty && q->on_first_push != nullptr) {
        q->on_first_push();
    }
    return true;
}

template<typename T, uint16_t SIZE>
bool queuePopFront(Queue<T, SIZE>* q, T* item) {
    if (queueIsEmpty(q)) {
        return false;
    }
    *item = q->data[q->head];
    q->head = (q->head + 1) % SIZE;
    q->count--;
    return true;
}

template<typename T, uint16_t SIZE>
bool queuePopBack(Queue<T, SIZE>* q, T* item) {
    if (queueIsEmpty(q)) {
        return false;
    }
    q->tail = (q->tail == 0) ? (SIZE - 1) : (q->tail - 1);
    *item = q->data[q->tail];
    q->count--;
    return true;
}

template<typename T, uint16_t SIZE>
bool queuePeekFront(const Queue<T, SIZE>* q, T* item) {
    if (queueIsEmpty(q)) {
        return false;
    }
    *item = q->data[q->head];
    return true;
}

template<typename T, uint16_t SIZE>
bool queuePeekBack(const Queue<T, SIZE>* q, T* item) {
    if (queueIsEmpty(q)) {
        return false;
    }
    uint16_t back_index = (q->tail == 0) ? (SIZE - 1) : (q->tail - 1);
    *item = q->data[back_index];
    return true;
}

template<typename T, uint16_t SIZE>
T* queueFront(Queue<T, SIZE>* q) {
    if (queueIsEmpty(q)) {
        return nullptr;
    }
    return &q->data[q->head];
}

template<typename T, uint16_t SIZE>
T* queueBack(Queue<T, SIZE>* q) {
    if (queueIsEmpty(q)) {
        return nullptr;
    }
    uint16_t back_index = (q->tail == 0) ? (SIZE - 1) : (q->tail - 1);
    return &q->data[back_index];
}

template<typename T, uint16_t SIZE>
void queueClear(Queue<T, SIZE>* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

#endif /* QUEUE_HPP */
