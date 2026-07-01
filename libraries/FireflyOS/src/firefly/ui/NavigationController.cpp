#include "NavigationController.h"

namespace firefly {

NavigationController::NavigationController() {
    stack_[0] = Route::Lock;
}

bool NavigationController::open(Route route) {
    if(route == Route::Lock) {
        lock();
        return true;
    }
    if(route == Route::Home) {
        stack_[0] = Route::Lock;
        stack_[1] = Route::Home;
        depth_ = 2;
        return true;
    }
    if(depth_ >= kDepth) return false;
    stack_[depth_++] = route;
    return true;
}

Route NavigationController::back() {
    if(depth_ > 2) {
        --depth_;
        return current();
    }
    lock();
    return Route::Lock;
}

Route NavigationController::current() const {
    return stack_[depth_ - 1];
}

void NavigationController::lock() {
    stack_[0] = Route::Lock;
    depth_ = 1;
}

}  // namespace firefly
