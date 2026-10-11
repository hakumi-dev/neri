#!/usr/bin/env bash
# Shared guest entry point for GitHub-hosted and local virtual machines.
set -euo pipefail

action=${1:-}
job=${2:-}
case "$action" in prepare|run) ;; *) echo 'Usage: jobs.sh <prepare|run> <job-id>' >&2; exit 2 ;; esac
case "$job" in
  macos-release-package|macos-native-debug|macos-native-sanitize|macos-native-thread-sanitize) platform=macos ;;
  linux-release-package|linux-native-debug|linux-native-sanitize|linux-native-thread-sanitize) platform=linux ;;
  *) echo "Unknown Unix CI job: $job" >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
: "${RUNNER_TEMP:?Set RUNNER_TEMP to a guest temporary directory}"
case "$RUNNER_TEMP" in /*) ;; *) echo 'RUNNER_TEMP must be absolute' >&2; exit 2 ;; esac
mkdir -p "$RUNNER_TEMP"
if [ "$platform" = macos ]; then
  test "$(uname -s)" = Darwin
  test "$(uname -m)" = arm64
  test "$(sw_vers -productVersion | cut -d . -f 1)" = 15
else
  test "$(uname -s)" = Linux
  test "$(uname -m)" = x86_64
  . /etc/os-release
  test "$ID" = ubuntu && test "$VERSION_ID" = 24.04
fi

install_sqlite() {
  sqlite_work=$(mktemp -d "$RUNNER_TEMP/sqlite.XXXXXXXX")
  curl --fail --location --retry 3 https://sqlite.org/2026/sqlite-amalgamation-3530400.zip -o "$sqlite_work/sqlite.zip"
  if [ "$platform" = macos ]; then
    openssl="$(brew --prefix openssl@3)/bin/openssl"
  else
    openssl=openssl
  fi
  test "$("$openssl" dgst -sha3-256 "$sqlite_work/sqlite.zip" | awk '{print $NF}')" = 628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
  if [ "$platform" = macos ]; then
    tar -xf "$sqlite_work/sqlite.zip" -C "$sqlite_work"
    sqlite_prefix="$RUNNER_TEMP/sqlite-3.53.4"
    mkdir -p "$sqlite_prefix/lib" build/native/native-release
    cc -O2 -dynamiclib -DSQLITE_THREADSAFE=1 \
      -Wl,-install_name,"$sqlite_prefix/lib/libsqlite3.dylib" \
      "$sqlite_work/sqlite-amalgamation-3530400/sqlite3.c" \
      -o "$sqlite_prefix/lib/libsqlite3.dylib"
    ln -sf "$sqlite_prefix/lib/libsqlite3.dylib" build/native/native-release/libsqlite3.dylib
    otool -D "$sqlite_prefix/lib/libsqlite3.dylib"
    shasum -a 256 "$sqlite_prefix/lib/libsqlite3.dylib"
  else
    bsdtar -xf "$sqlite_work/sqlite.zip" -C "$sqlite_work"
    sqlite_prefix=/opt/sqlite-3.53.4
    sqlite_source="$sqlite_work/sqlite-amalgamation-3530400"
    cc -O2 -fPIC -shared -DSQLITE_THREADSAFE=1 -Wl,-soname,libsqlite3.so.0 \
      "$sqlite_source/sqlite3.c" -o "$sqlite_work/libsqlite3.so.0" -pthread -ldl -lm
    sudo install -d "$sqlite_prefix/lib/pkgconfig" "$sqlite_prefix/include"
    sudo install -m 755 "$sqlite_work/libsqlite3.so.0" "$sqlite_prefix/lib/"
    sudo ln -sf libsqlite3.so.0 "$sqlite_prefix/lib/libsqlite3.so"
    sudo install -m 644 "$sqlite_source/sqlite3.h" "$sqlite_source/sqlite3ext.h" "$sqlite_prefix/include/"
    cat <<EOF | sudo tee "$sqlite_prefix/lib/pkgconfig/sqlite3.pc" >/dev/null
prefix=$sqlite_prefix
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: SQLite
Description: SQLite database engine
Version: 3.53.4
Libs: -L\${libdir} -lsqlite3
Libs.private: -pthread -ldl -lm
Cflags: -I\${includedir}
EOF
    echo "$sqlite_prefix/lib" | sudo tee /etc/ld.so.conf.d/neri-test-sqlite.conf >/dev/null
    sudo ldconfig
    export PKG_CONFIG_PATH="$sqlite_prefix/lib/pkgconfig"
    test "$(pkg-config --modversion sqlite3)" = 3.53.4
    cat > "$sqlite_work/probe.c" <<'EOF'
#include <sqlite3.h>
#include <stdio.h>
int main(void) {
    printf("SQLite headers=%d runtime=%s threadsafe=%d\n", SQLITE_VERSION_NUMBER, sqlite3_libversion(), sqlite3_threadsafe());
    return SQLITE_VERSION_NUMBER != 3053004 || sqlite3_libversion_number() != SQLITE_VERSION_NUMBER || sqlite3_threadsafe() != 1;
}
EOF
    cc $(pkg-config --cflags sqlite3) "$sqlite_work/probe.c" $(pkg-config --libs sqlite3) -o "$sqlite_work/probe"
    env -i PATH=/usr/bin:/bin "$sqlite_work/probe"
    env -i PATH=/usr/bin:/bin ldd "$sqlite_work/probe" | grep -F "libsqlite3.so.0 => $sqlite_prefix/lib/libsqlite3.so.0"
    sha256sum "$sqlite_prefix/lib/libsqlite3.so.0"
  fi
}

if [ "$action" = prepare ]; then
  if [ "$platform" = macos ]; then
    brew install llvm@22 cmake ninja
    test "$("$(brew --prefix llvm@22)/bin/llvm-config" --version)" = 22.1.8
    if [ "$job" = macos-release-package ]; then
      brew install openssl@3
      install_sqlite
    fi
  else
    sudo apt-get update
    sudo apt-get install -y ca-certificates curl gnupg lsb-release software-properties-common cmake ninja-build build-essential zlib1g-dev libzstd-dev libedit-dev libffi-dev libxml2-dev libssl-dev libsqlite3-dev libarchive-tools
    curl --fail --location --retry 3 https://raw.githubusercontent.com/opencollab/llvm-jenkins.debian.net/cf853fefa308484749df8a87004c580a6c0fe830/llvm.sh -o "$RUNNER_TEMP/neri-llvm.sh"
    printf '%s  %s\n' 03878e08f47b66cc95bc4b544b0db3c6d9ce8d60e6cf2492ae357984330a9eae "$RUNNER_TEMP/neri-llvm.sh" | sha256sum -c -
    sudo bash "$RUNNER_TEMP/neri-llvm.sh" 22
    sudo apt-get install -y llvm-22-dev
    test "$(llvm-config-22 --version)" = 22.1.8
    if [ "$job" = linux-release-package ]; then
      sudo apt-get install -y openssl pkg-config
      install_sqlite
    fi
  fi
  exit 0
fi

case "$job" in
  macos-release-package|linux-release-package)
    scripts/build.sh package --stage 3 --full-validation ;;
  macos-native-debug) scripts/build.sh native-test --debug ;;
  macos-native-sanitize) scripts/build.sh native-test --sanitize ;;
  macos-native-thread-sanitize) scripts/build.sh native-test --thread-sanitize ;;
  linux-native-*)
    build_type=Debug
    sanitizers=OFF
    thread_sanitizer=OFF
    if [ "$job" = linux-native-sanitize ]; then sanitizers=ON; fi
    if [ "$job" = linux-native-thread-sanitize ]; then thread_sanitizer=ON; fi
    cmake -S . -B build/linux-native -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" -DNERI_SANITIZERS="$sanitizers" -DNERI_THREAD_SANITIZER="$thread_sanitizer" \
      -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22 \
      -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm -DBUILD_TESTING=ON
    cmake --build build/linux-native --parallel 2
    ctest --test-dir build/linux-native --output-on-failure --no-tests=error ;;
esac
