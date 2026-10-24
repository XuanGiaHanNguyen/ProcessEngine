# pf_set_warnings(<target>): apply the project's warning flags to a target.
function(pf_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(PF_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
    if(PF_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
