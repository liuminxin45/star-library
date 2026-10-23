/* The compact yyjson number writer uses the CRT formatter even when its file
   APIs are disabled. Keep the declaration explicit without altering the pinned
   third-party source. */
#include <stdio.h>
#include "yyjson.c"
