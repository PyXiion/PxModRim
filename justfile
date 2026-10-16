set windows-shell := ["bash", "-cu"]

python := if os() == "windows" { "python" } else { "python3" }

@default:
    just --list

configure *args:
    cmake --preset default {{args}}

build *args: build-js
    [ -f build/default/CMakeCache.txt ] || cmake --preset default
    cmake --build --preset default {{args}}

# Build and start the app (Steam browser variant); each variant has its own build directory
run *args: (_run "SteamWorkshop" "default" args)

# Build and start the app with the native Workshop browser
run-native *args: (_run "NativeWorkshop" "native" args)

# Build and start the app as a plain mod sorter (no Workshop)
run-none *args: (_run "NoWorkshop" "none" args)

[private]
_run variant dir *args: build-js
    cmake -S . -B build/{{dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPX_WORKSHOP_VARIANT={{variant}}
    cmake --build build/{{dir}} --target pxmodrim
    PX_LOG_LEVEL=debug build/{{dir}}/src/app/pxmodrim {{args}}

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
