#==============================================================================
set( SerializationTestSuite
	test_binarybasic.cpp
	test_binary.cpp
	test_typeidjoin.cpp
	test_typeidmap.cpp
	test_extdata.cpp
)

create_test_sourcelist( SerializationTests test_serialization.cpp ${SerializationTestSuite})

add_executable(test_serialization ${SerializationTests})

target_link_libraries(test_serialization
	solid_serialization
	solid_utility
	solid_system
	${SYS_BASIC_LIBS}
)

add_test(NAME TestSerializationExtData		COMMAND  test_serialization test_extdata)
add_test(NAME TestSerializationBinaryBasic	COMMAND  test_serialization test_binarybasic)
add_test(NAME TestSerializationBinary		COMMAND  test_serialization test_binary)
add_test(NAME TestSerializationTypeIdJoin	COMMAND  test_serialization test_typeidjoin)
add_test(NAME TestSerializationTypeIdMap	COMMAND  test_serialization test_typeidmap)

#==============================================================================
