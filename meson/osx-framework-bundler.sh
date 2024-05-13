#!/bin/sh

# https://mesonbuild.com/Creating-OSX-packages.html

bin=$1
app_frm_dir="${MESON_INSTALL_DESTDIR_PREFIX}/Contents/Frameworks"
mkdir -p "${app_frm_dir}"

for lib in \
  SDL2 \
  SDL2_ttf \
  SDL2_image \
  SDL2_mixer \
  ; do
    frm_prefix=""
    if [ -d "${HOME}/Library/Frameworks/${lib}.framework" ]; then
        frm_prefix="${HOME}"
    fi
    
    umask 022
    cp -R --no-preserve=mode,ownership "${frm_prefix}/Library/Frameworks/$lib.framework" "${app_frm_dir}"
    
    install_name_tool -change "@rpath/${lib}.framework/Versions/A/${lib}" "@executable_path/../Frameworks/${lib}.framework/Versions/A/${lib}" "${MESON_INSTALL_DESTDIR_PREFIX}/Contents/MacOS/$bin"
done

