option(STATIC_LINK "Enable static linking" OFF)
option(SKIP_AUX "Bypass getauxval checks (for testing purposes)" OFF)
option(ENABLE_SANITIZE "Enable sanitizers" OFF)

if(SKIP_AUX)
	add_compile_definitions(PARPAR_SKIP_AUX_CHECK=1)
endif()
if(ENABLE_SANITIZE)
	add_compile_definitions(HAS_UBSAN=1)
endif()

include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)
include(CheckCXXSymbolExists)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

if(NOT TARGET_ARCH)
	if(CMAKE_GENERATOR_PLATFORM)
		set(TARGET_ARCH ${CMAKE_GENERATOR_PLATFORM})
	else()
		set(TARGET_ARCH ${CMAKE_SYSTEM_PROCESSOR})
	endif()
endif()

message("Building for ${TARGET_ARCH}")
if (${TARGET_ARCH} MATCHES "i386|i686|x86|x86_64|x64|amd64|AMD64|win32|Win32")
	set(IS_X86 TRUE)
	if(${TARGET_ARCH} MATCHES "x86_64|x64|amd64|AMD64")
		set(IS_X64 TRUE)
	endif()
endif()
if (${TARGET_ARCH} MATCHES "arm|ARM|aarch64|arm64|ARM64|armeb|aarch64be|aarch64_be")
	set(IS_ARM TRUE)
endif()
if (${TARGET_ARCH} MATCHES "riscv64|rv64")
	set(IS_RISCV64 TRUE)
endif()
if (${TARGET_ARCH} MATCHES "riscv32|rv32")
	set(IS_RISCV32 TRUE)
endif()

if(STATIC_LINK)
	if(MSVC)
		add_compile_options("$<$<NOT:$<CONFIG:Debug>>:/MT>$<$<CONFIG:Debug>:/MTd>")
	else()
		add_link_options(-static)
	endif()
endif()

if(MSVC)
	set(RELEASE_COMPILE_FLAGS /GS- /Gy /sdl- /Oy /Oi)
	set(RELEASE_LINK_FLAGS /OPT:REF /OPT:ICF)
	add_compile_options(/W2 "$<$<NOT:$<CONFIG:Debug>>:${RELEASE_COMPILE_FLAGS}>")
	add_link_options("$<$<NOT:$<CONFIG:Debug>>:${RELEASE_LINK_FLAGS}>")
else()
	# TODO: consider -Werror
	add_compile_options(-Wall -Wextra -Wno-unused-function)
	if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
		add_compile_options(-ggdb)
	else()
		if(NOT ENABLE_SANITIZE)
			add_compile_options(-fomit-frame-pointer)
		endif()
	endif()
	
	if(ENABLE_SANITIZE)
		set(SANITIZE_OPTS -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all)
		# -fsanitize=memory requires instrumented libraries, so not useful
		add_compile_options(-fno-omit-frame-pointer ${SANITIZE_OPTS})
		add_link_options(${SANITIZE_OPTS})
		
		if(NOT STATIC_LINK)
			#include(CheckLinkerFlag)
			#check_linker_flag(C -static-libasan HAS_LIBASAN)  # GCC
			#check_linker_flag(C -static-libsan HAS_LIBSAN)  # Clang
			CHECK_CXX_COMPILER_FLAG(-static-libasan HAS_LIBASAN)
			CHECK_CXX_COMPILER_FLAG(-static-libsan HAS_LIBSAN)
			if(HAS_LIBASAN)
				add_link_options(-static-libasan)
			endif()
			if(HAS_LIBSAN)
				add_link_options(-static-libsan)
			endif()
		endif()
	endif()
endif()
