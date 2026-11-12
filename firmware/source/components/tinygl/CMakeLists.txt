idf_component_register(SRC_DIRS "TinyGL/src" INCLUDE_DIRS "TinyGL/src" "TinyGL/include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized)
