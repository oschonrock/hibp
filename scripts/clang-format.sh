#!/usr/bin/env bash

# in adition to your IDE clang-format,it is worth running this
# periodically to ensure all formatting errors have been caught.

find app/ src/ test/ include/ -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
    | xargs -r -0 clang-format --verbose -i
