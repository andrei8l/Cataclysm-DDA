#!/bin/sh

bin="${MESON_INSTALL_DESTDIR_PREFIX}/$1"
lib_dir="${MESON_INSTALL_DESTDIR_PREFIX}/$2"
mkdir -p "${lib_dir}"

for lib in \
   libbacktrace \
   libbrotlicommon \
   libbrotlidec \
   libbrotlienc \
   libfreetype \
   libhwy \
   libjpeg \
   libjxl \
   liblzma \
   libmimalloc \
   libpcre \
   libpng \
   libSDL2_image \
   libSDL2_mixer \
   libSDL2_ttf \
   libSDL2[-.] \
   libsharpyuv \
   libtcmalloc \
   libtiff \
   libwebp \
   libzstd \
   ; do
     lib_file=$(ldd "${bin}" | grep "${lib}" | cut -d ' ' -f 3)
     if [ -n "${lib_file}" ]; then
       install --strip "${lib_file}" "${lib_dir}"
     fi
done
