// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <typename Type>
using ThreadsafeQueue = Queue<Type, Mutex>;

template <typename Type, usize k_size>
using ThreadsafeBoundedQueue = BoundedQueue<Type, k_size, Mutex>;

using ThreadsafeFunctionQueue = FunctionQueue<Mutex>;
