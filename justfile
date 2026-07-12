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

typecheck:
    uv run mypy --config-file pyproject.toml src/

pyright:
    uv run python -m pyright -p pyproject.toml src/ tests/

ruff-fix:
    uv run ruff check {{ruff_config}} src/ tests/ --fix

ruff-format-fix:
    uv run ruff format {{ruff_config}} src/ tests/

fix: ruff-fix ruff-format-fix

check: typecheck pyright
    @echo "Done"

ci: check test

dev-setup:
    uv venv --allow-existing
    uv sync --dev

clean:
    rm -rf build/ dist/ *.egg-info
    rm -rf .pytest_cache .mypy_cache .ruff_cache
    find . -path ./rimsort-original -prune -o -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
