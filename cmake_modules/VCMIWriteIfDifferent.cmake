include_guard(GLOBAL)

# Configure-time resources must keep their mtime when their bytes are unchanged.
# Unlike file(GENERATE), this preserves availability during configuration.
function(vcmi_write_if_different path contents)
    if(EXISTS "${path}")
        file(READ "${path}" previous_contents)
        if("${previous_contents}" STREQUAL "${contents}")
            return()
        endif()
    endif()
    file(WRITE "${path}" "${contents}")
endfunction()
