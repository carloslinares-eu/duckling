# duckling_set_warnings(<target>)
# Strict warnings for duckling's own code (third-party targets are SYSTEM).
function(duckling_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
  endif()
endfunction()
