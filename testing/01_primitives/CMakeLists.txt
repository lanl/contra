foreach(_test primitives arrays assignment) # literals
  create_test(
    NAME test_${_test}
    COMMAND $<TARGET_FILE:contra> ${CMAKE_CURRENT_SOURCE_DIR}/${_test}.cta
    COMPARE stdout
    STANDARD ${CMAKE_CURRENT_SOURCE_DIR}/${_test}.std)
endforeach()
