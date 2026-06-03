// Fixture with static functions for move-into static-stripping test
#include <stdio.h>

static int helper(int x) {
    return x + 1;
}

int public_func(int x) {
    return helper(x);
}
