#pragma once

#include <stdint.h>

namespace firefly {

struct NotificationSummary {
    char key[40]{};
    char app_name[32]{};
    char title[129]{};
    char body[257]{};
    int64_t posted_epoch = 0;
    bool pinned = false;
};

}  // namespace firefly
