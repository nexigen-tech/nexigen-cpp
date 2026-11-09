# speedup-validation-loop (design D2): single PCH cluster helper.
#
# nexigen_precompile(target): attach the shared prelude
# include/nexigen/core/pch.hpp to a target's private PCH. Libraries, test
# executables and example hosts each get their own PCH instead of a
# REUSE_FROM cross-cluster share — identical defines and flags today (design
# Context), but per-target PCH is robust against future flag drift and costs
# ~1 s per target at configure.
#
# Call AFTER the target exists; safe to call conditionally (see the
# if(TARGET) guards in tests/CMakeLists.txt). No re-inclusion problems:
# target_precompile_headers on an already-configured target is ignored only
# for the exact same content, and the helper is called once per target.

function(nexigen_precompile target_name)
    if(NOT TARGET ${target_name})
        message(WARNING "nexigen_precompile: no target ${target_name}, skipped")
        return()
    endif()
    target_precompile_headers(${target_name} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/nexigen/core/pch.hpp>)
endfunction()
