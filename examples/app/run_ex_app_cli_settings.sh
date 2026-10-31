#!/usr/bin/env bash
# Build (when needed) and run the command-line settings example from the
# repository root, so its generated directories land beside the checkout.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build}"

find_binary() {
    local candidate
    for candidate in \
        "${build_dir}/examples/app/ex_app_cli_settings" \
        "${build_dir}/debug/examples/app/ex_app_cli_settings" \
        "${build_dir}/release/examples/app/ex_app_cli_settings"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

binary="$(find_binary || true)"
if [[ -z "${binary}" ]]; then
    echo "run_ex_app_cli_settings: building ex_app_cli_settings in ${build_dir}" >&2
    if ! cmake --build "${build_dir}" --target ex_app_cli_settings; then
        echo "run_ex_app_cli_settings: build failed" >&2
        exit 1
    fi
    binary="$(find_binary || true)"
fi

if [[ -z "${binary}" ]]; then
    echo "run_ex_app_cli_settings: ex_app_cli_settings binary not found under ${build_dir}" >&2
    exit 1
fi

cd "${repo_root}"
if command -v timeout >/dev/null 2>&1; then
    exec timeout 60 "${binary}" "$@"
fi
exec "${binary}" "$@"
