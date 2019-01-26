#!/usr/bin/env sh

cd $MESON_SOURCE_ROOT 2>&1 > /dev/null
command -v git > /dev/null && git rev-parse --short=10 HEAD 2>/dev/null || echo ""
