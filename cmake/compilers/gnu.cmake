# GNU style (GCC/Clang) compiler specific settings

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_C_COMPILER_ID MATCHES "^(Apple)?Clang$")
    return()
endif()

enable_language(ASM)

set(ASM_SOURCES
    ${SOURCE_DIR}/asm/ftola.c
    ${SOURCE_DIR}/asm/snapvector.c
)

set(IOQ3_C_WARNING_FLAGS
    -Wall -Wimplicit -Wshadow
    -Wstrict-prototypes -Wformat=2 -Wformat-security
    -Wstrict-aliasing=2 -Wmissing-format-attribute
    -Wdisabled-optimization -Werror-implicit-function-declaration
    -Wno-format-zero-length -Wno-format-nonliteral
)
add_compile_options("$<$<COMPILE_LANGUAGE:C>:${IOQ3_C_WARNING_FLAGS}>")

# There are lots of instances of union based aliasing in the code
# that rely on the compiler not optimising them away, so disable it
add_compile_options(-fno-strict-aliasing)

# This is necessary to hide all symbols unless explicitly exported
# via the Q_EXPORT macro
add_compile_options(-fvisibility=hidden)
