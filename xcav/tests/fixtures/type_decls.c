// A struct and an enum with trailing semicolons
#include <stdio.h>

struct Vec3 {
    float x;
    float y;
    float z;
};

typedef enum {
    RED,
    GREEN,
    BLUE
} Color;

int use_types(void) {
    return 0;
}
