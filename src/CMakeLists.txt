add_subdirectory(core)
add_subdirectory(registry)
add_subdirectory(functions)
add_subdirectory(model_manager)
add_subdirectory(prompt_manager)
add_subdirectory(custom_parser)
add_subdirectory(secret_manager)

set(EXTENSION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/flockmtl_extension.cpp ${EXTENSION_SOURCES}
    PARENT_SCOPE)
