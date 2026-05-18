#ifndef __HGD_IMAGE_GEN_H__
#define __HGD_IMAGE_GEN_H__

#include <png.h>
#include <zlib.h>

#include <errno.h>
#include <math.h>

#ifdef _MSC_VER
#include <float.h>
#ifndef isfinite
#define isfinite(x) _finite(x)
#endif
#endif

#include <stdarg.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "def.h"
#include "type.h"
#include "functions.h"

#endif