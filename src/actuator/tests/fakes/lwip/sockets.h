#pragma once
#include <cstddef>
constexpr int MSG_DONTWAIT = 1;
int send(int fd, const void* data, size_t length, int flags);
