#!/bin/sh
# Builds the sysroot used by cmake/HaikuCompileCheck.toolchain.cmake:
#   - Haiku headers + app_server sources (sparse, blobless clone)
#   - libstdc++ headers from the host GCC, with Haiku's os/locale config
#     (from haiku/buildtools) replacing the Linux target directory.
#
# Usage: tools/setup-haiku-check-sysroot.sh [ROOT] [HAIKU_REF]
# Requires: git, a host GCC with libstdc++ headers, clang >= 15.
set -eu

ROOT="${1:-$HOME/haiku-check}"
REF="${2:-master}"
GCCVER="$(gcc -dumpversion | cut -d. -f1)"
CXX_DIR="/usr/include/c++/$GCCVER"
CXX_TARGET_DIR="$(dirname "$(find /usr/include -path "*c++/$GCCVER/bits/c++config.h" | head -1)")/.."

mkdir -p "$ROOT"
cd "$ROOT"

if [ ! -d haiku/.git ]; then
	git clone --filter=blob:none --no-checkout --depth 1 --branch "$REF" \
		https://github.com/haiku/haiku haiku
	git -C haiku sparse-checkout set --no-cone /headers/ /src/servers/app/ \
		/src/add-ons/decorators/ /src/kits/interface/WindowStack.cpp
	git -C haiku checkout -q
fi

if [ ! -d buildtools/.git ]; then
	git clone --filter=blob:none --no-checkout --depth 1 \
		https://github.com/haiku/buildtools buildtools
fi

rm -rf cxx-target
mkdir -p cxx-target/bits
cp "$CXX_TARGET_DIR"/bits/*.h cxx-target/bits/
for f in os/haiku/os_defines.h os/haiku/ctype_base.h os/haiku/ctype_inline.h \
		os/haiku/error_constants.h locale/generic/c_locale.h \
		locale/generic/messages_members.h locale/generic/time_members.h; do
	git -C buildtools show "HEAD:gcc/libstdc++-v3/config/$f" \
		> "cxx-target/bits/$(basename "$f")"
done
cp cxx-target/bits/c_locale.h cxx-target/bits/c++locale.h

# Linux-only libstdc++ configure results that do not hold on Haiku.
for m in _GLIBCXX_HAVE_LINUX_FUTEX _GLIBCXX_HAVE_LINUX_TYPES_H \
		_GLIBCXX_HAVE_SYS_SDT_H _GLIBCXX_USE_PTHREAD_RWLOCK_CLOCKLOCK \
		_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK \
		_GLIBCXX_HAVE_ARC4RANDOM _GLIBCXX_HAVE_GETENTROPY _GLIBCXX_USE_SENDFILE \
		_GLIBCXX_HAVE_SYS_SYSINFO_H _GLIBCXX_USE_GET_NPROCS _GLIBCXX_HAVE_EXECINFO_H \
		_GLIBCXX_HAVE_TLS _GLIBCXX_HAVE_LINK_H _GLIBCXX_USE_DECIMAL_FLOAT \
		_GLIBCXX_HAVE_UCHAR_H _GLIBCXX_USE_C11_UCHAR_CXX11 _GLIBCXX_USE_FCHMOD \
		_GLIBCXX_USE_FCHMODAT _GLIBCXX_HAVE_SYS_STATVFS_H _GLIBCXX_HAVE_LINUX_RANDOM_H \
		_GLIBCXX_USE_UCHAR_C8RTOMB_MBRTOC8_CXX20 _GLIBCXX_USE_INIT_PRIORITY_ATTRIBUTE \
		_GLIBCXX_HAVE_AT_QUICK_EXIT _GLIBCXX_HAVE_QUICK_EXIT _GLIBCXX_HAVE_TIMESPEC_GET; do
	sed -i -E "s@^#define $m\\b.*@/* haiku-check: $m disabled */@" \
		cxx-target/bits/c++config.h
done

ln -sfn "$CXX_DIR" cxx
# app_server sources need FreeType headers (host copy is fine for checking).
[ -d /usr/include/freetype2 ] && ln -sfn /usr/include/freetype2 freetype2

echo "Haiku compile-check sysroot ready in $ROOT"
echo "  cmake -S . -B build-check -G Ninja \\"
echo "    -DCMAKE_TOOLCHAIN_FILE=cmake/HaikuCompileCheck.toolchain.cmake \\"
echo "    -DHAIKU_CHECK_ROOT=$ROOT"
