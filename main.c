/*
 * HGDImageGen - PNG image to Geometry Dash .gmd converter
 *
 * Dependencies:
 *   libpng, zlib
 *
 * Visual Studio x64 build:
 *   Open HGDImageGen.slnx
 *   Select Debug | x64 or Release | x64
 *   Runtime library: Debug=/MDd, Release=/MD
 *   Build Solution
 *
 * CLI example:
 *   HGDImageGen.exe -i Test.png -o Test.gmd -n Test --width 64 --height 64
 */

#include "HGDImageGen.h"

int main(int argc, char **argv) {
    Options opt;
    parse_args(&opt, argc, argv);
    convert_image_to_gmd(&opt);
    return EXIT_SUCCESS;
}
