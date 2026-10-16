set windows-shell := ["bash", "-cu"]

python := if os() == "windows" { "python" } else { "python3" }

@default:
    just --list

configure *args:
    cmake --preset default {{args}}

build *args: build-js
    [ -f build/default/CMakeCache.txt ] || cmake --preset default
    cmake --build --preset default {{args}}

run *args: build
    PX_LOG_LEVEL=debug build/default/src/app/pxmodrim {{args}}

test: build
    QT_QPA_PLATFORM=offscreen ctest --preset default

build-release: build-js
    [ -f build/release/CMakeCache.txt ] || cmake --preset release
    cmake --build --preset release

build-js:
    {{python}} scripts/build-inject-js.py

format:
    find src tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i

format-check:
    find src tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror

lint:
    [ -f build/default/compile_commands.json ] || cmake --preset default
    run-clang-tidy -p build/default -quiet -extra-arg=-Wno-unknown-warning-option '^{{justfile_directory()}}/(src|tests)/.*\.cpp$'

lint-fix:
    [ -f build/default/compile_commands.json ] || cmake --preset default
    run-clang-tidy -p build/default -quiet -fix -extra-arg=-Wno-unknown-warning-option '^{{justfile_directory()}}/(src|tests)/.*\.cpp$'

check: format-check lint build

ci: check test

linux-copy-desktop:
    mkdir -p ~/.local/share/applications
    sed "s|Icon=pxmodrim|Icon={{justfile_directory()}}/src/ui/assets/logo.svg|" packaging/linux/pxmodrim.desktop > ~/.local/share/applications/pxmodrim.desktop
    kbuildsycoca6 --noincremental

clean:
    rm -rf build dist
