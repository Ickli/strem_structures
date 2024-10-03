#ifndef STREM_COMMON_H_
#define STREM_COMMON_H_
#include <stdbool.h>
#include <stddef.h>

#define MEMBER_SIZE(type, member) (sizeof( ((type *)0)->member ))
#define STREM_SIZE_MAX ((size_t)-1)

#endif // STREM_COMMON_H_
