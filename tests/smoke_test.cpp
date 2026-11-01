#include "core/version.h"

int main() {
    return VELORA_VERSION[0] != '\0' ? 0 : 1;
}
