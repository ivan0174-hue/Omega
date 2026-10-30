// Builds the platform neutral ../main.c with its entry point renamed, so it
// can be called from the Circle kernel (Circle provides the real main()).
#define main omega_main
#include "../main.c"
