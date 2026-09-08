#!/usr/bin/env bash

shopt -s extglob

app_dir="./dist/Dune 3D.app"
bin_dir="$app_dir/Contents/MacOS"
res_dir="$app_dir/Contents/Resources"
lib_dir="$res_dir/lib"
brew_prefix="$(brew --prefix)"

mkdir -p "$bin_dir" "$lib_dir" "$res_dir/share/icons" "$res_dir/share/glib-2.0"
# Using meson install omits LC_RPATHs in the main executable.
meson install -C build --destdir stage --tags runtime
find build/stage -name dune3d -type f -exec cp -v '{}' "$bin_dir/dune3d-bin" \;
cp Info.plist "$app_dir/Contents"
cp macos-launcher.sh "$bin_dir/dune3d"
chmod +x "$bin_dir/dune3d"
cp src/icons/dune3d.icns "$res_dir"

echo "APPL????" > "$app_dir/Contents/PkgInfo"

cp -r $brew_prefix/share/icons/Adwaita "$res_dir/share/icons"
cp -r $brew_prefix/share/glib-2.0/schemas "$res_dir/share/glib-2.0"

loaders_dir="$lib_dir/gdk-pixbuf-2.0/2.10.0/loaders"
rm -rf "$loaders_dir" "$loaders_dir.cache"
mkdir -p "$loaders_dir"

GDK_PIXBUF_MODULEDIR=$brew_prefix/lib/gdk-pixbuf-2.0/2.10.0/loaders gdk-pixbuf-query-loaders | while read item
do
  unquoted="${item#\"}"
  unquoted="${unquoted%\"}"
  if [[ -f "$unquoted" ]] ;
  then
    cp "$unquoted" "$loaders_dir"
    echo "\"@executable_path/../Resources/lib/gdk-pixbuf-2.0/2.10.0/loaders/$(basename "$unquoted")\"" >> "$loaders_dir.cache"
    dylibbundler -of -b -x "$loaders_dir/$(basename "$unquoted")" -d "$lib_dir" -p @executable_path/../Resources/lib/ -s $brew_prefix/lib
  else
    echo "$item" >> "$loaders_dir.cache"
  fi
done
dylibbundler -of -b -x  "$bin_dir/dune3d-bin" -d "$lib_dir" -p @executable_path/../Resources/lib/

# dylibbundler rewrote all load commands to @executable_path but also updated
# the LC_RPATH entries to @executable_path/../Resources/lib/. Since the LC_RPATH
# entries are redundant and duplicates prevent the app from launching when
# linked against the macOS 26 SDK, we strip them out entirely.
strip_rpaths() {
  if otool -L "$1" | grep '@rpath/' >/dev/null
  then
    echo "FATAL: @rpath found in $1" >&2
    exit 1
  fi
  rpaths=$(otool -l "$1" | awk '$2 == "LC_RPATH" {rp=1}
    rp && $1 == "path" {sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; rp=0}')
  if [[ -n "$rpaths" ]]
  then
    echo "* Stripping LC_RPATHs from $1"
    while IFS= read -r rp
    do
      install_name_tool -delete_rpath "$rp" "$1"
    done <<< "$rpaths"
    codesign --force --deep --preserve-metadata=entitlements,requirements,flags,runtime --sign - "$1"
  fi
}

while IFS= read -r item
do
  strip_rpaths "$item"
done < <(find "$app_dir" -type f \( -name '*.dylib' -o -name '*.so' \))

codesign --force --deep --preserve-metadata=entitlements,requirements,flags,runtime --sign - "$app_dir"
