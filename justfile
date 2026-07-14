ruff_config := "--config pyproject.toml"
pytest_opts := "--doctest-modules --no-qt-log"

@default:
    just --list

run: dev-setup
    LOGURU_LEVEL=DEBUG uv run python -m pxmodrim

test: dev-setup
    uv run pytest {{pytest_opts}} -s

test-verbose: dev-setup
    uv run pytest {{pytest_opts}} -v --tb=short -s

pyright:
    uv run python -m pyright -p pyproject.toml src/ tests/

ruff-fix:
    uv run ruff check {{ruff_config}} src/ tests/ --fix

ruff-format-fix:
    uv run ruff format {{ruff_config}} src/ tests/

fix: ruff-fix ruff-format-fix

check: pyright check-deps

check-deps:
    uv run python scripts/check-deps.py

ci: check test

dev-setup:
    uv venv --allow-existing
    uv sync --dev

linux-copy-desktop:
    mkdir -p ~/.local/share/applications
    sed "s|Icon=pxmodrim|Icon={{justfile_directory()}}/src/pxmodrim/ui/assets/logo.svg|" packaging/linux/pxmodrim.desktop > ~/.local/share/applications/pxmodrim.desktop
    kbuildsycoca6 --noincremental

build:
    uv run python packaging/build.py

build-release:
    uv run python packaging/build.py --release

build-clean:
    rm -rf build/ dist/ *.build/ *.dist/ *.onefile-build/

clean:
    rm -rf build/ dist/ *.egg-info
    rm -rf .pytest_cache .mypy_cache .ruff_cache
    find . -path ./rimsort-original -prune -o -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
